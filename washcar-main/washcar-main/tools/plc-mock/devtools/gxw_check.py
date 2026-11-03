#!/usr/bin/env python3
# gxw 交叉对账:把 GX Works2 工程文件(.gxw)里的梯形图解出来,和 CSV 指令表逐条对账。
#
# 为什么要它:plc-mock 执行的是 GX Works 导出的 CSV 指令表,不是梯形图本身。
# 这个工具从 .gxw(梯形图的原始工程)里独立解出程序,和 CSV 对齐,
# 证明"模拟器跑的程序 == 梯形图工程里的程序",而不是"模拟器跑的程序 == 我抄的 CSV"。
#
# .gxw 结构(OLE 复合文档):
#   <工程>.gxw -> _hdb(嵌套 OLE) -> 流 "12" = 程序块(同一块存了两遍)
#   程序块 = [块头][44444 字节载荷]
#   载荷记录 = [位置字节][长度 L][载荷 L-2 字节]
#   一个"元素" = [类型记录][操作数记录...] —— 类型记录首字节是元素类别:
#     00=LD 01=LDI 02=LDP 03=LDF 06=OR 07=ORI 08=ORP 09=ORF
#     0c=AND 0d=ANI 0e=ANDP 0f=ANDF 14=INV 18=ORB 19=ANB 1a=MPS 1b=MRD 1c=MPP
#     20/21=OUT 23=SET 24=RST  其余是各种应用指令(比较/传送/算术…)
#   类别码与 CSV 指令条数一一对应(见 verify_mock 的 G5)。
#
# 用法: python gxw_check.py <gxw> <csv>
# 退出码 0 = 对账通过,非 0 = 有偏差
import io, os, re, sys

import olefile

# 区域标签 -> 软元件区
#   f1 是"点数前缀"里的 K(梯形图上写成 K4M260 这种),gxw 里 K 和设备是分开两条记录
TAGS = {0x90: 'M', 0x9c: 'X', 0x9d: 'Y', 0x98: 'S', 0xa8: 'D', 0xe8: 'K', 0xc2: 'T', 0xc5: 'C',
        0xf1: 'K', 0xd0: 'P', 0xd1: 'I', 0xea: 'H', 0xe9: 'K', 0xf0: 'Z', 0xcc: 'Z'}
# 元素类别码 -> CSV 指令助记符
CLASSES = {
    0x00: 'LD', 0x01: 'LDI', 0x02: 'LDP', 0x03: 'LDF',
    0x06: 'OR', 0x07: 'ORI', 0x08: 'ORP', 0x09: 'ORF',
    0x0c: 'AND', 0x0d: 'ANI', 0x0e: 'ANDP', 0x0f: 'ANDF',
    0x14: 'INV', 0x18: 'ORB', 0x19: 'ANB', 0x1a: 'MPS', 0x1b: 'MRD', 0x1c: 'MPP',
    0x20: 'OUT', 0x21: 'OUT', 0x23: 'SET', 0x24: 'RST',
}
BLOCK_OFFSET = 0x3c
BLOCK_SIZE = 44444


def load_program_block(path):
    """取出 .gxw 里的程序块载荷。"""
    outer = olefile.OleFileIO(path)
    hdb = outer.openstream('_hdb').read()
    inner = olefile.OleFileIO(io.BytesIO(hdb))
    data = inner.openstream('12').read()
    return data[BLOCK_OFFSET:BLOCK_OFFSET + BLOCK_SIZE]


def decode_records(data):
    """按记录边界切: [位置字节][长度 L][载荷 L-2 字节]"""
    records = []
    index = 0
    while index < len(data) - 1:
        length = data[index + 1]
        if length < 3 or index + length > len(data):
            index += 1
            continue
        records.append((index, data[index], length, data[index + 2:index + length]))
        index += length
    return records


