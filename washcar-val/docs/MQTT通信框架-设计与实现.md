# MQTT 通信框架：设计思路与实现说明

> 本文回答两件事：
> 1. **为什么这样搭**（分层边界、队列与泵、topic 模型、上下行数据流、并发与降级策略）
> 2. **代码落在哪里**（每个类负责什么，改哪里能改行为）
>
> 协议字段以 `docs/Flexem_MQTT_协议对接总结.md` 为准，本文只讲实现。
> 代码结构总览见 `docs/代码结构与运行逻辑-交接文档.md`。

---

## 1. 目标与约束

| 项 | 内容 |
|---|---|
| 传输 | libmosquitto（不做手写报文拼装） |
| QoS / retain / 会话 | `0` / `false` / `cleanSession=true`（云端硬约束，不可配） |
| topic 发布格式 | `flexem.{boxNo}.{endpoint}`（生产抓包格式，分隔符可配） |
| topic 订阅格式 | 点号与斜杠**都订阅**（`flexem.{boxNo}.command` + `flexem/{boxNo}/command`） |
| 下行 endpoint | `command` / `read` / `reboot` |
| 上行 endpoint | `monitor` / `readdata` / `pushalarm` / `commandreply` |
| 载荷 | 平铺 JSON，业务字段与 `boxNo`、`time` 同级；`time` 为**整型** Unix 秒 |
| 回执策略 | 命令成功才回 `commandreply`；失败只写日志 |
| reboot | 只识别并记录，默认不执行重启（可配置开） |

设计时的一条硬性判断：**协议里"约定"的部分优先于"现成代码怎么写"**。
所以本次改动的第一原则是「按约定补齐」，而不是「让现有实现自洽」。

第二条原则：**MqttClient 的回调里不做事**。这是本次重构的核心动机——
旧实现把业务执行放在了 mosquitto 的接收线程里，一条驿公里启动命令能占住它 3.2 秒。

---

## 2. 分层：谁管什么

```
                       ┌───────────────────────────────────────┐
                       │ 远端云端（Flexem 平台）                 │
                       └───────┬───────────────────▲───────────┘
                   收 command/read/reboot     发 4 类上报
                               │                   │
┌──────────────────────────────▼───────────────────┴───────────────┐
│ presentation（表现层）                                              │
│                                                                  │
│  MqttClient ──handler(endpoint,payload)──► rawBuffer.push()       │
│   只做管道：连接/重连/订阅/发布/                                   │
│   topic↔endpoint 归一化；不解析 JSON、不认识业务字段                │
│                                                                  │
│  DownlinkPump  每 50ms 从 rawBuffer 取一条：                       │
│     CommandParser::parse → bizBuffer.push(BusinessCommand)        │
│     （只解析，不执行；执行在业务层的分派线程上）                    │
│                                                                  │
│  UplinkPump    每 50ms 从 upBuffer 取一条：                        │
│     mqtt.publish(endpointFor(kind), payload)                      │
└──────────────────────────────┬───────────────────────────────────┘
                               │ BusinessCommand（经 bizBuffer）
┌──────────────────────────────▼───────────────────────────────────┐
│ business_layer（命令分派线程）                                     │
│  bizBuffer.popWait → BusinessLayer::onCommand                    │
│  命令在这里**同步执行**（驿公里启动最长 3.2s），不堵上游            │
└──────────────────────────────┬───────────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────────┐
│ business_layer（业务层）                                          │
│  BusinessLayer  命令分派 + 组装 commandreply/readdata             │
│  IWashCarService（海天 / 驿公里）状态机判定 + 点位读写              │
│  ReportScheduler 节拍：到点调 service 的三个上报方法               │
│  ★ 全程只说 ReportKind，不认识 topic、不认识 MQTT                  │
└──────────────────────────────┬───────────────────────────────────┘
                               │ IDeviceService（只返回 PlcPointValues）
┌──────────────────────────────▼───────────────────────────────────┐
│ data_layer（数据层）                                              │
│  DeviceService（采集线程+缓存） → FxPlcInstanceSet → IPlcInstance │
└──────────────────────────────────────────────────────────────────┘
```

