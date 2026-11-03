#!/usr/bin/env python3
# 打印梯形图主程序(或子程序)的指定区间。
# 用法: python dump.py <csv> <起始> <结束> [子程序号]
import sys, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from core.ladder import parse_csv

csv_path = sys.argv[1]
start = int(sys.argv[2])
end = int(sys.argv[3])
main, subs, entry = parse_csv(csv_path)
if len(sys.argv) > 4:
    # 子程序号两种写法都收:P10 或 10
    key = sys.argv[4]
    block = subs[key] if key in subs else subs['P%s' % key]
else:
    block = main
for index in range(max(0, start), min(len(block), end)):
    op, args = block[index]
    print('[%4d] %-8s %s' % (index, op, ' '.join(args)))