def decode_elements(data):
    """切成元素: [类型记录][操作数记录...]"""
    elements = []
    current = None
    for offset, position, length, payload in decode_records(data):
        tag = payload[0] if payload else -1
        is_type = bool(payload) and tag not in TAGS
        if is_type:
            current = {'offset': offset, 'type': payload, 'operands': []}
            elements.append(current)
        elif current is None:
            current = {'offset': offset, 'type': None, 'operands': []}
            elements.append(current)
            if len(payload) >= 2 and tag in TAGS:
                area = TAGS[tag]
                value = int.from_bytes(payload[1:], 'little')
                current['operands'].append('%s%d' % (area, value))
        else:
            if len(payload) >= 2 and tag in TAGS:
                area = TAGS[tag]
                value = int.from_bytes(payload[1:], 'little')
                if area in ('K', 'H') and value > 0x7FFFFFFF:
                    value -= 0x100000000          # 负常量在 gxw 里是补码
                current['operands'].append('%s%d' % (area, value))
    return elements


def decode_devices(data):
    """只取软元件引用:[(区, 号)]。"""
    out = []
    for offset, position, length, payload in decode_records(data):
        if len(payload) >= 2 and payload[0] in TAGS:
            out.append((TAGS[payload[0]], int.from_bytes(payload[1:], 'little')))
    return out


def csv_instructions(path):
    """CSV 指令表: [(步号, 助记符, [操作数...])](操作数续行并入上一条)"""
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
    return instructions


def expand_operand(token):
    """CSV 操作数 -> gxw 里的记录序列

    K4M260 = [K4, M260](点数前缀在前);D499Z0 = [Z0, D499](变址在前,和 gxw 一致)
    """
    token = token.strip()
    if not token:
        return []
    match = re.match(r'^([Kk])(\d+)([XYMSTCZD])(\d+)$', token)
    if match:
        return ['K' + match.group(2), match.group(3) + match.group(4)]
    match = re.match(r'^([XYMSTCZD])(\d+)([ZY])(\d+)$', token)
    if match:
        return ['Z' + match.group(4), match.group(1) + match.group(2)]
    match = re.match(r'^([XYMSTCDKHZ])(\d+)$', token)
    if match:
        area, number = match.group(1), int(match.group(2))
        if area in ('X', 'Y'):
            number = int(str(number), 8)
        if area == 'H':                          # 十六进制常量在 gxw 里是十进制值
            number = int(match.group(2), 16)
        return [area + str(number)]
    return [token]


def class_name(element, index=0):
    """元素类别。程序第一个元素没有类型记录,按 LD 处理。"""
    if element['type'] is None:
        return 'LD' if index == 0 else None
    return CLASSES.get(element['type'][0])


def align_prefix(gxw, csv):
    """单调对齐,返回 (一致条数, 首个错位下标)。"""
    index = 0
    matched = 0
    for position, device in enumerate(csv):
        found = -1
        for offset in range(0, 8):
            if index + offset >= len(gxw):
                break
            if gxw[index + offset] == device:
                found = offset
                break
        if found < 0:
            return matched, position
        matched += 1
        index += found + 1
    return matched, None


def compare_structure(elements, instructions):
    """逐元素比对:类别 + 操作数必须和 CSV 指令表一致。

    对齐方式:已知类别(LD/AND/OR/OUT/… 22 类)的条数两边完全一致,拿它们当锚点,
    锚点之间再逐条比"应用指令"(比较/传送/算术)。
    返回 (一致条数, 差异列表, 元素数, 指令数)。
    """
    gxw_items = []
    for index, element in enumerate(elements):
        gxw_items.append((class_name(element, index), element['operands']))
    csv_items = []
    for step, op, args in instructions:
        operands = []
        for token in args:
            operands += expand_operand(token)
        if not operands and re.match(r'^(P|I)\d+$', op):
            operands = [op]                  # 标签行(P0/I680):gxw 里标签名是操作数
        csv_items.append((op if op in CLASSES.values() else None, operands, op))

    differences = []
    same = 0
    gi = ci = 0
    while gi < len(gxw_items) and ci < len(csv_items):
        g_class, g_operands = gxw_items[gi]
        c_class, c_operands, c_op = csv_items[ci]
        if g_class is not None and c_class is not None:
            # 锚点: 类别必须一致
            if g_class != c_class:
                differences.append('#%d 类别 gxw=%s csv=%s' % (gi, g_class, c_class))
                break
            if g_operands != c_operands:
                differences.append('#%d %s 操作数 gxw=%s csv=%s' % (gi, c_op, g_operands, c_operands))
            else:
                same += 1
            gi += 1
            ci += 1
            continue
        if g_class is None and c_class is None:
            # 应用指令: 类别由操作数体现,操作数必须一致
            if g_operands != c_operands:
                differences.append('#%d %s 操作数 gxw=%s csv=%s' % (gi, c_op, g_operands, c_operands))
            else:
                same += 1
            gi += 1
            ci += 1
            continue
        # 一边是已知类别一边是应用指令: 说明某边的类别识别或条数有偏差
        differences.append('#%d 类别错位 gxw=%s csv=%s(%s)'
                           % (gi, g_class or '应用指令', c_class or c_op, c_operands))
        break
    return same, differences, len(gxw_items), len(csv_items)


