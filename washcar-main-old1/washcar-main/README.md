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
bash wsl-cross/build-wsl.sh
bash wsl-cross/deploy-wsl.sh run
```

> 源码建议放在 WSL 家目录下（如 `~/washcar-wsl`），不要直接在 `/mnt/c/...` 编译，IO 慢很多。

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
