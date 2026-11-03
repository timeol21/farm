#!/usr/bin/env python3
# 虚拟 PLC 的可重复验收闸门。
#
# 用法: python verify_mock.py [csv] [--gxw <工程.gxw>]
# 退出码 0 = 全部 PASS,非 0 = 有 FAIL
#
# 闸门:
#   G1 助记符覆盖   : CSV 里出现的每个助记符都必须有实现(以前这条是假的:未知指令永远抓不到)
#   G2 未知指令零发生: 真跑 N 次扫描后,没有任何助记符落到"未识别"
#   G3 梯形结构复算 : 每条输出指令的结果,和"按梯形图结构重算一遍"的结果一致(第二套实现)
#   G4 业务链       : D142 步骤映射 / 复位→就绪→启动 / 停止清步 / 故障锁存 / 洗车标志
#   G4x 语义口径    : 算术宽度(MUL/DDIV)、计数器宽度、ZRST C·T、K4M 位组合、定时器封顶
#                     —— 期望值按 FX3U 手册,不按当前实现反推
#   G5 gxw 交叉对账 : 和 GX Works2 工程文件(.gxw)里的梯形图逐个软元件对账
import io, os, random, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
for path in (ROOT, HERE):
    if path not in sys.path:
        sys.path.insert(0, path)

from core.ladder import Ladder, known_op, parse_operand, parse_csv   # noqa: E402
from simulators.haitian import machine                                # noqa: E402

DEV = re.compile(r'^([XYMSTCDT])([0-9]+)$')
OUTPUT_OPS = ('OUT', 'SET', 'RST', 'ZRST', 'ZRSTP', 'MOV', 'MOVP', 'DMOV', 'DMOVP', 'BMOV',
               'ADD', 'DADD', 'DADDP', 'SUB', 'DSUB', 'DSUBP', 'MUL', 'DMUL', 'DIV',
               'DDIV', 'DDIVP', 'INCP', 'DECP', 'DINCP', 'SUM', 'SPD')
LD_OPS = ('LD', 'LDI', 'LDP', 'LDF')
AND_OPS = ('AND', 'ANI', 'ANDP', 'ANDF')
OR_OPS = ('OR', 'ORI', 'ORP', 'ORF')
MOVE_OPS = ('MOV', 'MOVP', 'DMOV', 'DMOVP')   # 传送成功 => 结果位 ON

results = []
DEFAULT_CSV = os.path.join(ROOT, 'docs', '海天', '海天洗车机.csv')
csv_arg = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else DEFAULT_CSV
csv_path = csv_arg
gxw_path = None
if '--gxw' in sys.argv:
    gxw_path = sys.argv[sys.argv.index('--gxw') + 1]


def report(name, ok, detail):
    results.append((name, ok, detail))
    print('%-6s %-28s %s' % ('PASS' if ok else 'FAIL', name, detail))


def build():
    """按现场条件造一台 PLC。

    现场条件与洗车流程演练(tests/wash_cycle.py)共用同一套表(simulators/haitian/machine.py),
    避免闸门和演练两套口径。现场表的口径见 docs/PLC寄存器定义.md(PLC 端 HMI 实测截图)。
    """
    ladder = Ladder(csv_path)
    for number in range(128):
        ladder.X[number] = 0
    for number, value in machine.X_IDLE.items():
        ladder.X[number] = value
    for number in machine.X_FAULT:
        ladder.X[number] = 0
    for number, value in machine.M_CONFIG.items():
        ladder.M[number] = value
    for number, value in machine.INJECT_M.items():
        ladder.M[number] = value
    ladder.D[134] = 1          # 故障屏蔽位(上电为 1)
    ladder.D[534] = 1500      # 行走脉冲上限(C220 的预设定)
    return ladder


def enter_step(ladder, step_number):
    """直接落到某个状态步:清掉其它步并离开初始步。"""
    ladder.S.clear()
    ladder.S[step_number] = 1


