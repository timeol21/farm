# washcar-test —— 洗车机主控（PLC + MQTT 精简版）

> 从 `washcar-val` 裁剪而来，只保留三条链路：**洗车业务状态机 + PLC 通讯 + MQTT 通讯**。
> 移除摄像头、AI 检测、HTTP 服务、SQLite 落库、MQTT 断网补发。
> 目标平台：RK3568 盒子（aarch64 / glibc 2.31 / Ubuntu 20.04）。

📄 **要快速理解代码 / 交给另一个 AI 改代码，先读 [`docs/代码结构与运行逻辑-交接文档.md`](docs/代码结构与运行逻辑-交接文档.md)**
——分层、线程模型、三条主链路、点位表机制、状态机规则、MQTT 协议、已知缺口与改动指南都在里面。
编译部署细节见 [`docs/编译与部署-两条路线.md`](docs/编译与部署-两条路线.md)。

## 1. 和另外两套代码的关系

| 项目 | 定位 | 产物 | 依赖 |
|---|---|---|---|
| `washcar-main` | 正式版（原始） | `main` 7.1 MB | rknn / rga / ffmpeg / openssl / sqlite |
| `washcar-val` | 测试版（六批次精简 + 摄像头封装） | `main` 7.1 MB | 同上 |
| **`washcar-test`** | **本项目：只留 PLC + MQTT** | **`main` 3.1 MB** | **mosquitto + pthread** |

三套代码**完全独立**：各自的源码、构建目录、运行目录互不覆盖。

## 2. 保留了哪些业务逻辑

| 模块 | 说明 |
|---|---|
| 洗车状态机 | `ServiceState { IDLE, WORKING, FAULT }`，海天/驿公里各自的状态判定策略 |
| 海天 PLC | 三菱 FX 串口协议（`readS/forceX/forceY/forceM/readD/readY/readM/readX`） |
| 驿公里 PLC | Modbus TCP（`TcpPlcInstance`） |
| 洗车服务 | `HaiTianWashCarService` / `YiGongLiWashCarService`：启动、停止、复位、中止、停止营业、安全复位、点位操作 |
| 点位表 | 数据驱动：`config` 里的 29 个信号 + 4 个服务定义采集什么、下发什么 |
| MQTT | `flexem.{boxNo}.{endpoint}` 协议（分隔符可配），**用 libmosquitto 实现**：command/read/reboot 三路下行，commandreply/monitor/readdata/pushalarm 四路上行 |
| 队列与线程 | **三条有界队列**（原始 `RawMessage` 256 / 业务 `BusinessCommand` 256 / 上行 `UplinkBuffer` 128）+ 定频泵；线程全部由 `common/thread/ThreadManager` 发放，全局上限 16 |
| 上报调度 | `ReportScheduler` 单线程驱动变化/故障/每日物料三类主动上报 |

## 3. 删掉了什么

- 摄像头全链路（`Camera` / `VideoCapture` / FFmpeg 解码 / 关键帧）
- AI 检测（yolov8 / 车牌 OCR / rknn NPU 推理）
- HTTP 服务（原 8080 端口）
- SQLite 数据库 + MQTT 断网补发（outbox）
- 三层架构的抽象层（`IDevice` / `AcquisitionTask` 的大部分派生类）

> **分层**：`presentation`（表现层，含解析：MQTT + 两段队列 + 两个泵 + `CommandParser`）
> → `business_layer`（业务：命令分派线程 + 洗车服务 + 上报调度）
> → `data_layer`（只放 plc：`fx_plc` / `tcp_plc`）。
> `common` 装不属于任何一层的东西：config / log / sysmon / **thread** / buffer / message。
> `main.cpp` 退化成纯装配——连程序目录解析、读配置、sink 装配、`std::signal` 都下沉到了
> `common/common.h`。程序**不再 `chdir`**：路径由 `common/path/ProgramPath` 算绝对路径，
> 配置和日志各自拼自己的绝对路径。详见交接文档 §4 §5 §6 §7。

**线程统一从 `common/thread/ThreadManager` 取**（上限 `concurrency.maxThreads`，默认 16）：
任何地方都不要再写 `std::thread`，否则线程数失控时盒子会被拖死，且现场看不出是谁起的。
稳态 8 条线程，名字都带业务含义（`log` / `sysmon` / `acquisition` / `downlink` /
`biz-dispatch` / `uplink` / `report` / `washcar-monitor`）。详见交接文档 §6.1。

