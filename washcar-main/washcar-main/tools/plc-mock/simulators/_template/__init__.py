"""新机型骨架 —— 复制这个目录，改名为机型 key（不要以 `_` 开头），再改三个文件。

以「再加一台海天同款」为例：

    cp -r simulators/_template simulators/haitian2
    # 改 simulator.py 里的 key/title/ladder_path/flow
    mkdir -p docs/海天2 && cp 你的梯形图.csv docs/海天2/

`SIMULATOR` 暴露出来就会被 `simulators.discover()` 自动注册，
`python3 start.py --plc haitian2` 和看板的机型下拉立刻能看到。
"""

from .simulator import SIMULATOR

__all__ = ['SIMULATOR']
