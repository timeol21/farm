# test 代码要求与思路

> 本文是 `washcar-test` 的**编码宪法**。
> 它不描述"代码现在长什么样"（那是《代码结构与运行逻辑-交接文档》的职责），
> 而是描述**"代码必须长成什么样，以及为什么"**——即历次提要求时定下的规矩、被纠正过的误解、以及每条规矩对应的落地位置。
>
> 改动本项目的任何代码之前，先过一遍第 2 章「十条铁律」。违反任何一条，即使能编译通过、能跑通，也算不合格。

---

## 1. 这份文档的定位

### 1.1 项目边界

`washcar-test` 是 `washcar-main` 的**裁剪重写版**，目标平台 RK3568：

| 保留 | 删除 |
|---|---|
| 海天 PLC（FX 串口）、驿公里 PLC（Modbus TCP） | 摄像头 / RTSP / 视频解码 |
| MQTT（libmosquitto，Flexem 协议） | AI 检测（yolov8 / 车牌识别） |
| 日志、配置、系统监控 | GPIO 门锁、温湿度传感器、串口直连设备 |
| 洗车业务流程与状态机 | HTTP 8080、SQLite 落库与断网补发、legacy `/device/*` 主题 |

因此本文所有规矩都建立在**"这是一个只有 PLC + MQTT 的最小闭环"**这个前提上。不要用"正式版也有"来论证某个模块该进来。

### 1.2 三条贯穿始终的判准

遇到本文没写到的情形，用这三条自己判断，不要回来问：

1. **归属判准**：这个代码回答的是"消息怎么进出"（表现层）、"业务该怎么反应"（业务层）、"硬件怎么读写"（数据层），还是"谁都要用但不是业务"（common）？
2. **时序判准**：这段代码是"一次性、会失败、失败要当场知道"（→ `init`），还是"开始持续工作"（→ `start`）？
3. **依赖判准**：这两个模块是不是必须互相认识？如果是，中间加一个接口或一个数据结构，放到两者都能看见的地方（`common`）。

---

## 2. 十条铁律

以下每条都是**用户明确提过的要求**，按提出顺序编号。每条给出：要求 → 为什么 → 怎么落地 → 反面样例。

### 铁律 1：严格分层，不许跨层认识

**要求**：`presentation` / `business_layer` / `data_layer` 三层 + `common` 公共设施。层与层之间只能通过**接口**或**数据结构**通信，不许 `#include` 对方的实现头文件。

**为什么**：任何一层要替换（换 MQTT 库、换 PLC 品牌、业务逻辑重写）时，改动不能外溢。

**落地**：

| 层 | 层根 | 对外暴露 | 依赖方向 |
|---|---|---|---|
| 表现层 | `PresentationLayer` | `init/start/stop` | 只依赖 `common` |
| 业务层 | `BusinessLayer` | `init/start/stop` | 依赖 `IDeviceService`（接口） |
| 数据层 | `DataLayer` | `init/start/stop` + `device()` | 只依赖 `common` |
| 公共 | `Common` | `init/start/stop/running` | 谁都不依赖 |

**反面样例**（本项目真实踩过）：
- 正式版里 `data_layer/camera/camera.cpp` 反向 `#include business_layer/...` —— 数据层依赖业务层，倒挂。
- `business_layer` 曾经直接持 `MqttSender` 具体类 —— 换成 `IReportSink` 接口后，业务层不知道 MQTT 存在。

**自检命令**：
```bash
grep -rn '#include "business_layer' include/data_layer include/presentation src/data_layer src/presentation   # 必须 0 行
grep -rn '#include "presentation'    include/business_layer src/business_layer                                  # 必须 0 行
```

---

### 铁律 2：main.cpp 只做装配，一行实现细节都不许有

**要求**：`main` 里只能出现"初始化 / 启动 / 停止"的调用。工作目录、读配置、日志、信号、`while` 循环、`try/catch`、局部 `std::atomic<bool> g_running`——全部不许出现。