**点位数据的流法是单向的**，改代码时别破：

```
PLC ──(采集线程，每 intervalMs 一轮)──► DeviceService 缓存 ──► 业务层（只读快照，零 IO）
```

两个关键约束：

1. **读 PLC 只有采集线程**（写点位 `forceFxM/forceFxX` 是命令通路，不算读）。
   业务层任何"读"都只是从内存拷贝一份快照，最坏滞后一个采集周期。
2. **写缓存只在值变化时**。每轮照常把点位表读一遍，但逐点比对后只写变化的点；
   读失败的点保留旧值、不删除。稳态下读得多、写得少，
   退出时日志会打 `acquisition stopped, polls=…, freshPoints=…, writtenPoints=…, unchanged=…%`。
   详见交接文档 §7.5。

## 4. 目录结构

```
washcar-test/
├── CMakeLists.txt
├── config.example.json          # 配置模板（首次运行也会自动生成）
│
├── build-on-box.sh              # ★ 路线 A：盒子/本机编译 + 组装 deployed/
├── check-box-env.sh             # ★ 路线 A：盒子环境自检（只读）
│
├── third_party/                 # 内置依赖，盒子无需 apt 装 dev 包
│   ├── mosquitto/include/       #   mosquitto.h / mqtt_protocol.h
│   ├── mosquitto/lib/aarch64/   #   libmosquitto.so.1.6.9（只放实体文件，不放软链）
│   └── nlohmann/json.hpp
│
├── wsl-cross/                   # 路线 B：WSL 交叉编译
│   ├── toolchain-aarch64-wsl.cmake
│   ├── build-wsl.sh             # 交叉编译
│   ├── dev.sh                   # 日常快捷键
│   ├── deploy-wsl.sh            # 部署到盒子
│   └── check-wsl-env.sh         # 环境自检
│
├── include/
│   ├── presentation/                     # 表现层（含解析层）
│   │   ├── presentation_layer.h          #   层根：init/start/stop
│   │   ├── mqtt_client.h                 #   mosquitto 封装（topic↔endpoint 归一化）
│   │   ├── downlink_pump.h               #   原始队列 → 解析 → 业务队列（50ms）
│   │   ├── uplink_pump.h                 #   上行队列 → publish（50ms）
│   │   ├── command_parser.h              #   RawMessage → BusinessCommand（命令方言都在这）
│   │   └── endpoint_map.h                #   ReportKind → endpoint
│   ├── business_layer/
│   │   ├── business_layer.h              #   层根：命令分派线程 + 回执组装
│   │   ├── report/report_scheduler.h     #   上行主动上报调度
│   │   └── washcar/                      #   状态机 + 海天/驿公里服务
│   ├── data_layer/                       # 数据层（只放 plc 相关）
│   │   ├── data_layer.h                  #   层根
│   │   ├── idevice_service.h             #   跨层唯一契约（只返回 PlcPointValues）
│   │   ├── device_service.h              #   采集线程 + 点位表 + 缓存 + 变化检测
│   │   ├── fx_plc/                       #   ★PLC 实现沿用主项目封装，全在此目录
│   │   │   ├── iplc_instance.h           #   PLC 抽象接口
│   │   │   ├── fx_plc_instance.h         #   三菱 FX 串口
│   │   │   ├── tcp_plc_instance.h        #   Modbus TCP
│   │   │   └── fx_plc_instance_set.h     #   FxPlcInstanceSet（addPlc/getPlc/getAll）
│   │   ├── device/                       #   device_status.h / device_data.h
│   └── common/                           # 公共设施
│       ├── common.h                      #   公共设施入口（工作目录+读配置+日志+线程预算+信号）
│       ├── buffer/{message_buffer,uplink_buffer}.h   # 三个有界队列（原始/业务/上行）
│       ├── message.h                     #   RawMessage/Report/IReportSink + BusinessCommand
│       ├── plc_point.h                   #   PlcPointValues（跨层点位值容器）
│       ├── thread/thread_manager.h       #   ★全项目线程唯一出口（上限+登记表+异常兜底）
│       ├── time/time.h                   #   时间工具（唯一平台分支点）
│       ├── config/                       #   配置
│       ├── log/                          #   日志（log_system.h = 装配唯一入口）
│       └── sysmon/system_monitor.h       #   盒子自身指标（→ data/log/sys_*.txt）
├── src/                                  # 与 include 对称（32 个编译单元）
│
├── deployed/                             # ★可运行文件（两条编译路线共用，已 gitignore）
│   ├── main                              #   strip 后的 ARM aarch64 可执行文件
│   ├── libmosquitto.so.1                 #   RUNPATH=$ORIGIN 从同目录加载
│   └── config.json                       #   由构建脚本从项目根拷入
└── build-wsl/  build-box/  .ccache/      # 构建中间产物与 ccache 缓存（均在本项目内）
```

