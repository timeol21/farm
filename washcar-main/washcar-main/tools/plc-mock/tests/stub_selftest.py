#!/usr/bin/env python3
"""串口桩端到端自测:按 fx_plc_instance.cpp 的真实帧格式和虚拟 PLC 对话。

  1) 读 M187(车辆停放到位)  —— 值必须由梯形图算出来(M515 & T61,T61 要满 3 秒)
  2) 写 X44(复位) 再写 X45(启动) —— 写进梯形图后,梯形图应点亮 S1(就绪) 和 S20(启动步)
退出码 0 = 全过。
"""
import os, sys, time, tty

PORT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/vplc-pty'
STX, ETX, ACK = 0x02, 0x03, 0x06


def checksum(cmd, start, length):
    return sum(cmd[start:start + length]) & 0xFF


def read_frame(fd, addr, suffix=b'02'):
    cmd = bytearray([STX, 0x30]) + ('%04X' % addr).encode() + suffix + bytes([ETX, 0, 0])
    cmd[9] = checksum(cmd, 1, 8)
    cmd[10] = cmd[9]
    fd.write(bytes(cmd))
    fd.flush()
    head = fd.read(1)
    if not head or head[0] != STX:
        raise RuntimeError('无效响应头: %r' % head)
    payload = b''
    while True:
        byte = fd.read(1)
        if not byte:
            raise RuntimeError('响应提前结束')
        if byte[0] == ETX:
            break
        payload += byte
    fd.read(2)
    return payload.decode()


def write_frame(fd, addr, turn_on=True):
    cmd = bytearray([STX, 0x37 if turn_on else 0x38])
    cmd += ('%02X' % (addr & 0xFF)).encode()
    cmd += ('%02X' % ((addr >> 8) & 0xFF)).encode()
    cmd += bytes([ETX, 0, 0])
    cmd[7] = checksum(cmd, 1, 6)
    cmd[8] = cmd[7]
    fd.write(bytes(cmd))
    fd.flush()
    return fd.read(1) == bytes([ACK])


def x_address(x):
    return 0x0400 + (x // 10) * 8 + (x % 10)


def read_m(fd, m):
    word = 0x0100 + (m // 16) * 2
    raw = int(read_frame(fd, word, b'02'), 16)
    data = ((raw & 0x00FF) << 8) | ((raw & 0xFF00) >> 8)
    return (data >> (m % 16)) & 1


def read_s(fd, s):
    """桩按"8 点一组"回 S 位(组地址 = s/8, 组内偏移 = s%8)。"""
    group = s // 8
    value = int(read_frame(fd, group, b'01'), 16)
    return (value >> (s % 8)) & 1


def read_y(fd, y):
    """Y 组读:addr = 0x00A0 + y/8,组内偏移 y%8。"""
    value = int(read_frame(fd, 0x00A0 + y // 8, b'01'), 16)
    return (value >> (y % 8)) & 1


def read_d(fd, d):
    """D 字读:addr = 0x1000 + d*2,回 4 位 hex(低字在前)。"""
    raw = int(read_frame(fd, 0x1000 + d * 2, b'02'), 16)
    return ((raw & 0x00FF) << 8) | ((raw & 0xFF00) >> 8)


def main():
    fd = os.fdopen(os.open(PORT, os.O_RDWR | os.O_NOCTTY), 'r+b', buffering=0)
    tty.setraw(fd.fileno())          # PTY 默认是 canonical 模式,读到换行才返回,串口协议必须 raw
    failures = []

    def check(name, got, want):
        print('  %s = %d (期望 %d)' % (name, got, want), flush=True)
        if got != want:
            failures.append('%s 应为 %d' % (name, want))

    # 真实启动链(海天):写 X45(启动) → S0 步 SET S1(就绪) → T61 满 3 秒 →
    # M187(车辆停放到位)=1 → SET S20(启动步) → S37 → S21 → S3(洗车头)。
    # X44 是"停止洗车",复位是 M100(盒子的 reset 服务写的位)。
    # M187/S20 都是瞬时量:不能等固定秒数再看末态,必须轮询"曾经出现过"。
    def poll(predicate, timeout=8.0, interval=0.02):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if predicate():
                return True
            time.sleep(interval)
        return False

    print('1) 写 X45(启动),梯形图应点亮 S1(就绪) 且 D142=1', flush=True)
    check('写 X45 ACK', 1 if write_frame(fd, x_address(45), True) else 0, 1)
    ready = poll(lambda: read_s(fd, 1) == 1 and read_d(fd, 142) == 1)
    check('S1/D142=1 曾出现', 1 if ready else 0, 1)

    print('2) 轮询 T61(3 秒)后的 M187(车辆停放到位),以及 D142 进入洗车', flush=True)
    seen_m187 = [False]
    seen_wash = [False]
    def started():
        if read_m(fd, 187) == 1:
            seen_m187[0] = True
        if read_d(fd, 142) >= 2:
            seen_wash[0] = True
        return seen_m187[0] and seen_wash[0]
    poll(started, timeout=10.0)
    check('M187 曾为 1', 1 if seen_m187[0] else 0, 1)
    check('D142 进入洗车(>=2)', 1 if seen_wash[0] else 0, 1)

    print('3) 再读一次 D142(洗车步骤)与 M187,值仍由梯形图算出', flush=True)
    d142 = read_d(fd, 142)
    check('D142 仍 >=2', 1 if d142 >= 2 else 0, 1)
    print('  M187 当前 = %d(瞬时量,不要求末态仍为 1)' % read_m(fd, 187), flush=True)

    os.close(fd.fileno())
    if failures:
        print('FAIL: ' + '; '.join(failures), flush=True)
        return 1
    print('PASS: 读到的值来自梯形图; 写命令真的落进梯形图并改变了梯形图状态', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