**为什么**：`main` 是唯一能一眼看出"系统由什么组成、以什么顺序启动"的地方。混进实现细节后，启动顺序就埋进了代码里，接手的第一个人就会看漏。

**落地**：`src/main.cpp` 严格五段，全部内容就是五次调用：

```
1. common.init(argv0)                  // 工作目录 + 配置 + 日志装配 + 线程预算 + 系统监控 + 信号
2. common.start()                      // 日志线程 + 配置 dump + 采样线程
3. 三个队列 init                        // rawBuffer / bizBuffer / upBuffer
4. 三层 init -> 三层 start             // 顺序见 §4.1
5. waitForExit -> 三层 stop -> common.stop
```

**反面样例**（本项目真实发生过）：
- 初版 `main` 里有 `std::string exeDir = ...; initLogging(); initRuntime(); std::signal(...)` 和 `while(g_running)` —— 被要求"该放初始化的放初始化，该放开始的放开始"。
- 第二版把 config + log 抽出 `Runtime`，又被要求合成**一个**入口：main 里只能看到"读配置文件的初始化还有启动"这两层语义，不该看见 config 和 log 是两个东西。最终类名定为 `Common`（语义明确，不用 `Common` 这种含义泛的词）。

**判据**：`main.cpp` 超过 150 行，或出现任何 `#include <thread>` / `<fstream>` / `<csignal>`，就是违规。

---

### 铁律 3：表现层 = MQTT 实现 + 原始队列 + 解析 + 业务队列

**要求**（用户原话）：表现层里放 MQTT 的实现，拿到原始 MQTT 消息放到消息队列；然后有线程从队列取原始消息，把**原始消息**转成**业务消息**放到业务消息队列；业务层有对应线程从业务消息队列取出来交给业务处理。

**为什么**：MQTT 回调线程是 mosquitto 的网络线程，**必须立刻返回**。任何解析、业务判断、PLC 等待都不能在里面做——否则一慢就把心跳和收包一起堵死。

**落地**（两段生产者-消费者）：

```
mosquitto 网络线程
   └─ MqttClient 回调：只组 RawMessage {topic, payload, ts}
         └─ rawBuffer.push()            ← 立刻返回
                ↓
   DownlinkPump 线程（50ms 一拍 popWait）
         └─ CommandParser::parse(RawMessage) -> BusinessCommand
                └─ bizBuffer.push()
                       ↓
   BusinessLayer::dispatchLoop 线程（50ms 一拍 popWait）
         └─ onCommand(BusinessCommand) -> 洗车服务 / 数据层
```

**关键约束**：
- `CommandParser` **只翻译不执行**——不认识 PLC、不认识洗车流程，只把各家命令方言（海天 `Manual start`、驿公里 `washcar stop`）统一成 `CommandType`。
- 命令方言必须**全部集中**在 `CommandParser::classifyCommand` 一个函数里。新增机型改这里，不许散落到业务层。
- 上行只有一段队列：业务层 → `upBuffer` → `UplinkPump` → MQTT。因为上行不需要二次解析。

---

### 铁律 4：业务层读 PLC 状态 = 读内存，永远不现场读 PLC

**要求**：PLC 点位由采集线程按固定频率读进内存缓存；业务层要数据时读的是**内存快照**，不是去开串口。

**为什么**：三层后果，缺一不可记：
1. 业务线程和采集线程抢同一条串口 → 每条命令都要等一整轮采集；
2. PLC 断线时快照恒为空，**每条命令都去开一次串口**（对串口是灾难）；
3. 现场值与按快照设计的判定逻辑混用 → 判定结果不可复现。