## 5. 依赖

**编译期**（两条路线都一样）
- C++17 编译器（g++ >= 9）、cmake >= 3.10、make
- mosquitto 头文件 + 库 → **优先用系统包，缺了自动回退到 `third_party/mosquitto/`**
- nlohmann/json → 同上，回退到 `third_party/nlohmann/json.hpp`

也就是说盒子上**不需要** `apt install libmosquitto-dev` / `nlohmann-json3-dev` —— 仓库里带了 aarch64 副本。

> ⚠️ `third_party/mosquitto/lib/aarch64/` 里**只有一个实体文件** `libmosquitto.so.1.6.9`，
> 没有 `.so` / `.so.1` 软链 —— 这是故意的，软链从 Windows 传到盒子会丢，或变成 0 字节空文件。
> CMake 会自己 glob `libmosquitto.so*` 选可用的那个，不用你手工补。
> 上传源码时务必把 `third_party/` 整个目录带上（`ls -l` 必须看到约 99KB 的那个文件）。

路线 B 额外需要：`gcc-9-aarch64-linux-gnu` / `g++-9-aarch64-linux-gnu` + sysroot `/opt/rk3568-sysroot`。

**运行期（盒子上）**
- `libstdc++.so.6` / `libm` / `libgcc_s` / `libpthread` / `libdl` —— 盒子自带
- `libssl.so.1.1` + `libcrypto.so.1.1` —— libmosquitto 依赖，20.04 默认自带
- `libmosquitto.so.1` —— **盒子上通常没有，随产物放在 main 同目录**（`RUNPATH=$ORIGIN` 加载）

## 6. 编译（两条路线任选，产物都在项目内，互不干扰）

| | 路线 A：盒子本机编译 | 路线 B：WSL 交叉编译 |
|---|---|---|
| 适用 | 盒子能装编译器、想现场改代码 | 盒子资源紧张 / 批量出包 |
| 传输量 | 源码（约 1 MB，含 third_party） | 产物（约 1 MB） |
| 构建目录 | `build-box/` | `build-wsl/` |
| 运行目录 | `deployed/` | `deployed/`（build-wsl.sh 直接组装好） |
| 命令 | `bash build-on-box.sh` | `bash wsl-cross/build-wsl.sh` |

### 6.1 路线 A：盒子上直接编译

```bash
# ① 先自检（只读，不改任何东西；缺什么一目了然）
cd /home/ztl/program/washcar-test
bash check-box-env.sh

# ② 编译 + 组装运行目录 + ldd 校验
bash build-on-box.sh

# ③ 冒烟跑 5 秒看看启动日志
bash build-on-box.sh run

# 重来一遍
bash build-on-box.sh clean && bash build-on-box.sh
```

产物：
```
washcar-test/build-box/main       带符号调试版（gdb 用）
washcar-test/build-box/src/main   同上的 CMake 原始位置
washcar-test/deployed/main        strip 后的部署版
washcar-test/deployed/libmosquitto.so.1
```

可选环境变量：`JOBS=4`（默认 2，盒子内存小）、`BUILD_DIR=`、`RUN_DIR=`。

### 6.2 路线 B：WSL 交叉编译

```bash
# 首次：搭环境（只需一次）
sudo bash wsl-cross/setup-env.sh

# 编译
bash wsl-cross/build-wsl.sh               # 增量
bash wsl-cross/build-wsl.sh rebuild       # 全量重来

# 或者用日常快捷键
bash wsl-cross/dev.sh build               # 编译
bash wsl-cross/dev.sh test                # 跑测试
bash wsl-cross/dev.sh run                 # qemu 跑起来看日志
```

