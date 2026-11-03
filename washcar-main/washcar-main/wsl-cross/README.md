# WSL 交叉编译 (RK3568 / aarch64)

在 WSL Ubuntu-22.04 中交叉编译 washcar，产物直接跑在 RK3568 盒子（Ubuntu 20.04 / glibc 2.31）上。

与 `docker-cross/` 的区别：不需要 Docker，直接用 WSL 原生工具链 + 独立 sysroot，编译更快、内存占用更小。

## 一键编译（Windows 双击，推荐日常用这个）

**双击 `wsl-cross\一键编译.cmd`** 就行。它自己找 WSL、把源码同步过去、编完把产物拉回
`wsl-cross\dist\`，全程有进度条和实时报错，不用记任何命令。

这个入口在原有脚本之上加了三样东西，对应三个实际痛点：

| 痛点 | 现在的做法 |
|------|-----------|
| 编译时屏幕上什么都没有，只能等 | 实时进度条 + 当前正在编的文件 + 实时错误/警告 |
| 每次要记 `BUILD_TARGETS=... bash xxx.sh` | 菜单选目标（`main` / `all` 预设，也能多选） |
| 盒子产物留在 WSL 里，现场还得想办法捞出来 | 自动回传到 `wsl-cross\dist\aarch64\`，含依赖库和校验清单 |

### 交互流程

```
双击 一键编译.cmd
  ↓
[1] 选编译方式   1) 原生 x86_64（WSL 内直接跑，秒级验逻辑）
                 2) RK3568 aarch64（盒子现场用，产物回传 Windows）
  ↓
[2] 选编译目标   1) main   2) all（全部目标）   3…) 单个 target
                 写法：1   或   1 3 5   或   直接写 target 名
  ↓
[3] 选构建类型   原生: 快速(-O0 -g0) / 调试(-O0 -g)
                 aarch64: Release(strip) / Debug(带符号)
  ↓
确认摘要 → 回车开编
  ↓
编完给出产物清单（Windows 路径 + 大小）+ 下一步操作菜单
```

### 编译目标从哪来（加测试不用改脚本）

菜单里的目标清单**不是写死的，也没有任何需要手工维护的标记表**，每次运行都从
`src/CMakeLists.txt` 现扫：

| 来源 | 取的什么 |
|------|----------|
| `add_executable(name ...)` 或项目自己的 `washcar_test_target(name ...)` | 目标名 + 菜单里的排列顺序 |
| 上方那行 `# name: 说明` 注释 | 菜单里显示的中文说明（分隔符 `:` / `：` / `──` 都认；没写就显示 `(CMakeLists 里没写说明)`） |

所以以后加单元测试只要写：

```cmake
# my_new_test: 一句话说明（会自动出现在菜单里）
washcar_test_target(my_new_test FULL SOURCES text/my_new_test.cpp)
```

（`FULL` = 带上整份业务代码；只编自己那几个源文件就去掉它。也支持原生 `add_executable`。）

菜单里立刻多一项。PowerShell 侧不维护任何目标表，不会有"改了 CMakeLists 忘了改脚本"这种事。

### 按构建模式排除 target（不用改脚本）

有些 target 只在某一种构建里存在，比如 `yolov8_perf_test` 在原生 x86_64 下编不过
（宿主的 gcc-11 对 `font8x8_basic.h` 里 `char` 数组装 `0xFF` 报窄化错误，盒子的 gcc-9 才放行）。
这种不是靠脚本"打标记"排除的，而是直接在 CMakeLists 里把它从构建图里拿掉：

```cmake
# 只在盒子(aarch64)构建里定义
if(NOT WASHCAR_NATIVE)
    add_executable(yolov8_perf_test ...)
endif()
```

脚本这边是两层：

1. **菜单**按 CMakeLists 文本解析（超集），所以 `yolov8_perf_test` 在任何模式下都能看见；
2. **开编前** configure 完，问构建系统实际有哪些可执行 target（`ninja -t targets`），
   把菜单里选了但这个模式不存在的剔掉，打一行提示：
   ```
   ! 这些 target 在当前构建里不存在,已跳过:yolov8_perf_test
   ```

