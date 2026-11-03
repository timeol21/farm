"""扫描循环 + 软元件目录 + 工序追踪。

三件事分开看：

1. **扫描**：每个周期先跑现场物理模型（输出 Y → 机构 → 反馈 X），再跑梯形图，
   最后压上人工强制值。顺序和真机一致：物理反馈先到，梯形图再读。
2. **软元件目录**：从梯形图指令表统计每个软元件被引用多少次（读/写分开），
   看板按这个频率排序 —— 常用的点排前面，改逻辑时一眼看到影响面。
3. **工序追踪**：按机型给的"当前步软元件"（海天是 D142）记录轨迹和停留时长，
   看板的流程图就靠它高亮。
"""

import threading
import time
from collections import OrderedDict

from .ladder import parse_operand

AREA_ORDER = {'X': 0, 'Y': 1, 'M': 2, 'S': 3, 'D': 4, 'T': 5, 'C': 6, 'Z': 7}
BIT_AREAS = ('X', 'Y', 'M', 'S', 'T')
WORD_AREAS = ('D', 'C')

# 指令 -> 哪个操作数是"写"。没列出的指令一律按"读"算。
WRITE_INDEX = {
    'OUT': (0,), 'SET': (0,), 'RST': (0,), 'ZRST': (0, 1), 'ZRSTP': (0, 1),
    'MOV': (1,), 'MOVP': (1,), 'DMOV': (1,), 'DMOVP': (1,),
    'ADD': (2,), 'DADD': (2,), 'DADDP': (2,), 'SUB': (2,), 'DSUB': (2,), 'DSUBP': (2,),
    'MUL': (2,), 'DMUL': (2,), 'DIV': (2,), 'DDIV': (2,), 'DDIVP': (2,),
    'INCP': (0,), 'DECP': (0,), 'DINCP': (0,),
    'BMOV': (1,), 'SUM': (1,), 'SPD': (1,),
}


class DeviceCatalog:
    """软元件清单：谁被引用过、引用了多少次、是读还是写、有没有人工注解。"""

    def __init__(self, ladder, notes=None):
        self.notes = dict(notes or {})
        self.by_name = {}
        self.entries = []
        self._collect(ladder)
        self.entries.sort(key=lambda item: (-item['refs'], AREA_ORDER.get(item['area'], 9),
                                            item['number']))

    # ---------------------------------------------------------------- 采集
    def _bump(self, token, is_write):
        operand = parse_operand(token)
        if operand.kind not in ('dev', 'idx', 'block'):
            return
        area = operand.area
        if area not in AREA_ORDER:
            return
        name = '%s%d' % (area, operand.number)
        entry = self.by_name.get(name)
        if entry is None:
            entry = {'name': name, 'area': area, 'number': operand.number,
                     'refs': 0, 'reads': 0, 'writes': 0,
                     'kind': 'bit' if area in BIT_AREAS else 'word',
                     'note': self.notes.get(name, '')}
            self.by_name[name] = entry
            self.entries.append(entry)
        entry['refs'] += 1
        if is_write:
            entry['writes'] += 1
        else:
            entry['reads'] += 1

    def _collect(self, ladder):
        blocks = [ladder.main]
        blocks.extend(ladder.subs.values())
        blocks.extend(ladder.others)
        for block in blocks:
            for op, args in block:
                write_index = WRITE_INDEX.get(op, ())
                for index, token in enumerate(args):
                    if not token:
                        continue
                    self._bump(token, index in write_index)

    # ---------------------------------------------------------------- 查询
    def summary(self):
        areas = OrderedDict((area, {'count': 0, 'refs': 0}) for area in AREA_ORDER)
        for entry in self.entries:
            bucket = areas.setdefault(entry['area'], {'count': 0, 'refs': 0})
            bucket['count'] += 1
            bucket['refs'] += entry['refs']
        return {'total': len(self.entries), 'areas': areas}

    def dump(self):
        return self.entries


