#!/usr/bin/env python3
# rung 条件树:把指令表的支路结构还原成表达式树。
#
# 用途:
#   · render.py   —— 画梯形图(给人对照 GX Works)
#   · verify_mock.py —— 第二套实现,和 ladder.py 的数值栈机互相印证
#
# 结构口径(和 FX 助记符一致):
#   · LD/LDI/LDP/LDF 和 LD 类比较:起新支路,把旧结果压栈
#   · AND/ANI/ANDP/ANDF 和 AND 类比较:和当前支路串联
#   · OR/ORI/ORP/ORF 和 OR 类比较:和当前支路并联
#   · ANB/ORB:把之前压栈的支路取回来做串联/并联
#   · MPS/MRD/MPP:多输出分支(同一条件挂多个输出)
#   · INV:取反
LD_OPS = ('LD', 'LDI', 'LDP', 'LDF')
AND_OPS = ('AND', 'ANI', 'ANDP', 'ANDF')
OR_OPS = ('OR', 'ORI', 'ORP', 'ORF')


class Node:
    """条件树节点。kind='lit' 时 left 是 0/1 值、label 是触点文字。"""
    __slots__ = ('kind', 'label', 'left', 'right')

    def __init__(self, kind, label=None, left=None, right=None):
        self.kind = kind
        self.label = label
        self.left = left
        self.right = right

    def value(self):
        if self.kind == 'lit':
            return self.left
        if self.kind == 'not':
            return 1 - self.left.value()
        if self.kind == 'and':
            return self.left.value() & self.right.value()
        return self.left.value() | self.right.value()


def is_compare(op):
    return op[:2] in ('LD', 'AN', 'OR') and op[-1:] in ('=', '<', '>')


class TreeState:
    """和 ladder.py 并行推进的条件树。"""

    def __init__(self):
        self.tree = None
        self.block = []
        self.mps = []

    def reset(self):
        self.tree = None
        self.block = []
        self.mps = []

    def feed(self, op, args, value=0, label=''):
        compare = is_compare(op)
        if op in LD_OPS or (compare and op.startswith('LD')):
            self.block.append(self.tree)
            self.tree = Node('lit', label, value)
        elif op in AND_OPS or (compare and op.startswith('AND')):
            self.tree = Node('and', None, self.tree, Node('lit', label, value))
        elif op in OR_OPS or (compare and op.startswith('OR')):
            self.tree = Node('or', None, self.tree, Node('lit', label, value))
        elif op == 'ANB':
            self.tree = Node('and', None, self.tree,
                             self.block.pop() if self.block else None)
        elif op == 'ORB':
            self.tree = Node('or', None, self.tree,
                             self.block.pop() if self.block else None)
        elif op == 'MPS':
            self.mps.append(self.tree)
        elif op == 'MRD':
            self.tree = self.mps[-1] if self.mps else self.tree
        elif op == 'MPP':
            self.tree = self.mps.pop() if self.mps else self.tree
        elif op == 'INV':
            self.tree = Node('not', None, self.tree)
