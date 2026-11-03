"""PLC 模拟器内核。

分层（外层目录只放启动程序 / 说明 / 看板，实现都在这里）：

    ladder.py       梯形图解释执行（FX 栈机语义）
    serial_stub.py  用 PTY 冒充 FX 串口，按主程序的帧格式应答
    runtime.py      扫描循环、物理模型调度、软元件目录与快照、调速/暂停/强制写入
    broker.py       自建 MQTT broker（测试服务器）
    dashboard.py    网页看板 + JSON API（HTTP）
    framing.py      FX 串口帧编解码
"""

__all__ = ['ladder', 'serial_stub', 'runtime', 'broker', 'dashboard', 'framing']