**落地**：
- `DeviceService` 的采集线程是**全系统唯一 PLC 读者**。
- `viewAllDeviceStatus()` / `readPoints()` / `pointStatus()` 一律走 `gateway_.snapshot()`，返回 `PlcPointValues`。
- 缓存为空就返回空表——**不许有兜底 `poll()`**。调用方已有处理：状态机不判定、`readPoints` 回 `WASHCAR_NO_PLC`。宁可报错，不许偷偷读硬件。
- 唯一例外：`DeviceService::start()` 起线程**之前**同步 `poll()` 一轮，保证 `start()` 返回时缓存已就绪，上电后第一条命令不落空。这是全系统唯一一处采集线程之外的 PLC 读，且必须写注释说明。

**反面样例**（本项目真实踩过）：`viewAllDeviceStatus()` 里那句 `if (!gateway_.hasSnapshot()) gateway_.poll();` —— 已删除。

---

### 铁律 5：值没变就不写内存

**要求**：采集到的一轮点位数据，与缓存逐点比较，**相同的不写**，只写变化的点；本轮读失败的点**保留旧值**，不许被清空。

**为什么**：整表覆盖会让一次串口丢包丢掉整轮数据；而"值不变也写"会让下游的变化检测、日志、上报全部失效（每轮都等于"有变化"）。

**落地**：`DeviceService::mergeIntoCache(plcId, fresh)`
- 逐点 diff：相同 → 跳过；不同 → 只写该点；
- 本轮没读到（读失败）→ 保留旧值，不清零；
- `changed` 标志**只在真正写入时**置位（首轮除外）；
- `logKeyStateChanges` 只在 `changed` 为真时调用。

**配套**：`DeviceService::Stats{polls, freshPoints, writtenPoints}`，`stop()` 时打印一行 `unchanged=xx%`，用来证明"没有空写"。

**验收判据**：值不变连采 5 轮，`writtenPoints` 必须为 0；改 1 个点，`writtenPoints` 必须为 1。

---

### 铁律 6：所有线程统一到 common 管理，从那里取

**要求**（用户原话）：线程不放在各层，统一到 common 管理，需要线程从这里取，**防止超出系统可以接受的线程数量**。

**为什么**：盒子是 4 核 ARM、内存有限。线程数无节制增长（每机型、每设备、每任务各起一条）会在现场表现为"启动正常、跑几天卡死"。必须有一个全局账本。

**落地**：`common/thread/ThreadManager` + `ManagedThread`

```cpp
ManagedThread t;
if (!t.start("acquisition", [this] { loop(); })) {   // false = 超预算 / 已启动
    LOG_ERROR("[DeviceService] 线程预算不足，采集未启动");
    return false;
}
```

能力清单：
| 能力 | 说明 |
|---|---|
| 全局上限 | `concurrency.maxThreads`（默认 16），`Common::init` 第 4 步设置，早于任何线程 |
| 超限拒绝 | 返回 `false` 并打 ERROR，**附带当前存活线程名列表** |
| 线程体兜底 | 统一 `try/catch`，任一循环抛异常只死该线程，不再 `std::terminate` 整个进程 |
| 真实存活数 | RAII 守卫，线程函数返回即销号，`count()` 不等 `join` |
| 退出对账 | `Common::stop()` 打印 `剩余 N 条, 累计拒绝 M 次` |

**已迁移的线程创建点**：共 **9 处调用，8 类线程**（`washcar-monitor` 按机型写了两处，运行时只启其中一个）：

| 线程名 | 创建位置 | 职责 |
|---|---|---|
| `log` | `common/log/log.cpp` | 异步日志 worker |
| `sysmon` | `common/sysmon/system_monitor.cpp` | 盒子指标采样 |
| `acquisition` | `data_layer/device_service.cpp` | PLC 采集（唯一 PLC 读者） |
| `downlink` | `presentation/downlink_pump.cpp` | 原始报文 → 业务命令 |
| `uplink` | `presentation/uplink_pump.cpp` | 上行报文 → MQTT |
| `biz-dispatch` | `business_layer/business_layer.cpp` | 业务命令分派 |
| `report` | `business_layer/report/report_scheduler.cpp` | 上报节拍 |
| `washcar-monitor` | `washcar/HaiTianWashcar_service.cpp`、`YiGongLiWashcar_service.cpp` | 洗车过程状态监控 |