class PlcRuntime:
    """虚拟 PLC 的运行时：扫描线程 + 状态快照。"""

    def __init__(self, simulator, scan_ms=50, time_scale=1.0):
        self.simulator = simulator
        self.ladder = simulator.build()
        self.plant_state = simulator.new_state()
        self.time_scale = float(time_scale)
        self.scan_ms = int(scan_ms)
        self.ladder.time_scale = self.time_scale
        self.catalog = DeviceCatalog(self.ladder, simulator.device_notes())
        self.lock = threading.RLock()
        self.overrides = OrderedDict()
        self.paused = False
        self.running = False
        self.scans = 0
        self.started_at = time.time()
        self.step_value = None
        self.step_entered_at = None
        self.trail = []
        self.fault = {}
        self.last_scan_ms = 0.0
        self.scan_rate = 0.0
        self._rate_time = time.time()
        self._rate_scans = 0
        self._thread = None
        self._wake = threading.Event()

    # ---------------------------------------------------------------- 生命周期
    def start(self):
        self.running = True
        self._thread = threading.Thread(target=self._loop, name='plc-scan', daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self.running = False
        self._wake.set()

    # ---------------------------------------------------------------- 扫描
    def scan_once(self):
        with self.lock:
            started = time.perf_counter()
            self.simulator.plant(self.ladder, self.plant_state)
            # 强制值必须在 scan 之前压上去：现场模型每个扫描都会重写 X（车到位/限位/刷子行程），
            # 如果放在 scan 之后，强制 X 点会被下一轮 plant() 覆盖，梯形图永远读不到 —— 等于强制无效。
            self._apply_overrides()
            self.ladder.scan(self.scan_ms / 1000.0)
            self.scans += 1
            self.last_scan_ms = (time.perf_counter() - started) * 1000.0
            self._track_step()
            now = time.time()
            if now - self._rate_time >= 0.5:
                self.scan_rate = (self.scans - self._rate_scans) / (now - self._rate_time)
                self._rate_time = now
                self._rate_scans = self.scans

    def _loop(self):
        next_at = time.perf_counter()
        while self.running:
            period = self.scan_ms / 1000.0
            if not self.paused:
                try:
                    self.scan_once()
                except Exception as exc:            # 单轮出错不能拖垮整个模拟器
                    self.fault = {'code': -1, 'text': '扫描异常: %s' % exc}
            next_at += period
            now = time.perf_counter()
            gap = next_at - now
            if gap > 0:
                self._wake.wait(min(gap, 0.02))
                self._wake.clear()
            else:
                next_at = now                      # 追不上就丢掉欠账，不补跑

    # ---------------------------------------------------------------- 调速 / 暂停
    def set_speed(self, time_scale):
        """倍速：虚拟步长恒为 50ms，只压缩墙钟周期（和 run_local_native.sh 一个口径）。"""
        time_scale = max(0.1, float(time_scale))
        with self.lock:
            self.time_scale = time_scale
            self.scan_ms = max(1, int(round(50.0 / time_scale)))
            self.ladder.time_scale = time_scale
        self._wake.set()
        return {'timeScale': self.time_scale, 'scanMs': self.scan_ms}

    def pause(self):
        self.paused = True
        return self.status()

    def resume(self):
        self.paused = False
        self._wake.set()
        return self.status()

    def step_once(self):
        if not self.paused:
            self.pause()
        self.scan_once()

    # ---------------------------------------------------------------- 命令（与盒子实现对齐）
    # 对齐依据（src/business_layer/washcar/HaiTianWashcar_service.cpp）：
    #   start  ：只写 X45=true（不自动复位、不释放）
    #   stop   ：只写 M0=true
    #   reset  ：先写 M0=false（clearStop），sleep(1)，再写 M100=true；
    #            M100 由梯形图自己清（进入 S0 时 CALL P0 → ZRST M1 M131）
    # 启动的前置条件不并进命令，改为前置检查面板上的独立动作（见 apply_precondition）。
    def demo_start(self):
        """返回是否真的下发了（机型没定义点位时返回 False）。"""
        services = getattr(self.simulator, 'services', None) or {}
        if not services.get('start'):
            return False
        self._press(services['start'])
        return True

    def demo_stop(self):
        services = getattr(self.simulator, 'services', None) or {}
        if not services.get('stop'):
            return False
        self._press(services['stop'])
        return True

    def demo_reset(self):
        services = getattr(self.simulator, 'services', None) or {}
        if not services.get('reset'):
            return False
        threading.Thread(target=self._reset_sequence, args=(services,),
                         name='demo-reset', daemon=True).start()
        return True

    def _press(self, name):
        """单次写一个位（等价盒子的 writePoint：写一次就留在 PLC 里）。"""
        with self.lock:
            self._write(*self._parse_name(name), 1)

    def _reset_sequence(self, services):
        """复位两步：先清停止标志，隔 1 秒再写复位位 —— 和 resetWashCar 一致。"""
        clear = services.get('clearStop')
        reset = services.get('reset')
        if clear:
            with self.lock:
                self._write(*self._parse_name(clear), 0)
        time.sleep(1.0)                      # 项目里就是 sleep(1)
        if reset:
            with self.lock:
                self._write(*self._parse_name(reset), 1)
        self.reset_trail()

    # ---------------------------------------------------------------- 前置检查动作
    # 动作的业务含义由机型解释（simulator.apply_precondition），运行时只负责转发。
    # 机型实现必须只改现场输入，不许写命令位。
    def apply_precondition(self, action):
        if not action:
            return False
        return bool(self.simulator.apply_precondition(action, self))

    def preconditions(self):
        """每项前置检查的实时状态（判据与盒子 startChecks 一致：信号 == 1）。"""
        items = []
        for item in getattr(self.simulator, 'preconditions', ()):
            signal = item.get('signal', '')
            try:
                value = self.read(signal) if signal else None
            except (ValueError, IndexError):
                value = None
            items.append({
                'key': item.get('key', ''),
                'title': item.get('title', ''),
                'signal': signal,
                'value': value,
                'passed': value == 1,
                'action': item.get('action', ''),
                'button': item.get('button', ''),
                'hint': item.get('hint', ''),
            })
        return items

    # ---------------------------------------------------------------- 工序
    def _track_step(self):
        value = self.simulator.current_step(self.ladder)
        self.fault = self.simulator.alarms(self.ladder)
        if value == self.step_value:
            return
        now = time.time()
        if self.step_value is not None and self.step_entered_at is not None:
            self.trail.append({
                'value': self.step_value,
                'title': self.simulator.step_title(self.step_value),
                'enteredAt': self.step_entered_at,
                'dwell': round(now - self.step_entered_at, 2),
            })
            if len(self.trail) > 400:
                del self.trail[:100]
        self.step_value = value
        self.step_entered_at = now

    def reset_trail(self):
        with self.lock:
            self.trail = []
            self.step_entered_at = time.time()

    # ---------------------------------------------------------------- 强制写入
    def set_override(self, name, value):
        area, number = self._parse_name(name)
        with self.lock:
            self._write(area, number, int(value))
            self.overrides[name.upper()] = int(value)
        return self.overrides_list()

    def clear_overrides(self, name=None):
        with self.lock:
            if name:
                self.overrides.pop(name.upper(), None)
            else:
                self.overrides.clear()
        return self.overrides_list()

    def _apply_overrides(self):
        for name, value in self.overrides.items():
            area, number = self._parse_name(name)
            self._write(area, number, value)

    def overrides_list(self):
        with self.lock:
            return [{'name': name, 'value': value} for name, value in self.overrides.items()]

    @staticmethod
    def _parse_name(name):
        token = str(name).strip().upper()
        if len(token) < 2 or token[0] not in AREA_ORDER or not token[1:].isdigit():
            raise ValueError('软元件名不合法: %s' % name)
        return token[0], int(token[1:])

    def _write(self, area, number, value):
        ladder = self.ladder
        if area == 'X':
            ladder.X[number] = 1 if value else 0
        elif area == 'Y':
            ladder.Y[number] = 1 if value else 0
        elif area == 'M':
            ladder.M[number] = 1 if value else 0
        elif area == 'S':
            ladder.S[number] = 1 if value else 0
        elif area == 'D':
            ladder.D[number] = int(value) & 0xFFFF
        elif area == 'C':
            ladder.set_c_dword(number, int(value))
        elif area == 'T':
            entry = ladder.T.get(number)
            if entry:
                entry[2] = 1 if value else 0
        elif area == 'Z':
            ladder.Z[number] = int(value)

    # ---------------------------------------------------------------- 读数
    def read(self, name):
        area = name[0]
        number = int(name[1:])
        ladder = self.ladder
        # X/Y/M/S 走 ladder.bit()：M8000/M8011 这类特殊继电器不在 M 字典里，
        # 直接查字典会永远读到 0（看板上会变成"常 ON 继电器显示 0"的假象）。
        if area in ('X', 'Y', 'M', 'S'):
            return ladder.bit(area, number)
        if area == 'D':
            return ladder.word(number)
        if area == 'T':
            entry = ladder.T.get(number)
            return 1 if entry and entry[2] else 0
        if area == 'C':
            return ladder.c_dword(number)
        if area == 'Z':
            return ladder.Z.get(number, 0)
        return 0

    def values(self, names=None):
        """按名字取值。names=None 时返回目录里全部软元件（几百 KB，别在轮询里用）。"""
        with self.lock:
            if names is None:
                names = [entry['name'] for entry in self.catalog.entries]
            result = {}
            for name in names:
                try:
                    result[name] = self.read(name)
                except (ValueError, IndexError):
                    continue
            return result

    # ---------------------------------------------------------------- 状态
    def status(self):
        simulator = self.simulator
        with self.lock:
            now = time.time()
            step = simulator.flow_step(self.step_value)
            trail = list(self.trail)
        return {
            'sim': {
                'key': simulator.key,
                'title': simulator.title,
                'ladder': simulator.ladder_name,
                'note': simulator.note,
            },
            'scan': {
                'scans': self.scans,
                'scanMs': self.scan_ms,
                'timeScale': self.time_scale,
                'paused': self.paused,
                'rate': round(self.scan_rate, 1),
                'lastScanMs': round(self.last_scan_ms, 3),
                'uptime': round(now - self.started_at, 1),
            },
            'step': {
                'value': self.step_value,
                'index': step['index'],
                'title': step['title'],
                'motion': step['motion'],
                'sStep': step['sStep'],
                'detail': step['detail'],
                'enteredAt': self.step_entered_at,
                'dwell': round(now - self.step_entered_at, 2) if self.step_entered_at else 0,
                'trail': trail[-64:],
            },
            'flow': simulator.flow(),
            'fault': self.fault,
            'overrides': self.overrides_list(),
            'preconditions': self.preconditions(),
            'devices': self.catalog.summary(),
        }
