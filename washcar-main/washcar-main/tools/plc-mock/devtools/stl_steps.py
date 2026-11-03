#!/usr/bin/env python3
# 步进梯形图(STL)状态机结构分析:按 STL 把程序切成步块,列出每个步的转移目标和转移条件。
#
# 用法:
#   python stl_steps.py <csv>              列出所有步
#   python stl_steps.py <csv> S3 S4        只看指定步(步号可写 S3 或 3)
#
# 输出里 "条件" 是从转移指令往前回溯出来的整条 rung 链(按 FX 块栈规则)。
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from core.ladder import parse_csv

LD_OPS = ('LD', 'LDI', 'LDP', 'LDF')
CONTACT_OPS = LD_OPS + ('AND', 'ANI', 'ANDP', 'ANDF', 'OR', 'ORI', 'ORP', 'ORF')


def rung_start(block, index):
    """从一条输出类指令往前扫,按块栈深度找到 rung 起点。"""
    depth = 0
    for j in range(index - 1, -1, -1):
        op = block[j][0]
        if op in ('ORB', 'ANB'):
            depth += 1
        elif op.startswith('LD') and op != 'AND':
            depth -= 1
            if depth < 0:
                return j
    return max(0, index - 1)


def leaves(block, start, end):
    """rung 里所有叶子条件(触点/比较),按出现顺序。"""
    out = []
    for j in range(start, end + 1):
        op, args = block[j]
        if op in CONTACT_OPS or (op.startswith('LD') and op[2:3] in ('=', '<', '>')):
            out.append('%s %s' % (op, ' '.join(args)))
    return out


def main():
    csv_path = sys.argv[1]
    wanted = set()
    for token in sys.argv[2:]:
        token = token.upper()
        wanted.add(token if token.startswith('S') else 'S' + token)

    main_block, subs, others = parse_csv(csv_path)
    blocks = [('主', main_block)] + [('P%s' % k, v) for k, v in sorted(subs.items())] + \
             [('其它%d' % i, b) for i, b in enumerate(others)]

    for block_name, block in blocks:
        positions = [i for i, item in enumerate(block) if item[0] == 'STL']
        for order, position in enumerate(positions):
            end = positions[order + 1] if order + 1 < len(positions) else len(block)
            number = block[position][1][0].upper()
            if wanted and number not in wanted:
                continue
            print('=== 步 %s   块 %s  #%d..#%d ===' % (number, block_name, position, end))
            for j in range(position + 1, end):
                op, args = block[j]
                if op not in ('SET', 'OUT', 'RST') or not args:
                    continue
                target = args[0].upper()
                if not target.startswith('S'):
                    continue
                chain_start = rung_start(block, j)
                chain = ['%s %s' % (block[k][0], ' '.join(block[k][1]))
                         for k in range(chain_start, j)]
                print('  转移 %-4s %-4s ← %s' % (op, target, ' ; '.join(chain)))
                print('        叶子: %s' % ', '.join(leaves(block, chain_start, j)))
            print()


if __name__ == '__main__':
    main()