**唯一例外**：`mosquitto_loop_start()` 内部的网络线程库自带，无法接管。已知并接受。

**为什么不做固定 N 个 worker 的线程池**（重要，避免后续重复提议）：
本项目的线程**全部是长驻阻塞循环**（采集、两个泵、两个调度器），不是"来一个任务跑一下"的短任务。
池子 < 循环数 → 长驻线程占满池子，后到的必然死锁；池子 == 循环数 → 退化成 `std::thread` 的一层壳。
要防的不是"线程池大小"，而是"**无节制增长**"，所以做的是**有上限的登记 + 拒绝 + 可观测**。

---

### 铁律 7：配置从 config.json 读，但只读可调的活参数

**要求**：能调的都从配置文件读，不许硬编码；但**属于实现结构的参数不进配置**。

**为什么**：硬编码的值现场改不了，必须重新编译；而把"泵每拍 50ms"这类内部节拍开放出去，只会增加"填错后行为不可预测"的风险。

**属于配置的**（`config.json` 十大段）：
`box`（boxNo / boxType / logLevel / logKeepDays / plcEnabled）、`mqtt`（brokerIp / port / 账号 / 心跳 / 重连 / topicSeparator / rebootEnabled）、`plcSerial`（端口 / 波特率 / 超时 / 帧间隔）、`plcTcp`、`stateMachine`、`timing`（五类上报周期）、`machine`（点位表）、`queueCapacity`（三个队列容量）、`sysmon`（开关 / 周期）、`concurrency`（maxThreads）。
`box` 段里**没有**日志目录：日志固定落 `<程序目录>/data/log`，由日志系统自己决定，见 §3.4。

**明确不进配置的**（有意为之）：pump 节拍 50ms、断链空转 200ms、退出轮询 200ms、report 调度 100ms。

**两条硬规矩**：
1. **配置读取路径唯一**：`<程序目录>/config.json`（即 `deployed/config.json`）。程序**不再 `chdir`**，而是由 `ProgramPath` 算出程序目录后拼绝对路径。**根目录的 `config.json` 只是构建时被 `build-on-box.sh` / `build-wsl.sh` 拷过去的源文件，改它不生效。** 现场改配置永远改 `deployed/config.json`。
2. **不许有死配置**：字段写进结构体、能被模板写出来、却没有任何消费者——这类字段必须要么接上、要么删掉。（现存一个：`stateMachine.logStateTransition`，待清理。）

**`topicSeparator` 的特别说明**：这个参数只影响**发布**（`buildTopic` 用 `separator_`）；**订阅**永远同时注册 `.` 和 `/` 两种写法（3 个 endpoint × 2 = 6 条）。所以填错的表现不是"连不上"，而是"**能控、但看不到上报**"——命令到了，回执静默丢失。改完要重启才生效，非法值静默回退 `"."`。

---

### 铁律 8：跨层契约放 common，不放任何一层的私有目录

**要求**：两个层都要用的数据结构（原始报文、业务命令、点位快照），放在 `common/` 下。

**为什么**：放到 `presentation/` 会让 `business_layer` 反向 `include` 表现层；放到 `business_layer/` 同理。`common` 是唯一双方都能看、且不产生依赖的方向。

**落地**：
- `common/message.h` —— `RawMessage`、`CommandType`、`BusinessCommand`、`PlcPointValues` 等全部跨层契约；
- `common/plc_point.h` —— 点位快照值（数据层产出、业务层消费）；
- `common/buffer/` —— 队列实现（三层都用）。