# ---------------------------------------------------------------- G1 助记符覆盖
def gate_mnemonics():
    main, subs, others = parse_csv(csv_path)
    used = set()
    for block in [main] + list(subs.values()) + others:
        for op, args in block:
            used.add(op)
    missing = sorted(op for op in used if not known_op(op) and not re.match(r'^(P|I)\d+$', op))
    report('G1 助记符全部有实现', not missing,
           'CSV 用到 %d 种助记符,未实现: %s' % (len(used), missing or '无'))


# ---------------------------------------------------------------- G2 未知指令零发生
def gate_unknown():
    ladder = build()
    for _ in range(30):
        ladder.scan(0.05)
    ladder.X[44] = 1
    for _ in range(5):
        ladder.scan(0.05)
    ladder.X[44] = 0
    ladder.X[45] = 1
    for _ in range(80):
        ladder.scan(0.05)
    report('G2 运行中无未识别指令', not ladder.unknown,
           '未知: %s' % (ladder.unknown or '无') + ('  错误: %s' % ladder.last_error if ladder.last_error else ''))


# ---------------------------------------------------------------- G3 梯形结构复算
class Node:
    """条件树节点:与 ladder.py 的栈机是另一套实现,用来互相印证。
    kind=lit 时 left 存 0/1 字面值。"""
    __slots__ = ('kind', 'left', 'right')

    def __init__(self, kind, left=None, right=None):
        self.kind = kind
        self.left = left
        self.right = right


def eval_tree(node):
    if node is None:
        return 1
    if node.kind == 'lit':
        return node.left
    if node.kind == 'not':
        return 1 - eval_tree(node.left)
    if node.kind == 'and':
        return eval_tree(node.left) & eval_tree(node.right)
    return eval_tree(node.left) | eval_tree(node.right)



def gate_structure():
    """扫描时逐条输出指令复算条件:数值栈机 vs 条件树,必须一致。

    条件树是与数值栈机并行推进的第二套实现:同一支路左结合,ANB/ORB 并入之前压栈的支路。
    触点值取数值栈机算好的值(避免重复触发边沿记忆),树结构本身独立算。
    """
    ladder = build()
    mismatches = []
    counter = {'checked': 0}
    original = ladder.exec_one
    state = {'tree': None, 'block': [], 'mps': []}

    def traced(op, args, pc=0, block=None):
        jump = original(op, args, pc, block)
        if block is None:
            return jump
        value = ladder.contact_values.get('%d:%d' % (id(block), pc))
        value = value if value is not None else 0
        compare = op[:2] in ('LD', 'AN', 'OR') and op[-1:] in ('=', '<', '>')
        if op in LD_OPS or (compare and op.startswith('LD')):
            state['block'].append(state['tree'])
            state['tree'] = Node('lit', value)
        elif op in AND_OPS or (compare and op.startswith('AND')):
            state['tree'] = Node('and', state['tree'], Node('lit', value))
        elif op in OR_OPS or (compare and op.startswith('OR')):
            state['tree'] = Node('or', state['tree'], Node('lit', value))
        elif op == 'ANB':
            state['tree'] = Node('and', state['tree'],
                                  state['block'].pop() if state['block'] else None)
        elif op == 'ORB':
            state['tree'] = Node('or', state['tree'],
                                  state['block'].pop() if state['block'] else None)
        elif op == 'MPS':
            state['mps'].append(state['tree'])
        elif op == 'MRD':
            state['tree'] = state['mps'][-1] if state['mps'] else state['tree']
        elif op == 'MPP':
            state['tree'] = state['mps'].pop() if state['mps'] else state['tree']
        elif op == 'INV':
            state['tree'] = Node('not', state['tree'])
        elif op == 'STL':
            # 步通电 => 结果位 ON(与 ladder.py 的 STL 语义一致):
            # 步体里的第一条 MOV/OUT 要靠这个 ON 才拿得到条件。
            step = int(str(args[0])[1:])
            if ladder.S.get(step):
                state['tree'] = Node('lit', 1)
        elif op in OUTPUT_OPS:
            if op in MOVE_OPS:
                # 传送: 条件成立才执行, 执行成功 => 结果位 ON; 条件不成立 => 结果位保持 OFF。
                # (与 ladder.py 的 MOV 分支一致)
                expected = 1 if eval_tree(state['tree']) else 0
                state['tree'] = Node('lit', expected)
            else:
                expected = eval_tree(state['tree'])
            if expected != ladder.R:
                mismatches.append((pc, op, ' '.join(args), ladder.R, expected))
            counter['checked'] += 1
            state['block'].clear()          # 输出之后支路结束,和数值栈机一致
        return jump

    ladder.exec_one = traced
    for _ in range(12):
        ladder.scan(0.05)
    ladder.X[44] = 1
    for _ in range(4):
        ladder.scan(0.05)
    ladder.X[44] = 0
    ladder.X[45] = 1
    for _ in range(60):
        ladder.scan(0.05)
    report('G3 梯形结构复算一致', not mismatches,
           '复算 %d 条输出指令,不一致 %d 处 %s'
           % (counter['checked'], len(mismatches), mismatches[:3] if mismatches else ''))