所以以后要按架构 / 开关排除任何 target，**只在 CMakeLists 里加 `if(...)` 就行**，
脚本不用动。判断条件按真实原因选：这里是编译器版本差异（选 `WASHCAR_NATIVE`），
如果是真的架构差异才用 `CMAKE_SYSTEM_PROCESSOR`。

> 顺带区分一下：`EXCLUDE_FROM_ALL` 只是不让 `all` 目标带上它，target 依然存在、
> 依然能显式编 —— 对"这个 target 根本编不过"的场景没用。

想看当前发现结果：

```bash
wsl -- bash -c "bash ~/washcar-win/wsl-cross/build-win.sh --list-targets"
```

带参数调用则完全跳过菜单（适合脚本/快捷方式）：

```powershell
# 编盒子产物，不弹任何菜单
.\wsl-cross\一键编译.cmd -Mode aarch64 -Targets main -BuildType Release -Yes

# 只编两个测试
.\wsl-cross\一键编译.cmd -Mode native -Targets "config_test washcar_test" -Yes

# 全量重编（先清构建目录，排查"缓存看起来不对"时用）
.\wsl-cross\一键编译.cmd -Mode aarch64 -Targets main -Rebuild -Yes

# 只想看环境齐不齐
.\wsl-cross\一键编译.cmd -Check
```

### 编译进度与报错怎么显示

进度条走的是 ninja 的 `[x/y]` 计数，边编边刷，不占屏幕行：

```
  [1/5] 环境体检
  [2/5] 同步源码到 WSL 原生盘
  [3/5] configure
  [4/5] 编译 (main · -j 4)
  [==================] 100%  240/240  Linking CXX executable main        12.4s
  [5/5] 收集产物并回传到 Windows
  [main]    8.2M  C:\...\wsl-cross\dist\aarch64\main
```

报错是**边编边冒**的（不是编完才倒一大坨），并且带源码上下文那两行：

```
  x /src/business_layer/washcar/washcar_state_machine.cpp:88:19: error: 'mode' was not declared
        88 |     if (mode == "jog") {
           |         ^~~~
  ! /src/common/log/log.cpp:31:9: warning: unused variable 't'
```

失败时末尾还会给：错误条数、首个错误的位置、日志最后 30 行、以及常见坑的修复建议
（缺 `nlohmann/json.hpp` / 混进 gcc-11 的 libstdc++ / sysroot 不完整）。
完整日志始终落盘：`wsl-cross\dist\logs\build-<native|aarch64>.log`。

### 产物回传到哪、怎么用

| 编译方式 | 回传目录 | 内容 |
|----------|----------|------|
| 原生 x86_64 | `wsl-cross\dist\native\` | 各 target + `libwashcar_arch_stub.so`（运行依赖，必须同目录） |
| 盒子 Release | `wsl-cross\dist\aarch64\` | `main`（strip 过）+ `librknnrt.so` + `librga.so` |
| 盒子 Debug | `wsl-cross\dist\aarch64-debug\` | 同上，但不 strip，留全符号 |

每次编译会**整目录重建**（不残留上一轮的旧产物），并生成：

- `MANIFEST.txt` — 时间、模式、target、编译器、git 短 SHA、各文件 sha256
- `MANIFEST.txt.sha256` — 拷到盒子上可 `sha256sum -c` 验完整性
- `部署说明.txt` — 一步步的拷贝 + 重启命令

手动拷到盒子（Windows PowerShell，或直接拖 WinSCP）：

```powershell
scp ".\wsl-cross\dist\aarch64\main" ".\wsl-cross\dist\aarch64\librknnrt.so" `
    ".\wsl-cross\dist\aarch64\librga.so" ztl@192.168.31.248:~/washcar/deployed/
```

编完后菜单里的 `[C] 复制拷贝命令` 直接把这条命令塞进剪贴板，`[O] 打开产物目录` 打开资源管理器。

> `config.json` 不要覆盖 —— 那是现场配置，产物目录里本来就没有它。

### 便于手工调试的几个点

