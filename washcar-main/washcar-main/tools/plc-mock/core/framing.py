"""FX 串口帧编解码（与 src/.../fx_plc_instance.cpp 的帧格式一一对应）。

读帧（11 字节）: 02 30 AA BB CC DD '0' '2' 03 SUM SUM
    D 寄存器 addr = 0x1000 + d*2          回 4 位 hex（低字节在前）
    M 寄存器 addr = 0x0100 + (m/16)*2     回 4 位 hex（收发两侧各做一次字节交换）
    X 组     addr = 0x0080 + group        回 2 位 hex
    Y 组     addr = 0x00A0 + group        回 2 位 hex
    S 位     addr = s/8                   回 2 位 hex

写帧（9 字节）: 02 37|38 LL LL HH HH 03 SUM SUM，回单字节 0x06
    X 位  addr = 0x0400 + (X/10)*8 + X%10   （八进制线性位）
    Y 位  addr = 0x0500 + Y                （本工程未用到）
    M 位  addr = 0x0800 + M

注意：读帧地址是**高位在前**，写帧地址是**低字节在前**，两边不一样。
"""

STX = 0x02
ETX = 0x03
ACK = 0x06


def framed(hex_body):
    """把帧体（hex 文本）包成 STX ... ETX SUM SUM。"""
    frame = b"\x02" + hex_body.encode() + b"\x03"
    return frame + ("%02X" % (sum(frame[1:]) & 0xFF)).encode()


def bitfield(bits, base, width):
    """从位字典里取一段，低位在前。"""
    value = 0
    for index in range(width):
        if bits.get(base + index, 0):
            value |= 1 << index
    return value


def x_write_address(x):
    """X 位写地址（八进制线性位）。"""
    return 0x0400 + (x // 10) * 8 + (x % 10)


def y_write_address(y):
    return 0x0500 + y


def m_write_address(m):
    return 0x0800 + m