### 2.1 为什么必须有队列 + 泵

三条约束逼出这个结构：

1. **MQTT 回调不能阻塞**。`mosquitto_loop` 线程同时负责收报文、发心跳、处理重连。
   回调里做 3.2 秒的 PLC 等待 → 心跳可能超时、云端下发的第二条命令排在后面。
   所以回调**只做一次内存 push**（微秒级）。
2. **命令必须串行执行**。洗车机是单机设备，两条命令并发写 PLC 会互相踩。
   一条队列 + 一个消费者，天然串行，不需要额外的命令锁。
3. **上报不能阻塞业务**。`monitor`/`pushalarm` 由 4 个线程产生
   （洗车监控线程、上报调度线程、下行泵里的命令回执、read 应答）。
   如果业务线程直接调 `publish`，就会和 MQTT 线程抢 `publishMutex_`，
   断链时还会在业务线程里白等。现在业务只 push（也是微秒级）。

### 2.2 为什么解析要单独成层

`CommandParser` 只做一件事：**`RawMessage` → `BusinessCommand`**。

- 它知道"`{"Manual start":1}` 是启动"、"`{"washcar stop":1}` 是驿公里停止"。
- 它不知道启动要写哪个点位、要不要过状态机。
- `BusinessLayer` 拿到的是 `CommandType::START` + 原始 JSON，
  不需要再判断"这个字段是海天的还是驿公里的"。

好处是**命令方言集中在一处**（`classifyCommand` 的十几个 `if`），
加机型/加命令不会污染业务逻辑。位点名的合法性判断（`parsePoint`）也在这里，
非法键直接不进 `BusinessCommand`，业务层不用再防一遍。

### 2.3 为什么业务层只认 `ReportKind`

```cpp
enum class ReportKind { MONITOR, PUSHALARM, READDATA, COMMAND_REPLY };
```

业务层调 `sendReport(ReportKind::MONITOR, json.dump())`，
`comm::endpointFor(kind)`（`presentation/endpoint_map.h`）把它翻成 `"monitor"`。

这样换协议（比如以后云厂商换了 topic 命名、或某个上报改走别的 endpoint）
只改 `endpoint_map.h` 一行，业务代码零改动。**全项目只有 `MqttClient::buildTopic` 拼 topic。**

---

## 3. Topic 模型

### 3.1 拼接规则

```cpp
// MqttClient::buildTopic()
"flexem" + sep + boxNo + sep + endpoint      // sep = '.' 或 '/'
```

| boxNo | sep | endpoint | 结果 |
|---|---|---|---|
| 123456 | `.`（默认） | command | `flexem.123456.command` |
| 123456 | `/` | command | `flexem/123456/command` |

### 3.2 为什么"发布一种、订阅两种"

- **发布只用一种**：两种都发会让云端对同一条上报收到两份，属性面板会抖。
- **订阅两种**：生产抓包是点号，Excel 配置模板是斜杠；订阅端多收 3 个 topic
  的成本可以忽略（QoS 0、无流量），换来的是"云端换写法不用动盒子"。

订阅矩阵（启动即订阅，`cleanSession=1` 所以**每次重连都会重订阅**）：

| topic | 用途 |
|---|---|
| `flexem.{boxNo}.command` / `flexem/{boxNo}/command` | 洗车命令 |
| `flexem.{boxNo}.read` / `flexem/{boxNo}/read` | 指定点位读取 |
| `flexem.{boxNo}.reboot` / `flexem/{boxNo}/reboot` | 重启请求 |

> 风险提示：如果云端**同时**往点号与斜杠两条 topic 下发同一条命令，
> 盒子会执行两次（`read` 无副作用，`command` 有）。当前云端只发一种，
> 未做去重；若以后出现，应在 `CommandParser` 里按 `time` 做 3 秒窗口去重
> （`RawMessage` 已经带了 `recvMs`，加一个 `lastCommandMs_` 就够）。

### 3.3 消息归一化