- **Debug 档**：`-BuildType Debug` 出带符号的产物（盒子用 `dist\aarch64-debug\`，原生用 `-O0 -g` 重编），
  现场可 `gdb ./main` / `addr2line -e main <地址>` / 分析 core dump。
- **`[G] gdb 抓崩溃栈`**：原生目标编完可直接在菜单里用 gdb 跑一遍，崩了自动打
  `bt full` + `info registers`，不用手敲。
  **注意**：快速档是 `-O0 -g0`，只留符号表不留调试信息 —— 栈里能看到函数名，但**没有源码行号**
  （`gdb` 会提示 `No line number information available`）。要行号必须用「调试」档（`-O0 -g`），
  产物会大 4 倍左右（`config_test` 3.6M → 15M）。脚本里已经加了检测，用快速档产物跑 gdb 会直接
  提示这一点。
- **`[R] 直接跑 main`**：不掺 mock，就是自己起 main 看日志。`data/` 和日志落在 `~/washcar-run-main`，
  **配置文件在仓库里：`wsl-cross/run/config.json`**（首次运行现场生成，`ai.aiEnabled` 自动关掉，
  原因见下面「AI 检测与 rknn」）。**默认一直跑**（Ctrl+C 停；`-RunSeconds 60` 改成到点自停）。
  **它不启虚拟 PLC / broker / 看板，也不会自动下发洗车命令**：MQTT 走
  `wsl-cross/run/config.json`（默认 `127.0.0.1:1883`、`boxNo=300823040333`，即本机模拟器的 broker），
  串口走 `/tmp/vplc-pty`。所以直跑前要先把模拟器起来（`python3 tools/plc-mock/start.py`），
  否则日志里会持续报 PLC/MQTT 连接失败 —— 那是正常的。要连现场云就改这个文件里的 `brokerIp`。
  其它 target 的 `[R]` 仍是直接跑，工作目录 `~/washcar-build-native/run`，超时 300s。
- **`[E] 改配置`**（仅 main）：用编辑器打开 `wsl-cross\run\config.json`（有 VS Code 用 `code`，
  否则记事本）。改完保存，回菜单按 `[R]` 就是拿它跑。改坏了 `[R]` 会先报 JSON 解析错误并指出
  行列号，不会让你对着 main 的一堆"字段缺失"去猜。想用别的配置文件：`-Config <路径>`。
- **`[M] 全链路模拟`**（仅 main）：起一整套闭环（`tools/plc-mock` 的梯形图虚拟 PLC + 自建 MQTT 测试服务器
  + 网页看板 + main），跑一轮完整洗车：采集 → 状态机 → 故障判定 → MQTT 上行 → 注入云端复位/启动 → 写 PLC
  回执，最后打印 `D142 轨迹: 1 → 2 → … → 10`。
  倍速由 `-TimeScale` 控制（默认 25×，约 40s 跑完；`-TimeScale 50` 更快，`1` 是照实约 7 分钟）。
  > 前 20~30 秒没有输出是正常的：在等 mock PLC / broker / main 起来。
  > 日志落在 `~/plcmock-run-native/`，main 的写在 `main.log`；跑的时候会打印一行
  > `DASHBOARD http://127.0.0.1:18081/`，浏览器打开能看流程图和实时软元件值。
  >
  > 和 `tools/plc-mock` 的耦合面只有三样：**一个脚本路径** + 两个环境变量
  > （`BUILD_DIR` / `TIME_SCALE`）+ 退出码。输出是原样转发、不解析，所以那边改逻辑、
  > 改打印、加模块都不会影响这里；`tools/` 也不在 rsync 白名单里，不参与编译。
  > 想换成别的 mock 脚本：设 `WC_MOCK_SCRIPT=/path/to/xxx.sh`。
- **`-ShowDetail`**：把编译器的原始输出（不只是错误行）也打出来，排查链接/configure 问题时用。
- **锁**：同一份源码副本不允许两个编译同时跑（`/tmp/washcar-build.lock`），避免 `rsync --delete` 互踩。

### AI 检测与 rknn（WSL 里跑 main 必看）

