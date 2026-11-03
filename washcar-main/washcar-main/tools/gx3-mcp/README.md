# GX Works3 梯形图 MCP · 核对版

这是针对你提供的 FX5U `.gx3` 工程重新实现的只读解析器。读取工程保存文件，无需打开 GX Works3，不连接 PLC，不修改原工程。运行服务只需要 Python 3.10 或更新版本，无第三方依赖。

**先打开 `analysis/yigongli/data/核对页面.html`，逐项与 GX Works3 核对。结构测试通过不等于全工程已经人工验收。**

## 目录结构(按厂商拆分, 支持多厂商工程)

```text
gx3-mcp/
├── server.py / gx3/ / review.py / test_*.py / start_mcp.cmd     # 工具(顶层, 与厂商无关)
├── mcp-config.json / codex-config.toml / checksums.sha256        # 接入配置与校验(指向具体厂商工程)
├── projects/                                                     # 各厂商原始 GX Works 工程
│   ├── yigongli/source_snapshot.gx3 + source-provenance.json     # 驿公里 FX5U
│   └── haitian/                                                  # 海天工程到位后放这里
└── analysis/                                                     # 各厂商解析结果
    ├── yigongli/data/  (project.json / 核对页面.html / coverage 等)
    └── haitian/
```

接入新厂商工程(如海天): 把 `.gx3` 放入 `projects/<厂商>/`, 跑 `review.py --project ... --out analysis/<厂商>/data`, 再更新 `mcp-config.json` / `codex-config.toml` / `start_mcp.cmd` 的 `--project` 路径。

## 这次重做的关键

- 直接解析 `V1` 字符串表：读取数量及 UTF-16 长度，然后将每个占位符绑定到对应语法树字段。不会按冒号随意切断注释、按固定指令名单筛选，或把设备类型与数值分开猜配。
- 递归保留所有原始节点、操作数次序和修饰结构，包含 `K4M250`、寄存器位选择、字符串、比较触点、ME、脉冲指令、子程序入口和跨行连接。
- 使用显式横线、纵线和触点端口建立拓扑。导线合并为连接节点，触点作为带条件的有向边；输出指令是端点。串并联结构不会被压成一个“全部条件同时满足”的列表。
- 两个梯形图库的有序块经条件连接、指令、输出顺序、操作数、入口和注释签名核对后去重。选择与步号表 GUID 对应的 `f147681f1865ec6a_LDDB.db`，不依赖文件大小。另一个库的原始数据仍可查询。
- 所有软元件注释保留来源、语言编号及删除标志，不用单一字典覆盖同名软元件的其他注释。标签文本另行保存，类别未判定时不冒充注释。
- 每次服务启动读取一个固定快照，各查询都返回 SHA-256。分页接口返回总数和 `next_offset`，不会悄悄截断结果。

## 本工程核对范围

| 项目 | 数量 / 结果 |
|---|---:|
| 去重后记录 | 678 |
| 梯形图块 | 644 |
| 程序说明 | 33 |
| 结束块 | 1 |
| 触点与指令 | 2,604 |
| 软元件注释 | 3,521 |
| 其他标签文本（不计入已确认注释） | 20 |
| 与步号表对应 | 678 / 678 |
| 两库逻辑签名一致 | 678 / 678 |

截图中首四个梯级已作为回归样例：步号 `0、22、29、36`；`S0/S1/S2/S3/S4/S5/S81` 为常闭，`SM8013` 为上升沿，`INC D2000`；随后分别为 `Y6→D2001`、`Y7→D2002`、`Y14→D2003`，注释与截图相符。

FX5U 的 X/Y 显示为八进制编号。此工程 LDDB 地址字段把显示字符按十六进制编码保存，例如字段值 `20` 对应显示 `Y14`；注释库保存实际八进制地址，`12` 对应 `Y14`。两套编码分别处理，原始字段同时保留。不能把“LDDB 的编码转换”当成“FX5U 使用十六进制 I/O 编号”。

## 接入 Agent

`mcp-config.json` 是本机可直接使用的通用 stdio MCP 配置；`codex-config.toml` 是对应的 Codex 配置片段。它们使用绝对路径指向本目录中的 `server.py` 和 `projects/yigongli/source_snapshot.gx3`。移动目录或切换厂商工程后须更新路径。

本次已将服务登记到当前任务的 `.codex/config.toml`，并由 Codex CLI 确认识别为 `gx3_ladder`、`enabled=true`。当前对话已经载入的工具列表可能需要重新加载任务才能更新。