**反面样例**：`CommandType` 一度放在 `presentation/command_types.h`，`business_layer` 为了用它必须 `include` 表现层 —— 已下沉到 `common/message.h` 并删除原头文件。

**派生规矩**：数据层的公开接口签名里**不许出现任何 PLC 类型**（`FxPlcStatus`、`PlcId`、`IPlcInstance`）。`IDeviceService` 只返回 `PlcPointValues` 这种纯粹的值对象。

---

### 铁律 9：初始化与启动分离，失败必须当场知道

**要求**：每个模块都要有独立的 `init()` 和 `start()`。

**为什么**：
- `init` = 一次性、会失败、失败要**立刻**知道（读配置、建目录、开文件、绑 socket）；
- `start` = 开始持续工作（起线程、开始收包）。

合起来会产生"配置读完了但日志线程起不来"的半截状态，也没法控制从哪一刻开始真正落盘。

**配套规矩**：
1. 任何 `init` / `start` 失败都要走统一收尾，**不许直接 `return -1`**——否则已起来的线程不 `join`、日志不 `flush`，退出时最后几条日志（往往正是失败原因）会丢。`main` 里的 `fail()` lambda 就是干这个的。
2. **`stop` 顺序与 `start` 严格相反**，且每一层内部也有顺序（见 §4.2）。
3. 可降级的组件（`sysmon`）失败只告警不拦截——判断依据是"它挂了洗车业务还能不能干活"。

---

### 铁律 10：物理纪律——LF 行尾、产出不出项目根

**要求 A：脚本必须是 LF 行尾。**
Windows 侧任何就地改写文本文件的操作，必须显式 `newline='\n'`，否则 `\r` 会把整份文件转成 CRLF。`.sh` 转 CRLF 后 `set -euo pipefail` 的选项名会变成 `pipefail\r`，报错信息还会因为回车被冲成乱序（现场表现：`：无效的选项名h: 第 24 行： set: pipefail`）。

- 已有 `.gitattributes`（`* text=auto eol=lf` + 逐类型声明）；
- 自检：`grep -rlI $'\r' . | grep -v lib/` 必须为空；
- 现场止血：`sed -i 's/\r$//' build-on-box.sh && bash -n build-on-box.sh`。

**要求 B：所有产出不出 `washcar-test/` 根目录。**
构建目录 `build-wsl/` `build-box/`、产物 `deployed/`、缓存 `.ccache/`、数据 `deployed/data/`，全部在项目内。
两个外部路径是**只读工具链**，不算产出：`/opt/rk3568-sysroot`（交叉 sysroot）、`/tmp/vplc-pty`（仅开发用的虚拟 PLC）。

**自检**：`grep -rn '/home/ztl/washcar/' *.sh wsl-cross/*.sh` 必须 0 行（这是历史遗留的错误落点）。

---

## 3. 分层细则：每层该放什么 / 不该放什么

### 3.1 表现层 `presentation/`

| 该放 | 不该放 |
|---|---|
| `MqttClient`（libmosquitto 封装，回调只入队） | 任何业务判断（"这条命令能不能执行"） |
| `DownlinkPump`（取原始消息 → 解析 → 投业务队列） | 直接调 `IWashCarService`（已改为投队列） |
| `CommandParser`（命令方言 → `CommandType`） | 任何 PLC / 硬件知识 |
| `UplinkPump`（取上行报文 → publish） | 决定"什么时候该上报什么"（那是业务层的节拍器） |
| `EndpointMap`（上报类型 → topic 的唯一映射） | 拼 topic 字符串的散落代码 |

**层根**：`PresentationLayer::init(rawBuffer, bizBuffer, upBuffer)` / `start()` / `stop()`。

> 历史澄清：曾经有一个独立的 `comm_layer`（通讯层）承担 MQTT + 两个泵。那是第一轮"把 mqtt 放在通讯层"这个说法被理解成"新开一层"的产物，**不是用户要的东西**。用户要的只是"把 log 和 config 做一层简单封装"，那件事由 `Common` 承担。所以 `comm_layer` 已整体并入 `presentation`，目录删除。