原生构建里 `rknn` / `rga` / `turbojpeg` **都换成了桩库**（`wsl-cross/native-stub/`），
x86 上不需要真库也能跑 —— 这条不是问题。

真正的坑在配置：**模板里 `ai.aiEnabled = true`，而且带两个示例模型**，路径指向配置里写的
`*.rknn`。文件不存在时 `AiModelFactory` 直接抛 `std::runtime_error`，main 里没人接 →
`terminate` → 进程 abort（实测）：

```
fopen lib/rknn/models/yolov8.rknn fail!
[ERROR] [Ai] read model file failed: lib/rknn/models/yolov8.rknn
terminate called after throwing an instance of 'std::runtime_error'
  what():  [Detection] [AiModelFactory] load model failed, modelId=yolov8
```

所以在 WSL 里跑 main 只有两条路：`ai.aiEnabled = false`，或者把 `.rknn` 放到配置写的路径。
`[R]` 已经自动帮你关掉了；要验 AI 那条路就把值改回 `true` 并放好模型。

底层干活的是 `wsl-cross/build-win.sh`（可以直接在 WSL 里跑，见脚本头部注释）；
Windows 侧只负责菜单和渲染，构建逻辑没有第二份实现。

## 一次性环境准备


在 WSL Ubuntu-22.04 中执行（需要 root）：

```bash
sudo bash wsl-cross/setup-env.sh
```

脚本会自动完成：
1. 配置 apt 多架构源（amd64 用 jammy，arm64 用 focal 对齐盒子 glibc 2.31）
2. 安装 gcc-9 aarch64 交叉编译器 + cmake/ninja/ccache/sshpass
3. 下载 focal arm64 开发库（openssl/ffmpeg/sqlite 等）到 `/opt/rk3568-sysroot`
4. 修复 sysroot 里的 .so 符号链接问题
5. 安装 nlohmann-json / libstdc++-9 / OpenCL 到 sysroot
6. 链接测试验证

> **为什么用 gcc-9 而不是默认的 gcc-11？**
> Ubuntu 22.04 自带的 gcc-11 交叉编译器，其 libstdc++.so 依赖 glibc 2.32+ 符号（`__libc_single_threaded`、`pthread_create@GLIBC_2.34` 等），但盒子是 glibc 2.31，运行会报 `version 'GLIBC_2.34' not found`。gcc-9 的 libstdc++ 兼容 glibc 2.31。

## 日常编译

在 WSL 中，项目根目录下：

```bash
# 增量编译（首次会自动 configure；源码自动同步到 WSL 原生盘）
bash wsl-cross/build-fast.sh

# 改了 CMakeLists.txt 后重新 configure
bash wsl-cross/build-fast.sh configure

# 全量重编（清构建目录 + configure + build）
bash wsl-cross/build-fast.sh rebuild

# 清构建目录
bash wsl-cross/build-fast.sh clean

# 只编某个测试 target（秒级验证）
BUILD_TARGETS="config_test" bash wsl-cross/build-fast.sh

# 只同步源码不编译
bash wsl-cross/build-fast.sh --sync-only
```

产物统一在 `~/washcar-build/`：
- `~/washcar-build/src/main` — unstripped，带 debug info，gdb 用
- `~/washcar-build/main` — stripped，推到盒子跑这个

`build-fast.sh` 会把 `src/<target>` 复制/ strip 到 `~/washcar-build/<target>`，
`deploy-wsl.sh` 固定取这个路径。

> 原来还有一个 `build-wsl.sh`（源码留 `/mnt/c` 编译），已删除：它被 `build-fast.sh`
> 全面取代（冷构建快 33%、增量持平），而且两者共用同一个构建目录 `~/washcar-build`，
> 交替用会互相把对方的 CMake 缓存和 ccache 作废 —— 实测交替时每次都要全量重编
> 75~97 秒，连续用同一个脚本只要 2~4 秒。

## 快速逻辑验证（WSL 内跑，不碰盒子）

日常改代码要验的是业务逻辑（状态机 / 命令策略 / MQTT 协议 / 点位表 / 配置 / 数据库），
这些**不依赖 NPU、RGA、摄像头和现场 PLC**。`wsl-cross/verify.sh` 把闸门拆成几层，
全部在 WSL 本机完成，不出 WSL：