产物：`deployed/`（`main` + `libmosquitto.so.1` + `config.json`，main 为 ARM aarch64 ELF，strip 后约 3.2 MB）

> 所有可运行文件统一落在**项目根的 `deployed/`**，两条编译路线位置一致。
> 构建过程不往 `$HOME` / `/tmp` 写任何东西（ccache 也在项目内的 `.ccache/`）。

> `wsl-cross/setup-env.sh` 里已经包含 `libmosquitto-dev:arm64` 的安装。
> 但即使没装也能编 —— CMake 会回退到 `third_party/mosquitto/lib/aarch64/`。

## 7. 部署与运行

### 7.1 路线 B 的部署（WSL → 盒子）

```bash
BOX_HOST=ztl@<盒子IP> BOX_PASS=<密码> bash wsl-cross/deploy-wsl.sh
```

脚本会传 `main` + `libmosquitto.so.1`（+ 有 `config.json` 就一起传），然后跑 `ldd` 校验。

### 7.2 路线 A 的部署（源码 → 盒子）

```bash
rsync -av --delete \
  --exclude 'build-wsl' --exclude 'build-box' --exclude 'deployed' \
  washcar-test/ ztl@<盒子IP>:/home/ztl/program/washcar-test/
```

然后在盒子上 `bash build-on-box.sh`。

### 7.3 运行

```bash
ssh ztl@<盒子IP>
cd /home/ztl/program/washcar-test/deployed
./main
```

首次运行会在可执行文件同目录生成 `config.json`，改完再启动。

## 8. 配置说明（config.json）

| 配置项 | 说明 |
|---|---|
| `box.boxNo` | **每台盒子必须唯一**。它同时是 MQTT clientId 和 topic 前缀，撞号会互踢 |
| `box.boxType` | `haiTian`（默认）或 `yiGongLi`，决定走串口还是 Modbus TCP |
| `box.plcEnabled` | `false` 时完全不注册 PLC（只调 MQTT 时用） |
| `plcSerial.port` | 海天 FX 串口。真机改 `/dev/ttyUSB10`；`/tmp/vplc-pty` 是虚拟 PLC |
| `plcSerial.baudRate/dataBits/parity/stopBits` | FX 默认 `9600 / 7 / even / 1` |
| `plcTcp.host/port/unitId` | 驿公里 Modbus TCP，默认 `192.168.31.250:502` unitId=1 |
| `mqtt.brokerIp/port/username/password` | 测试盒子走本地 broker：`127.0.0.1:1883`，账号密码留空；连云端换成 `116.204.32.197` + `admin` |
| `mqtt.topicSeparator` | `"."`（默认，生产抓包格式）或 `"/"`（Excel 配置写法）；只影响**发布**，订阅恒双格式 |
| `mqtt.reconnectBaseSec` | 重连退避起始间隔，默认 2s（指数增长，上限固定 30s） |
| `mqtt.reconnectMaxRetry` | 连续失败多少次后开始报 ERROR（库本身会一直重试，不会退出进程） |
| `mqtt.rebootEnabled` | `false` 时 reboot 只记录不执行（协议约定）；`true` 时发 SIGTERM 优雅退出，靠守护脚本拉起 |
| `timing.valueChangeIntervalMs` / `faultReportIntervalMs` | 变化 / 故障上报周期，默认 3000ms |
| `timing.dailyReportHour/Minute` | 每日物料上报时点，默认 8:00 |
| `machine.<boxType>` | 软元件表，按机型分段（`haiTian` / `yiGongLi`）。**整段不写就用代码内置的默认表** |
| `machine.<boxType>.acquisition` | 采集清单。键名是**单字母** `D`/`Y`/`M`/`X`/`S`，值是点位数组；`intervalMs` 是**读 PLC 的频率**（默认 500ms，下限 100ms） |
| `machine.<boxType>.signals` | 信号表：`name` 内部名 / `point` 软元件 / `report`（`property` 进 monitor、`event` 进 pushalarm、`none` 只本地用）/ `cloudKey` / `normalValue` / `comment` |
| `machine.<boxType>.services` | 云端可调命令：`name` / `cloudField`（云端下发的字段名）/ `write`（选择条件→软元件） |
| `box.logLevel` | `info` / `debug` / `warning` / `error` |
| `box.logKeepDays` | 日志保留天数，默认 7。启动时和跨零点各清一次，只删 `log_YYYY-MM-DD.txt` |
| `plcSerial.timeoutMs` | **单帧读超时**，默认 200ms（串口层按 100ms 向上取整成 VTIME）。**别设到 1000ms**：PLC 连着但不应答时每点白等 1 秒，一轮 37 次读就拖死采集线程 |
| `plcSerial.frameDelayMs` | 帧间静默，默认 10ms，代码兜底不小于 4ms（RTU 要求 ≥3.5 字符时间） |
| `plcTcp.timeoutMs` | 单个 Modbus 请求收发超时，默认 2000ms |
| `queueCapacity.downCapacity` / `upCapacity` / `logCapacity` | 三个队列容量，默认 256 / 128 / 4096。日志队列满了丢最旧并计数，防磁盘卡住时内存涨爆 |
| `sysmon.enabled` | 是否记录盒子自身指标（CPU/内存/温度/磁盘），默认 `true`。关掉不影响任何业务 |
| `sysmon.intervalMs` | 指标采样周期，默认 **5000ms**，低于 1000 夹到 1000 |
| `concurrency.maxThreads` | **全项目线程总数上限**，默认 16。超了会拒绝启动那条线程并打 ERROR（附存活线程列表） |

