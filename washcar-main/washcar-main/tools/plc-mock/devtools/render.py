#!/usr/bin/env python3
# 把 GX Works 导出的指令表还原成梯形图,方便和 GX Works 里的画面逐条对照。
#
# 用法:
#   python render.py <csv> [选项]
#     --text              输出纯文本(每行一条 rung),不生成 HTML
#     --out <文件.html>   HTML 输出路径(默认 梯形图.html)
#     --start <n> --end <n>  只渲染指令下标区间(下标用 dump.py 查)
#     --steps             用 GX Works 步号(CSV 第一列)标行
#
# 画法:条件树横向串联、并联分支竖排;触点 ┤M187├ / ┤/M187├ / 比较 [= D482 D484];
#       输出 (线圈) 或 [指令 操作数]。这是"文字版梯形图",和 GX Works 的画面结构一致,
#       但列位置是排出来的,不是原图的坐标。
import io, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from devtools.rungtree import TreeState, is_compare

OUTPUT_OPS = ('OUT', 'SET', 'RST', 'ZRST', 'ZRSTP', 'MOV', 'MOVP', 'DMOV', 'DMOVP', 'BMOV',
               'ADD', 'DADD', 'DADDP', 'SUB', 'DSUB', 'DSUBP', 'MUL', 'DMUL', 'DIV',
               'DDIV', 'DDIVP', 'INCP', 'DECP', 'DINCP', 'SUM', 'SPD', 'FROM', 'TO')
CONTACT_LABEL = {'LDI': '/', 'ANI': '/', 'ORI': '/', 'LDP': '↑', 'ANDP': '↑', 'ORP': '↑',
                 'LDF': '↓', 'ANDF': '↓', 'ORF': '↓'}


def parse(path):
    """返回 [(程序名, [ (步号, 助记符, [操作数]) ])]。"""
    rows = [line.rstrip('\r\n').split('\t') for line in io.open(path, encoding='utf-16')]
    instructions = []
    for row in rows[3:]:
        if len(row) < 4:
            continue
        step = row[0].strip().strip('"').strip()
        op = row[2].strip().strip('"').strip()
        args = [row[3].strip().strip('"').strip()]
        if len(row) > 4 and row[4].strip().strip('"').strip():
            args.append(row[4].strip().strip('"').strip())
        if op:
            instructions.append([step, op, args])
        elif instructions and args[0]:
            instructions[-1][2].append(args[0])
    programs = []
    name = None
    current = []
    for item in instructions:
        op = item[1]
        if op[:1] in ('P', 'I') and op[1:].isdigit():
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
    return programs


def contact_label(op, args):
    """触点文字:M187 / /M187(常闭) / ↑M187(上升沿) / 比较触点 = D136 K0"""
    arg = args[0] if args else ''
    if is_compare(op):
        right = args[1] if len(args) > 1 else ''
        return '%s %s %s' % (op[2:], arg, right)
    return '%s%s' % (CONTACT_LABEL.get(op, ''), arg)


def output_label(op, args):
    text = ' '.join(args)
    if op == 'OUT':
        return '(%s)' % text
    if op == 'SET':
        return '-(%s)-' % text
    if op == 'RST':
        return '-(/%s)-' % text
    return '[%s %s]' % (op, text)


def tree_text(node):
    if node is None:
        return ''
    if node.kind == 'lit':
        return node.label or ''
    if node.kind == 'not':
        return '[%s]' % tree_text(node.left)
    if node.kind == 'and':
        return tree_text(node.left) + tree_text(node.right)
    return tree_text(node.left) + ' || ' + tree_text(node.right)


def or_branches(node):
    """把左斜的 or 链摊平成一列分支(GX Works 的并联触点就是平铺竖排)。"""
    branches = []
    current = node
    while current is not None and current.kind == 'or':
        branches.append(current.right)
        current = current.left
    branches.append(current)
    branches.reverse()
    return branches