| 层 | 做什么 | 怎么实现 |
|----|--------|----------|
| 环境自检 | 工具链和原生依赖库齐不齐 | 少一个就报 FAIL 并给安装命令 |
| 语法闸门 | 全量源文件扫一遍，专门抓编译错误 | `g++ -fsyntax-only` 并行，**用盒子构建那套 gcc-9 + sysroot**，不产生目标文件 |
| 原生编译 | x86_64 编出可执行文件 | `WASHCAR_NATIVE=ON`，硬件库走桩，业务逻辑路径完全真实 |
| 逻辑测试 | 11 个测试 target 逐个跑，收退出码分档 | 每个测试独立超时 + 独立日志，失败只打尾巴 |
| 梯形图闸门 | `tools/plc-mock` 自检 | 可用 `--no-mock` 跳过 |

```bash
# WSL 内（仓库路径不含括号时直接跑）
bash wsl-cross/verify.sh              # 全跑（约 40s）
bash wsl-cross/verify.sh --fast       # 秒级档：跳过语法闸门（约 12s）
bash wsl-cross/verify.sh --quick      # 跳过语法闸门（只改了一两行时）
bash wsl-cross/verify.sh --syntax-only  # 只跑语法闸门
bash wsl-cross/verify.sh --cross        # 追加 aarch64 交叉编译闸门（确认盒子产物没坏）
bash wsl-cross/verify.sh --no-mock      # 不跑梯形图闸门

# Windows PowerShell 侧（仓库路径带括号也能跑）
.\wsl-cross\verify-win.ps1
.\wsl-cross\verify-win.ps1 -Quick
```

退出码：`0` = 全过，`1` = 有 FAIL，`2` = 环境不满足，`3` = 用法错误。

### 迭代速度（实测，WSL 4 核，2026-10-27）

| 场景 | 墙钟耗时 | 构成 |
|------|----------|------|
| **改一行代码 → 秒级结论** | **12 秒** | `--fast`：增量编译 4s + 6 个测试 5s + 梯形图 3s |
| 改一行代码 → 完整结论 | **42 秒** | 同步 12s + 语法闸门 16s + 编译 6s + 测试 4s + 梯形图 4s |
| 加 `--cross`（确认盒子产物） | 46 秒 | 上面 + aarch64 交叉编译 4s |

逻辑测试阶段 4 秒跑完 470 条断言。测试并成 6 个文件后比原来快得多 ——
以前有个 51 秒的全链路测试（驿公里命令 × 13 场景），那个 target 已经不存在，
「两档只差 50 秒」的说法作废。

日常节奏：

- 改状态机 / 点位表 / 配置 / 协议 → `--fast`，12 秒看一眼
- 改命令链 / MQTT / 启动准入 → 默认档，42 秒看全
- 提交前 → 加 `--cross`，确认盒子产物还能编

### 为什么要有原生 x86_64 这一路

盒子产物是 aarch64，交叉编译出来**只能在盒子上跑**。改一行代码要走
「编译 → scp → 上盒子 → 起进程 → 看日志」一整圈，反馈以分钟计，还占着现场设备。

仓库里的 `librknnrt.so` / `librga.so` / `libturbojpeg.a` 都只有 aarch64 版，x86 上链不上。
`wsl-cross/native-stub/` 里有**同签名**的空实现顶掉：

| 桩 | 顶掉什么 | 影响面 |
|----|----------|--------|
| `rknn_stub.c` | RKNN 运行时（7 个函数） | 只影响 AI 推理；模型加载按「失败」路径走 |
| `rga_stub.c` | RGA 硬件加速（11 个函数） | 只影响图像硬件加速（项目本来就 `DISABLE_RGA`） |
| `turbojpeg_stub.c` | JPEG 编解码（9 个函数） | 只影响 AI 图像预处理 |

**业务逻辑路径完全真实** —— 状态机、命令策略、MQTT 协议、点位表、配置、数据库、
HTTP 服务在原生构建里跑的是同一份生产代码，没有替身。桩只在第一次被调用时打一行
stderr 提示，不会静默生效。