> 三条队列的容量在 `queueCapacity` 段里（`downCapacity` 同时用于原始与业务队列，`upCapacity` 用于上行），日志队列是 `logCapacity`。
> 它们不是日常运维参数，只在排查"消息被丢"时需要调。旧配置里的 `queuing` 段仍然可读，不用急着改。
>
> **日志目录不在配置里**：固定为 `<程序目录>/data/log`，由 `LogSystem` 自己创建。`box.logPath` 已下线。

### 8.1 日志文件布局

运行目录下的 `data/log/` 里始终只有**两类**文件，按前缀严格分开、各自轮转：

| 文件 | 内容 | 写入节奏 | 谁负责 |
|---|---|---|---|
| `log_YYYY-MM-DD.txt` | 业务日志：启动、命令、PLC 读写失败、状态机判定 | 有事件才写，闲时是空的 | `LogSystem`（logger `work`） |
| `sys_YYYY-MM-DD.txt` | 盒子自身指标：CPU / 内存 / 温度 / 磁盘 / 自身占用 | **固定节拍**，默认每 5 秒一行 | `SystemMonitor`（logger `sysmon`） |

```bash
tail -f data/log/sys_$(date +%F).txt     # 现场实时看盒子负载
```

指标文件长这样（第一行是字段说明，只在文件新建时写一次）：

```
[2026-11-01 16:17:56] # sysmon: cpu=CPU使用率 load1/5/15=平均负载 ...
[2026-11-01 16:17:58] cpu=8.5% load1=0.9 load5=0.5 load15=0.2 mem_used=693MB mem_total=15877MB mem_pct=4.4% mem_avail=15184MB swap_used=0MB swap_total=4096MB temp=na freq=na rss=7.1MB threads=4 disk_free=974922.2GB disk_total=1031018.4GB disk_used_pct=5.4% uptime=97s
```

| 字段 | 达标线 | 异常说明什么 |
|---|---|---|
| `cpu` | 长期 < 60% | 持续 80%+ 说明有死循环或线程自旋 |
| `load1` | 单核 < 1.0 | 长期超标 = 处理不过来 |
| `mem_used` | **平稳波动** | 单调上涨不回落 = 内存泄漏 |
| `temp` | < 75℃ | 过 80℃ 会触发降频，表现是"越跑越卡" |
| `threads` | **恒定** | 持续上涨 = 线程泄漏（本项目的线程数应固定） |
| `disk_free` | > 500MB | 写满后日志和配置全部失效，是"跑几天就挂"的头号原因 |
| `na` | — | 该平台取不到（如 WSL 没有 thermal zone / cpufreq） |

两类文件都按 `box.logKeepDays`（默认 7 天）清理，各删各的前缀，互不影响。
一天约 1.7 万行、3~4MB；7 天最多占 25MB 左右。

## 9. MQTT 协议

topic 前缀：`flexem{sep}{boxNo}{sep}{endpoint}`，`sep` 由 `mqtt.topicSeparator` 决定，
**默认 `.`（生产抓包格式）**；改成 `"/"` 即为 Excel 配置写法。