`MqttClient` 在 `start()` 时建立 `topicToEndpoint_`（完整 topic → endpoint）映射。
收到消息后查表，命中的才回调，未命中直接丢弃并打 DEBUG。
因此 `CommandParser` 只看到 `"command"` / `"read"` / `"reboot"` 三个字符串，
不需要 `rfind('/')`，也不会被 `flexem` 前缀的变化影响。

---

## 4. 下行链路（云端 → PLC）

```
云端 publish  flexem.123456.command
                 │  {"Manual start":1,"boxNo":"123456","time":1774500653}
                 ▼
MqttClient::handleMessage      topic→endpoint 归一化，查表命中
                 ▼
rawBuffer.push({endpoint,payload,nowMs()})     ← MQTT 线程到此结束（微秒级）
                 ┄┄┄┄┄┄ 第 1 段队列（削峰）┄┄┄┄┄┄
                 ▼
DownlinkPump::loop             50ms 一拍，popWait 拿到即解析
                 ▼
CommandParser::parse           try/catch 兜住全部异常，失败返回 UNKNOWN
                 │  UNKNOWN → 记 WARN 就地丢弃（不往业务队列塞垃圾）
                 ▼
bizBuffer.push(BusinessCommand) ← 下行泵到此结束
                 ┄┄┄┄┄┄ 第 2 段队列（解耦）┄┄┄┄┄┄
                 ▼
                 ▼
BusinessLayer::onCommand       50ms 一拍的分派线程取到，按 CommandType 分派
                 │  makeOperation() 统一补 reqSource=mqtt / boxNo / plcId
                 ▼
XxxWashCarService::startWashCar  状态机前置校验 → 写 PLC 点位
                 ▼
BusinessLayer::replySuccess    upBuffer.push({COMMAND_REPLY, {"message":"success","time":<int>}})
```

### 4.1 三个 endpoint 的职责

| endpoint | 处理 | 是否回执 |
|---|---|---|
| `command` | 按字段名调 start/stop/reset/... | 成功回 `commandreply`，失败**不回** |
| `read` | 解析点位名 → 采样 → 发 `readdata` | 以 `readdata` 本身作为应答 |
| `reboot` | 记录日志（`CommandType::REBOOT`） | 不回 |

### 4.2 `read` → `readdata` 的实现要点

请求是**平铺**的、值为 `null`：

```json
{"M187":null,"Y51":null,"D142":null,"M1":null,"boxNo":"123456","time":null}
```

处理分三步：

1. `CommandParser::extractRequestedPoints()` 遍历所有键，用 `washcar::cfg::parsePoint()`
   （`include/common/config/point.h`）判合法性 —— 只认 `D/Y/M/X/S` + 数字，
   `boxNo`、`time` 这类公共键由 `isMetadataKey()` 显式跳过；键名统一规范化成 `"M187"` 形式。
2. `IWashCarService::readPoints(points)` 采样。**实现只写在 `WashCarServiceBase` 一处**：
   - 走 `viewAllDeviceStatus()` 拿 PLC 状态缓存，不发额外 PLC 请求；
   - `D` 回 16 位整数，`M/Y/X/S` 回 `0/1`；
   - **采不到的点直接不进报文**（不用 0 冒充），日志里留 `skipped` 计数；
   - `points` 为空时回点位表 `acquisition` 里配的全部点。
3. `BusinessLayer::handleRead` 组装报文：业务点 + `boxNo` + `time`（整型）→ push `READDATA`。

> 为什么不复用 `getWashCarStatus()`：它带业务语义（`D142` 步骤、状态机描述、
> `message` 拼接），而且只处理 `D/Y/M/X`，不含 `S` 位（驿公里 S0~S81 会漏）。
> 采样和"查状态"是两件事，接口分开后各自都能独立演进。

### 4.3 `reboot` 的约定

协议明确"盒子只识别和记录，不直接执行重启"。默认行为是**只落一条 WARN 日志**
（`mqtt.rebootEnabled = false`）。

