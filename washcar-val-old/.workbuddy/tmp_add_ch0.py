# -*- coding: utf-8 -*-
import io

P = r"E:/Desktop/washcar-main/washcar-main/docs/盒子上部署Broker与MQTTX联调手册.md"
s = io.open(P, encoding="utf-8").read()

CH0 = u'''## 零、执行环境、目录与命令总索引

> **先读这一章。** 本文档的命令分散在「盒子」和「PC」两个地方执行，混淆是最常见的错误来源。这一章把所有约定集中说明，后面每章开头也会再标一次当章的执行环境。

---

### 0.1 两台机器，两个执行位置

| 标识 | 机器 | 怎么进去 | 提示符长什么样 |
|---|---|---|---|
| **【盒】** | 盒子 RK3568（`192.168.31.249`） | PC 上 `ssh ztl@192.168.31.249` | `ztl@RK356X:~/washcar/deployed$` |
| **【PC】** | 你的 Windows（`192.168.31.196`） | 本机 cmd / PowerShell / MQTTX | `C:\\Users\\lqh15>` 或 `PS C:\\Users\\lqh15>` |

**本文档全程只用这两台。** 之前提到的云服务器 `49.7.211.55` 已清空、本方案不使用，相关章节仅作历史记录保留。

判别技巧：**看提示符。** 出现 `ztl@RK356X` 就是盒子，出现 `C:\\Users\\` 或 `PS C:` 就是 PC。

**能力分工：**

| | 【盒】能做 | 【PC】能做 |
|---|---|---|
| 跑 broker（mosquitto） | ✅ | ❌（本方案没在 PC 装） |
| 跑盒子程序 | ✅ | ❌ |
| 命令行发/收 MQTT | ✅ `mosquitto_pub/sub` | ❌（没装客户端工具） |
| 图形界面收发 MQTT | ❌ | ✅ MQTTX |
| 从外部验证盒子端口 | ❌（自己测自己没意义） | ✅ `curl telnet` |

---

### 0.2 四个关键目录（全部在盒子上）

| 路径 | 放什么 | 属主 | 谁产生 | 什么时候用到 |
|---|---|---|---|---|
| `/home/ztl/washcar/deployed/` | 盒子程序、`config.json`、`lib/`、`data/` | `ztl` | 部署时拷入 | 五、六、十五、十七 |
| `/etc/mosquitto/conf.d/local.conf` | broker 配置 | `root` | 三、写配置 | 三、四、十一 |
| `/var/log/mosquitto/mosquitto.log` | broker 日志（mosquitto 自动写） | `mosquitto` | 自动 | 七、十一 |
| `/tmp/box-mqtt.log` | **盒子程序**日志（由 `tee` 产生） | `root` | 六、启动命令 | 七、十三、十五 |

**四者的关系：**

```
/etc/mosquitto/conf.d/local.conf
        │ mosquitto 启动时读取
        ▼
    mosquitto（broker）──── 自动写 ────> /var/log/mosquitto/mosquitto.log

/home/ztl/washcar/deployed/config.json
        │ 盒子程序启动时读取
        ▼
    盒子程序 ──── 启动命令里的 tee ────> /tmp/box-mqtt.log
        │
        └── 连 127.0.0.1:1883 ──> mosquitto
```

**为什么目录重要（三条硬约束）：**

| 约束 | 后果 |
|---|---|
| `config.json` **必须在程序可执行文件同级目录** | 否则程序读不到配置，用全套默认值（broker 会是旧的 `116.204.32.197`），表现为"连不上" |
| `local.conf` **必须在 `/etc/mosquitto/conf.d/`** 且被主配置 `include_dir` | 否则 broker 回到"只监听 127.0.0.1"，PC 连不进来 |
| 启动程序前 **必须先 `cd` 到 deployed** | 程序里 `./data`、`lib/rknn/models/...` 都是**相对路径**，在别的目录启动会找不到模型和数据库 |

> 最后一条尤其容易踩：日志里会出现 `yolov8 model loaded` 失败、`command.db` 打开失败之类，看着像代码 bug，其实是启动目录错了。

---

### 0.3 一条命令的完整上下文（示例拆解）

以最常用的启动命令为例：

```bash
# 【盒】工作目录 /home/ztl/washcar/deployed
sudo ./washcar 2>&1 | tee /tmp/box-mqtt.log
```

| 要素 | 说明 |
|---|---|
| **在哪执行** | 【盒】盒子，SSH 进去之后 |
| **工作目录** | **必须**是 `/home/ztl/washcar/deployed`（`config.json`、`lib/`、`data/` 都在这里） |
| **为什么 `sudo`** | 要打开串口 `/dev/ttyUSB*`（普通用户无权限），且要写 `/tmp` |
| **`2>&1`** | 把 stderr 合并进 stdout，否则错误信息不会进日志文件 |
| **`tee`** | 同时输出到屏幕和文件。用 `| tee` 才能前台看着、后台留着 |
| **日志归谁** | `root`（因为 sudo）。普通用户 `grep` 一般能读；若报 Permission denied，用 `sudo grep` |

---

### 0.4 连 broker 时，地址到底填什么（最容易搞错的一张表）

| 你在哪执行 | `-h` / Host 填什么 | 为什么 |
|---|---|---|
| **盒子程序**（`config.json` 的 `mqtt.brokerIp`） | `127.0.0.1` | 程序和 broker 同一台机，走内核回环，最稳 |
| **盒子终端** `mosquitto_pub` / `mosquitto_sub` | `127.0.0.1` | 同上 |
| **PC 的 MQTTX** | `192.168.31.249` | 跨机，必须用**盒子的内网 IP** |
| **PC 的** `curl -v telnet://…` | `192.168.31.249` | 同上 |

> **PC 上填 `127.0.0.1` 是个典型事故**——那指的是 PC 自己，消息根本不会到盒子。现象是"两端都正常、就是收不到对方"。

---

### 0.5 命令总索引（在哪台机、哪个目录、要不要 sudo）

| 章 | 命令 | 环境 | 工作目录 | sudo | 前置依赖 |
|---|---|---|---|---|---|
| 二 | `apt update` / `apt install -y mosquitto` | 【盒】 | 任意 | ✅ | 能连外网 |
| 二 | `apt install -y mosquitto-clients tmux` | 【盒】 | 任意 | ✅ | 同上 |
| 三 | `tee /etc/mosquitto/conf.d/local.conf`（heredoc） | 【盒】 | 任意 | ✅ | 已装 mosquitto |
| 三 | `cat /etc/mosquitto/conf.d/local.conf` | 【盒】 | 任意 | ❌ | 上一步已执行 |
| 四 | `systemctl restart mosquitto` | 【盒】 | 任意 | ✅ | 配置文件无语法错 |
| 四 | `systemctl enable mosquitto` | 【盒】 | 任意 | ✅ | 上一步成功 |
| 四 | `ss -lntp \\| grep 1883` | 【盒】 | 任意 | ❌ | 服务已起 |
| 四 | `mosquitto -c … -v`（前台排错） | 【盒】 | 任意 | ✅ | **服务已停**，否则端口冲突 |
| 五 | `grep -nA6 '"mqtt"' config.json` | 【盒】 | **deployed** | ❌ | `config.json` 存在 |
| 五 | 编辑 `config.json` | 【盒】 | **deployed** | ❌ | 有该目录写权限 |
| 六 | `tmux new -s box` | 【盒】 | 任意 | ❌ | 已装 tmux |
| 六 | `sudo ./<程序名> 2>&1 \\| tee /tmp/box-mqtt.log` | 【盒】 | **deployed** | ✅ | broker 已起、config 已改 |
| 七 | `ss -tnp \\| grep 1883` | 【盒】 | 任意 | ❌ | 程序已启动 |
| 七 | `tail /var/log/mosquitto/mosquitto.log` | 【盒】 | 任意 | ✅ | broker 已起 |
| 七 | `grep -a 'recv topic' /tmp/box-mqtt.log` | 【盒】 | 任意 | ❌ | 程序已启动 |
| 八 | MQTTX 新建连接、订阅、发布 | **【PC·界面】** | — | — | 盒子 broker 在监听、MVTTX 已装 |
| 十 | `which mosquitto_pub \\|\\| apt install -y mosquitto-clients` | 【盒】 | 任意 | ✅ | — |
| 十 | `mosquitto_sub -h 127.0.0.1 -t '…' -C 1 -W 60 -v` | 【盒】 | 任意 | ❌ | broker 已起 |
| 十 | `mosquitto_pub -h 127.0.0.1 -t '…' -m '{}' -q 0` | 【盒】 | 任意 | ❌ | broker 已起 |
| 十 | MQTTX 发布 `flexem/123456/read` | **【PC·界面】** | — | — | 已订阅 `flexem/#` |
| 十一 | `tee -a /etc/mosquitto/conf.d/local.conf`（加日志级别） | 【盒】 | 任意 | ✅ | 已装 mosquitto |
| 十一 | `tail -f /var/log/mosquitto/mosquitto.log` | 【盒】 | 任意 | ✅ | 已开 `log_type all` |
| 十三 | `grep -a … /tmp/box-mqtt.log` | 【盒】 | 任意 | ❌ | 程序曾启动过 |
| 十三 | `scp ztl@192.168.31.249:/tmp/box-mqtt.log …` | **【PC】** | 任意 | — | PC 与盒子同网段 |
| 十五 | `tmux ls` / `tmux attach -t box` | 【盒】 | 任意 | ❌ | 有 box 会话 |
| 十五 | `tmux kill-session -t box` | 【盒】 | 任意 | ❌ | 有 box 会话 |
| 十六 | `Ctrl-B` 然后 `D`（脱离） | 【盒·tmux 内】 | — | — | 正在 tmux 里 |
| 十七 | `tar czf /tmp/washcar-deployed.tar.gz deployed/` | 【盒·旧】 | `/home/ztl/washcar` | ❌ | — |
| 十七 | `scp ztl@旧IP:/tmp/….tar.gz .` | **【PC】** | 任意（存哪就哪） | — | 能 SSH 到旧盒子 |
| 十七 | `tar xzf washcar-deployed.tar.gz` | 【盒·新】 | `/home/ztl/washcar` | ❌ | 已把包传上来 |
| 十八 | `systemctl stop/disable mosquitto` | 【盒】 | 任意 | ✅ | — |
| 十八 | `apt purge -y mosquitto …` | 【盒】 | 任意 | ✅ | 已停服务 |

> 表中 【PC·界面】= 在 MQTTX 图形界面里点，不涉及命令行和目录。

---

### 0.6 章节之间的先后依赖

```
第一章  原理（读，不执行）
   ↓
第二章  装 broker ──────────┐
   ↓                        │
第三章  写 broker 配置       │  必须在 broker 装好之后，
   ↓                        │  且必须重启才生效
第四章  启动 + 开机自启 ─────┘
   ↓   (此时 broker 已在 0.0.0.0:1883 监听)
第五章  改盒子程序 config.json
   ↓   (config 只在程序启动时读一次)
第六章  启动程序（tmux）──────── 必须在 broker 起来之后，
   ↓                              否则程序会一直退避重连
第七章  三重证据确认"真连上了"
   ↓
第八章  MQTTX 建连接 + 订阅
   ↓
第九章  双向验证清单（read / readdata）
   ↓
第十章  盒子终端 ↔ MQTTX 互发（三轮通信证明）
   ↓
第十五章 日常操作（每天重复 6→7→10 的循环）
   ↓
第十六章 tmux / 第十七章 换机 / 第十八章 清理（按需）
```

**四条"顺序错了就查不出问题"的地方：**

| 必须满足的顺序 | 违反后的现象 |
|---|---|
| 先起 broker，再起程序 | 程序日志刷 `MQTT connect failed`，但程序会一直重试，不是致命错 |
| 先改 `config.json`，再起程序 | 改完不重启，程序仍用旧地址连，现象是"改了没生效" |
| 先订阅，再发布 | 发布瞬间没有订阅者，QoS0 消息直接丢弃，现象是"发了没反应" |
| 先装 `mosquitto-clients`，再用 `mosquitto_pub` | 报 `command not found` |

---

'''