def tree_html(node):
    if node is None:
        return '<span class="w"></span>'
    if node.kind == 'lit':
        text = node.label or ''
        symbol = '[ %s ]' % text if (' ' in text and not text.startswith('/')) else '─┤├─'
        return '<span class="c"><i>%s</i><b>%s</b></span>' % (text, symbol)
    if node.kind == 'not':
        return '<span class="not">[/]</span>%s[/]' % tree_html(node.left)
    if node.kind == 'and':
        return '<span class="s">%s%s</span>' % (tree_html(node.left), tree_html(node.right))
    branches = or_branches(node)
    return '<span class="p">%s</span>' % ''.join(
        '<span>%s</span>' % tree_html(branch) for branch in branches)


def build_rungs(program):
    """把一段程序切成 rung:每个输出指令一条,条件取当前条件树。

    同一个条件树可能驱动多个输出(MPS/MPP),这些输出画在同一组的不同行上。
    """
    state = TreeState()
    rungs = []
    for index, (step, op, args) in enumerate(program):
        if op in OUTPUT_OPS:
            rungs.append((index, step, state.tree, output_label(op, args)))
            state.block.clear()
            continue
        label = contact_label(op, args) if op not in ('INV', 'MPS', 'MRD', 'MPP', 'ANB', 'ORB') else ''
        state.feed(op, args, 0, label)
    return rungs


def group_by_tree(rungs):
    """把共用同一条件树(MPS 分支)的输出并成一组。"""
    groups = []
    index_by_tree = {}
    for rung in rungs:
        key = id(rung[2])
        if key in index_by_tree:
            groups[index_by_tree[key]][1].append(rung)
        else:
            index_by_tree[key] = len(groups)
            groups.append((rung[2], [rung]))
    return groups


def main():
    csv_path = sys.argv[1]
    args = sys.argv[2:]
    text_only = '--text' in args
    out_path = '梯形图.html'
    if '--out' in args:
        out_path = args[args.index('--out') + 1]
    start = int(args[args.index('--start') + 1]) if '--start' in args else 0
    end = int(args[args.index('--end') + 1]) if '--end' in args else None
    use_steps = '--steps' in args

    programs = parse(csv_path)
    lines = []
    html = []
    for name, program in programs:
        lines.append('===== 程序 %s (%d 条指令) =====' % (name or '主程序', len(program)))
        html.append('<h2>程序 %s <small>%d 条指令</small></h2>' % (name or '主程序', len(program)))
        rungs = [r for r in build_rungs(program)
                 if r[0] >= start and (end is None or r[0] <= end)]
        for tree, items in group_by_tree(rungs):
            for index, step, _, output in items:
                marker = step if (use_steps and step) else str(index)
                condition = tree_text(tree) or '─'
                lines.append('%6s  %-44s %s' % (marker, condition, output))
                html.append('<div class="rung"><span class="rail">┃</span>'
                            '<span class="step">%s</span>'
                            '<span class="net">%s</span><span class="out">%s</span></div>'
                            % (marker, tree_html(tree), output))
    if text_only:
        print('\n'.join(lines))
        return 0
    page = """<!doctype html><meta charset="utf-8"><title>海天洗车机 梯形图</title>
<style>
body{font:13px/1.7 "Consolas","Microsoft YaHei",monospace;background:#fff;color:#111;margin:16px}
h2{font-size:15px;margin:18px 0 8px;border-bottom:1px solid #ddd;padding-bottom:4px}
h2 small{color:#888;font-weight:normal}
.rung{display:flex;align-items:center;gap:6px;white-space:nowrap;padding:0}
.rail{color:#999}
.step{color:#888;min-width:52px;text-align:right}
.net{display:inline-flex;align-items:center;flex:1}
.c{position:relative;display:inline-flex;flex-direction:column;align-items:center;
   border-left:2px solid #333;border-right:2px solid #333;padding:0 4px;margin:0 4px}
.c i{font-style:normal;font-size:11px;color:#333;line-height:1.1}
.c b{font-weight:normal;display:block;height:10px}
.s{display:inline-flex;align-items:center}
.p{display:inline-flex;flex-direction:column;border-left:1px solid #333;padding-left:8px;margin:0 4px}
.p>span{display:inline-flex;align-items:center}
.not{color:#c00;margin:0 2px}
.out{border:1px solid #333;border-radius:2px;padding:0 6px;background:#f4f4f4;margin-left:12px}
</style>
%s""" % '\n'.join(html)
    with io.open(out_path, 'w', encoding='utf-8') as f:
        f.write(page)
    print('已生成 %s' % out_path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