### 3.2 业务层 `business_layer/`

| 该放 | 不该放 |
|---|---|
| `BusinessLayer`（分派线程 + `onCommand` 路由） | MQTT 细节（只经 `IReportSink` 发） |
| `IWashCarService` 及海天 / 驿公里实现 | 串口 / socket 操作 |
| `WashCarStateMachine`、`WashCarOperation` | topic 字符串拼接 |
| `ReportScheduler`（**何时**报**什么**） | **怎么**发（交给表现层） |
| 业务规则、准入判定、故障码翻译 | 点位表解析（配置层的事） |

**关于 `business_layer/report/` 为什么在业务层**：`ReportScheduler` 决定的是业务规则——值变化报、故障报、每日物料报，各自什么周期、什么条件、报哪些字段。这是业务判断，不是通讯判断。"怎么发出去"被抽象成 `IReportSink` 交给表现层。放在表现层会让表现层拥有业务节奏，放在 common 会让 common 认识业务。

**关键**：业务层持 `IDeviceService`（接口），持 `IReportSink`（接口），**不持任何具体实现类**。

### 3.3 数据层 `data_layer/`

**只放 PLC 相关。**

| 该放 | 不该放 |
|---|---|
| `IPlcInstance` + `fx_plc/`（含 Modbus TCP） | 摄像头、AI、GPIO、传感器（已全删） |
| `DeviceService`（采集 / 缓存 / 变化检测 / 写点位） | 业务判定 |
| `DeviceService`（采集线程 + 快照对外） | MQTT |
| `IDeviceService`（唯一跨层契约） | 任何 `#include "business_layer/..."` |

统一收在 `fx_plc/` 下：`fx_plc_instance`（海天）、`tcp_plc_instance`（驿公里），
实例由 `FxPlcInstanceSet` 登记——沿用主项目封装，不改其实现。

### 3.4 公共设施 `common/`

| 子模块 | 职责 | 备注 |
|---|---|---|
| `config/` | 配置解析 / 校验 / 模板生成 | **实现一行未改**，`Common` 只负责"什么时候读" |
| `log/` | 日志系统（队列 + 异步线程 + File/Console sink） | `LOG_*` 宏必须 `init` 后就能用；目录自己定（`<程序目录>/data/log`） |
| `sysmon/` | 盒子自身指标采样 | 写**专用文件** `sys_YYYY-MM-DD.txt`，与业务日志物理分开 |
| `thread/` | `ThreadManager` + `ManagedThread` | 见铁律 6 |
| `buffer/` | `MessageBuffer<T>` / `UplinkBuffer` | 有界，满则丢最旧并计数 |
| `path/` | `ProgramPath`：程序目录 + 相对转绝对 | **不做 `chdir`**，只算路径 |
| `message.h` / `plc_point.h` / `topics.h` | 跨层契约 | 见铁律 8 |
| `time/` | 时间格式化工具 | 唯一实现，不许各文件自带 `#ifdef _WIN32` |
| `common.h` | `Common`：init/start/stop 顺序编排 | 只编排，不实现 |

**`Common::init` 六个阶段（顺序是硬约束）**：
```
1.   ProgramPath::init          // 算出程序目录，供后面拼绝对路径（不改进程工作目录）
2.   ConfigManager::load        // 不存在则自动写模板；只解析文件，不写日志不建目录
3.   LogSystem::init            // 建 <程序目录>/data/log、装配 sink/logger（不开线程）
                                // 返回后 LOG_* 全项目可用
4.   ThreadManager::setLimit    // 必须早于任何线程启动
5.   SystemMonitor::init        // 失败只告警；它写日志目录，所以必须排在 3 之后
6.   安装 SIGINT/SIGTERM
```