# ---------------------------------------------------------------- G4 业务链
def gate_business():
    # D142 洗车步骤映射
    ladder = build()
    ladder.scan(0.05)
    bad = []
    for step, step_name in sorted(machine.STEP_TABLE.items(), key=lambda kv: int(kv[1][1:])):
        enter_step(ladder, int(step_name[1:]))
        ladder.scan(0.05)                      # 只扫一次:第二次可能已经推进到下一步
        if ladder.D.get(142) != step:
            bad.append((step_name, step, ladder.D.get(142)))
    report('G4a D142 洗车步骤映射', not bad,
           '10 个工序步全对上' if not bad else '不符: %s' % bad)

    # 复位 -> 就绪 -> 启动
    # 复位动作按盒子的 reset 服务写 M100(见 src/common/config/default_point.cpp),
    # 不是按 X44 停止键;X44 是"停止洗车"。
    ladder = build()
    ladder.scan(0.05)
    ladder.M[100] = 1
    for _ in range(4):
        ladder.scan(0.05)
    ladder.M[100] = 0
    for _ in range(10):
        ladder.scan(0.05)
    ready = bool(ladder.S.get(1)) and ladder.D.get(142) == 1 and bool(ladder.M.get(1))
    report('G4b 复位后就绪 (S1 + D142=1 + M1)', ready,
           'S1=%s D142=%s M1=%s' % (ladder.S.get(1), ladder.D.get(142), ladder.M.get(1)))

    # S20 / M187 都是**瞬时量**:S20 步执行完立刻转 S37,OUT M187 的条件链
    # (M515 & T61)也会被后续 MPP 覆盖。所以不能用末态判断,记录扫描过程中
    # 启动链有没有推进过:S20 → S37 → S21 → S3(洗车头)。
    ladder = build()
    ladder.scan(0.05)
    ladder.M[100] = 1
    for _ in range(4):
        ladder.scan(0.05)
    ladder.M[100] = 0
    for _ in range(10):
        ladder.scan(0.05)
    chain = {'steps': set(), 'm187': False}
    original_exec = ladder.exec_one

    def traced_exec(op, args, pc=0, block=None):
        jump = original_exec(op, args, pc, block)
        for number in (20, 37, 21, 3):
            if ladder.S.get(number):
                chain['steps'].add(number)
        if ladder.M.get(187):
            chain['m187'] = True
        return jump

    ladder.exec_one = traced_exec
    ladder.X[45] = 1
    for _ in range(70):
        ladder.scan(0.05)
    started = 20 in chain['steps'] and chain['m187']
    report('G4c 写 X45 -> T61 到位 -> S20', started,
           '启动链经过 %s  M187 曾为1=%s  T61=%s'
           % (sorted(chain['steps']), chain['m187'], ladder.T.get(61, [0, 0, 0])[2]))

    # 停止:清掉所有步
    ladder = build()
    ladder.scan(0.05)
    enter_step(ladder, 3)
    ladder.scan(0.05)
    held = ladder.S.get(3, 0)
    ladder.X[44] = 1
    for _ in range(4):
        ladder.scan(0.05)
    cleared = not ladder.S.get(3, 0) and not ladder.S.get(1, 0)
    report('G4d 按停止 -> 清掉所有步', cleared,
           '清前 S3=%s, 清后 S3=%s S1=%s' % (held, ladder.S.get(3), ladder.S.get(1)))

    # 故障锁存 M501 + M523 复位
    ladder = build()
    ladder.scan(0.05)
    ladder.D[84] = 1
    for _ in range(4):
        ladder.scan(0.05)
    latched = bool(ladder.M.get(501, 0))
    ladder.M[523] = 1
    ladder.D[84] = 0
    for _ in range(4):
        ladder.scan(0.05)
    report('G4e M501 故障锁存 + M523 复位', latched and not ladder.M.get(501, 0),
           '锁存时 M501=%s, 复位后 M501=%s' % (latched, ladder.M.get(501)))

    # 洗车标志 Y174
    ladder = build()
    ladder.scan(0.05)
    enter_step(ladder, 1)
    ladder.scan(0.05)
    idle = ladder.Y.get(174, 0)
    ladder = build()
    ladder.scan(0.05)
    enter_step(ladder, 3)
    ladder.scan(0.05)
    report('G4f Y174 洗车标志', idle == 0 and ladder.Y.get(174, 0) == 1,
           '空闲 Y174=%s, 洗车 Y174=%s' % (idle, ladder.Y.get(174, 0)))

    # 三操作数算术:梯形图上 [MUL D3000 K60 D3004] 表示 D3004 = D3000 * K60
    # (直接验解释器的算术口径;程序里 D3000 会被 SPD 覆盖,不好从外部喂值)
    # FX 语义:ADD/SUB 1 字;MUL 结果 32 位(D+1:D);DIV 商在 D、余数在 D+1;
    #          DDIV 商在 D/D+1、余数在 D+2/D+3。
    ladder = build()
    ladder.D[10] = 7
    ladder.D[12] = 300
    ladder.D[100] = 100
    ladder.D[11] = 100
    ladder.arith('MUL', ['D10', 'K60', 'D20'])        # D20=420, D21=0
    ladder.arith('MUL', ['D12', 'K300', 'D24'])       # 90000 = 0x0001_5F90
    ladder.arith('DIV', ['D100', 'K4', 'D22'])        # 商 25、余 0
    ladder.arith('SUB', ['D11', 'K30', 'D26'])        # 70
    ladder.set_dword(30, 1000)
    ladder.arith('DDIV', ['D30', 'K3', 'D32'])        # 商 333、余 1
    arithmetic_ok = (
        ladder.D.get(20) == 420 and ladder.D.get(21) == 0
        and ladder.D.get(24) == 24464 and ladder.D.get(25) == 1
        and ladder.D.get(22) == 25 and ladder.D.get(23) == 0
        and ladder.D.get(26) == 70
        and ladder.D.get(32) == 333 and ladder.D.get(33) == 0
        and ladder.D.get(34) == 1 and ladder.D.get(35) == 0
    )
    report('G4g 算术 3 操作数(目标在最后)', arithmetic_ok,
           'MUL 7*60->%s/%s, MUL 300*300->%s/%s, DIV 100/4->%s r%s, SUB->%s, DDIV 1000/3->%s/%s r%s/%s'
           % (ladder.D.get(20), ladder.D.get(21), ladder.D.get(24), ladder.D.get(25),
              ladder.D.get(22), ladder.D.get(23), ladder.D.get(26),
              ladder.D.get(32), ladder.D.get(33), ladder.D.get(34), ladder.D.get(35)))

    # FX3U 的 C0~C199 是 16 位计数器(1 个字):相邻计数器不能互相串扰
    ladder = Ladder(csv_path)
    for _ in range(3):
        ladder.run_counter(4, 1, 'K800')
    for _ in range(5):
        ladder.run_counter(5, 1, 'K400')
    before = (ladder.c_dword(4), ladder.c_dword(5))
    ladder.R = 1
    ladder.reset_device('C4')
    after = (ladder.c_dword(4), ladder.c_dword(5))
    report('G4h 16 位计数器互不串扰', before == (3, 5) and after == (0, 5),
           'C4/C5 计数后=%s, RST C4 后=%s' % (before, after))

    # ZRST 必须能清 C/T(原实现只处理 D/M/S/Y/Z)
    ladder = Ladder(csv_path)
    ladder.set_c_dword(3, 7)
    ladder.set_c_dword(4, 9)
    ladder.T[5] = [1.0, 2.0, 1]
    ladder.R = 1
    ladder.exec_one('ZRST', ['C3', 'C4'])
    ladder.exec_one('ZRST', ['T5', 'T5'])
    cleared = (ladder.c_dword(3) == 0 and ladder.c_dword(4) == 0
               and ladder.T.get(5, [0.0, 0.0, 0])[2] == 0)
    report('G4i ZRST 能清 C/T', cleared,
           'C3=%s C4=%s T5=%s' % (ladder.c_dword(3), ladder.c_dword(4), ladder.T.get(5)))

    # K<n> 位组合 = n 位十六进制数字 = n*4 位:K4M 只读写 16 位(1 个字)
    ladder = Ladder(csv_path)
    for index in range(16):
        ladder.M[260 + index] = 1
    ladder.M[240] = 1                     # 目标块之外:M236~M259 不能被顺手清掉
    ladder.R = 1
    ladder.exec_one('MOV', ['K4M260', 'K4M220'])
    target_ok = all(ladder.M.get(220 + index) == 1 for index in range(16))
    outside = [number for number in range(236, 260) if ladder.M.get(number)]
    report('G4j K4M 只搬 16 位', target_ok and outside == [240],
           'M220~M235 全 1=%s, M236~M259 只保留哨兵 %s' % (target_ok, outside))

    # 定时器当前值封顶在预设定;MOV 可以写定时器当前值
    ladder = Ladder(csv_path)
    ladder.R = 1
    for _ in range(200):
        ladder.virtual_time += 0.05
        ladder.exec_one('OUT', ['T0', 'K10'])
    value = ladder.operand_value(parse_operand('T0'), numeric=True)
    ladder.exec_one('MOV', ['K5', 'T1'])
    written_value = ladder.operand_value(parse_operand('T1'), numeric=True)
    report('G4k 定时器封顶 + 可写', value == 10 and written_value == 5,
           'T0 当前值=%s(期望 10), MOV K5 T1 后 T1=%s' % (value, written_value))