打开 `mqtt.rebootEnabled` 后，`BusinessLayer::dispatch` 的 `REBOOT` 分支会
`std::raise(SIGTERM)`，交给 `Common` 装的信号处理 → `g_running=false` → 三层依次 stop，
进程正常退出（退出码 0），再由 systemd / 守护脚本拉起。

不直接调 `exit()`：那样线程不会被 join、异步日志可能丢最后几条。
这个开关存在的原因是：现场调试偶尔需要远程重启，但绝不能让云端误下发就把生产盒子打停。

### 4.4 失败为什么不回执

协议原文："失败且盒子在线时，按对方确认不返回应答"。实现上 `replySuccess()`
只在 `ErrorCode::SUCCESS` 分支被调用。

代价是云端看不到失败原因（启动被状态机拦、PLC 写失败都表现为"没反应"）。
排查依赖盒子日志里的 `[Business] command failed: <type>, msg=<reason>`。
若云端后续要求失败可观测，应新增错误回执报文（`{"message":"<reason>","time":...}`），
而不是"所有失败都沉默"——改点是 `BusinessLayer::dispatch` 的 else 分支。

---

## 5. 上行链路（PLC → 云端）

四条上行通道，来源与触发方式各不相同，但**出口只有一个**：`upBuffer.push()`。

| endpoint | 触发方式 | 代码位置 | 报文 |
|---|---|---|---|
| `commandreply` | 命令成功后立即 | `BusinessLayer::replySuccess` | `{"message":"success","time":<int>}` |
| `monitor` | ①洗车中每 500ms ②点位变化每 3s | 洗车监控线程 / `executeValueChangeTask` | 平铺点位 + `boxNo` + `time` |
| `pushalarm` | 故障扫描每 3s；每日物料到点 | `executeFaultReportTask` / `executeDailyReportTask` | 平铺故障点（+`faultName`）+ `boxNo` + `time` |
| `readdata` | 收到 `read` 后 | `BusinessLayer::handleRead` | 平铺点位 + `boxNo` + `time` |

```
业务线程（4 个来源） → IReportSink::push(Report) → UplinkBuffer
                                                    │
                        ┌───────────────────────────┘
                        │ monitor/readdata：顶掉同类旧报文（replaced++）
                        │ pushalarm/commandreply：逐条排队，满 128 才丢最旧（dropped++）
                        ▼
UplinkPump（50ms 一拍）
   │  !isConnected() → 睡 200ms 再来，**不消费队列**
   │  已连接 → popWait → mqtt.publish(endpointFor(kind), payload)
   ▼
flexem{sep}{boxNo}{sep}{endpoint}
```

### 5.1 `UplinkBuffer`：为什么"同类只留最新"

`Report::replaceable()` 决定一条报文能否被同类的更新报文替换：

```cpp
bool replaceable() const {
    return kind == ReportKind::MONITOR || kind == ReportKind::READDATA;
}
```

- **`monitor` / `readdata` 描述的是"当前状态"**。断了 10 分钟再攒 200 条历史状态发出去，
  云端面板会闪 200 次再回到当前值——纯噪音。留最新一条就够。
- **`pushalarm` / `commandreply` 是"事件"**。故障报警丢掉等于漏报；
  命令回执丢掉云端会以为命令没执行。所以逐条排队，只有队列满（128）才丢最旧。

这条规则同时解决了"断链期间队列膨胀"：不需要额外的容量上限策略，
`monitor` 自然收敛到 1 条。

### 5.2 为什么 `ReportScheduler` 不再管链路

老实现里调度器拿一个 `linkReady` 回调，断链时**整轮跳过**，
避免 `executeValueChangeTask` 在发不出去的情况下清掉变化标志。

现在不需要了：`executeValueChangeTask` 只负责"检测到变化 → 组报文 → push"，
push 之后清标志是安全的——因为**报文已经在队列里了**，重连后一定会发出去
（哪怕中间被更新的 `monitor` 顶掉，那也是更新的状态，不会丢信息）。

所以调度器退化成一个纯粹的节拍器：

