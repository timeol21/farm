# tools/plc-mock — 洗车机 PLC 模拟器

用 Python 解释执行 GX Works 导出的梯形图，把海天洗车机的 PLC 跑在 PC / WSL 上：
不连现场 PLC、不下载程序、改点子随便试。旁边配一个**自建 MQTT 测试服务器**和一块
**网页看板**，专门给人工审查用 —— 流程图高亮当前工序、软元件按使用频率排序列出实时值、
任意主题任意负载手动下发命令。

外层只放三样东西：**启动程序**、**说明文档**、**运行时看板**。

```
tools/plc-mock/
├─ start.py                  ← 唯一启动入口（虚拟 PLC + broker + 看板 + 可选主程序）
├─ run_local_native.sh       ← wsl-cross / verify.sh 用的验收壳（路径没变）
├─ README.md                 ← 本文
├─ web/                      ← 网页看板（纯静态，无构建）
│   ├─ index.html  app.js  app.css
├─ core/                     ← 引擎
│   ├─ ladder.py             梯形图解释器（FX 栈机语义）
│   ├─ runtime.py            扫描循环 / 软元件目录 / 工序追踪 / 调速暂停
│   ├─ serial_stub.py        PTY 冒充 FX 编程口
│   ├─ framing.py            FX 串口帧编解码
│   ├─ broker.py             自建 MQTT 3.1.1 broker
│   └─ dashboard.py          看板 HTTP 服务 + JSON API
├─ simulators/               ← 机型（扩展位）
│   ├─ base.py               机型接口
│   ├─ haitian/              海天：machine.py(现场条件表) plant.py(物理模型)
│   └─ _template/            复制即用的骨架（不参与注册）
├─ tests/                    ← 测试与验收
├─ devtools/                 ← 人工审查工具（梯形图侧，只读）
├─ config/main.json          ← 给盒子主程序 main 的配置
└─ docs/                     ← 所有说明文档都在这（见 docs/README.md）
    ├─ README.md             文档索引与目录约定
    ├─ 接入标准.md            跨机型契约：接口/数据/看板/切换
    └─ 海天/                  机型说明 + 语义底稿 + 梯形图 CSV/.gxw + 原始资料
```

## 快速开始

```bash
cd tools/plc-mock

# 1) 起模拟器（虚拟 PLC + MQTT 测试服务器 + 看板），浏览器开提示的地址
python3 start.py

# 2) 连盒子主程序跑全链路（先编一份 native main，见 wsl-cross/README.md）
python3 start.py --main ~/washcar-build-native/src/main

# 3) 25 倍速（虚拟步长仍是 50ms，一轮洗车约 17 秒）
python3 start.py --time-scale 25

# 4) 复现 2026-10-22 现场那台"卡在洗车头、40 秒后报 25"的机器
python3 start.py --preset stall-s3-t99

# 其它
python3 start.py --list            # 有哪些机型
python3 start.py --check           # 只装载不启动，验证梯形图能解析
```

默认端口：看板 `127.0.0.1:18081`、MQTT `0.0.0.0:1883`、PTY 链接 `/tmp/vplc-pty`。
**看板不用 18080** —— 那是盒子主程序自己的 HTTP 端口。
端口被别的实例占着时，`start.py` 会直接报错退出（不会连着别人的 broker 跑），并把占用者
（pid / 命令行 / 工作目录）列出来：

```bash
python3 start.py --stop     # 收掉所有还在跑的模拟器实例（先 SIGTERM，6 秒不退再 SIGKILL）
```

> 没退干净的实例一般是"另开一个终端 `python3 start.py` 忘了关"。注意
> `pkill -f "plc-mock/start.py"` **杀不掉它** —— 那种启动方式命令行里只有 `python start.py`，
> 没有完整路径；用 `--stop` 或者 `pkill -f "start\.py"`。

## 看板怎么用