Codex 使用 `[mcp_servers.gx3_ladder]` 配置。可把片段加入所需项目的 `.codex/config.toml`，或已有个人配置；不要覆盖其中的其他设置。[配置依据](https://learn.chatgpt.com/docs/extend/mcp#configure-with-configtoml)。

通用客户端启动参数：

```text
command: C:/python3.10/python.exe
args: ["本目录绝对路径/server.py", "--project", "工程文件绝对路径.gx3"]
```

也可让支持 Windows `.cmd` 启动方式的客户端使用 `start_mcp.cmd`。stdio 服务等待客户端输入，单独启动不会弹出查询界面；人工核对请打开 HTML 文件。

默认配置使用交付包里的固定工程快照，以便你的核对结果可复现。需要读取后续保存的工程时，把 `--project` 指向该工程，再重启 MCP 服务。它不包含 GX Works3 尚未保存的修改或 PLC 实时状态。

## 查询工具

| 工具 | 用途 |
|---|---|
| `project_info` | 先调用：获取来源、校验值、覆盖计数、重复库证明与解析限制 |
| `list_blocks` | 按程序说明、指令或操作数字符串寻找梯级，返回块 ID 与步号 |
| `get_block` | 完整梯级、操作数、注释、连接节点、触点边、STL/子程序上下文；`raw=true` 返回原记录和语法树 |
| `search_device` | 精确查询软元件所有出现位置；包含已支持的范围成员，保留显式/推导来源区别 |
| `device_relations` | 逐个出现位置返回直接上下游、全部上游触点及 CALL 入口 |
| `trace_device` | 跨梯级查询可能的软元件控制/数据影响，支持最多 5 层；属于部分静态分析 |
| `search_comments` | 搜索全部注释及标签文本；空查询分页枚举 |
| `source_inventory` | 全部原始成员、校验值及数据库表数量 |
| `read_source_rows` | 查询原始数据库记录，包括被去重的库，不开放任意 SQL |
| `read_source_bytes` | 按块读取原始成员的 Base64 字节，供进一步研究尚未解码的元数据 |

推荐查询顺序：

1. `project_info {}`，先看覆盖与限制。
2. `search_device {"device":"D2000"}`，找到记录的 `block_id`。
3. `get_block {"block_id":"f147681f1865ec6a_LDDB.db:b2"}`，读取精确串并联结构及注释。
4. `device_relations {"device":"Y14"}`，查看各处引用的相邻元件。
5. `search_comments {"query":"左刷"}`，按业务名称反查。

返回 `next_offset` 非空时，用该值继续下一页。`all_upstream_contacts` 表示图可达关系，不等于这些触点之间全部为 AND。判断具体串并联须使用 `get_block.topology`。同一个软元件的不同出现位置有不同的元素 ID，不能仅按软元件名称合并电气节点。

## 明确的限制

- 这版验证对象是交付的 FX5U 工程，不声称覆盖所有 GX Works3 版本、CPU 系列和未见过的图形结构。原始数据始终保留，结构异常会列入问题清单。
- `ADPRW`、`RS2`、`CRC` 的完整参数已读取，但参数的读写角色和可变缓冲区作用范围未确认，标记为 `unknown`。不要把范围查询“没有命中”当成这些指令绝不影响该软元件。
- STL 上下文及 CALL 入口能查询，但不模拟扫描周期、跳转、子程序执行条件或多字状态变化。`trace_device` 不展开 CALL 的调用条件，也不推测未知缓冲区影响。
- `.txc`、`.w3pa` 等二进制项目元数据保留原始字节，未完整语义解码。程序显示名称的对应关系尚未确认，使用数据库成员名作标识。不能声称所有二进制字段都已解释。
- 核对页面采用逻辑坐标，排版不是 GX Works3 的像素级复刻。所有元件的完整文字在图下表格和原始记录中可查。

## 文件与验证

- `analysis/yigongli/data/核对页面.html`：离线单文件页面，可按元件、指令、注释、步号搜索；全部注释支持分页浏览。
- `analysis/yigongli/data/project.json`：结构化数据、完整语法树、原记录、引用及所有 SQLite 表行。
- `analysis/yigongli/data/coverage.json`：覆盖计数、来源与限制。
- `analysis/yigongli/data/mcp-test-results.json`：官方 Python MCP 客户端实测结果。
- `analysis/yigongli/data/browser-test-results.json`：核对页面全部记录渲染与搜索检查结果。
- `projects/yigongli/source_snapshot.gx3` / `source-provenance.json`：固定工程快照及原始路径、哈希。

运行回归测试：

```powershell
python .\test_project.py
python .\test_mcp_client.py
```

第二项仅测试环境需要已安装的官方 `mcp` Python 包；服务器本身不需要安装它。

重新生成核对资料：

```powershell
python .\review.py --project .\projects\yigongli\source_snapshot.gx3 --out .\analysis\yigongli\data
```

协议按 MCP 的 [stdio 传输](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)和[工具规范](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)实现，返回结构化结果及兼容的 JSON 文本。这里只提供读取工具。