**订阅**（点号与斜杠两种写法都订阅，共 6 条）：
`command`、`read`、`reboot`

**发布**：`commandreply`、`monitor`、`readdata`、`pushalarm`

命令用 payload 里的字段名区分：

| 字段 | 厂商 | 动作 |
|---|---|---|
| `"Manual start": 1` | 海天 / 驿公里 | 启动洗车 |
| `"stop": 1` | 海天 | 停止 |
| `"reset": 1` | 海天 / 驿公里 | 复位 |
| `"washcar stop": 1` | 驿公里 | 停止洗车 |
| `"machine abort": 1` | 驿公里 | 中止（m206） |
| `"reset safely": 1` | 驿公里 | 安全复位（m203） |
| `"stop work": 1` | 驿公里 | 停止营业（m501） |
| `"stop service": 1` | 驿公里 | 停止营业（m510） |
| `"reboot": 1` | — | 只记录，默认不执行 |

响应：`{"message":"success","time":<整型秒>}` 发到 `commandreply`（**只有成功才回**）。

读点：向 `read` 发平铺点位（值 `null`），如 `{"D142":null,"M1":null}`，
盒子回 `readdata`：`{"D142":3,"M1":0,"boxNo":"123456","time":1782121980}`。

上行定时上报由 `ReportScheduler` 驱动（变化 / 故障 / 每日物料），
节奏见 `timing.*`；详细设计见 `docs/MQTT通信框架-设计与实现.md`。

**内部链路**（决定了"断链会丢什么"）：

```
MQTT 收包 → 下行队列(256) → DownlinkPump(50ms) → CommandParser → BusinessLayer → DataLayer(PLC)
业务层 push → 上行队列(128) → UplinkPump(50ms) → MQTT 发出
```

- MQTT 回调**只入队**，不执行业务 → 慢命令（驿公里启动 3.2s）不再阻塞网络线程。
- 上行队列对 `monitor`/`readdata` **同类只留最新**，`pushalarm`/`commandreply` 逐条排队。
- 断链时上行泵**不消费队列**，重连后补发当前最新状态（不是完整历史补发）。

**三条硬约束**（云端要求，代码里已写死）：
- `retain = false`
- `QoS = 0`
- `cleanSession = 1`

## 10. 验证状态

已在本机（WSL + qemu 用户态模拟）验证：

