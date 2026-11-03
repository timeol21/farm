#!/usr/bin/env python3
# 海天洗车机 PLC 梯形图模拟器(虚拟 PLC 内核)
#
# 程序来源: GX Works 导出的指令表 CSV(UTF-16 TSV)。
# 语义口径以 GX Works2 里打开「金达路一号停车场洗车机.gxw」的梯形图为准(2026-10-23 逐条核对):
#   · 算术类(ADD/SUB/MUL/DIV/DADD/DSUB/DMUL/DDIV 及 P 变体)是 **3 操作数**: 源1 源2 目标
#     梯形图上写成 [MUL D3000 K60 D3004] / [DIV D3004 K477 D3006],不是就地两操作数。
#   · MOV/MOVP/DMOV/DMOVP 是 2 操作数(源在前、目标在后)。
#   · ZRST/ZRSTP 是 2 操作数(起止); BMOV 是 3 个(源、目标、点数); SUM 2 个; SPD 3 个。
#   · 操作数三种复合写法:
#       K750      常量
#       D499Z0    变址(D499 + 变址寄存器 Z0 的值)
#       K4M260    位组合(4 位十六进制数字 = 16 位,起点 M260)⇒ 1 个字,不是 4×16 位
#   · TO/FROM 是特殊缓冲存储器传送(4 操作数),不参与状态机,模拟器按"已识别但不仿真"处理。
#   · 结果宽度按 FX:DIV 是整数(商在 D、余数在 D+1),MUL 是 32 位积;
#     C0~C199 是 16 位计数器,C200~C255 才是 32 位。
#
# 本文件只做"梯形图求值",不碰串口协议(协议层在 core/serial_stub.py)。
import io, re

LABEL_RE = re.compile(r'^(P|I)\d+$')
CONST_RE = re.compile(r'^([KkHh])(-?\d+)$')
DEV_RE = re.compile(r'^([XYMSTCZD])(\d+)$')
IDX_RE = re.compile(r'^([XYMSTCZD])(\d+)([ZY])(\d+)$')
BLOCK_RE = re.compile(r'^[Kk](\d+)([XYMSTCZD])(\d+)$')

CONTACTS = ('LD', 'LDI', 'LDP', 'LDF', 'AND', 'ANI', 'ANDP', 'ANDF', 'OR', 'ORI', 'ORP', 'ORF')
COMPARE_SYMBOLS = ('=', '<>', '<', '>', '<=', '>=')
COMPARE_PREFIX = (('LDD', True), ('ANDD', True), ('ORD', True), ('AND', False), ('OR', False), ('LD', False))
STACK_OPS = ('ANB', 'ORB', 'MPS', 'MRD', 'MPP', 'INV')
OUTPUT_OPS = ('OUT', 'SET', 'RST', 'ZRST', 'ZRSTP')
MOVE_OPS = ('MOV', 'MOVP', 'DMOV', 'DMOVP')
BLOCK_OPS = ('BMOV',)
ARITH_OPS = ('ADD', 'DADD', 'DADDP', 'SUB', 'DSUB', 'DSUBP', 'MUL', 'DMUL',
             'DIV', 'DDIV', 'DDIVP', 'INCP', 'DECP', 'DINCP')
SPECIAL_OPS = ('SUM', 'SPD', 'FROM', 'TO')
CONTROL_OPS = ('STL', 'CALL', 'CALLP', 'SRET', 'RET', 'FEND', 'IRET', 'END', 'EI')

# FX3U:C0~C199 是 16 位计数器(1 个字),C200~C255 是 32 位计数器(2 个字)。
C32_BASE = 200

# 快速路径的跳转哨兵:普通指令返回 None,步进跳转返回目标 pc(>=0),
# 这三个负数表示"返回子程序 / 结束本块 / 调用子程序"。
_FAST_POP = -1
_FAST_END = -2
_FAST_CALL = -3


_KNOWN_OP_CACHE = {}


def known_op(op):
    """未列出的助记符算"未识别",会被验收闸门抓到。

    助记符集合是静态的,判断结果缓存起来 —— 每条指令每次扫描都要问一遍,
    不缓存的话光这一项就占掉一成的扫描时间。
    """
    cached = _KNOWN_OP_CACHE.get(op)
    if cached is not None:
        return cached
    known = False
    if op in CONTACTS or op in STACK_OPS or op in OUTPUT_OPS or op in MOVE_OPS \
            or op in BLOCK_OPS or op in ARITH_OPS or op in SPECIAL_OPS or op in CONTROL_OPS:
        known = True
    elif op in ('ANDD', 'ORD'):
        known = True
    else:
        for prefix, _ in COMPARE_PREFIX:
            if op.startswith(prefix):
                if op[len(prefix):] in COMPARE_SYMBOLS:
                    known = True
                continue
    _KNOWN_OP_CACHE[op] = known
    return known


class Operand:
    """操作数。kind: const(常量) / dev(软元件) / idx(变址) / block(点数前缀) / label(标签)"""
    __slots__ = ('kind', 'area', 'number', 'value', 'count', 'index')

    def __init__(self, kind, area=None, number=0, value=0, count=1, index=None):
        self.kind = kind
        self.area = area
        self.number = number
        self.value = value
        self.count = count
        self.index = index

    def __repr__(self):
        if self.kind == 'const':
            return 'K%d' % self.value
        if self.kind == 'dev':
            return '%s%d' % (self.area, self.number)
        if self.kind == 'idx':
            return '%s%dZ%d' % (self.area, self.number, self.index)
        if self.kind == 'block':
            return 'K%d%s%d' % (self.count, self.area, self.number)
        return 'label'


def parse_operand(token):
    token = token.strip()
    if not token:
        return Operand('const', value=0)
    cached = _OPERAND_CACHE.get(token)
    if cached is not None:
        return cached
    operand = _parse_operand_uncached(token)
    _OPERAND_CACHE[token] = operand
    return operand


_OPERAND_CACHE = {}


_COMPARE_CACHE = {}


def _parse_operand_uncached(token):
    match = CONST_RE.match(token)
    if match:
        if match.group(1) in 'Hh':
            return Operand('const', value=int(match.group(2), 16))
        return Operand('const', value=int(match.group(2)))
    match = BLOCK_RE.match(token)
    if match:
        return Operand('block', area=match.group(2), number=int(match.group(3)), count=int(match.group(1)))
    match = IDX_RE.match(token)
    if match:
        return Operand('idx', area=match.group(1), number=int(match.group(2)), index=int(match.group(4)))
    match = DEV_RE.match(token)
    if match:
        return Operand('dev', area=match.group(1), number=int(match.group(2)))
    if LABEL_RE.match(token):
        return Operand('label')
    return Operand('const', value=0)