| 区域 | 看什么 |
|------|--------|
| 洗车流程图 | 11 个工序节点，当前 D142 高亮 + 台车标记；走过的节点变绿；下面一行是工序轨迹和每步停留时长 |
| 关键点位条 | 工序号 D142、故障码 D2、洗车标志 Y74、车辆到位 M187、行走脉冲 C220、前进 Y30 **常驻显示**，不受排序和滚动影响，变一下闪一下 |
| 软元件监视 | **按梯形图里的引用次数排序**。引用越多 = 改逻辑时越要先看它。可按区（X/Y/M/S/D/T/C）过滤、搜名字或说明、勾"只看变化"；值一变整行闪一下，表格底部显示"近 2 秒变化 N 个" |
| MQTT 测试服务器 | 上半部分下发（主题+负载+QoS+预设按钮），下半部分实时滚动收发报文（↑盒子上行 / ↓测试下发），点报文展开完整负载。状态行里的"盒子已连接"按 **broker 上的连接/最近上行**判断 —— 用 `[R]` 在另一个终端直跑的 main 也算连接，不算"未启动" |
| 人工干预 | 上半部分是**启动前置检查**：五项（车辆到位 / 启动前置条件 / 左右防撞杆 / 车高）各一行，显示实时值和通过状态，能由外部输入决定的都带独立按钮（「车辆到位」「松开左杆」「松开右杆」「车辆达标」），**点按钮只改现场输入、不发任何命令**；「启动前置条件 M1」由梯形图合成，只展示。下半部分是**启动 / 停止 / 复位**三条命令（只写 X45 / 只写 M0 / 先 M0=0 隔 1 秒再 M100）；盒子在跑时走 MQTT、经盒子准入，没有盒子才退化为直接写点。再下面是强制写入任意软元件（**在扫描前压入**，可覆盖现场模型写的 X） |

> 只想快速确认"看板是活的"：点「复位」→ 等前置检查里"车辆停放到位"变绿 → 点「启动」。约 1 秒后 D142 从 0 开始变，
> 约 1 分钟走完 9 个工序（1 倍速；切到 50× 大约 10 秒）。D142 不在表格当前屏也没关系，顶部关键点位条一直有。

顶栏：扫描速率、运行时长、倍速（1×/5×/25×/50×）、暂停、单步。
**暂停 + 单步**是查"某一拍到底发生了什么"最顺手的组合：暂停后值冻结，点单步走一个扫描周期。

软元件值的口径：

- `X/Y/M/S` 显示 0/1；`D` 显示十进制原值；`C` 显示 32 位计数值
- `T` 显示**到位位**（触点通没通），不是当前计时值 —— 主程序也读不到 T 的当前值
- 没被梯形图引用过的软元件不会出现在列表里（列出来的就是有意义的）

## 自建 MQTT 测试服务器

broker 是项目里自己实现的（`core/broker.py`），只依赖标准库，起来就能用：

- 支持 CONNECT / SUBSCRIBE / UNSUBSCRIBE / PUBLISH（双向，QoS 0/1）/ PING / DISCONNECT
- 不支持 retain、持久会话、QoS2、遗嘱、账号密码 —— 本地测试用不上，真接云要换真 broker
- 看板里的"下发"就是 broker 主动 publish，会真的送到订阅者；送达几个会提示

海天的主题（配在 `config/main.json` 的 `mqtt.topics`）：

| 方向 | 主题 | 用途 |
|------|------|------|
| 云→盒子 | `flexem/{boxNo}/command` | 启动 / 停止 / 复位 |
| 云→盒子 | `flexem/{boxNo}/read` | 立即读取软元件 |
| 云→盒子 | `flexem/{boxNo}/reboot` | 重启盒子系统 |
| 盒子→云 | `flexem/{boxNo}/monitor` | 属性变化上报 |
| 盒子→云 | `flexem/{boxNo}/readdata` | read 的响应 / 心跳 |
| 盒子→云 | `flexem/{boxNo}/pushalarm` | 故障上报 |
| 盒子→云 | `flexem/{boxNo}/commandreply` | 命令成功回执 |

看板上的预设按钮对应这几条（payload 与 `AGENTS.md §3.1` 一致）：

```json
{"Manual start":1}   {"stop":1}   {"reset":1}   {"D142":null}   {"reboot":1}
```

> 命令失败且盒子在线时**不返回应答**，云端靠超时判定 —— 所以看板上看不到 `commandreply`
> 不一定是 broker 的问题，可能本来就没回。

> **启动前必须先复位**：刚上电 PLC 停在 S0，`M187`（车辆到位）由 #423 线圈保持 0 ——
> 写入它的是 S1 步内的 #1756 线圈（`M187 = M515 & T61`，`T61` 是 `M515 & !X003 & X24` 的 3 秒确认定时器），
> S0 时这段不执行。复位进 S1 后要**再等 3 秒** T61 到点，M187 才变 1。
> 此时发 `{"Manual start":1}` 会被启动准入拦下，回 `washPosition incorrect`（看板显示"停车位置不正确"）。
> 正确顺序：`{"reset":1}` → 等 `D142=1` 且 `M187=1` → `{"Manual start":1}`。
> 看板上就是两个独立按钮：先「复位」，等"车辆停放到位"变绿，再「启动」。没有合并按钮。
> **复位不会掩盖"车没到位"**：M515/X24/X3 不满足时，复位后 M187 依旧是 0，启动照样被拒。

