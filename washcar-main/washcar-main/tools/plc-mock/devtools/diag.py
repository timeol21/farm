#!/usr/bin/env python3
# 运行时诊断:当前停在哪个步、该步的转移条件里哪一条不满足、步内输出是什么状态。
#
# 用法:
#   python diag.py <csv>                     从上电静态跑,打印状态机激活情况
#   python diag.py <csv> --state <json>      用给定状态打印(状态格式见 wash_cycle.py)
#
# 诊断对象是 STL 步进块:每个步末尾的 SET/OUT Sn 就是它的转移条件。
import json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from core.ladder import Ladder
from devtools.stl_steps import rung_start

LD_OPS = ('LD', 'LDI', 'LDP', 'LDF')
CONTACT_OPS = LD_OPS + ('AND', 'ANI', 'ANDP', 'ANDF', 'OR', 'ORI', 'ORP', 'ORF')


def blocks_of(ladder):
    return [('主', ladder.main)] + [('P%s' % k, v) for k, v in sorted(ladder.subs.items())] + \
           [('其它%d' % i, b) for i, b in enumerate(ladder.others)]


def step_blocks(ladder):
    """切出所有 STL 步块: [(块名, 步号, 起始, 结束, block), ...]"""
    out = []
    for block_name, block in blocks_of(ladder):
        positions = [i for i, item in enumerate(block) if item[0] == 'STL']
        for order, position in enumerate(positions):
            end = positions[order + 1] if order + 1 < len(positions) else len(block)
            number = block[position][1][0].upper()
            out.append((block_name, number, position, end, block))
    return out


def leaf_state(ladder, op, args):
    """叶子条件的当前状态: (值, 说明)。"""
    if op in CONTACT_OPS:
        value = ladder.bit_value(args[0])
        sign = '正' if op in ('LD', 'AND', 'OR', 'LDP', 'ANDP', 'ORP') else '反'
        return value, '%s=%d' % (args[0], value)
    if op.startswith('LD') and op[2:3] in ('=', '<', '>'):
        symbol, double = ladder.split_compare(op)
        left = ladder.value_of(args[0], double=double)
        right = ladder.value_of(args[1], double=double)
        return ladder.compare_op(symbol, left, right), '%s=%g %s %s=%g' % (
            args[0], left, symbol, args[1], right)
    return None, op


def diagnose(ladder, focus=None):
    """打印所有激活步的转移条件诊断。"""
    active = sorted(number for number, value in ladder.S.items() if value)
    print('激活的 S 状态: %s' % ', '.join('S%d' % n for n in active))
    print('D142(工序号) = %s' % ladder.D.get(142))
    print('D2(错误码) = %s   D134 = %s   D136 = %s' %
          (ladder.D.get(2), ladder.D.get(134), ladder.D.get(136)))
    print('C 计数器: %s' % ', '.join('C%d=%d' % (n, v) for n, v in sorted(ladder.C.items()) if n > 0))
    print('关键 X: %s' % ', '.join('X%d=%d' % (n, ladder.X.get(n, 0))
                                   for n in (1, 2, 3, 6, 7, 10, 11, 14, 15, 22, 24, 44, 45)))
    print('关键 Y: %s' % ', '.join('Y%d=%d' % (n, ladder.Y.get(n, 0))
                                   for n in (0, 10, 12, 16, 17, 20, 21, 22, 23, 24, 25,
                                             30, 31, 40, 41, 42, 45, 47, 74)))
    print('D3000(流量计周期) = %s   D3004(升/分) = %s' %
          (ladder.D.get(3000), ladder.D.get(3004)))
    print()

    for block_name, number, start, end, block in step_blocks(ladder):
        step_number = int(number[1:])
        if step_number not in active:
            continue
        if focus and number != focus:
            continue
        print('--- 步 %s (块 %s #%d..#%d) ---' % (number, block_name, start, end))
        for j in range(start + 1, end):
            op, args = block[j]
            if op not in ('SET', 'OUT') or not args:
                continue
            target = args[0].upper()
            if not target.startswith('S'):
                continue
            chain_start = rung_start(block, j)
            items = []
            for k in range(chain_start, j):
                kop, kargs = block[k]
                if kop in CONTACT_OPS or (kop.startswith('LD') and kop[2:3] in ('=', '<', '>')):
                    value, text = leaf_state(ladder, kop, kargs)
                    if value is not None:
                        items.append((kop, text, value))
            if not items:
                continue
            print('  转移 %s %s' % (op, target))
            for kop, text, value in items:
                print('      %-4s %-28s %s' % (('真' if value else '假'), text, kop))
        print()


def main():
    csv_path = sys.argv[1]
    ladder = Ladder(csv_path)
    if '--state' in sys.argv:
        path = sys.argv[sys.argv.index('--state') + 1]
        with open(path, encoding='utf-8') as f:
            state = json.load(f)
        ladder.X.update({int(k): v for k, v in state.get('X', {}).items()})
        ladder.M.update({int(k): v for k, v in state.get('M', {}).items()})
        ladder.Y.update({int(k): v for k, v in state.get('Y', {}).items()})
        ladder.S.update({int(k): v for k, v in state.get('S', {}).items()})
        ladder.D.update({int(k): v for k, v in state.get('D', {}).items()})
    focus = None
    for token in sys.argv[2:]:
        if token.upper().startswith('S') and token[1:].isdigit():
            focus = token.upper()
    diagnose(ladder, focus)


if __name__ == '__main__':
    main()