> **为什么配置排在日志前面**：日志的级别 / 保留天数 / 队列容量本身就写在 `config.json` 里。
> 不先读配置，日志只能先按写死的值装起来。配置解析这一步不写日志（失败走 stderr），
> 挡在日志前面不会造成任何日志丢失。**日志是第一个"装配完即可用"的子系统**，这一点没有变。

**`Common::stop` 顺序**：`sysmon → log`。反了会丢采样行（日志线程先死，采样还在写）。

---

## 4. 启动与关闭的时序纪律

### 4.1 启动顺序（`main` 第 4 段）

```
dataLayer.init()          // PLC 连接、点位表
businessLayer.init(...)   // 装配洗车服务、上报节拍器
presentationLayer.init()  // MQTT 客户端（此时还不连）

dataLayer.start()         // 采集线程 + 启动前同步补一轮快照
businessLayer.start()     // 关键：分派线程必须先就位
presentationLayer.start() // MQTT 连接，开始往 rawBuffer 投消息
```

**为什么业务层先于表现层**：反了的话，上电瞬间收到的第一条命令会在 `bizBuffer` 里等到分派线程起来。虽然不会丢，但日志顺序不直观、排查时容易误判成"命令丢了"。

### 4.2 关闭顺序（`main` 第 5 段）

```
presentationLayer.stop()  // 先断上游：不再产生新报文
businessLayer.stop()      // 再停业务：不再产生新上报
dataLayer.stop()          // 最后停采集：不再写缓存
common.stop()             // sysmon -> log，让上面每一步的收尾日志都落盘
```

层内部也有顺序，例如 `BusinessLayer::stop()`：
`分派线程（shutdown 队列 + join）→ ReportScheduler → 洗车状态监控`。

**判据**：任何 `stop()` 里的顺序调整，都必须能回答"如果反了会丢什么"。

---

## 5. 代码风格与可读性要求

1. **注释写"为什么"，不写"是什么"**。`// i++` 无意义；`// 反了会丢采样行` 有价值。
2. **头文件顶部写设计意图**。本项目的每个层根、每个 `common` 子模块，头文件里都有一段说明"它负责什么、不负责什么、为什么这样切"。新增模块照此办理。
3. **不许留死代码**。删掉的模块要真删（本项目删了摄像头、AI、GPIO、温湿度、串口直连、legacy MQTT、`.backup-refactor/` 快照）。留注释掉的代码块等于留一个陷阱。
4. **日志分级**：`ERROR` 只给"需要人介入"的；重连重试前 3 次 `WARNING`、到限一次 `ERROR`、之后降 `DEBUG`（防刷屏）。
5. **中文标识**：给用户看的启动横幅、错误提示用中文；代码标识符、日志 key=value 用英文。
6. **单文件职责单一**。`main.cpp` 150 行上限（见铁律 2）；出现第二个职责就拆。

---

## 6. 明确"不做"的事

以下都是**有意识放弃**的，不要以"完整性"为由加回来：

| 不做 | 原因 |
|---|---|
| 断网补发 / 落库重发 | 内存队列 + 丢弃即可；SQLite 依赖已整体移除 |
| 固定 N worker 线程池 | 见铁律 6 的论证 |
| MQTT 回调里做业务处理 | 会堵死 mosquitto 网络线程 |
| 业务层现场读 PLC 兜底 | 见铁律 4 |
| 把内部节拍（50ms/200ms）配置化 | 见铁律 7 |
| 摄像头 / AI / HTTP 调试口 | 本项目定位就是 PLC + MQTT 最小闭环 |
| 软链接形式的第三方库 | Windows 上会退化成 0 字节文件，传盒子后 CMake 认不出（历史事故） |

---

## 7. 验收清单（改完代码逐条打勾）

