#!/usr/bin/env python3
# 打印某个指令所在 rung 的完整条件链(从上一条输出类指令之后开始)。
# 用法: python rung.py <csv> <输出设备> [块名]
import sys, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from core.ladder import parse_csv

csv_path = sys.argv[1]
target = sys.argv[2].upper()
main, subs, others = parse_csv(csv_path)
blocks = [('主', main)] + [('P%s' % k, v) for k, v in sorted(subs.items())] + \
         [('其它%s' % i, b) for i, b in enumerate(others)]

OUT_OPS = ('OUT', 'SET', 'RST', 'ZRST', 'ZRSTP', 'MOV', 'MOVP', 'DMOV', 'DMOVP', 'BMOV',
           'ADD', 'DADD', 'DADDP', 'SUB', 'DSUB', 'DSUBP', 'MUL', 'DMUL', 'DIV', 'DDIV',
           'DDIVP', 'INCP', 'DECP', 'DINCP', 'STL')

for block_name, block in blocks:
    for index, (op, args) in enumerate(block):
        if not args or args[0].upper() != target or op not in OUT_OPS:
            continue
        start = index
        while start > 0 and block[start - 1][0] not in OUT_OPS:
            start -= 1
        print('--- %s 块 #%d  %s %s ---' % (block_name, index, op, ' '.join(args)))
        for j in range(start, index + 1):
            print('   [%4d] %-8s %s' % (j, block[j][0], ' '.join(block[j][1])))
        print()