| 项 | 结果 |
|---|---|
| 路线 B 交叉编译 + 链接 | ✅ 重构前 30 个编译单元全通过，产物 ARM aarch64 ELF（重构后代码结构变化，**交叉编译待重跑**） |
| 路线 B 依赖回退 | ✅ 藏掉 sysroot 里的 mosquitto 后，CMake 自动用 `third_party/` 并链接成功 |
| 路线 A 编译脚本 | ✅ `build-on-box.sh` 走通：configure → build → 组装 → strip → ldd 全绿（重构前） |
| 路线 A RUNPATH | ✅ `libmosquitto.so.1 => <deployed>/./libmosquitto.so.1`，确认 `$ORIGIN` 生效 |
| 路线 A 环境自检 | ✅ `check-box-env.sh` 6 节全部输出正常（x86 上仅"CPU 架构"报 FAIL，符合预期） |
| 架构重组后的本机构建 | ✅ WSL 本机 cmake+ninja 全量编译链接通过（9381 行 / 29 个编译单元） |
| 启动流程 | ✅ 运行时 → 队列 → 数据层 → 业务层 → 表现层，顺序与日志一致 |
| `plcEnabled=false` 调试路径 | ✅ 不注册 PLC 也能起（`[DataLayer] plcEnabled=false, no PLC registered`） |
| 采集周期读配置 | ✅ 配 700 时横幅打 `采集周期 700ms (来自 config.json 的 machine 段)`；删掉 machine 段后打 `1000ms (来自内置默认表)` |
| **缓存只写变化值** | ✅ mock PLC 23 项断言全通过：5 轮值不变 → 读 15 次、写 0 次；改 1 点 → 只写 1 点；读失败 → 保留旧值；业务读快照 100 次 → PLC 读 0 次 |
| **线程统一管理** | ✅ 启动打 `线程预算: 16 条`；退出打 `线程已全部回收, 剩余 0 条`；8 条线程全部有名字；任一循环抛异常只死该线程（`ManagedThread` 内 try/catch） |
| **下行两段队列** | ✅ 假 broker 发 `{"Manual start":1}` → `rawBuffer` → 解析 → `bizBuffer` → 分派线程 → `startWashCar rejected: no PLC`（链路每一段都有对应日志） |
| **盒子状态监控** | ✅ 实跑 12s 写入 6 行；`cpu` 随负载从 0% 变 8.5%；`uptime` 与 `/proc/uptime` 一致；文件新建时写一次字段说明、重启不重复；`enabled=false` 不建文件；`intervalMs=100` 被夹到 1000 |
| **指标与业务日志分离** | ✅ `sys_*.txt` 有 10 行指标、`log_*.txt` 里 `cpu=` 命中 0 次 |
| MQTT 连接真实 broker | ✅ 上一版连通 `116.204.32.197:1883` |
| **下行链路端到端** | ✅ 本地假 broker 注入 `{"Manual start":1}` / `{"reboot":1}` / `{"D142":null}` → 三条都到达业务层（`startWashCar rejected: no PLC`、`reboot requested, recorded only`、`readPoints rejected`），无 PLC 时按约定不回执 |
| **上行链路端到端** | ✅ 连推 5 条 `monitor` + 2 条 `pushalarm` + 1 条 `commandreply`，broker 实收 4 条：`monitor` 只剩最新一条（`{"seq":5}`），两条 `pushalarm` 与 `commandreply` 全部保留 |
| **上报 topic 拼装** | ✅ broker 侧实收 `flexem.123456.monitor` / `.pushalarm` / `.commandreply`，分隔符与 `topicSeparator` 一致 |
| 订阅 | ✅ 6/6：`flexem.123456.command` + `flexem/123456/command` 等点号/斜杠双份 |
| `reboot` 开关 | ✅ `rebootEnabled=false` 只记日志；`=true` 时注入 `{"reboot":1}` → `shutting down` → 三层依次 stop → 退出码 0 |
| 优雅退出 | ✅ SIGINT 后顺序为 `UplinkPump → DownlinkPump → MQTT → stopWashCarStatusMonitor → ReportScheduler → DeviceService`，thread join 正常 |

**未验证（需真机）**：

- 海天 FX 串口与真实 PLC 的报文往返
- 驿公里 Modbus TCP 与真实 PLC 通讯
- 完整洗车业务流程（需要 PLC 侧有响应）
- 命令**成功**回执（`plcEnabled=false` 时所有命令都会因"no plc"失败）
- 上报调度在真机上的报文节奏（3s / 每日 8:00）
- 断链 10 分钟后的收敛行为（需要可控 broker + 真实 PLC 产生变化）

> ⚠️ 已知：broker 启动时不可达 → `PresentationLayer::start()` 返回 false → 进程退出（既有行为，见 §11）。

## 11. 已知限制

1. **不做持久化补发**：上行队列是内存队列，进程退出即丢。断链期间 `monitor`/`readdata` 只保留最新一条、重连补当前状态；`commandreply`/`pushalarm` 逐条排队（最多 128 条），历史过程无法还原。
2. **无 HTTP 调试接口**：原 8080 端口已移除，调试只能走 MQTT 或看日志。
3. **无单元测试**：原项目的 4 个测试依赖 SQLite/全量配置，精简版先跑通主链路，测试待补。
4. **broker 启动时不可达会退出**：`mosquitto_connect_async` 同步失败 → `PresentationLayer::start()` 返回 false → `main` 返回 -1，盒子上电早于网络就绪时需要外部守护脚本反复拉起（既有行为，本次未改；建议加有限次重试）。
5. **`point` 类命令未接**：`CommandType` 里 `POINT_ON/OFF/STATUS`、`CLEAR_ALARM`、`CHANGE_MODE` 都已定义，`BusinessLayer::dispatch` 也实现了转发，但 `CommandParser` 还没给它们分配 JSON 字段名（协议未约定）。要开时在 `command_parser.cpp::classifyCommand` 加一行判断即可。
6. **reboot 依赖守护脚本**：打开 `mqtt.rebootEnabled` 后盒子只是优雅退出进程，需要 systemd/看护脚本把它拉起来才算"重启"。