## 加一台新机型

`simulators/` 下每个**包**只要在 `__init__.py` 里暴露 `SIMULATOR`，就会被自动注册，
`--plc <包名>` 和看板立刻能选到。四步：

```bash
cp -r simulators/_template simulators/你的机型
# 1) simulator.py 改 key/title/ladder_path/flow
# 2) 资料（梯形图 CSV / .gxw）放到 docs/你的机型/
# 3) plant() 补"输出 Y → 机构 → 反馈 X"
# 4) python3 start.py --plc 你的机型 --check
```

细节和接口说明见 `docs/接入标准.md`。当前只实现了 **fx-serial** 通讯层（三菱 FX 编程口）；
驿公里那类是 Modbus TCP，需要在 `core/` 里另写一个通讯桩，机型里把 `transport` 改掉
（`start.py` 会识别并给出提示，不会假装连上）。扫描循环、broker、看板都是机型无关的。

## 测试与验收

```bash
python3 tests/verify_mock.py            # 16 条验收闸门（含 .gxw 逐条对账），退出码 0 = 全过
python3 tests/wash_cycle.py             # 洗车流程演练：上电→复位→启动→9 个工序
python3 tests/wash_cycle.py --field     # 复现现场故障（判定"症状复现"，不是"洗车成功"）
python3 tests/broker_smoke.py           # broker 自测：订阅/下发/回执/消息记录
python3 tests/bench.py                  # 扫描吞吐（快速路径 vs 逐条解释）

# 串口帧协议自测（要先起 start.py，另开一个终端）
python3 tests/stub_selftest.py
```

| 测试 | 证明什么 |
|------|----------|
| `verify_mock.py` | 助记符全覆盖、无未识别指令、梯形结构用第二套实现复算一致、业务链（D142/复位/启动/停止/故障锁存）、快慢路径逐扫描等价、与 GX Works 工程对账 |
| `wash_cycle.py` | 9 个洗车工序真的能走完、末态无故障；`--field` 能复现现场症状 |
| `stub_selftest.py` | 串口桩按 `fx_plc_instance.cpp` 的真实帧格式应答，写命令真的落进梯形图 |
| `broker_smoke.py` | 自建 broker 的连接/订阅/下发/上行记录都对 |
| `bench.py` | 扫描性能（改解释器后防止退化） |

`wsl-cross` 里的入口没变：

```bash
bash tools/plc-mock/run_local_native.sh        # 全链路闭环（脚本内部调 start.py）
```

> 这个脚本会自己避让：18081 / 1883 被手动开着看板占住时，本轮自动换空闲端口，
> PTY 也按 `RUN_DIR` 分开（`/tmp/vplc-pty-<运行目录名>`），所以"一边开着看板、
> 一边点一键编译的跑一遍"不会互相打架。实际端口看它打印的 `DASHBOARD ...` 行。

## 人工审查工具（devtools/）

只读，不参与运行时；都接受"梯形图 CSV + 软元件名"：

```bash
python3 devtools/trace.py docs/海天/海天洗车机.csv M187       # 谁读它、谁写它
python3 devtools/deps.py  docs/海天/海天洗车机.csv M187 3      # 要让它成立还需要什么
python3 devtools/explain.py docs/海天/海天洗车机.csv Y030      # 条件链逐条求值
python3 devtools/dump.py  docs/海天/海天洗车机.csv 400 430
python3 devtools/render.py docs/海天/海天洗车机.csv --out 梯形图.html
python3 devtools/diag.py  docs/海天/海天洗车机.csv S3          # 运行时卡在哪
python3 devtools/gxw_check.py docs/海天/*.gxw docs/海天/*.csv
python3 devtools/analysis/sweep_params.py --list                        # 现场故障参数反推
```

## 老路径 → 新路径

| 原来 | 现在 |
|------|------|
| `plc_stub_ladder.py` + `mini_mqtt_broker.py` | `start.py`（串口 → `core/serial_stub.py`，broker → `core/broker.py`） |
| `verify_mock.py` | `tests/verify_mock.py` |
| `wash_cycle.py` | `tests/wash_cycle.py`（物理模型 → `simulators/haitian/plant.py`） |
| `stub_selftest.py` / `bench.py` | `tests/` 下同名 |
| `mock_profile.py` + `wash_cycle.py` 里的点位表 | `simulators/haitian/machine.py` |
| `ladder.py` | `core/ladder.py` |
| `diag/trace/deps/explain/rung/dump/render/gxw_check/stl_steps/rungtree.py` | `devtools/` 下同名 |
| `analysis/sweep_params.py` | `devtools/analysis/sweep_params.py` |
| `data/海天洗车机.csv` 等素材 | `docs/海天/`（资料随机型放 docs） |
| `config_plcmock.json` | `config/main.json` |
| `closed_loop*.sh` / `run_box.sh` / `send_cmd.sh` / `restart_box.sh` / `run_local.sh` / `e2e_stub.sh` | 删除（功能由 `start.py` + 看板覆盖；盒子上直接用 `python3 start.py`） |
| `probe.py` / `probe_rung.py` / `timeline.py` | 删除（一次性调试脚本，`devtools/` 里的工具能复现同类排查） |

