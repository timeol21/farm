"""梯形图侧的人工审查工具（只读，不参与运行时）。

    dump.py       打印指令区间
    trace.py      某个软元件被谁读、被谁写
    deps.py       要让它成立还需要哪些条件
    rung.py       打印一条输出指令的完整条件链
    explain.py    条件链逐条求值，标出哪一条是假的
    diag.py       运行时诊断：卡在哪个步、哪条转移条件不成立
    render.py     把指令表还原成梯形图（HTML / 文本）
    gxw_check.py  和 GX Works2 工程 .gxw 交叉对账
    stl_steps.py  STL 步结构（步范围 / 转移目标），给 diag 用
    analysis/     现场故障反推（参数扫描）
"""