# ---------------------------------------------------------------- G6 快慢路径等价
def _edge_state(ladder):
    """把 edge key 里的 id(block) 换成稳定的块名,两台 Ladder 才能逐项对比。"""
    labels = {id(ladder.main): 'main'}
    for name, body in ladder.subs.items():
        labels[id(body)] = 'sub:' + name
    for index, body in enumerate(ladder.others):
        labels[id(body)] = 'other:%d' % index
    result = {}
    for key, value in ladder.prev_edges.items():
        if not value:
            continue
        head, rest = key.split(':', 1)
        result[(labels.get(int(head), head), rest)] = value
    return result


def _fast_snapshot(ladder):
    return (
        dict(ladder.X), dict(ladder.Y), dict(ladder.M), dict(ladder.S),
        {number: value for number, value in ladder.D.items() if value},
        dict(ladder.Z),
        {number: tuple(state) for number, state in ladder.T.items()},
        {number: value for number, value in ladder.C.items() if value},
        dict(ladder.c_preset),
        dict(ladder.x_period), dict(ladder.x_rise_time), dict(ladder.prev_x),
        _edge_state(ladder),
        ladder.R, tuple(ladder.block_stack), tuple(ladder.mps_stack),
        ladder.current_step, ladder.call_depth, ladder.first_scan, ladder.scans,
        round(ladder.virtual_time, 9), dict(ladder.unknown), ladder.last_error,
    )