anchor = u"## 一、为什么把 broker 装在盒子上"
assert anchor in s
s = s.replace(anchor, CH0 + anchor, 1)

# 每章开头的执行环境横幅
banners = [
    (u"## 二、在盒子上安装 Mosquitto",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **任意** · 全部命令需要 `sudo`。环境与目录约定见「零」。"),
    (u"## 三、写配置",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **任意**（写的是绝对路径）· 必须 `sudo`（`/etc/mosquitto/` 属 root）。"),
    (u"## 四、启动与自启",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **任意** · `systemctl` / `ss` / `mosquitto -v` 都需要 `sudo`。"),
    (u"## 五、改盒子程序的 config.json",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **必须是 `/home/ztl/washcar/deployed`**（`config.json` 与可执行文件同级，否则程序读不到）· 编辑文件一般不需要 `sudo`。"),
    (u"## 六、让程序常驻运行",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **必须是 `/home/ztl/washcar/deployed`**（程序用 `./data`、`lib/rknn/models/` 等相对路径）· 启动命令需要 `sudo`（开串口设备）。**前置：broker 已起、config.json 已改。**"),
    (u"## 七、确认盒子程序真的连上了 broker",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **任意** · `ss` 与 `grep` 不需要 sudo；读 `/var/log/mosquitto/` 下的日志需要 `sudo`。"),
    (u"## 八、PC 端 MQTTX 操作",
     u"> **【执行环境】** **PC（Windows）图形界面**，不涉及命令行和目录。**前置：盒子 broker 已在 `0.0.0.0:1883` 监听。**"),
    (u"## 九、双向验证清单",
     u"> **【执行环境】** 混合：发布动作在 **【PC】MQTTX 界面**，观测动作在 **【盒】任意目录** 或 MQTTX 的 `Received` tab。"),
    (u"## 十、用 `flexem/123456/read` 验证两端互通",
     u"> **【执行环境】** 混合：`mosquitto_pub/sub` 在 **【盒】** 上（工作目录任意，不需要 sudo）；MQTTX 操作在 **【PC】** 界面。地址差异见 0.4。"),
    (u"## 十一、排查步骤（按顺序）",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **任意** · 改 `conf.d` 配置和读 broker 日志需要 `sudo`。"),
    (u"## 十三、日志被 PLC 报错淹没怎么办",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **任意**（日志是绝对路径 `/tmp/box-mqtt.log`）· `grep` 不需要 sudo；`scp` 那条在 **【PC】** 的 cmd 里执行。"),
    (u"## 十五、日常操作手册",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 启动程序相关命令的工作目录 **必须是 `/home/ztl/washcar/deployed`**；查询类命令（`ss` / `tmux ls` / `grep`）目录任意。"),
    (u"## 十六、tmux 完整操作手册",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）。`tmux` 命令在**任意目录**执行；快捷键在**tmux 会话内部**按（不是普通 shell 里）。"),
    (u"## 十七、在新盒子上从头部署（换机清单）",
     u"> **【执行环境】** 三段式：打包在 **【盒·旧】**、`scp` 在 **【PC】**、解包与部署在 **【盒·新】**。每段工作目录都不同，逐条看标注。"),
    (u"## 十八、清理（撤掉本方案）",
     u"> **【执行环境】** 盒子（`ztl@RK356X`）· 工作目录 **任意** · 卸载与删系统目录需要 `sudo`。"),
    (u"## 十九、完整操作顺序（最短路径）",
     u"> **【执行环境】** 混合，按注释里的 `【盒】` / `【PC】` 区分。盒子侧工作目录默认 `/home/ztl/washcar/deployed`。"),
    (u"## 二十、常见问题速查",
     u"> **【执行环境】** 混合。表中「处理」列的命令，除注明【PC】外，都在 **【盒】** 上、工作目录任意。"),
]

for head, banner in banners:
    assert head in s, head
    s = s.replace(head, head + u"\n\n" + banner, 1)

# 目录加入第零章
s = s.replace(
    u"| 一~五 | 原理、装 broker、写配置、改 config.json | **首次部署** |",
    u"| **零** | **执行环境、目录、命令总索引（先读这一步）** | **动手之前** |\n| 一~五 | 原理、装 broker、写配置、改 config.json | **首次部署** |"
)

io.open(P, "w", encoding="utf-8").write(s)
print("OK, lines:", len(s.splitlines()))