## 命令行参数

| 参数 | 默认 | 说明 |
|------|------|------|
| `--plc` | `haitian` | 机型 key |
| `--preset` | 无 | 现场预设（`stall-s3-t99`） |
| `--time-scale` | `1` | 虚拟时钟倍速；扫描周期自动取 `50/倍速`，保证虚拟步长 50ms |
| `--scan-ms` | 自动 | 显式指定扫描周期（覆盖上面的自动换算） |
| `--main` | 无 | 盒子主程序路径；不填就只跑模拟器 |
| `--main-config` | `config/main.json` | 主程序配置模板（会复制到运行目录并调采样节奏） |
| `--no-tune-main` | 关 | 不调主程序配置（默认会把波特率调到 115200、采样调密，否则高倍速采不到短工序） |
| `--run-dir` | `~/plcmock-run` | 日志、主程序配置都落这里（也可用 `PLC_MOCK_DIR`） |
| `--pty` | `/tmp/vplc-pty` | 串口桩的符号链接路径 |
| `--broker-port` | `1883` | MQTT 端口 |
| `--http-port` / `--http-host` | `18081` / `127.0.0.1` | 看板；`--http-host 0.0.0.0` 可给同网段看 |
| `--no-serial` / `--no-broker` / `--no-dashboard` | 关 | 单独关掉某一块（调梯形图时只开看板很快） |
| `--box-no` | 取配置 | 覆盖盒号（决定 MQTT 主题） |
| `--open` | 关 | 起来后自动开浏览器 |
| `--check` / `--list` | — | 自检 / 列机型 |
| `--stop` | — | 收掉还在跑的模拟器实例（含占着端口的），Linux/WSL 用 |

## 注意

- **这是模拟器不是"行为模型"**：执行的是真梯形图。PLC 的物理动作（车到位、刷子升降）
  在梯形图里没有软元件能表示，所以由机型自己的 `plant()` 补"输出 Y → 机构 → 反馈 X"。
  不接物理模型的裸梯形图只能走到 D142=3，那种模式只适合测协议。
- **别把两个"网页"搞混**：
  - `start.py` 的看板（默认 `18081`）—— 模拟器自己的，看流程图、软元件、手动下发 MQTT 命令；
  - 仓库根目录的 `washcar_http_tool.html` —— **盒子自己的 HTTP** 测试页（默认 `192.168.31.248:8080`，
    模拟器里主程序 HTTP 是 `127.0.0.1:18080`）。它打不开就提示"盒子不在线"，指的是**这个 HTTP 端口连不上**，
    跟模拟器的 broker 是两条线，别拿它判断模拟器在不在线。
- **一个 PTY 只能给一个实例用**：手动起的实例默认用 `/tmp/vplc-pty`，
  `run_local_native.sh` 用 `/tmp/vplc-pty-<运行目录名>`。如果两个实例指向同一个链接，
  main 会报 `/tmp/vplc-pty: No such file or directory` 或"接收超时" —— 那是抢串口，不是协议问题。
  另外**没加 `--main` 的实例里没有盒子程序**，看板顶部的提示条会明说，此时下发命令必然"送达 0 个订阅者"。
- **串口桩依赖 Linux PTY**：Windows 上只能起 broker + 看板（`--no-serial`）。生产/验收都在 WSL。
- **改了点位表 / 梯形图 CSV 之后重跑 `tests/verify_mock.py`**，看还是不是 16/16。
- 软元件语义、D142 工序表、故障码这些**业务口径**以 `AGENTS.md` 与 `docs/PLC寄存器定义.md` 为准；
  模拟器只负责忠实执行，不自己发明规则。
- 为什么这么解释梯形图、参数从哪来、现场故障怎么反推的 —— 见 `docs/海天/梯形图语义与现场标定.md`。
- 接新机型 / 改看板的接口契约、数据结构、切换方式 —— 见 `docs/接入标准.md`；
  海天机型事实（点位/命令/前置检查/工序/故障码/预设）—— 见 `docs/海天/README.md`。