```
每 100ms 醒一次
  ├─ 距上次变化上报 ≥ timing.valueChangeIntervalMs   → executeValueChangeTask
  ├─ 距上次故障上报 ≥ timing.faultReportIntervalMs   → executeFaultReportTask
  └─ 距上次时钟扫描 ≥ dailyCheckIntervalMs（夹在 1000~5000ms）
        └─ 当前 hh:mm == timing.dailyReportHour/Minute
           且当天未触发（lastDailyReportDate_）
              → executeDailyReportTask
```

三个设计决定：

1. **一个线程而不是三个**：三个任务都是"读缓存 + 组报文 + push"，耗时在毫秒级；
   分三个线程只会让上游缓存锁竞争变多。100ms 节拍对 3s/60s 的周期完全够用。
2. **日报的检查间隔被夹到 `[1000, 5000]`ms**：`executeDailyReportTask` 内部按
   `hh==H && mm==M` 判断，调用必须落在目标那一分钟内。配置里填 60000 会整段跨过目标分钟
   导致漏报，所以 `ReportScheduler` 构造时把 `dailyReportCheckIntervalMs` 夹到 5s 以内，
   另用 `lastDailyReportDate_` 保证一天只触发一次。
3. **调度器不认识 MQTT**：构造只要一个 `IWashCarService&`。换 broker、换客户端、换队列实现
   都不影响它。

### 5.3 报文的公共字段

所有上行报文统一由 `WashCarServiceBase::addCommonFlexemFields(msg, boxNo)` 补：

```cpp
msg["boxNo"] = boxNo.empty() ? ConfigManager::instance().box().boxNo : boxNo;
msg["time"]  = washcar::timeutil::nowSeconds();   // int64
```

`time` 是**整型**。此前 `commandreply` 用 `std::to_string()` 拼成字符串，
`{"time":"1782119879"}` 与约定的 `{"time":1782119879}` 不一致，已改为整型。

---

## 6. 线程与并发

| 线程 | 谁创建 | 周期 | 干什么 | 与 MQTT 的关系 |
|---|---|---|---|---|
| mosquitto loop | `mosquitto_loop_start` | 库自己管 | 收发、心跳、重连 | 回调**只入队**，不执行业务 |
| 下行泵 | `PresentationLayer::start` | 50ms | 出队 → 解析 → 业务执行 | 唯一产生 `commandreply`/`readdata` 的地方 |
| 上行泵 | `PresentationLayer::start` | 50ms（断链 200ms） | 出队 → publish | 唯一调 `publish` 的线程 |
| 采集线程 | `DataLayer::start` | `acquisition.intervalMs`（默认 500） | 轮询 PLC 写缓存 | 无 |
| 洗车监控线程 | `startWashCarStatusMonitor` | `washcarMonitorIntervalMs`（500） | 洗车中推 `monitor` | 只 push |
| 上报调度线程 | `BusinessLayer::start` | 100ms 节拍 | 3s/5s 周期推上报 | 只 push |
| 主线程 | `main` | 200ms 等退出信号 | 装配 + 退出 | 按序停止 |

同步点：

- **队列**：`MessageBuffer` / `UplinkBuffer` 内部 `mutex_ + condition_variable`。
  `popWait(out, timeoutMs)` 就是"定时器"——有数据立刻醒，没数据最多等一个 tick，
  既不忙等、也不会漏掉"到点时刚好来了新报文"。
- **`publish` 只有一个调用者**（上行泵），所以 `publishMutex_` 实际上不会被争用，
  但保留它是必需的：`mosquitto_publish` 本身不是线程安全的，将来若有人从别处调也不会崩。
- **`DeviceService` 两把锁**：`mutex_` 保护 PLC 集合与缓存，`keyStateMutex_` 保护关键点位
  日志基准。必须分开——`logKeyStateChanges()` 在 `poll()` 的持锁段里被调用，
  `std::mutex` 不可重入，共用一把会自锁。
- **串行语义**：命令全部在下行泵一个线程里执行 → `m_currentStatus` 这个共享成员
  不会被并发写；`readPoints` 也在同一线程 → 与命令不会交叉。