### 跑哪些测试

一个服务一个测试文件，文件自包含（断言助手写在文件内），互不依赖、不需要额外辅助程序。

| target | 类型 | 验什么 |
|--------|------|--------|
| `config_test` | 纯逻辑 | 配置读写 / 校验 / 模板生成、补丁保存回滚、内置点位表自洽、配置台畸形请求不崩、日志查询 |
| `washcar_test` | 纯逻辑 | 海天 / 驿公里两套状态机规则 + 洗车服务四条命令 + 维修互斥红线 |
| `command_test` | 纯逻辑 | 云端报文格式 + 命令服务收发全链路 + MQTT 断网重连补发 |
| `http_test` | 纯逻辑 | HTTP 静态文件服务 + 控制器抛异常回 500 且服务不死 |
| `timer_test` | 纯逻辑 | 定时器两档任务 / 改周期 / 删除 / 停机 |
| `detection_test` | 全链路 | 检测服务生命周期 / 帧缓冲 + AI 模型工厂 + 检测区域 |
| `fx_test_client_socket` | 客户端工具 | FX PLC 协议层（要外部 vplc socket，不是自跑测试） |

不编的（要真硬件 / 真云）：`camera_test`（摄像头）、`mqtt_test`（真 broker）、
`tcp_plc_test`（真 PLC）、`ai_load_test` / `yolov8_perf_test`（NPU 模型）。

### 跑主程序的全链路闭环

`tools/plc-mock/run_local_native.sh` 把梯形图虚拟 PLC、自建 MQTT 测试服务器、网页看板和**原生 main**
串起来，在 WSL 本机跑完整链路（采集 → 状态机 → 故障判定 → MQTT 上报）：

```bash
bash wsl-cross/build-native.sh              # 先编出原生 main
bash tools/plc-mock/run_local_native.sh
```

> 更细的用法（指定机型、现场预设、暂停单步）看 `tools/plc-mock/README.md`。
> 老的 `run_local.sh`（跑 aarch64 main）在 WSL 里执行不了，已删除；现在统一走这个脚本。

### 原生构建的已知边界

- 原生构建用宿主 gcc-11，盒子构建用 gcc-9。`font8x8_basic.h` 里 `char` 数组装 `0xFF`
  在 gcc-11 下是窄化错误 → `yolov8_perf_test` 这个 target 原生编不了（它要 NPU，本来也跑不了）。
  语法闸门用 gcc-9，不受这个影响。
- `build-native.sh` 和 `build-fast.sh` 共用同一份 WSL 源码副本（`~/washcar-wsl`），两边不要同时跑。

## 部署到盒子

密码不写进仓库。推荐用 SSH key（也支持 ssh-agent）：

```bash
# 传输 + ldd 依赖校验
BOX_SSH_KEY=~/.ssh/id_ed25519 bash wsl-cross/deploy-wsl.sh

# 传输 + 校验 + 启动运行 3 秒
BOX_SSH_KEY=~/.ssh/id_ed25519 bash wsl-cross/deploy-wsl.sh run
```

没有 key 时才用密码；`sshpass -e` 从环境变量读，不会出现在 `ps` 里：

```bash
BOX_PASS=你的密码 bash wsl-cross/deploy-wsl.sh
```

默认盒子 `ztl@192.168.31.248`，目标目录 `~/washcar/deployed`。可用环境变量覆盖：

```bash
BOX_HOST=user@other-ip BOX_SSH_KEY=~/.ssh/id_ed25519 bash wsl-cross/deploy-wsl.sh
```

## 性能对比

| 方式 | 冷编译 | 增量编译 | 内存占用 |
|------|--------|----------|----------|
| 盒子本机编译 | 3-5 分钟 | 1-2 分钟 | 高（易 OOM） |
| Docker 交叉编译 | 1-2 分钟 | 5-15 秒 | Docker 常驻 1-2GB |
| **WSL 交叉编译** | **~1.5 分钟** | **5-10 秒** | **低（无 Docker 开销）** |
| **原生 x86_64（只验逻辑）** | **57 秒**（实测，清空 ccache 后） | **2-6 秒**（实测） | 低 |
| **整套逻辑闸门**（语法→编译→跑测试→梯形图→交叉编译） | — | **75 秒**（实测，增量状态） | 低 |

