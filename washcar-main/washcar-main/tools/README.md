# tools/

本目录放给 TRAE / VS Code 用的工具文件,不参与产品构建。

## gx3-mcp/

驿公里 FX5U `.gx3` 工程只读解析工具链(MCP stdio 服务 + 解析数据 + 工程快照)，随项目走。快速使用与文件清单见 `gx3-mcp/README.md` 和根 `README.md` 对应章节。

## plc-mock/

海天洗车机 PLC 梯形图的虚拟 PLC:用 Python 解释执行 GX Works 导出的梯形图，在 PC 上把 PLC 跑起来，可当串口 PLC 给主程序连。快速使用见 `plc-mock/README.md`。

## 如果你以后要做真正的远程调试

编译产物是 aarch64 二进制,Windows 本机跑不动,调试只能走盒子:先在盒子上跑 `gdbserver :1234 ./main`,VS Code / TRAE 这边用 `cppdbg` attach(需装 C/C++ 扩展)。这块后面再搞。