**退出顺序**（`main.cpp`）：

```
commLayer.stop()      → 上行泵停（stop 时 shutdown 队列）→ 下行泵停 → mqtt.stop()
businessLayer.stop()  → ReportScheduler 停 → stopWashCarStatusMonitor()
dataLayer.stop()      → 采集线程停
```

先停上游：不再有新的 publish、不再有新的命令执行；再停业务：不再产生上报；
最后停采集：不再写缓存。反过来会在关闭瞬间产生"队列里有报文但没人发"的脏状态。

---

## 7. 配置项

```jsonc
"mqtt": {
  "brokerIp": "116.204.32.197",
  "port": 1883,
  "username": "admin",
  "password": "llx@123456",
  "heartbeatSec": 30,
  "reconnectBaseSec": 2,
  "reconnectMaxRetry": 5,
  "topicSeparator": ".",     // "." 生产抓包格式 | "/" Excel 配置写法
  "rebootEnabled": false     // false = reboot 只记录；true = 触发 SIGTERM 优雅退出
}
```

| 项 | 影响 | 改错的后果 |
|---|---|---|
| `topicSeparator` | 发布 topic 的分隔符；订阅端恒双格式 | 填错 → 云端收不到上报（订阅仍能收命令，下行正常、上行静默） |
| `rebootEnabled` | 是否真的触发退出（`BusinessLayer` 的 `REBOOT` 分支读取） | 误开 → 云端一发 reboot 盒子就停；关掉则只记日志 |
| `brokerIp` | **不能为空**：`ConfigManager::load` 解析时就会拒绝空值并返回 false | 留空 → 进程起不来，报 `mqtt.brokerIp missing or empty` |

`timing` 段决定上报节奏（`valueChangeIntervalMs` / `faultReportIntervalMs` /
`dailyReportCheckIntervalMs` / `dailyReportHour` / `dailyReportMinute`），
由 `ReportScheduler` 在构造时读取一次；改配置需重启生效。

两个**队列容量**来自配置的 `queueCapacity` 段（`downCapacity` 默认 256 / `upCapacity` 默认 128），
不在配置文件里——它们不是运维参数，是"内存里的余量"。

---

## 8. 与协议约定的对齐情况

| 约定项 | 状态 | 落点 |
|---|---|---|
| 发布 `flexem.{boxNo}.{endpoint}` | 已完成 | `MqttClient::buildTopic` |
| 订阅兼容 `flexem/{boxNo}/{endpoint}` | 已完成 | `MqttClient::start` |
| 订阅 `command`/`read`/`reboot` | 已完成 | 同上（6 条） |
| `read` → `readdata` 回点位值 | 已完成 | `CommandParser::extractRequestedPoints` + `BusinessLayer::handleRead` + `readPoints` |
| `commandreply` 仅成功回、`time` 整型 | 已完成 | `BusinessLayer::replySuccess` |
| `monitor` 变化上报 | 已完成 | `ReportScheduler` + `executeValueChangeTask` |
| `pushalarm` 故障上报 | 已完成 | `ReportScheduler` + `executeFaultReportTask` |
| 每日物料上报 → `pushalarm` | 已完成 | `ReportScheduler` + `executeDailyReportTask` |
| `reboot` 只识别不执行 | 已完成 | `BusinessLayer::dispatch` 的 `REBOOT` 分支 |
| 平铺 payload + `boxNo`/`time` 同级 | 已完成 | `addCommonFlexemFields` |
| 命令 MQTT 回调不阻塞 | 已完成（本轮重构） | `presentation` 的队列 + `DownlinkPump` |
| 生产盒子号不用于测试 | 属运维纪律 | — |

---

## 9. 已知限制与后续演进