def gate_equivalence():
    """快速路径和逐条解释器跑同一串输入,每一步的内部状态必须完全一致。

    这条闸门专门抓"快速路径把某条指令实现错了"——G3 用的是旧解释器 + 条件树,
    覆盖不到快速路径;G4 只看业务末态,抓不到中间量差异。
    """
    fast = build()
    slow = build()
    fast.use_fast = True
    slow.use_fast = False
    rng = random.Random(20261025)
    mismatch = None
    total = 600
    for index in range(total):
        if index == 5:
            fast.M[100] = slow.M[100] = 1
        if index == 9:
            fast.M[100] = slow.M[100] = 0
            fast.X[45] = slow.X[45] = 1
        if index == 250:
            fast.X[45] = slow.X[45] = 0
        if index == 260:
            fast.X[44] = slow.X[44] = 1
        if index == 264:
            fast.X[44] = slow.X[44] = 0
        if index == 400:
            fast.M[100] = slow.M[100] = 1
        if index == 404:
            fast.M[100] = slow.M[100] = 0
        if index > 10 and index % 17 == 0:
            number = rng.randrange(0, 128)
            value = rng.randrange(0, 2)
            fast.X[number] = slow.X[number] = value
            number = rng.randrange(0, 900)
            fast.M[number] = slow.M[number] = value
            number = rng.randrange(0, 8000)
            value = rng.randrange(0, 0x10000)
            fast.D[number] = slow.D[number] = value
            index_z = rng.randrange(0, 8)
            value = rng.randrange(0, 50)
            fast.Z[index_z] = slow.Z[index_z] = value
            number = rng.randrange(0, 30)
            fast.C[number] = slow.C[number] = value
        fast.scan(0.05)
        slow.scan(0.05)
        first = _fast_snapshot(fast)
        second = _fast_snapshot(slow)
        if first != second:
            field = next(i for i, pair in enumerate(zip(first, second)) if pair[0] != pair[1])
            mismatch = '第 %d 扫描第 %d 项: 快速=%r 逐条=%r' % (
                index, field, first[field], second[field])
            break
    report('G6 快慢路径逐项等价', mismatch is None,
           ('%d 扫描状态/定时器/边沿/子程序全部一致' % total) if mismatch is None else mismatch)


# ---------------------------------------------------------------- G5 gxw 交叉对账
def gate_gxw():
    path = gxw_path
    if not path:
        data_dir = os.path.dirname(os.path.abspath(csv_path))
        candidates = [os.path.join(data_dir, name) for name in os.listdir(data_dir)
                      if name.endswith('.gxw')]
        if not candidates:
            report('G5 gxw 交叉对账', True, '未提供 .gxw,跳过')
            return
        path = candidates[0]
    try:
        from devtools import gxw_check
        ok = gxw_check.check(path, csv_path)
    except ImportError as exc:
        report('G5 gxw 交叉对账', True, '跳过(缺依赖 %s); 装 olefile 后可跑' % exc)
        return
    except Exception as exc:
        report('G5 gxw 交叉对账', False, '异常: %s' % exc)
        return
    report('G5 gxw 交叉对账', ok, '工程: %s' % os.path.basename(path))


gate_mnemonics()
gate_unknown()
gate_structure()
gate_business()
gate_equivalence()
gate_gxw()

print()
failed = [name for name, ok, _ in results if not ok]
print('%d/%d 通过' % (len(results) - len(failed), len(results)))
if failed:
    print('未通过:', ', '.join(failed))
sys.exit(1 if failed else 0)
