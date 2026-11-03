# Framework


## 配置nlohmann-json
```json
sudo apt install -y nlohmann-json3-dev
```



## 交叉编译开发流程

项目在 RK3568 盒子上跑，在 Windows 电脑上用 WSL2 + gcc-9 + 独立 sysroot 交叉编译。

### WSL 交叉编译

完整说明看 `wsl-cross/README.md`。快速上手：

```bash
# 首次：初始化环境（root，只需一次）
sudo bash wsl-cross/setup-env.sh

# 日常：编译 + 部署到盒子
bash wsl-cross/build-fast.sh
bash wsl-cross/deploy-wsl.sh run
```

> 源码建议放在 WSL 家目录下（如 `~/washcar-wsl`），不要直接在 `/mnt/c/...` 编译，IO 慢很多。
> `build-fast.sh` 会自己把源码 rsync 到 `~/washcar-wsl` 再编,不用手工搬。

## 现场配置台

盒子启动后，局域网浏览器打开 `http://<盒子IP>:8080/`：
- 改 MQTT 主题（默认斜杠，可切点号）、盒号、Broker；
- 增加/编辑自定义监控点，选变化上报/立即读取/故障上报；
- 按服务（大厅/洗车/MQTT/PLC/AI/配置台）查看日志，支持级别和关键字过滤；
- 校验、应用（重启生效）、历史回滚、改登录密码。

默认账号 `admin` / `washcar`，首次登录后请在“设置”里改密码。
用法和配置文件说明见 `docs/远程配置台.md`。

辅助目录:
- `wsl-cross/` — WSL 交叉编译：环境初始化、CMake 工具链、build/deploy 脚本
- `build-wsl/` — WSL 交叉编译产物(被 `.gitignore`)
- `.ccache-wsl/` — WSL ccache 缓存(被 `.gitignore`)

## PLC 工程(GX Works3)分析工具

`tools/gx3-mcp/` 保存了驿公里 FX5U `.gx3` 工程的只读解析工具链，无需打开 GX Works3、不连 PLC、不改原工程。来源是早期逆向 PLC 程序时的 GX3 MCP 核对版工具链，随项目走以便日后排查梯形图、软元件拓扑与注释。

| 路径 | 内容 |
|------|------|
| `server.py` + `gx3/` | MCP stdio 只读服务：`project_info` / `get_block` / `search_device` / `device_relations` / `trace_device` / `search_comments` 等查询工具 |
| `projects/yigongli/` | 驿公里原始工程快照 `source_snapshot.gx3` + `source-provenance.json`（海天工程到位后放 `projects/haitian/`） |
| `analysis/yigongli/data/` | 解析结果：`project.json`(全量语法树与引用)、`核对页面.html`(离线人工核对入口)、`coverage.json` 等（海天分析放 `analysis/haitian/`） |
| `README.md` | 工具完整说明：目录结构、查询顺序、工具清单、已知限制、回归测试、新厂商接入 |

快速使用：

```bash
# MCP 服务(供 Agent 接入查询梯形图/软元件)
python tools/gx3-mcp/server.py --project tools/gx3-mcp/projects/yigongli/source_snapshot.gx3

# 人工核对：浏览器打开 analysis/yigongli/data/核对页面.html，逐项与 GX Works3 比对
# 回归测试
python tools/gx3-mcp/test_project.py
python tools/gx3-mcp/test_mcp_client.py
```

详细用法、查询工具清单与已知限制见 `tools/gx3-mcp/README.md`。

## 海天梯形图虚拟 PLC

`tools/plc-mock/` 是海天洗车机 PLC 梯形图的虚拟 PLC:用 Python 解释执行 GX Works 导出的梯形图，在 PC/WSL 上把 PLC 跑起来，不连现场 PLC。
旁边带**自建 MQTT 测试服务器**和**网页看板**，专给人工审查用：流程图高亮当前工序、软元件按使用频率列实时值、任意主题负载手动下发命令。

| 路径 | 内容 |
|------|------|
| `start.py` | 唯一启动入口（虚拟 PLC + broker + 看板 + 可选主程序） |
| `core/ladder.py` | 梯形图解释执行器（核心） |
| `core/runtime.py` | 扫描循环、软元件目录、工序追踪 |
| `core/broker.py` | 自建 MQTT broker（测试服务器） |
| `web/` | 网页看板（纯静态，无构建） |
| `simulators/haitian/` | 海天机型：现场条件表 + 物理模型（代码） |
| `docs/海天/` | 海天资料包：机型说明 + 梯形图 CSV/.gxw + 原始资料 |
| `simulators/_template/` | 新机型扩展骨架 |
| `tests/` | 验收闸门、洗车演练、串口桩自测、broker 自测、吞吐基准 |
| `devtools/` | 梯形图审查工具（trace / deps / explain / render / gxw_check …） |

```bash
cd tools/plc-mock
python3 start.py                    # 起模拟器 + MQTT 测试服务器 + 看板
python3 tests/verify_mock.py        # 16 条验收闸门（退出码 0 = 全过）
```

语义口径以 GX Works2 里打开 `.gxw` 的梯形图为准（算术类是三操作数：源1 源2 目标）。
用法、看板说明见 `tools/plc-mock/README.md`；**接新机型 / 改看板的接口契约**见 `tools/plc-mock/docs/接入标准.md`；语义/参数来源见 `tools/plc-mock/docs/海天/梯形图语义与现场标定.md`；文档索引见 `tools/plc-mock/docs/README.md`。