| # | 限制 | 影响 | 建议 |
|---|---|---|---|
| 1 | 无持久化补发 | `UplinkBuffer` 是内存队列，进程退出即丢 | 需要时引入轻量 outbox（落盘 + 重连 flush），不要上 SQLite |
| 2 | 断链期间 `pushalarm` 会攒着 | 重连后云端收到"迟到的报警" | 若云端要求时效性，给 `pushalarm` 加过期时间（`Report` 带 `createdMs`，泵取出时丢弃超 N 分钟的） |
| 3 | 无失败回执 | 云端看不到拒绝原因 | 与云端约定错误报文格式后，在 `BusinessLayer::dispatch` 的 else 分支补 |
| 4 | 无双格式去重 | 云端若两种 topic 都发会执行两次 | 在 `CommandParser` 按 `time` 做 3 秒窗口去重（`RawMessage.recvMs` 已具备） |
| 5 | `readdata` 采不到的点直接省略 | 云端可能期望"全量键" | 与云端确认：省略 vs `null` |
| 6 | 命令串行执行 | 一条慢命令（驿公里 3.2s）会推迟后续命令 | 这是**有意的**：单机设备并发写 PLC 更危险。若要并发必须先在设备层加互斥 |
| 7 | 上报周期改了要重启 | — | 若需热更新，把 timing 改成原子变量 |
| 8 | **broker 在启动时不可达 → 进程直接退出** | 盒子上电早于网络就绪时，程序退出且不自动重来（`mosquitto_connect_async` 同步失败 → `MqttClient::start` 返回 false → `PresentationLayer::start` 返回 false → `main` `return -1`） | 改造前就有的行为，本轮**未改动**。建议：`PresentationLayer::start()` 对 `mqtt_.start()` 做有限次重试（如每 5s、共 12 次），或改成"启动失败不退出、只停表现层并后台重连" |
| 9 | `reboot` 走信号而非重启 | 打开 `rebootEnabled` 后也只是退出进程，靠守护脚本拉起 | 已按此设计；若盒子没有守护脚本，开这个开关等于远程关机 |
| 10 | 无 MQTT 连接状态对外可见 | 主线程无法感知断链时长 | 如果需要健康检查，让 `PresentationLayer` 暴露一个 `isOnline()`，主线程定期打日志 |

---

## 10. 扩展指南

| 想做什么 | 改哪里 |
|---|---|
| 让上报改走斜杠 | `config.json` 里 `mqtt.topicSeparator` 改 `"/"`，重启 |
| 加一个上行 endpoint | `common/message.h` 的 `ReportKind` 加一项 → `presentation/endpoint_map.h` 加一行映射 → 业务侧 `sendReport(新kind, payload)` |
| 加一个下行 endpoint | `src/presentation/mqtt_client.cpp` 的 `kSubscribeEndpoints` 加一项（`common/topics.h` 加宏）→ `CommandParser::parse` 加分支（`endpoint` 字符串判断） |
| 加一条命令 | `presentation/command_types.h` 加 `CommandType` → `command_parser.cpp::classifyCommand` 加字段判断 → `business_layer.cpp::dispatch` 加 `case` → 服务接口/子类实现 → 点位表 `services` 加写点位 |
| 调整上报节奏 | `config.json` 的 `timing.*`，重启 |
| 调整队列容量 | `config.json` 的 `queueCapacity.downCapacity` / `queueCapacity.upCapacity`，重启生效 |
| 换队列实现（比如落盘） | 只换 `common/buffer/*`：`MessageBuffer` 保 `push/popWait/shutdown`，`UplinkBuffer` 保 `IReportSink` 接口 |
| 换 MQTT 客户端库 | 重写 `presentation/mqtt_client.*`，保持 `start/publish/isConnected/MessageHandler` 四个契约 |
| 加链路健康检查 | `PresentationLayer` 暴露 `isOnline()`（转发 `mqtt_.isConnected()`），`main` 主循环定期打日志 |

---

## 11. 自测方法

1. **编译**：`bash wsl-cross/build-wsl.sh`（或盒子上 `bash build-on-box.sh`）。
2. **起进程**：`cd deployed && ./main`，启动日志应出现：

