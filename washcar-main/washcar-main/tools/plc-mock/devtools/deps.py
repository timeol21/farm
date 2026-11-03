#!/usr/bin/env python3
# 梯形图条件依赖分析:从目标软元件逆向展开"要让它成立,还需要哪些点"。
# 用法: python deps.py <csv> <目标软元件> [深度]
import sys, os, collections
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from core.ladder import parse_csv

csv_path = sys.argv[1]
target = sys.argv[2].upper()
max_depth = int(sys.argv[3]) if len(sys.argv) > 3 else 3

main, subs, entry = parse_csv(csv_path)
blocks = [('主程序', main)] + [(k, subs[k]) for k in sorted(subs)]

# 置位源:哪些指令把某个软元件写成 1
writers = collections.defaultdict(list)
# 读取点:哪些指令读某个软元件(当条件用)
readers = collections.defaultdict(list)
SET_OPS = ('OUT', 'SET')

for block_name, block in blocks:
    for index, (op, args) in enumerate(block):
        if op in SET_OPS and args:
            writers[args[0].upper()].append((block_name, index))
        for token in args:
            if token:
                readers[token.upper()].append((block_name, index))


OUTPUT_OPS = ('OUT', 'SET', 'RST', 'ZRST', 'ZRSTP', 'MOV', 'MOVP', 'DMOV', 'DMOVP', 'BMOV',
               'ADD', 'DADD', 'DADDP', 'SUB', 'DSUB', 'DSUBP', 'MUL', 'DMUL', 'DIV',
               'DDIV', 'DDIVP', 'INCP', 'DECP', 'DINCP')
COND_OPS = ('LD', 'LDI', 'LDP', 'LDF', 'AND', 'ANI', 'ANDP', 'ANDF', 'OR', 'ORI', 'ORP',
            'ORF', 'MPS', 'MRD', 'MPP', 'ANB', 'ORB', 'INV', 'STL')


def conditions_of(block_name, index):
    """取该指令所在 rung 的条件链:往回扫到上一个输出指令(rung 边界)为止。"""
    block = dict(blocks)[block_name]
    chain = []
    back = index - 1
    while back >= 0:
        op, args = block[back]
        if op in OUTPUT_OPS:
            break
        if op in COND_OPS and args:
            chain.insert(0, (back, op, args[0]))
        back -= 1
    return chain


def leaves():
    """没有置位源的软元件 = 只能靠外部输入(X 点)或注入。"""
    return set(readers) - set(writers)


external_only = leaves()
print('目标:', target)
print()

visited = set()
queue = [(target, 0, '目标')]
seen_paths = []

while queue:
    name, depth, prefix = queue.pop(0)
    if name in visited or depth > max_depth:
        continue
    visited.add(name)

    write_sites = writers.get(name, [])
    read_sites = readers.get(name, [])

    if not write_sites:
        kind = 'X 输入(外部给)' if name.startswith('X') else '无置位源(需外部注入)'
        print('%s%s  ← %s' % ('  ' * depth, name, kind))
        continue

    for block_name, index in write_sites:
        op, args = dict(blocks)[block_name][index]
        chain = conditions_of(block_name, index)
        cond_text = ' '.join(
            ('!' + a if o == 'LDI' else a) for _, o, a in chain if a)
        print('%s%s  ← [%s #%d] %s %s   条件: %s' %
              ('  ' * depth, name, block_name, index, op, ' '.join(args), cond_text or '(无)'))
        for _, _, operand in chain:
            if not operand or not operand[0].isalpha():
                continue
            operand = operand.upper()
            if operand in external_only and operand not in writers:
                print('%s  - %s (外部给/注入)' % ('  ' * (depth + 1), operand))
            elif operand not in visited:
                queue.append((operand, depth + 1, prefix))

print()
print('=== 全局无置位源的软元件(只能外部给或注入),前 60 个 ===')
for name in sorted(external_only)[:60]:
    if name.startswith('X'):
        continue
    print('  ', name)