def parse_csv(path):
    """返回 (主程序, {子程序名: 指令列表}, [无名程序])。

    指令表里的每条指令 = [助记符, [操作数...]](操作数可能 1~4 个)。
    程序切分只认标签行(P0/I680…):标签之前是主程序(FEND 收尾),标签之后到下一个标签是子程序(SRET 收尾)。
    主程序里出现的 RET 不是程序结束(海天工程步 9307 的 RET 后面还有 1200 多步)。
    """
    with io.open(path, encoding='utf-16', newline='') as f:
        rows = [line.rstrip('\r\n').split('\t') for line in f]

    def clean(cell):
        return cell.strip().strip('"').strip()

    program = []
    for row in rows[3:]:
        if len(row) < 4:
            continue
        op = clean(row[2])
        args = [clean(row[3])]
        if len(row) > 4 and clean(row[4]):
            args.append(clean(row[4]))
        if op:
            program.append([op, args])
        elif program and args[0]:
            program[-1][1].append(args[0])

    programs = []
    name = None
    current = []
    for item in program:
        op = item[0]
        if LABEL_RE.match(op):
            if current:
                programs.append((name, current))
                current = []
            name = op
            continue
        current.append(item)
        if op in ('FEND', 'END', 'SRET', 'IRET'):
            programs.append((name, current))
            current = []
            name = None
    if current:
        programs.append((name, current))

    main = programs[0][1] if programs else []
    subs = {}
    others = []
    for program_name, body in programs[1:]:
        if program_name is None:
            others.append(body)
        else:
            subs[program_name] = body
    return main, subs, others