## 文件结构

```
wsl-cross/
├── 一键编译.cmd                   # ★ Windows 双击入口（转调 build-menu.ps1）
├── build-menu.ps1                # ★ 交互菜单 + 进度条/报错渲染
├── build-win.sh                  # ★ 构建引擎（体检→同步→configure→编译→回传）
├── setup-env.sh                  # 一次性环境初始化（root 执行）
├── toolchain-aarch64-wsl.cmake   # CMake 工具链（gcc-9 + 独立 sysroot）
├── build-fast.sh                 # 盒子产物构建入口（源码同步到 WSL 原生盘再编）
├── build-native.sh               # 原生 x86_64 编译（逻辑验证用，硬件库走桩）
├── verify.sh                     # 逻辑验证闸门（语法→编译→跑测试→梯形图）
├── verify-win.ps1                # Windows 侧一键入口（绕开路径里的括号）
├── deploy-wsl.sh                 # 部署脚本（支持 SSH key / 密码）
├── check-wsl-env.sh              # 交叉编译环境体检（查 sysroot crt / RUNPATH 等静默错配）
├── native-stub/                  # 原生构建用的硬件库桩（RKNN / RGA / turbojpeg）
├── run/                          # 直跑 main 的本地配置（git 忽略）
│   └── config.json               #   [E] 编辑 / [R] 用它跑；首次自动生成
├── dist/                         # 一键编译回传落点（git 忽略）
│   ├── aarch64/                  #   盒子 Release 产物 + 依赖库 + 校验清单
│   ├── aarch64-debug/            #   盒子 Debug 产物（带符号）
│   ├── native/                   #   原生 x86_64 产物
│   └── logs/                     #   最近一次完整编译日志
└── README.md                     # 本文件
```

> ★ = 一键编译这条路新增的。原有的 `build-fast.sh` / `build-native.sh` / `verify.sh` 全部保留不动，
> 一键编译只是把它们的能力包装成"能双击、看得见进度"的入口；同步那一步仍然调用
> `build-fast.sh --sync-only`，rsync 排除规则只有一份。

## 与 docker-cross 的关系

- 两套方案并存，互不干扰。WSL 产物在 `~/washcar-build/`，Docker 产物在 `build-cross/`。
- 编译产物完全兼容（都是 aarch64 + glibc 2.31 + gcc-9）。
- 原 Docker 方案可以保留作为备选，也可以停用 Docker Desktop 节省资源。

## 常见问题

**Q: 编译报 `nlohmann/json.hpp: No such file or directory`**
A: sysroot 里没装 nlohmann-json。重新跑 `setup-env.sh`，或手动：
   `cd /tmp && apt-get download nlohmann-json3-dev && sudo dpkg -x nlohmann-json3-dev_*.deb /opt/rk3568-sysroot/`

**Q: 链接报 `undefined reference to pthread_create@GLIBC_2.34`**
A: 用了 gcc-11 的 libstdc++。确认 toolchain 文件里是 `aarch64-linux-gnu-gcc-9`，且 sysroot 里有 `libstdc++.so`（20.04 版本 6.0.28）。

**Q: 链接器卡死（无限循环打开 libpthread.so.0）**
A: sysroot 里的 `libpthread.so` 是符号链接指向 `.so.0`，ld 在 --sysroot 下处理符号链接会死循环。setup-env.sh 会把这些 .so 替换为实际 ELF 文件副本。手动修复：
   `sudo cp /opt/rk3568-sysroot/lib/aarch64-linux-gnu/libpthread-2.31.so /opt/rk3568-sysroot/usr/lib/aarch64-linux-gnu/libpthread.so`

**Q: 盒子上跑报 `libxxx.so: cannot open shared object file`**
A: 盒子上缺那个库。在盒子上 `sudo apt install libxxx-dev:arm64`，或确认盒子系统版本是 Ubuntu 20.04。