**分层**
- [ ] `grep -rn '#include "business_layer' src/data_layer src/presentation include/data_layer include/presentation` → 0 行
- [ ] `grep -rn '#include "presentation' src/business_layer include/business_layer` → 0 行
- [ ] 数据层公开接口签名里没有 `FxPlc` / `PlcId` / `IPlcInstance`

**main**
- [ ] 仍是五段，无实现细节，无 `<thread>` / `<fstream>` / `<csignal>`
- [ ] 每个失败分支都走 `fail()`，不裸 `return -1`

**线程**
- [ ] `grep -rn 'std::thread' src/ include/ | grep -v thread_manager | grep -v 'thread::id'` → 0 行
      （`std::thread::id` 是线程号字段，不算违规；`std::thread t` 这类**创建**才算）
- [ ] 新增线程走 `ManagedThread::start(name, fn)`，名字唯一
- [ ] 启动日志有 `线程预算: N 条`，退出日志有 `剩余 0 条`

**数据流**
- [ ] 值不变连采 5 轮，`writtenPoints == 0`
- [ ] 业务层读快照 100 次，PLC 读调用次数零增长
- [ ] MQTT 回调函数体内只有组 `RawMessage` + `push`

**配置**
- [ ] 新增字段在 `config_object.h` + 序列化 + 默认值 + dump 四处齐全
- [ ] 没有"写了没人读"的死字段
- [ ] 现场改的是 `deployed/config.json`，不是根目录那份

**物理**
- [ ] `grep -rlI $'\r' . | grep -v lib/` → 空
- [ ] 所有脚本 `bash -n` 通过
- [ ] 无新增项目外路径

**端到端**
- [ ] 从零构建零 error（31 个编译单元量级）
- [ ] 假 broker 注入 `{"Manual start":1}` → 日志出现 `startWashCar rejected: no PLC available`
- [ ] SIGTERM 后退出码 0，顺序 `UplinkPump → DownlinkPump → MQTT → biz-dispatch → ReportScheduler → acquisition`

---

## 8. 演进史：哪些要求纠正了什么

留这一节是为了**防止同一个误解再犯**。

| 轮次 | 要求 | 我当时的做法 | 后续修正 |
|---|---|---|---|
| 1 | "mqtt 放在通讯层" | 新开 `comm_layer` 层 | 用户澄清：**只是想把 log 和 config 做一层简单封装**，不是要新层。`comm_layer` 已并入 `presentation` |
| 2 | "main 里不要这么多" | 抽出 `Runtime` 管 config+log | 用户要求合成**一个**入口的 init/start。`Runtime` 已删，职责并入 `Common` |
| 3 | "表现出来要有个解析层" | 解析最初和泵混在一起 | 拆出独立 `CommandParser`，只翻译不执行 |
| 4 | "线程统一到 common" | 各层各自 `std::thread` | 新增 `ThreadManager`，9 处创建点全部迁移 |
| 5 | "业务层不该现场读 PLC" | `viewAllDeviceStatus` 里有兜底 `poll()` | 删兜底；采集线程成为唯一 PLC 读者 |
| 6 | "值不变不写内存" | 整表 `std::move` 覆盖 | 改逐点 diff，读失败保留旧值 |
| 7 | "还有哪些参数能配置" | 审计出 3 个死配置 | 接上 MQTT 重连两项；`logStateTransition` 仍待清理 |
| 8 | "名字用简单的英文拼，不许有歧义词" | `Common` / `queuing()` / `chdirToExeDir()` | `Common`→`Common`；`queuing`→`queueCapacity`(段名同步，旧键仍可读)；删 `chdir`，改 `ProgramPath` 显式拼绝对路径；日志目录改由日志系统自定，`box.logPath` 下线 |

---

## 9. 一句话总结

> **层次靠接口隔开，main 只做装配，MQTT 回调只入队，业务读内存不读硬件，线程从 common 取且有上限，配置只放活参数，产出不出项目根。**

这七件事做到了，剩下的都是细节。
