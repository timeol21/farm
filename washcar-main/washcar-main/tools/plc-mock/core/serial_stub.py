"""PTY 冒充 FX 串口：把梯形图算出来的软元件值按真实帧格式喂给主程序。

一个进程里两条线程：扫描在 runtime 里跑，收发在这里跑；两边共用同一个 Ladder 实例，
读到的永远是梯形图当前值 —— 不会出现"桩自己编一个值"的情况。

用法上不直接调，`start.py` 负责装配。单独调试时：

    python3 -c "import sys; sys.path.insert(0,'.'); ..."
"""

import os
import select
import threading
import time

from . import framing


class SerialStub:
    """把 /tmp/vplc-pty 这类符号链接指向一个 PTY，并按 FX 协议应答。"""

    def __init__(self, ladder, link='/tmp/vplc-pty', log_path=None, logger=None):
        self.ladder = ladder
        self.link = link
        self.log_path = log_path
        self._log_file = open(log_path, 'a', buffering=1) if log_path else None
        self._log_lock = threading.Lock()
        self._external_logger = logger
        self._master = None
        self._slave_name = None
        self._thread = None
        self._running = False
        self.reads = 0
        self.writes = 0
        self.last_activity = 0.0
        self.started_at = 0.0
        self.error = ''

    # ---------------------------------------------------------------- 生命周期
    def start(self):
        master, slave = os.openpty()
        self._slave_name = os.ttyname(slave)
        self._master = master
        if os.path.islink(self.link) or os.path.exists(self.link):
            os.unlink(self.link)
        os.symlink(self._slave_name, self.link)
        self._running = True
        self.started_at = time.time()
        self._thread = threading.Thread(target=self._loop, name='serial-stub', daemon=True)
        self._thread.start()
        self.log('pty slave=%s linked to %s' % (self._slave_name, self.link))
        return self.link

    def stop(self):
        self._running = False
        if self._log_file:
            try:
                self._log_file.close()
            except OSError:
                pass

    def stats(self):
        return {
            'link': self.link,
            'slave': self._slave_name,
            'reads': self.reads,
            'writes': self.writes,
            'lastActivity': self.last_activity,
            'startedAt': self.started_at,
            'error': self.error,
        }

    def log(self, message):
        stamp = '%.3f %s\n' % (time.time(), message)
        if self._log_file:
            with self._log_lock:
                try:
                    self._log_file.write(stamp)
                except ValueError:
                    pass
        if self._external_logger:
            self._external_logger(message)

    # ---------------------------------------------------------------- 收发
    def _loop(self):
        buffer = bytearray()
        while self._running:
            try:
                ready, _, _ = select.select([self._master], [], [], 0.2)
            except (OSError, ValueError):
                break
            if not ready:
                continue
            try:
                chunk = os.read(self._master, 256)
            except OSError:
                break
            if not chunk:
                break
            self.last_activity = time.time()
            buffer.extend(chunk)
            offset = 0
            size = len(buffer)
            while size - offset >= 2:
                if buffer[offset] != framing.STX:
                    offset += 1
                    continue
                if buffer[offset + 1] == 0x30 and size - offset >= 11:
                    frame = bytes(buffer[offset:offset + 11])
                    offset += 11
                    response = self.handle_read(frame)
                    self.reads += 1
                elif buffer[offset + 1] in (0x37, 0x38) and size - offset >= 9:
                    frame = bytes(buffer[offset:offset + 9])
                    offset += 9
                    response = self.handle_write(frame)
                    self.writes += 1
                else:
                    break
                if response:
                    try:
                        os.write(self._master, response)
                    except OSError:
                        pass
            if offset:
                del buffer[:offset]
            if self.reads and self.reads % 500 == 0 and self.reads != getattr(self, '_logged_reads', -1):
                self._logged_reads = self.reads
                self.log('reads=%d writes=%d' % (self.reads, self.writes))

    def handle_read(self, frame):
        ladder = self.ladder
        addr = int(frame[2:6], 16)              # int() 直接吃 bytes，省一次 decode
        if addr >= 0x1000:                       # D 寄存器
            number = (addr - 0x1000) // 2
            value = ladder.word(number) & 0xFFFF
            self.log('read D%d = %d' % (number, value))
            # 帧体固定 4 位 hex：低字节在前（和 fx_plc_instance 的字节交换对齐）。
            return framing.framed("%02X%02X" % (value & 0xFF, (value >> 8) & 0xFF))
        if 0x0100 <= addr < 0x0200:              # M：16 位一组，fx 侧收到后交换字节
            base = ((addr - 0x0100) // 2) * 16
            data = framing.bitfield(ladder.M, base, 16)
            self.log('read M%d~M%d = %04X' % (base, base + 15, data))
            return framing.framed("%04X" % (((data & 0xFF) << 8) | ((data >> 8) & 0xFF)))
        if 0x0080 <= addr <= 0x0087:             # X 组
            value = framing.bitfield(ladder.X, (addr - 0x0080) * 10, 8)
            self.log('read Xgroup %d (X%d~) = %02X' % (addr - 0x0080, (addr - 0x0080) * 10, value))
            return framing.framed("%02X" % value)
        if 0x00A0 <= addr <= 0x00A9:             # Y 组
            value = framing.bitfield(ladder.Y, (addr - 0x00A0) * 10, 8)
            self.log('read Ygroup %d (Y%d~) = %02X' % (addr - 0x00A0, (addr - 0x00A0) * 10, value))
            return framing.framed("%02X" % value)
        if addr < 0x0080:                        # S 位
            value = framing.bitfield(ladder.S, addr * 8, 8)
            self.log('read Sgroup %d (S%d~) = %02X' % (addr, addr * 8, value))
            return framing.framed("%02X" % value)
        return None

    def handle_write(self, frame):
        """写帧真正落到梯形图的软元件上（不做"假确认"）。"""
        ladder = self.ladder
        operation = 'set' if frame[1] == 0x37 else 'reset'
        # 写帧地址低字节在前（见 fx_plc_instance.cpp forceM/forceX/forceY）。
        addr = int(frame[2:4], 16) | (int(frame[4:6], 16) << 8)
        value = 1 if operation == 'set' else 0

        if addr >= 0x0800:                       # M 位：0x0800 + M
            number = addr - 0x0800
            ladder.M[number] = value
            target = 'M%d' % number
        elif addr >= 0x0500:                     # Y 位：0x0500 + Y
            number = addr - 0x0500
            ladder.Y[number] = value
            target = 'Y%d' % number
        elif addr >= 0x0400:                     # X 位：0x0400 + (X/10)*8 + X%10
            linear = addr - 0x0400
            number = (linear // 8) * 10 + (linear % 8)
            ladder.X[number] = value
            target = 'X%d' % number
        else:
            self.log('write %s addr=%s (未识别的写地址)' % (operation, frame[2:6].decode()))
            return b"\x06"

        self.log('write %s %s' % (operation, target))
        return b"\x06"