```
>>> 初始化数据层
  - 采集周期 500ms (来自 config.json 的 machine 段), 点位 29 项, 服务 4 项
>>> 初始化业务层
  - 海天 HaiTianWashCarService
>>> 初始化表现层
  - broker 116.204.32.197:1883, clientId=123456, 分隔符 "."
[DeviceService] acquisition started, intervalMs=500
[Report] scheduler started, valueChange=3000ms, fault=3000ms
[MQTT] 正在连接 116.204.32.197:1883, clientId=123456, 发布前缀 flexem.123456.<endpoint>
[DownlinkPump] started, tickMs=50
[UplinkPump] started, tickMs=50
[MQTT] 已连接, 发布前缀 flexem.123456.<endpoint>
[MQTT] 已订阅 6/6 条: flexem.123456.command, flexem/123456.command, ...
```

3. **下行**：往 `flexem.123456.command` 发 `{"Manual start":1,"boxNo":"123456"}`
   → 云端应收到 `commandreply`；盒子日志出现 `[DownlinkPump] ...` 与
   `[HaiTianWashCarService] ...`，没有 `[Business] command failed`。
4. **读点**：往 `flexem.123456.read` 发 `{"D142":null,"M1":null,"boxNo":"123456"}`
   → 应收到 `readdata`，且 `time` 是数字不带引号。
5. **变化上报**：手动改一个被 `REPORT_PROPERTY` 标记的点（或用虚拟 PLC），
   3s 内应收到 `monitor`。
6. **reboot**：发 `{"reboot":1}`，默认只看到 `[Business] reboot requested, recorded only`，进程不退出。
7. **断链收敛**：停掉 broker 十几分钟再拉起，重连后应只收到**一条**最新的 `monitor`
   （说明 `UplinkBuffer` 的"同类只留最新"生效），而不是历史一堆。
8. **队列计数**：正常退出时日志会打 `[UplinkPump] stopped, dropped=N, replaced=M`。
   `replaced` 大是正常的（monitor 被更新），`dropped` 持续增长说明上行发得太慢。

### 11.1 没有 broker 时怎么验

盒子上没有 mosquitto 服务端也没关系——MQTT 3.1.1 的必要子集很小（CONNECT/CONNACK、
SUBSCRIBE/SUBACK、PUBLISH QoS0、PINGREQ/PINGRESP、DISCONNECT），
用 ~120 行 Python 就能写一个临时 broker：

```python
# 接收 CONNECT(0x10) → 回 b"\x20\x02\x00\x00"
# 接收 SUBSCRIBE(0x80) → 回 b"\x90" + 长度 + packetId + 0x00*n
# 接收 PUBLISH(0x30) → 解析 topic 长度 + payload，打印出来
# 接收 PINGREQ(0xC0) → 回 b"\xd0\x00"
# 主动下发：b"\x30" + 变长长度 + topicLen(2B) + topic + payload
```

把 `config.json` 的 `mqtt.brokerIp` 指向 `127.0.0.1`、`port` 指向该脚本，
`plcEnabled` 设为 `false`，然后：

| 想验什么 | 怎么做 | 期望 |
|---|---|---|
| 下行链路 | 向 `flexem.{boxNo}.command` 推一条 PUBLISH | 日志出现 `[HaiTianWashCarService] WASHCAR_START_ENTER`，随后 `[Business] command failed: START, msg=no plc`（无 PLC 时必然失败，但**证明了报文走到了业务层**） |
| `reboot` | 推 `{"reboot":1}` | 日志 `[Business] reboot requested, recorded only` |
| `read` | 推 `{"D142":null}` | 日志 `readPoints rejected: no PLC data`，**不产生** `readdata`（符合"失败不回"） |
| 上行合并 | 先不启动上行泵、连推 5 条 `monitor`，再启动 | broker 只收到 1 条（最新那条） |
| 上行事件 | 推 2 条 `pushalarm` + 1 条 `commandreply` | broker 收到 3 条，全部保留 |

> 上表是本轮重构后实际跑通过的组合（`plcEnabled=false` + 假 broker），
> 也是目前唯一能在没有真机 PLC 的情况下验证"解析层 → 业务层 → 上行泵"全链路的方法。