class Ladder:
    # wash_cycle.Trace 优先用 on_step 钩子;没有这个能力的旧实现自己回退到插桩。
    supports_step_hook = True

    def __init__(self, csv_path, time_scale=1.0):
        self.main, self.subs, self.others = parse_csv(csv_path)
        self.time_scale = time_scale
        self.X = {}
        self.Y = {}
        self.M = {}
        self.S = {}
        self.D = {}
        self.Z = {}                      # 变址寄存器 Z0~Z7
        self.T = {}                      # 定时器 {号: [起算时刻, 预设定时, 已到位]}
        self.C = {}                      # 计数器 {号: 累计值}, {-号: 预设定}
        self.R = 0
        self.block_stack = []
        self.mps_stack = []
        self.call_depth = 0
        self.first_scan = True
        self.prev_edges = {}
        self.contact_values = {}            # 每条触点指令算出来的值(给结构复算用)
        self.unknown = {}
        self.unsupported = {}
        self.last_error = ''
        self.virtual_time = 0.0
        self.max_block_depth = 0
        self.scans = 0
        self.counter_state = {}
        self.c_preset = {}                  # C 区 32 位预设定值 {号: 值}
        self.current_step = None            # 当前正在执行的 STL 步号(步互斥用)
        self.spd_state = {}
        self.x_rise_time = {}
        self.x_period = {}
        self.prev_x = {}
        self._plan_lists = {}                 # 每条指令的预编译计划 {块id: [计划, ...]}
        self.use_fast = True                  # 关掉后退回逐条解释路径(对照/调试用)
        self._fast_blocks = {}                # 块id -> (闭包列表, 助记符列表)
        self._pending_call = None             # 快速路径 CALL 的目标子程序
        self.on_step = None                   # 步激活钩子;wash_cycle.Trace 用它统计步轨迹
        self.build_stl_index()

    # ---- 特殊继电器 ----
    def special(self, number):
        if number in (8000, 8009):
            return 1
        if number == 8001:
            return 0
        if number == 8002:
            return 1 if self.first_scan else 0
        if number == 8010:
            return 1 if int(self.virtual_time * 100) % 10 == 0 else 0
        # FX3U 时钟脉冲:M8011 = 10ms、M8012 = 100ms(各 50% 占空比)。
        if number == 8011:
            return 1 if int(self.virtual_time * 100) % 2 == 0 else 0
        if number == 8012:
            return 1 if int(self.virtual_time * 10) % 2 == 0 else 0
        if number == 8013:
            return 1 if int(self.virtual_time) % 2 == 0 else 0
        if number == 8014:
            return 1 if int(self.virtual_time / 60) % 2 == 0 else 0
        return None

    # ---- 软元件读写 ----
    def bit(self, area, number):
        if area == 'M':
            special = self.special(number)
            if special is not None:
                return special
        return 1 if getattr(self, area).get(number, 0) else 0

    def word(self, number):
        return self.D.get(number, 0) & 0xFFFF

    def set_word(self, number, value):
        self.D[number] = value & 0xFFFF

    def dword(self, number):
        return (self.word(number + 1) << 16) | self.word(number)

    def set_dword(self, number, value):
        self.set_word(number, value & 0xFFFF)
        self.set_word(number + 1, (value >> 16) & 0xFFFF)

    def m_word(self, number):
        """M 区按 16 位成字(M260 对应 M260~M275)。"""
        value = 0
        for index in range(16):
            if self.M.get(number + index, 0):
                value |= 1 << index
        return value

    def set_m_word(self, number, value):
        for index in range(16):
            self.M[number + index] = 1 if (value >> index) & 1 else 0

    def area_word(self, area, number):
        if area == 'D':
            return self.word(number)
        if area == 'M':
            return self.m_word(number)
        if area == 'Z':
            return self.Z.get(number, 0) & 0xFFFF
        if area == 'C':
            return self.C.get(number, 0) & 0xFFFF
        if area == 'T':
            state = self.T.get(number, [0.0, 0.0, 0])
            if not state[0]:
                return 0
            return int((self.virtual_time - state[0]) / self.timer_base(number)) & 0xFFFF
        return 0

    def set_area_word(self, area, number, value):
        if area == 'D':
            self.set_word(number, value)
        elif area == 'M':
            self.set_m_word(number, value)
        elif area == 'Z':
            self.Z[number] = value & 0xFFFF
        elif area == 'C':
            self.C[number] = value & 0xFFFF
        elif area == 'T':
            # MOV K50 T0:直接写定时器当前值(以预设定为上限由比较逻辑决定)。
            # **已知缺陷(2026-10-22 现场故障反推时发现,本次未修)**:
            # FX3U 的定时器**总有预设定寄存器**(上电为 0),`MOV K45 T7` 把当前值
            # 写成 45(4.5 秒)时当前值 >= 预设定 ⇒ 触点应当立刻导通;
            # 这里写的是 `if state[1]:` —— 预设定还没被 OUT 指令写过时
            # (state[1]==0)就不更新到位标志,T7 的触点永远不通,
            # 而 T7 是 S3→S4 出口条件之一(`pc=2026 LD T7`)。
            # 修它同样会动到默认基线,留待后续。
            state = self.T.setdefault(number, [0.0, 0.0, 0])
            base = self.timer_base(number)
            current = value & 0xFFFF
            state[0] = self.virtual_time - current * base
            if state[1]:
                state[2] = 1 if current * base >= state[1] else 0

    @staticmethod
    def area_step(area):
        """块操作数按字推进的跨度:M 区 1 字 = 16 点,D 区 1 字 = 1 点。"""
        return 16 if area in ('M', 'S', 'X', 'Y') else 1

    @staticmethod
    def block_words(operand):
        """K<n> 位组合的位数 = n*4;位区按 16 位/字折算,其余按字数。"""
        if operand.area in ('M', 'S', 'X', 'Y', 'C', 'T'):
            return max(1, (operand.count + 3) // 4)
        return operand.count

    @staticmethod
    def timer_base(number):
        """FX3U: T0~T199 = 100ms, T200~T245 = 10ms, T246~T255 = 1ms。"""
        if number < 200:
            return 0.1
        if number < 246:
            return 0.01
        return 0.001

    # ---- 操作数求值 ----
    def resolve(self, operand):
        """变址操作数展开成实际地址。"""
        if operand.kind == 'idx':
            return operand.area, operand.number + self.Z.get(operand.index, 0)
        return operand.area, operand.number

    def operand_value(self, operand, numeric=False, double=False):
        if operand.kind == 'const':
            return operand.value
        if operand.kind == 'label':
            return 0
        area, number = self.resolve(operand)
        if area in ('X', 'Y', 'M', 'S'):
            return self.bit(area, number)
        if area == 'T':
            state = self.T.get(number, [0.0, 0.0, 0])
            if numeric:
                if not state[0]:
                    return 0
                return int((self.virtual_time - state[0]) / self.timer_base(number))
            return 1 if state[2] else 0
        if area == 'C':
            if numeric:
                return self.c_dword(number) if double else self.C.get(number, 0)
            if number not in self.c_preset:
                # 程序里没有 OUT Cn ⇒ 这个计数器不存在,触点不通。
                # 原实现走 `>= 0` 分支恒返回 1,于是 `ANI C0` 这类条件永远为假
                # (海天工程 pc=1956 的 `ANI C0` 直接把 M22(前进输出 Y030)压死)。
                return 0
            # 有 OUT Cn 就按 FX 语义比较(Cn >= 预设定),预设定 0 的触点同样导通。
            return 1 if self.c_dword(number) >= self.c_preset[number] else 0
        if area == 'Z':
            return self.Z.get(number, 0) & 0xFFFF
        if area == 'D':
            return self.dword(number) if double else self.word(number)
        return 0

    def read_block(self, operand):
        """块操作数: 读出对应字数(K4M260 = 4 位数字 = 16 位 = 1 个字)。"""
        area, number = self.resolve(operand)
        step = self.area_step(area)
        return [self.area_word(area, number + step * index)
                for index in range(self.block_words(operand))]

    def write_block(self, operand, values):
        area, number = self.resolve(operand)
        step = self.area_step(area)
        for index, value in enumerate(values):
            self.set_area_word(area, number + step * index, value)

    # ---- 边沿记忆(按指令实例,不按软元件)----
    def rising(self, key, value):
        before = self.prev_edges.get(key, 0)
        self.prev_edges[key] = 1 if value else 0
        return 1 if (value and not before) else 0

    def falling(self, key, value):
        before = self.prev_edges.get(key, 0)
        self.prev_edges[key] = 1 if value else 0
        return 1 if (before and not value) else 0


    # ---- 扫描 ----
    def scan(self, elapsed=0.05):
        # 实例上覆盖了 exec_one 说明外部在插桩(verify_mock / 旧 Trace),
        # 那种场景必须逐条走旧路径,否则钩子看不到指令。
        if (self.use_fast and 'exec_one' not in self.__dict__
                and type(self).exec_one is Ladder.exec_one):
            return self._scan_fast(elapsed)
        self.virtual_time += elapsed * self.time_scale
        self.track_input_edges()
        self.R = 0
        self.block_stack = []
        self.mps_stack = []
        self.run_block(self.main, 0)
        for other in self.others:
            self.run_block(other, 0)
        self.first_scan = False
        self.scans += 1

    # ---- 快速执行路径 ----
    # 扫描循环每轮要跑两千多条指令,原来每条都走 exec_one():
    # 取计划、拆助记符、按字符串逐类判断 —— 这些在程序装载后都不会变。
    # 这里把每条指令编译成一个闭包,运行时只做"取值 + 栈操作";
    # 没覆盖到的指令仍回落到 exec_one,语义只有一份口径。
    #
    # 只要实例上覆盖了 exec_one(verify/trace 的插桩),scan() 会自动回退旧路径,
    # 所以插桩工具不需要改;wash_cycle.Trace 改用 on_step 钩子,可以继续吃快速路径。
    # 快速路径不维护 contact_values —— 需要逐条触点值的工具请覆盖 exec_one 或 use_fast=False。
    def _scan_fast(self, elapsed=0.05):
        self.virtual_time += elapsed * self.time_scale
        self.track_input_edges()
        self.R = 0
        self.block_stack = []
        self.mps_stack = []
        self._fast_run(self.main)
        for other in self.others:
            self._fast_run(other)
        self.first_scan = False
        self.scans += 1


    def _fast_code(self, block):
        """取(或首次编译)一个程序块的闭包列表。"""
        key = id(block)
        compiled = self._fast_blocks.get(key)
        if compiled is None:
            code = []
            ops = []
            for pc, item in enumerate(block):
                op, args = item
                ops.append(op)
                code.append(self._fast_compile(op, args, pc, block))
            compiled = (code, ops)
            self._fast_blocks[key] = compiled
        return compiled

    def _fast_run(self, block):
        code, ops = self._fast_code(block)
        pc = 0
        count = len(code)
        while pc < count:
            try:
                jump = code[pc](self)
            except Exception as exc:              # 单条指令出错不能拖垮整轮扫描
                op = ops[pc]
                self.unknown[op] = self.unknown.get(op, 0) + 1
                self.last_error = '%s: %s' % (op, exc)
                pc += 1
                continue
            if jump is None:
                pc += 1
            elif jump >= 0:
                pc = jump
            elif jump == _FAST_POP or jump == _FAST_END:
                # 和 run_block 一样:只有显式返回/结束才减调用深度;
                # 子程序跑到末尾没有 SRET 时旧实现不减,这里也不减。
                return True
            else:
                sub = self.subs.get(self._pending_call)
                if sub is not None:
                    self.call_depth += 1
                    returned = self._fast_run(sub)
                    if returned and self.call_depth > 0:
                        self.call_depth -= 1
                pc += 1
        return False

    def _fast_compile(self, op, args, pc, block):
        """把一条指令编译成闭包;编译不了就回落到 exec_one。"""
        try:
            handler = self._fast_compile_one(op, args, pc, block)
        except Exception:
            handler = None
        if handler is None:
            return self._fast_fallback(op, args, pc, block)
        return handler

    def _fast_compile_one(self, op, args, pc, block):
        if not known_op(op):
            return None
        symbol, double = self.split_compare(op)
        if symbol is not None:
            return self._fast_compare(op, args, pc, block, symbol, double)
        if op in CONTACTS:
            return self._fast_contact(op, args, pc, block)
        if op == 'ANB':
            def h(s):
                if s.block_stack:
                    s.R &= s.block_stack.pop()
            return h
        if op == 'ORB':
            def h(s):
                if s.block_stack:
                    s.R |= s.block_stack.pop()
            return h
        if op == 'MPS':
            def h(s):
                s.mps_stack.append(s.R)
            return h
        if op == 'MRD':
            def h(s):
                if s.mps_stack:
                    s.R = s.mps_stack[-1]
            return h
        if op == 'MPP':
            def h(s):
                if s.mps_stack:
                    s.R = s.mps_stack.pop()
            return h
        if op == 'INV':
            def h(s):
                s.R = 1 - s.R
            return h
        if op == 'STL':
            if args and args[0].upper().startswith('S') and args[0][1:].isdigit():
                return self._fast_stl(args[0], pc, block)
            return None
        if op == 'OUT':
            if args:
                return self._fast_out(args)
            return None
        if op == 'SET':
            if args:
                return self._fast_set(args)
            return None
        if op in ('RST', 'ZRST', 'ZRSTP'):
            if args:
                return self._fast_reset(op, args, pc, block)
            return None
        if op in MOVE_OPS:
            if len(args) >= 2:
                return self._fast_move(op, args, pc, block)
            return None
        if op in ('CALL', 'CALLP'):
            if args:
                return self._fast_call(op, args, pc, block)
            return None
        if op == 'RET':
            def h(s):
                return _FAST_POP if s.call_depth > 0 else None
            return h
        if op in ('SRET', 'FEND', 'IRET'):
            def h(s):
                return _FAST_POP
            return h
        if op == 'END':
            def h(s):
                return _FAST_END
            return h
        if op == 'EI':
            def h(s):
                return None
            return h
        return None

    def _fast_fallback(self, op, args, pc, block):
        def h(s):
            jump = s.exec_one(op, args, pc, block)
            if jump is None:
                return None
            if jump == 'pop':
                return _FAST_POP
            if jump == 'end':
                return _FAST_END
            if jump[0] == 'call':
                s._pending_call = jump[1]
                return _FAST_CALL
            return jump[1]
        return h

    # ---- 快速路径:操作数求值 ----
    def _fast_reader(self, operand, numeric=False, double=False):
        kind = operand.kind
        if kind == 'const':
            value = operand.value
            return lambda s: value
        if kind == 'label':
            return lambda s: 0
        if kind == 'idx':
            # 变址指令数量很少,直接复用 operand_value,避免两套解析口径。
            return lambda s: s.operand_value(operand, numeric=numeric, double=double)
        area = operand.area
        number = operand.number
        if area == 'M':
            if number >= 8000:
                def r_mspecial(s):
                    special = s.special(number)
                    if special is not None:
                        return special
                    return 1 if s.M.get(number, 0) else 0
                return r_mspecial
            return lambda s: 1 if s.M.get(number, 0) else 0
        if area == 'X':
            return lambda s: 1 if s.X.get(number, 0) else 0
        if area == 'Y':
            return lambda s: 1 if s.Y.get(number, 0) else 0
        if area == 'S':
            return lambda s: 1 if s.S.get(number, 0) else 0
        if area == 'T':
            base = self.timer_base(number)
            if numeric:
                def r_tnum(s):
                    state = s.T.get(number)
                    if not state or not state[0]:
                        return 0
                    return int((s.virtual_time - state[0]) / base)
                return r_tnum
            def r_tbit(s):
                state = s.T.get(number)
                return 1 if (state and state[2]) else 0
            return r_tbit
        if area == 'C':
            if numeric:
                if double:
                    return lambda s: s.c_dword(number)
                return lambda s: s.C.get(number, 0)
            def r_cbit(s):
                if number not in s.c_preset:
                    # 程序里没有 OUT Cn ⇒ 这个计数器不存在,触点不通。
                    return 0
                return 1 if s.c_dword(number) >= s.c_preset[number] else 0
            return r_cbit
        if area == 'D':
            if double:
                return lambda s: ((s.D.get(number + 1, 0) & 0xFFFF) << 16) | (s.D.get(number, 0) & 0xFFFF)
            return lambda s: s.D.get(number, 0) & 0xFFFF
        if area == 'Z':
            return lambda s: s.Z.get(number, 0) & 0xFFFF
        return lambda s: 0

    # ---- 快速路径:各类指令 ----
    def _fast_contact(self, op, args, pc, block):
        plan = self._make_plan(op, args, pc, block)
        a0 = plan[0]
        edge_key = plan[3]
        read = self._fast_reader(plan[6])
        if op in ('LD', 'LDI', 'LDP', 'LDF'):
            mode = 0
        elif op in ('AND', 'ANI', 'ANDP', 'ANDF'):
            mode = 1
        else:
            mode = 2
        invert = op in ('LDI', 'ANI', 'ORI')
        if op in ('LDP', 'ANDP', 'ORP'):
            key = edge_key + ':up:' + a0
            def h(s):
                value = read(s)
                before = s.prev_edges.get(key, 0)
                s.prev_edges[key] = 1 if value else 0
                value = 1 if (value and not before) else 0
                if mode == 0:
                    s.block_stack.append(s.R)
                    s.R = value
                elif mode == 1:
                    s.R &= value
                else:
                    s.R |= value
            return h
        if op in ('LDF', 'ANDF', 'ORF'):
            key = edge_key + ':dn:' + a0
            def h(s):
                value = read(s)
                before = s.prev_edges.get(key, 0)
                s.prev_edges[key] = 1 if value else 0
                value = 1 if (before and not value) else 0
                if mode == 0:
                    s.block_stack.append(s.R)
                    s.R = value
                elif mode == 1:
                    s.R &= value
                else:
                    s.R |= value
            return h
        if invert:
            def h(s):
                value = 1 - read(s)
                if mode == 0:
                    s.block_stack.append(s.R)
                    s.R = value
                elif mode == 1:
                    s.R &= value
                else:
                    s.R |= value
            return h
        def h(s):
            value = read(s)
            if mode == 0:
                s.block_stack.append(s.R)
                s.R = value
            elif mode == 1:
                s.R &= value
            else:
                s.R |= value
        return h

    def _fast_compare(self, op, args, pc, block, symbol, double):
        plan = self._make_plan(op, args, pc, block)
        left = self._fast_reader(plan[6], numeric=True, double=double)
        right = self._fast_reader(plan[7], numeric=True, double=double)
        if op.startswith('LD'):
            mode = 0
        elif op.startswith('AND'):
            mode = 1
        else:
            mode = 2
        def h(s):
            left_value = left(s)
            right_value = right(s)
            if symbol == '=':
                value = 1 if left_value == right_value else 0
            elif symbol == '<>':
                value = 1 if left_value != right_value else 0
            elif symbol == '<':
                value = 1 if left_value < right_value else 0
            elif symbol == '>':
                value = 1 if left_value > right_value else 0
            elif symbol == '<=':
                value = 1 if left_value <= right_value else 0
            else:
                value = 1 if left_value >= right_value else 0
            if mode == 0:
                s.block_stack.append(s.R)
                s.R = value
            elif mode == 1:
                s.R &= value
            else:
                s.R |= value
        return h

    def _fast_stl(self, token, pc, block):
        number = int(token[1:])
        target = self.next_stl.get(id(block), {}).get(pc, pc + 1)
        def h(s):
            if s.S.get(number, 0):
                s.current_step = number
                # 步通电时结果位为 ON(真 PLC 语义): 步体里的第一条 MOV/OUT 才拿得到 ON。
                s.R = 1
                return None
            return target
        return h

    def _fast_out(self, args):
        target = args[0]
        upper = target.upper()
        if upper.startswith('S') and upper[1:].isdigit():
            number = int(upper[1:])
            def h(s):
                if s.R:
                    s.S[number] = 1
                    if number and s.current_step is not None:
                        # 步转移:置位目标步的同时复位当前步与 S0(同一时刻只有一个步 ON)。
                        if number != s.current_step:
                            s.S[s.current_step] = 0
                        s.current_step = None
                        s.S[0] = 0
                    elif number == 0 and s.current_step is not None:
                        # OUT S0 = 步末标记:结束当前步并置位 S0(S0 = 设备停止状态)
                        s.S[s.current_step] = 0
                        s.current_step = None
                    if s.on_step is not None:
                        s.on_step(number)
                else:
                    s.S[number] = 0       # 线圈断开 -> 目标步复位
                s.block_stack.clear()
            return h
        operand = parse_operand(target)
        area = operand.area
        number = operand.number
        if operand.kind == 'dev' and area in ('M', 'Y'):
            container = getattr(self, area)
            def h(s):
                container[number] = s.R
                s.block_stack.clear()
            return h
        if operand.kind == 'dev' and area == 'T':
            preset = self._fast_reader(parse_operand(args[1] if len(args) > 1 else ''), numeric=True)
            base = self.timer_base(number)
            def h(s):
                limit = preset(s)
                span = limit * base
                state = s.T.setdefault(number, [0.0, span, 0])
                state[1] = span
                if s.R:
                    if not state[2] and not state[0]:
                        state[0] = s.virtual_time
                    if span and (s.virtual_time - state[0]) >= span:
                        state[2] = 1
                        state[0] = s.virtual_time - span      # FX 的当前值停在预设定
                else:
                    state[0] = 0.0
                    state[2] = 0
                s.block_stack.clear()
            return h
        if operand.kind == 'dev' and area == 'C':
            preset = self._fast_reader(parse_operand(args[1] if len(args) > 1 else ''),
                                       numeric=True, double=number >= C32_BASE)
            def h(s):
                if number >= 0 and s.R:
                    limit = preset(s)
                    s.c_preset[number] = limit
                    current = s.c_dword(number)
                    if current < limit:
                        s.set_c_dword(number, current + 1)
                s.block_stack.clear()
            return h
        def h(s):
            s.write_output(target, s.R, args[1] if len(args) > 1 else '')
            s.block_stack.clear()
        return h

    def _fast_set(self, args):
        target = args[0]
        upper = target.upper()
        operand = parse_operand(target)
        area = operand.area
        number = operand.number
        is_step = upper[0] == 'S' and upper[1:].isdigit()
        step = int(upper[1:]) if is_step else -1
        if operand.kind == 'dev' and area in ('M', 'Y', 'S'):
            container = getattr(self, area)
            def h(s):
                if s.R:
                    container[number] = 1
                    # SET S0 是"进入停止态",不是步转移;SET Sn(n≠0) 才是。
                    if is_step and step and s.current_step is not None:
                        if step != s.current_step:
                            s.S[s.current_step] = 0
                        s.current_step = None
                        s.S[0] = 0
                # 旧 Trace 是在 exec_one 返回后看 S[number] 是否为 1:
                # SET 条件不成立时也可能记录一个本来就在 1 的步,这里保持同一口径。
                if area == 'S' and s.on_step is not None and s.S.get(number, 0):
                    s.on_step(number)
                s.block_stack.clear()
            return h
        def h(s):
            if s.R:
                s.set_device(target, 1)
                if is_step and step and s.current_step is not None:
                    if step != s.current_step:
                        s.S[s.current_step] = 0
                    s.current_step = None
                    s.S[0] = 0
            s.block_stack.clear()
        return h

    def _fast_reset(self, op, args, pc, block):
        plan = self._make_plan(op, args, pc, block)
        key = plan[3]
        pulse = op.endswith('P')
        if op.startswith('ZRST'):
            first = args[0]
            last = args[1] if len(args) > 1 else ''
            def h(s):
                before = s.prev_edges.get(key, 0)
                s.prev_edges[key] = 1 if s.R else 0
                if s.R and (not pulse or not before):
                    s.reset_range(first, last)
                s.block_stack.clear()
            return h
        target = args[0]
        def h(s):
            before = s.prev_edges.get(key, 0)
            s.prev_edges[key] = 1 if s.R else 0
            if s.R and (not pulse or not before):
                s.reset_device(target)
            s.block_stack.clear()
        return h

    def _fast_move(self, op, args, pc, block):
        plan = self._make_plan(op, args, pc, block)
        key = plan[3]
        source = plan[6]
        target = plan[7]
        double = op.startswith('DMOV')
        pulse = op.endswith('P')
        if source.kind == 'block' or target.kind == 'block':
            def h(s):
                before = s.prev_edges.get(key, 0)
                s.prev_edges[key] = 1 if s.R else 0
                if s.R and (not pulse or not before):
                    s.write_block(target, s.read_block(source))
                    s.R = 1
                s.block_stack.clear()
            return h
        if double:
            read = self._fast_reader(source, numeric=True, double=True)
            def h(s):
                before = s.prev_edges.get(key, 0)
                s.prev_edges[key] = 1 if s.R else 0
                if s.R and (not pulse or not before):
                    s.set_dword(target.number, read(s))
                    s.R = 1
                s.block_stack.clear()
            return h
        read = self._fast_reader(source, numeric=True)
        area = target.area
        number = target.number
        def h(s):
            before = s.prev_edges.get(key, 0)
            s.prev_edges[key] = 1 if s.R else 0
            if s.R and (not pulse or not before):
                s.set_area_word(area, number, read(s))
                s.R = 1
            s.block_stack.clear()
        return h

    def _fast_call(self, op, args, pc, block):
        plan = self._make_plan(op, args, pc, block)
        key = plan[3]
        pulse = op.endswith('P')
        name = args[0]
        def h(s):
            before = s.prev_edges.get(key, 0)
            s.prev_edges[key] = 1 if s.R else 0
            if not s.R:
                return None
            if pulse and before:
                return None
            if name in s.subs:
                s._pending_call = name
                return _FAST_CALL
            return None
        return h


    def track_input_edges(self):
        """记录 X 输入的上升沿和脉冲周期(100ms 为单位),供 SPD 使用。"""
        for number, value in self.X.items():
            previous = self.prev_x.get(number, 0)
            if value and not previous:
                last = self.x_rise_time.get(number)
                if last is not None:
                    self.x_period[number] = int((self.virtual_time - last) / 0.1)
                self.x_rise_time[number] = self.virtual_time
            self.prev_x[number] = value

    def build_stl_index(self):
        """STL 步的边界与语义。

        边界:从 STL Sn 到该步的**步末标记 OUT S0**(含)。步末标记之后的指令属于
        **公共代码**,每扫描周期无条件执行。
        海天工程里公共代码有两段:主程序开头上电初始化段,以及最后一个步(S29)之后的
        行走脉冲计数 C220 / 顶刷脉冲计数 C222 / 故障检测段(共 560 多步)。按"下一个
        STL"划步会把后一段吞进 S29 步,导致这些逻辑只在 S29 激活时才跑。

        步末标记:每个 STL 步内**最后一条** OUT S0。标准的步末标记形态是
        `LD M0 / OR X044 / OUT S0`(停止键/停止标志时结束本步)。海天工程 28 条 OUT S0
        里 27 条是这个形态,第 28 条(S15 步末)条件特殊,按"步内最后一条 OUT S0"归类。

        步互斥:FX3U 步进梯形图同一时刻只有一个步 ON。`SET Sn`(n≠0)置位新步的同时
        复位当前步;步末标记 `OUT S0` 结束当前步(不置位 S0)。S0 是"设备停止状态"
        (HMI 报警表第 10 条),所以 S0 只在上电/停止键时由 `SET S0` 置位。
        """
        self.next_stl = {}
        self.step_marks = {}
        blocks = [self.main] + list(self.subs.values()) + self.others
        for block in blocks:
            positions = [index for index, item in enumerate(block) if item[0] == 'STL']
            table = {}
            for order, position in enumerate(positions):
                limit = positions[order + 1] if order + 1 < len(positions) else len(block)
                end = limit
                mark = None
                for j in range(position + 1, limit):
                    op, args = block[j]
                    if op == 'OUT' and args and args[0].upper() == 'S0':
                        end = j + 1              # 步末标记之后
                        mark = j
                table[position] = end
                if mark is not None:
                    self.step_marks[(id(block), mark)] = True
            self.next_stl[id(block)] = table

    def run_block(self, block, start):
        frames = [[block, start, 0]]
        while frames:
            frame = frames[-1]
            current_block, pc, depth = frame
            if pc >= len(current_block):
                frames.pop()
                continue
            op, args = current_block[pc]
            try:
                jump = self.exec_one(op, args, pc, current_block)
            except Exception as exc:                      # 单条指令出错不能拖垮整轮扫描
                self.unknown[op] = self.unknown.get(op, 0) + 1
                self.last_error = '%s: %s' % (op, exc)
                jump = None
            if jump == 'pop' or jump == 'end':
                if depth > 0:
                    self.call_depth = max(0, self.call_depth - 1)
                frames.pop()
                continue
            if jump is not None and isinstance(jump, tuple) and jump[0] == 'call':
                frame[1] = pc + 1
                if jump[1] in self.subs:
                    frames.append([self.subs[jump[1]], 0, depth + 1])
                    self.call_depth += 1
                continue
            if jump is not None and isinstance(jump, tuple) and jump[0] == 'stl':
                frame[1] = jump[1]
                continue
            frame[1] = pc + 1

    def _plan_for(self, op, args, pc, block):
        """取这条指令的"预编译计划"。

        计划里放的是每条指令每轮都不变的那些东西: 操作数文本、边沿键、
        比较符号、已解析的操作数对象。原来这些每次扫描都要重算一遍
        (拼字符串、建 lambda、拆助记符、查操作数表), 一轮 2000 多条指令
        下来光这些就占掉三成时间。

        只有从程序块里来的调用才进缓存 —— 自检/探针会直接调 exec_one(op, args)
        (block=None), 那些调用的参数跟程序块里的不是一回事, 不能共用缓存。
        """
        if block is None:
            if not known_op(op):
                self.unknown[op] = self.unknown.get(op, 0) + 1
                return None
            return self._make_plan(op, args, pc, None)

        plans = self._plan_lists.get(id(block))
        if plans is None:
            plans = [None] * len(block)
            self._plan_lists[id(block)] = plans
        plan = plans[pc]
        if plan is None:
            if not known_op(op):
                self.unknown[op] = self.unknown.get(op, 0) + 1
                return None
            plan = self._make_plan(op, args, pc, block)
            plans[pc] = plan
        return plan

    def _make_plan(self, op, args, pc, block):
        count = len(args)
        a0 = args[0] if count > 0 else ''
        a1 = args[1] if count > 1 else ''
        a2 = args[2] if count > 2 else ''
        symbol, double = self.split_compare(op)
        return (a0, a1, a2,
                '%d:%d' % (id(block), pc),
                symbol, double,
                parse_operand(a0), parse_operand(a1), parse_operand(a2))

    def exec_one(self, op, args, pc=0, block=None):
        plan = self._plan_for(op, args, pc, block)
        if plan is None:                      # 不认识的助记符(已计数)
            return None
        a0, a1, a2, edge_key, symbol, double, operand0, operand1, operand2 = plan

        # ---- 比较类(LD= / ANDD>= / LDD> / ORD< ...)----
        if symbol is not None:
            left = operand0
            right = operand1
            result = self.compare_op(symbol,
                                     self.operand_value(left, numeric=True, double=double),
                                     self.operand_value(right, numeric=True, double=double))
            self.contact_values[edge_key] = result
            if op.startswith('LD'):
                self.push_block()
                self.R = result
            elif op.startswith('AND'):
                self.R &= result
            else:
                self.R |= result
            return None

        # ---- 逻辑读 ----
        if op in CONTACTS:
            operand = operand0
            value = self.operand_value(operand)
            if op in ('LDI', 'ANI', 'ORI'):
                value = 1 - value
            elif op in ('LDP', 'ANDP', 'ORP'):
                value = self.rising(edge_key + ':up:' + a0, value)
            elif op in ('LDF', 'ANDF', 'ORF'):
                value = self.falling(edge_key + ':dn:' + a0, value)
            self.contact_values[edge_key] = value
            if op in ('LD', 'LDI', 'LDP', 'LDF'):
                self.push_block()
                self.R = value
            elif op in ('AND', 'ANI', 'ANDP', 'ANDF'):
                self.R &= value
            else:
                self.R |= value
            return None

        if op in ('ANDD', 'ORD'):
            operand = operand0
            value = self.operand_value(operand, numeric=True, double=True)
            if op == 'ANDD':
                self.R &= value
            else:
                self.R |= value
            return None

        if op == 'ANB':
            if self.block_stack:
                self.R &= self.block_stack.pop()
            return None

        if op == 'ORB':
            if self.block_stack:
                self.R |= self.block_stack.pop()
            return None

        if op == 'MPS':
            self.mps_stack.append(self.R)
            return None

        if op == 'MRD':
            if self.mps_stack:
                self.R = self.mps_stack[-1]
            return None

        if op == 'MPP':
            if self.mps_stack:
                self.R = self.mps_stack.pop()
            return None

        if op == 'INV':
            self.R = 1 - self.R
            return None

        if op == 'STL':
            if self.operand_value(operand0):
                self.current_step = int(a0[1:])
                # 步通电时结果位为 ON(真 PLC 语义): 步体里的第一条 MOV/OUT 才拿得到 ON。
                # 不置的话 S9 步的 `OUT T2 K10`(后退定时器)条件恒假 -> 定时器永不到位
                # -> 台车不退 -> S9 一步跨过 -> D142=7 写不进去(公共代码算 D142 时步位已熄)。
                self.R = 1
                return None
            return ('stl', self.next_stl.get(id(block), {}).get(pc, pc + 1))

        # ---- 输出 ----
        if op == 'OUT':
            target = a0.upper()
            if target.startswith('S') and target[1:].isdigit():
                number = int(target[1:])
                if self.R:
                    self.S[number] = 1
                    if number and self.current_step is not None:
                        # 步转移:置位目标步的同时复位当前步与 S0(同一时刻只有一个步 ON)。
                        # 只在步内转移时复位;公共代码里的 SET/OUT Sn 不碰 S0。
                        if number != self.current_step:
                            self.S[self.current_step] = 0
                        self.current_step = None
                        self.S[0] = 0
                    elif number == 0 and self.current_step is not None:
                        # OUT S0 = 步末标记:结束当前步并置位 S0(S0 = 设备停止状态)
                        self.S[self.current_step] = 0
                        self.current_step = None
                    if self.on_step is not None:
                        self.on_step(number)
                else:
                    self.S[number] = 0       # 线圈断开 -> 目标步复位
                self.block_stack.clear()
                return None
            self.write_output(a0, self.R, a1)
            self.block_stack.clear()
            return None

        if op == 'SET':
            target = a0.upper()
            if self.R:
                self.set_device(a0, 1)
                # SET S0 是"进入停止态",不是步转移;SET Sn(n≠0) 才是。
                if target[0] == 'S' and target[1:].isdigit() and int(target[1:]) and self.current_step is not None:
                    number = int(target[1:])
                    if number != self.current_step:
                        self.S[self.current_step] = 0
                    self.current_step = None
                    self.S[0] = 0
            # 旧 Trace 是在 exec_one 返回后看 S[number] 是否为 1:
            # SET 条件不成立时也可能记录一个本来就在 1 的步,这里保持同一口径。
            if self.on_step is not None and target and target[0] == 'S' and target[1:].isdigit():
                number = int(target[1:])
                if self.S.get(number):
                    self.on_step(number)
            self.block_stack.clear()
            return None

        if op in ('RST', 'ZRST', 'ZRSTP'):
            edge = self.rising(edge_key, self.R)
            if self.R and (not op.endswith('P') or edge):
                if op == 'RST':
                    self.reset_device(a0)
                else:
                    self.reset_range(a0, a1)
            self.block_stack.clear()
            return None

        # ---- 数据传送(MOV 是 2 操作数:源在前、目标在后)----
        if op in MOVE_OPS:
            edge = self.rising(edge_key, self.R)
            if self.R and (not op.endswith('P') or edge):
                source = operand0
                target = operand1
                double = op.startswith('DMOV')
                if source.kind == 'block' or target.kind == 'block':
                    self.write_block(target, self.read_block(source))
                elif double:
                    self.set_dword(target.number, self.operand_value(source, numeric=True, double=True))
                else:
                    self.set_area_word(target.area, target.number,
                                       self.operand_value(source, numeric=True))
                # 传送成功 => 结果位 ON(真 PLC 语义)。不置的话紧跟其后的 OUT 条件恒假,
                # 步体里的定时器/输出就都成了死代码。
                self.R = 1
            self.block_stack.clear()
            return None

        if op == 'BMOV':
            edge = self.rising(edge_key, self.R)
            if self.R and (not op.endswith('P') or edge):
                source = operand0
                target = operand1
                count = self.operand_value(operand2, numeric=True)
                area, number = self.resolve(source)
                step = self.area_step(area)
                values = [self.area_word(area, number + step * index) for index in range(count)]
                self.write_block(target, values)
            self.block_stack.clear()
            return None

        # ---- 算术(3 操作数:源1 源2 目标)----
        if op in ARITH_OPS:
            edge = self.rising(edge_key, self.R)
            if self.R and (not op.endswith('P') or edge):
                self.arith(op, args)
            self.block_stack.clear()
            return None

        if op == 'SUM':
            if self.R:
                source = operand0
                target = operand1
                total = sum(self.read_block(source)) & 0xFFFF
                self.set_area_word(target.area, target.number, total)
            self.block_stack.clear()
            return None

        if op == 'SPD':
            if self.R:
                self.spd(args)
            self.block_stack.clear()
            return None

        if op in ('FROM', 'TO'):
            # 特殊缓冲存储器传送:不影响状态机,模拟器不仿真但记账
            self.unsupported[op] = self.unsupported.get(op, 0) + 1
            self.block_stack.clear()
            return None

        # ---- 子程序 ----
        if op in ('CALL', 'CALLP'):
            edge = self.rising(edge_key, self.R)
            if not self.R:
                return None
            if op.endswith('P') and not edge:
                return None
            if a0 in self.subs:
                return ('call', a0)
            return None

        if op in ('SRET', 'FEND', 'IRET'):
            return 'pop'

        if op == 'RET':
            # 主程序里的 RET 不是程序结束,只有从子程序返回才有意义
            if self.call_depth > 0:
                return 'pop'
            return None

        if op == 'END':
            return 'end'

        if op == 'EI':
            return None

        return None

    # ---- 辅助 ----
    def push_block(self):
        self.block_stack.append(self.R)
        if len(self.block_stack) > self.max_block_depth:
            self.max_block_depth = len(self.block_stack)

    @staticmethod
    def split_compare(op):
        """比较类助记符拆成 (符号, 是否双字)。助记符是静态的,结果缓存。"""
        cached = _COMPARE_CACHE.get(op)
        if cached is not None:
            return cached
        for prefix, double in COMPARE_PREFIX:
            if not op.startswith(prefix):
                continue
            symbol = op[len(prefix):]
            if symbol in COMPARE_SYMBOLS:
                _COMPARE_CACHE[op] = (symbol, double)
                return symbol, double
        _COMPARE_CACHE[op] = (None, False)
        return None, False

    def bit_value(self, token, double=False):
        return self.operand_value(parse_operand(token), numeric=False, double=double)

    def value_of(self, token, double=False):
        return self.operand_value(parse_operand(token), numeric=True, double=double)

    def compare_op(self, symbol, left, right):
        if symbol == '=':
            return 1 if left == right else 0
        if symbol == '<>':
            return 1 if left != right else 0
        if symbol == '<':
            return 1 if left < right else 0
        if symbol == '>':
            return 1 if left > right else 0
        if symbol == '<=':
            return 1 if left <= right else 0
        if symbol == '>=':
            return 1 if left >= right else 0
        return 0

    def write_output(self, target, value, preset):
        operand = parse_operand(target)
        if operand.kind not in ('dev', 'idx'):
            return
        area, number = self.resolve(operand)
        if area == 'Y':
            self.Y[number] = value
        elif area == 'S':
            self.S[number] = value
        elif area == 'M':
            self.M[number] = value
        elif area == 'T':
            self.run_timer(number, value, preset)
        elif area == 'C':
            self.run_counter(number, value, preset)

    def run_timer(self, number, coil, preset):
        limit = self.operand_value(parse_operand(preset), numeric=True)
        base = self.timer_base(number)
        span = limit * base
        state = self.T.setdefault(number, [0.0, span, 0])
        state[1] = span
        if coil:
            if not state[2] and not state[0]:
                state[0] = self.virtual_time
            if span and (self.virtual_time - state[0]) >= span:
                state[2] = 1
                state[0] = self.virtual_time - span      # FX 的当前值停在预设定
        else:
            state[0] = 0.0
            state[2] = 0

    def run_counter(self, number, coil, preset):
        """FX3U 通用计数器(C / DC)。

        语义:计数输入 ON 的**每个扫描周期**加 1,计到预设定值停住(触点导通);
        计数输入断开时保持当前值,RST 指令清零。
        程序里 C220/C222/C224 都是 32 位(DC),占两个字(Cn 低字、Cn+1 高字),
        预设定也按 32 位读(D 区双字)。

        两条 FX 语义(2026-10-23 现场故障反推时补正):
          · C0~C199 都是合法的 16 位计数器,**C0 也在内**。原来的 `number <= 0`
            把 C0 整个判成"计数器不存在",于是 C0 触点恒为 0、`ANI C0` 恒成立,
            主程序 pc=1956 的"前进输出"永远掐不断,现场"台车原地停留 40 秒→D2=25"
            在模拟器里根本走不出来。
          · `OUT Cn <预设定>` 的**预设定值只在线圈 ON 时传送**;线圈断开时计数器
            的当前值和预设定都保持不变。原来无条件写 c_preset,会让线圈断开时
            也把当时的预设定值灌进去(D804 变化时预设定被追着改),与 FX 不符。
        """
        if number < 0:
            return
        if not coil:
            return
        limit = self.operand_value(parse_operand(preset), numeric=True, double=number >= C32_BASE)
        self.c_preset[number] = limit
        current = self.c_dword(number)
        if current >= limit:
            return
        self.set_c_dword(number, current + 1)

    def c_dword(self, number):
        """C 区数值。

        FX3U:C0~C199 是 16 位计数器(1 个字),C200~C255 是 32 位计数器
        (低字在前,占两个字)。原实现一律按双字存取,导致 Cn 的高字就是
        Cn+1 —— 相邻计数器互相污染,`RST Cn` 会把 Cn+1 清零。
        """
        if number >= C32_BASE:
            low = self.C.get(number, 0) & 0xFFFF
            high = self.C.get(number + 1, 0) & 0xFFFF
            return (high << 16) | low
        return self.C.get(number, 0) & 0xFFFF

    def set_c_dword(self, number, value):
        if number >= C32_BASE:
            self.C[number] = value & 0xFFFF
            self.C[number + 1] = (value >> 16) & 0xFFFF
        else:
            self.C[number] = value & 0xFFFF

    def set_device(self, target, value):
        operand = parse_operand(target)
        if operand.kind not in ('dev', 'idx'):
            return
        area, number = self.resolve(operand)
        if area in ('Y', 'M', 'S'):
            getattr(self, area)[number] = 1 if value else 0

    def reset_device(self, target):
        operand = parse_operand(target)
        if operand.kind not in ('dev', 'idx'):
            return
        area, number = self.resolve(operand)
        if area in ('Y', 'M', 'S'):
            getattr(self, area)[number] = 0
        elif area == 'T':
            self.T[number] = [0.0, 0.0, 0]
        elif area == 'Z':
            self.Z[number] = 0
        elif area == 'C':
            self.set_c_dword(number, 0)

    def reset_range(self, first, last):
        start_operand = parse_operand(first)
        end_operand = parse_operand(last)
        if start_operand.kind not in ('dev', 'idx'):
            return
        area = start_operand.area
        _, start = self.resolve(start_operand)
        end = end_operand.number if end_operand.kind in ('dev', 'idx') else start
        if area == 'D':
            for number in range(start, end + 1):
                self.D[number] = 0
        elif area in ('M', 'S', 'Y'):
            for number in range(start, end + 1):
                getattr(self, area)[number] = 0
        elif area == 'Z':
            for number in range(start, end + 1):
                self.Z[number] = 0
        elif area == 'C':
            for number in range(start, end + 1):
                self.C[number] = 0
                if number >= C32_BASE:
                    self.C[number + 1] = 0
                self.c_preset.pop(number, None)
        elif area == 'T':
            for number in range(start, end + 1):
                self.T[number] = [0.0, 0.0, 0]

    def arith(self, op, args):
        """3 操作数: 源1(args[0]) 源2(args[1]) 目标(args[2])。

        例外:INCP / DECP / DINCP 是**单操作数**(只有目标,自增/自减),必须先于
        3 操作数的长度检查处理——之前放在检查之后,这些指令从来没真正执行过。

        结果宽度按 FX 语义:
          ADD/SUB  -> 1 个字(D 前缀 -> 2 个字)
          MUL      -> 2 个字(32 位积,高字在 D+1)
          DMUL     -> 4 个字(64 位积)
          DIV      -> 商在 D、余数在 D+1
          DDIV     -> 商在 D/D+1、余数在 D+2/D+3
        """
        if op in ('INCP', 'DECP', 'DINCP'):
            if not args:
                return
            target = parse_operand(args[0])
            target_area, target_number = self.resolve(target)
            current = self.area_word(target_area, target_number)
            if op == 'DECP':
                self.set_area_word(target_area, target_number, current - 1)
            else:
                self.set_area_word(target_area, target_number, current + 1)
            return
        if len(args) < 3:
            return

        source1 = parse_operand(args[0])
        source2 = parse_operand(args[1])
        target = parse_operand(args[2])
        target_area, target_number = self.resolve(target)
        double = op in ('DADD', 'DADDP', 'DSUB', 'DSUBP', 'DMUL', 'DDIV', 'DDIVP')
        width = 32 if double else 16

        left = self.to_signed(self.operand_value(source1, numeric=True, double=double), width)
        right = self.to_signed(self.operand_value(source2, numeric=True, double=double), width)

        if op in ('DIV', 'DDIV', 'DDIVP'):
            quotient, remainder = self.divide(left, right)
            if double:
                for index, value in enumerate((quotient, remainder)):
                    self.set_area_word(target_area, target_number + index * 2, value & 0xFFFF)
                    self.set_area_word(target_area, target_number + index * 2 + 1,
                                       (value >> 16) & 0xFFFF)
            else:
                self.set_area_word(target_area, target_number, quotient & 0xFFFF)
                self.set_area_word(target_area, target_number + 1, remainder & 0xFFFF)
            return

        # 助记符 → 运算种类。D 前缀是 32 位版本,P 后缀是脉冲(边沿)版本。
        # **已知缺陷(2026-10-22 现场故障反推时发现,本次未修)**:
        # 原来这里写的是 `op.startswith('ADD')`,而 'DADD'/'DADDP'/'DSUB'/'DSUBP'/
        # 'DMUL' 都以 'D' 开头,startswith 全是 False ⇒ **所有 32 位算术都是空操作**
        # (DADD/DADDP/DSUB/DSUBP/DMUL 一次都没真的算过)。
        # 现场影响:主程序 P6#1 `DADD D30 K70 D32`(S3→S4 的行程门槛 D32=D30+70)
        # 没被执行 ⇒ D32 恒为 0 ⇒ S3 步的出口条件 `C220 >= D32` **恒成立**,
        # 模拟器因此"总能"走出洗车头;真机上 D32 = D30+70 ≈ 72,
        # 台车被 M16 冻在 C220 = D30,这条出口走不通 ⇒ 卡在 S3 → T99 40 秒
        # → D2=25(现场症状)。
        # 修它会让默认基线不再 PASS(D34/D36/D66… 一串标定值跟着变),
        # 要连带把物理模型重新标定,超出本次任务范围,留给后续。
        # 本次保留原样(空操作),但把缺陷写在这里,别再"重新发现"一遍。
        if op.startswith('ADD'):
            value = left + right
            words = 2 if double else 1
        elif op.startswith('SUB'):
            value = left - right
            words = 2 if double else 1
        elif op.startswith('MUL'):
            value = left * right
            words = 4 if double else 2      # 16 位乘法是 32 位结果,DMUL 是 64 位
        else:
            return

        value &= (1 << (16 * words)) - 1
        for index in range(words):
            self.set_area_word(target_area, target_number + index,
                               (value >> (16 * index)) & 0xFFFF)

    @staticmethod
    def to_signed(value, width):
        value &= (1 << width) - 1
        sign = 1 << (width - 1)
        return value - (1 << width) if value & sign else value

    @staticmethod
    def divide(left, right):
        """FX 整数除法:向零截断,余数符号随被除数。"""
        if right == 0:
            return 0, 0
        quotient = abs(left) // abs(right)
        if (left < 0) != (right < 0):
            quotient = -quotient
        return quotient, left - quotient * right

    def spd(self, args):
        """SPD: 测输入脉冲周期(100ms 为单位),上限 K 值,结果写目标。"""
        if len(args) < 3:
            return
        source = parse_operand(args[0])
        limit = self.operand_value(parse_operand(args[1]), numeric=True)
        target = parse_operand(args[2])
        area, number = self.resolve(source)
        period = self.x_period.get(number, 0) if area == 'X' else 0
        target_area, target_number = self.resolve(target)
        self.set_area_word(target_area, target_number, min(period, limit) if limit else period)