def check(gxw_path, csv_path):
    block = load_program_block(gxw_path)
    elements = decode_elements(block)
    devices = decode_devices(block)
    instructions = csv_instructions(csv_path)

    # 类别计数对账:已知类别逐个比,应用指令(比较/传送/算术)按总数比
    class_count = {}
    instruction_elements = 0
    for index, element in enumerate(elements):
        name = class_name(element, index)
        if name:
            class_count[name] = class_count.get(name, 0) + 1
        else:
            instruction_elements += 1
    csv_count = {}
    csv_instructions_count = 0
    for step, op, args in instructions:
        if op in CLASSES.values():
            csv_count[op] = csv_count.get(op, 0) + 1
        else:
            csv_instructions_count += 1

    print('gxw 程序块: %d 个元素, %d 条软元件引用' % (len(elements), len(devices)))
    print('CSV 指令表: %d 条指令' % len(instructions))
    print()
    print('[1] 梯形图结构逐条对账(类别 + 操作数)')
    same, differences, gxw_total, csv_total = compare_structure(elements, instructions)
    print('    元素 %d vs 指令 %d, 逐条一致 %d 条, 差异 %d 处'
          % (gxw_total, csv_total, same, len(differences)))
    for item in differences[:12]:
        print('      %s' % item)

    print()
    print('[2] 各类别条数(梯形图元素 vs CSV 指令)')
    mismatch = []
    for name in sorted(set(class_count) | set(csv_count)):
        a, b = class_count.get(name, 0), csv_count.get(name, 0)
        flag = '' if a == b else '   <<< 不一致'
        if a != b:
            mismatch.append(name)
        print('    %-8s gxw=%-5d csv=%-5d%s' % (name, a, b, flag))
    flag = '' if instruction_elements == csv_instructions_count else '   <<< 不一致'
    if instruction_elements != csv_instructions_count:
        mismatch.append('应用指令')
    print('    %-8s gxw=%-5d csv=%-5d%s' % ('应用指令', instruction_elements, csv_instructions_count, flag))

    print()
    print('[3] 软元件引用顺序')
    csv_devices = []
    for step, op, args in instructions:
        for token in args:
            for match in re.finditer(r'([XYMSTCDK])(\d+)', token):
                area, number = match.group(1), int(match.group(2))
                if area in ('X', 'Y'):
                    number = int(str(number), 8)
                csv_devices.append((area, number))
    matched, position = align_prefix(devices, csv_devices)
    print('    %d 条引用在 gxw 里按同样顺序出现 (%.1f%% of CSV)'
          % (matched, 100.0 * matched / max(1, len(csv_devices))))
    if position is not None:
        print('    首个错位: CSV 第 %d 条 %s' % (position, csv_devices[position]))

    ok = (not differences and not mismatch and position is None)
    print()
    print('判定: %s' % ('通过(整份程序逐条一致)' if ok else '不通过'))
    return ok


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    return 0 if check(sys.argv[1], sys.argv[2]) else 1


if __name__ == '__main__':
    sys.exit(main())
