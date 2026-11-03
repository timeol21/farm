#!/usr/bin/env python3
# 解释某个输出指令的条件链:按块栈深度回溯出完整 rung,逐条求值并标出假条件。
# 用法: python explain.py <csv> <输出设备>
import sys, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from core.ladder import Ladder

csv_path = sys.argv[1]
target = sys.argv[2].upper()
ladder = Ladder(csv_path)

LD_OPS = ('LD', 'LDI', 'LDP', 'LDF')


def rung_start(block, index):
    """从输出指令往回扫,按块栈深度找到 rung 起点。"""
    depth = 0
    start = index
    for j in range(index - 1, -1, -1):
        op = block[j][0]
        if op in ('ORB', 'ANB'):
            depth += 1
        elif op.startswith('LD') and op not in ('AND',):
            depth -= 1
            if depth < 0:
                start = j
                break
    return start


def evaluate(block, start, end):
    """复算 rung 的条件链,返回每条指令的 (操作数取值, 结果 R)。"""
    R = 0
    block_stack = []
    mps_stack = []
    rows = []
    for j in range(start, end + 1):
        op, args = block[j]
        arg1 = args[0] if args else ''
        arg2 = args[1] if len(args) > 1 else ''
        operand = '-'
        if op in LD_OPS or op in ('AND', 'ANI', 'ANDP', 'ANDF', 'OR', 'ORI', 'ORP', 'ORF'):
            operand = ladder.bit_value(arg1)
        elif op.startswith('LD') and op[2:3] in ('=', '<', '>'):
            operand = ladder.value_of(arg1)
        if op in LD_OPS:
            value = operand
            if op == 'LDI':
                value = 1 - value
            block_stack.append(R)
            R = value
        elif op in ('AND', 'ANI', 'OR', 'ORI'):
            value = operand
            if op in ('ANI', 'ORI'):
                value = 1 - value
            R = (R & value) if op.startswith('AND') else (R | value)
        elif op in ('ANB', 'ORB'):
            if block_stack:
                other = block_stack.pop()
                R = (R & other) if op == 'ANB' else (R | other)
        elif op == 'MPS':
            mps_stack.append(R)
        elif op == 'MRD':
            if mps_stack:
                R = mps_stack[-1]
        elif op == 'MPP':
            if mps_stack:
                R = mps_stack.pop()
        elif op == 'INV':
            R = 1 - R
        rows.append((j, op, arg1, arg2, operand, R))
    return rows


for block_name, block in [('主', ladder.main)] + [('P%s' % k, v) for k, v in sorted(ladder.subs.items())]:
    for index, (op, args) in enumerate(block):
        if op != 'OUT' or not args or args[0].upper() != target:
            continue
        start = rung_start(block, index)
        print('=== %s 块 #%d  OUT %s   rung 从 #%d 开始 ===' % (block_name, index, target, start))
        for j, o, a, b, operand, R in evaluate(block, start, index):
            flag = '  <== 假' if R == 0 else ''
            print('   [%4d] %-8s %-8s %-6s 操作数=%-4s 结果R=%d%s' % (j, o, a, b, operand, R, flag))
        print()
