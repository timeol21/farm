#!/usr/bin/env python3
# 梯形图反查:给定软元件,列出所有读写它的指令及上下文。
# 用法: python trace.py <csv> <软元件> [更多软元件...]
import sys, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from core.ladder import parse_csv

csv_path = sys.argv[1]
targets = [t.upper() for t in sys.argv[2:]]
main, subs, entry = parse_csv(csv_path)

blocks = [('主程序', main)] + [(k, subs[k]) for k in sorted(subs)]

for target in targets:
    print('\n############ %s ############' % target)
    for block_name, block in blocks:
        for index, (op, args) in enumerate(block):
            if target in [a.upper() for a in args]:
                print('\n  [%s #%d] %s %s' % (block_name, index, op, ' '.join(args)))
                for j in range(max(0, index - 4), min(len(block), index + 5)):
                    mark = '  >>' if j == index else '    '
                    print('%s [%s #%d] %s %s' % (mark, block_name, j, block[j][0], ' '.join(block[j][1])))
