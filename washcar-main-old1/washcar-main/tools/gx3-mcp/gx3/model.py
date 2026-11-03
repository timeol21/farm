"""Structural normalization and electrical-net topology for the observed FX5U format."""
import collections
import re
from .syntax import fields, pair, walk


COMPARISONS = {'=', '<>', '<', '>', '<=', '>='}
DEV_CODES = {1: 'M', 2: 'SM', 8: 'S', 16: 'X', 17: 'Y', 32: 'D',
             33: 'SD', 66: 'T', 70: 'C', 101: 'P'}


def device_name(family, number):
    family = 'S' if family == 'SfcS' else family
    # LDDB stores FX X/Y display digits as a hexadecimal-coded value.
    # This is NOT the physical octal address used by the DC database.
    return family + (format(number, 'X') if family in {'X', 'Y', 'B', 'W'} else str(number))


def device_operand(node):
    f = fields(node)
    family, number = f['s'], int(f['a'])
    name = device_name(family, number)
    return {'kind': 'device', 'text': name, 'device': name,
            'family': 'S' if family == 'SfcS' else family,
            'raw_address': number, 'raw_family': family, 'raw': node}


def operand(node):
    f = fields(node)
    kind = node['type']
    if kind == 'd':
        return device_operand(node)
    if kind == 'c':
        s, v = f.get('s'), f.get('v')
        if s == 'String':
            text = f.get('t', '"' + v + '"')
        elif s in {'K_1', 'K', 'Ks'}:
            text = 'K' + v
        elif s in {'H_1', 'H'}:
            text = 'H' + format(int(v), 'X')
        elif s == 'Dots':
            text = '.' + format(int(v), 'X')
        else:
            return {'kind': 'unresolved', 'text': None, 'raw': node}
        return {'kind': 'constant', 'text': text, 'value': v, 'encoding': s, 'raw': node}
    if kind == 'M':
        base, modifier = operand(f['b']), operand(f['m'])
        if modifier.get('encoding') == 'Ks':
            text = modifier['text'] + base['text']
            mode = 'digit_group'
        elif modifier.get('encoding') == 'Dots':
            text = base['text'] + modifier['text']
            mode = 'bit_select'
        else:
            text, mode = None, 'unresolved'
        return {'kind': 'modified_device', 'text': text, 'mode': mode,
                'base': base, 'modifier': modifier, 'raw': node}
    return {'kind': 'unresolved', 'text': None, 'raw': node}


def referenced_devices(arg):
    if arg['kind'] == 'device':
        return [arg['device']]
    if arg['kind'] == 'modified_device':
        return referenced_devices(arg['base'])
    return []


def normalize(root, block_id):
    out = {'kind': 'unknown', 'elements': [], 'verticals': [], 'pointers': [],
           'comments': [], 'issues': [], 'dimensions': None}
    if root['type'] == 'st':
        out['kind'] = 'statement'
    elif root['type'] == 'end':
        out['kind'] = 'end'
    elif root['type'] == 'cb':
        out['kind'] = 'ladder'
    else:
        out['issues'].append('Unknown root: ' + root['type'])
    for path, node in walk(root):
        f = fields(node)
        if node['type'] == 'c' and 't' in f and 's' not in f:
            out['comments'].append({'text': f['t'], 'style': f.get('st'), 'ast_path': path})
    for p in fields(fields(root).get('pg')).get('ps', []):
        if fields(p).get('s'):
            out['pointers'].append({'operand': operand(fields(p)['s']), 'pos': pair(fields(p)['pos'])})
    fg = fields(fields(root).get('fg'))
    if fg:
        out['dimensions'] = pair(fg['dim'], 'x')
    for i, raw in enumerate(fg.get('es', [])):
        f = fields(raw)
        item = {'id': f'{block_id}:e{i}', 'source_index': i, 'pos': pair(f['pos']),
                'ast_path': f'$.fg.es[{i}]', 'raw': raw, 'operands': []}
        s = f['s']
        if s == '-':
            item.update(kind='wire', width=1)
        elif isinstance(s, dict) and s['type'] == 'ce':
            ce = fields(s)
            op = ce['op']
            of = fields(op)
            args = [operand(a) for a in ce.get('args', [])]
            item.update(kind='contact' if op['type'] == 'ct' else 'instruction',
                        opcode=of['op'], display_opcode={'c': 'OUT', 'RETSTL': 'RET'}.get(of['op'], re.sub(r'__\d+$', '', of['op'])),
                        activation=of.get('ct'), operands=args,
                        operand_types=[fields(a).get('vt') for a in of.get('as', [])])
            item['width'] = len(args) + 1 if of['op'] in COMPARISONS else 1
            if item['kind'] == 'contact':
                item['contact_mode'] = ({'p': 'rising_edge', 'f': 'falling_edge'}.get(of.get('ct'))
                                        or {'a': 'normally_open', 'b': 'normally_closed', 'ME': 'rising_edge', 'f': 'inversion'}.get(of['op'], 'comparison' if of['op'] in COMPARISONS else 'unresolved'))
            if any(a.get('text') is None for a in args):
                out['issues'].append('Unresolved operand: ' + item['id'])
            if len(args) != len(of.get('as', [])):
                out['issues'].append('Operand signature mismatch: ' + item['id'])
        elif isinstance(s, dict) and s['type'] in {'src', 'dst'}:
            item.update(kind='continuation_' + s['type'], continuation=fields(s)['n'], width=1)
        else:
            item.update(kind='unknown', width=None)
            out['issues'].append('Unknown element: ' + item['id'])
        out['elements'].append(item)
    out['verticals'] = [pair(fields(v)['pos']) for v in fg.get('vs', [])]
    return out


def topology(block):
    """Union ONLY explicit wires/verticals; contacts remain directed, conditional edges.

    No horizontal edges are invented across blank cells. Output instructions are
    sinks, not conductive contacts. Left rail is shared, never the output rail.
    """
    parent = {}

    def find(p):
        p = tuple(p)
        parent.setdefault(p, p)
        if parent[p] != p:
            parent[p] = find(parent[p])
        return parent[p]

    def union(a, b):
        a, b = find(a), find(b)
        if a != b:
            parent[max(a, b)] = min(a, b)

    nodes = block['elements']
    continuation = collections.defaultdict(dict)
    wire_segments = []
    for e in nodes:
        x, y = e['pos']
        find((x, y))
        if e['kind'] == 'wire':
            union((x, y), (x + 1, y))
            wire_segments.append({'from': [x, y], 'to': [x+1, y], 'source': e['id']})
        elif e['kind'] == 'contact':
            find((x + e['width'], y))
        elif e['kind'].startswith('continuation_'):
            continuation[e['continuation']][e['kind']] = (x, y)
    for i, (x, y) in enumerate(block['verticals']):
        union((x, y - 1), (x, y))
        wire_segments.append({'from': [x, y-1], 'to': [x, y], 'source': f'$.fg.vs[{i}]'})
    excluded_rails = set()
    for key, pair_ in continuation.items():
        if set(pair_) == {'continuation_src', 'continuation_dst'}:
            src, dst = pair_['continuation_src'], pair_['continuation_dst']
            # dst occupies one cell; its output resumes at x+1.
            union(src, (dst[0] + 1, dst[1]))
            excluded_rails.add(dst)
        else:
            block['issues'].append('Unmatched continuation: ' + key)
    rail = (-1, -1)
    find(rail)
    for point in list(parent):
        if point[0] == 0 and point not in excluded_rails:
            union(rail, point)
    groups = collections.defaultdict(list)
    for p in parent:
        groups[find(p)].append(list(p))
    ids = {r: f'n{i}' for i, r in enumerate(sorted(groups))}
    net = lambda p: ids[find(p)]
    edges, sinks = [], []
    for e in nodes:
        x, y = e['pos']
        if e['kind'] == 'contact':
            edges.append({'element': e['id'], 'from': net((x, y)), 'to': net((x+e['width'], y))})
        elif e['kind'] == 'instruction':
            sinks.append({'element': e['id'], 'input': net((x, y))})
    reachable = {net(rail)}
    while True:
        after = reachable | {e['to'] for e in edges if e['from'] in reachable}
        if after == reachable:
            break
        reachable = after
    dangling = [e['element'] for e in edges if e['from'] not in reachable]
    dangling += [e['element'] for e in sinks if e['input'] not in reachable]
    if dangling:
        block['issues'].append(f'{len(dangling)} elements not reachable from left rail')
    upstream = collections.defaultdict(list)
    downstream = collections.defaultdict(list)
    for edge in edges:
        downstream[edge['from']].append(edge['element'])
        upstream[edge['to']].append(edge['element'])
    for sink in sinks:
        downstream[sink['input']].append(sink['element'])
    adjacency = []
    for n in ids.values():
        for a in upstream[n]:
            for b in downstream[n]:
                adjacency.append({'from': a, 'to': b, 'net': n})
    return {'status': 'needs_review' if block['issues'] else 'structurally_resolved',
            'verification': 'reverse_engineered; GX Works3 visual review pending',
            'left_rail': net(rail), 'nets': [{'id': ids[k], 'points': sorted(v)} for k, v in sorted(groups.items())],
            'wire_segments': wire_segments, 'contacts': edges, 'sinks': sinks,
            'adjacency': adjacency, 'unreachable_elements': dangling,
            'meaning': 'Static rung wiring, not runtime values or proof of execution. Vertical wires merge nets; contact edges retain AND/OR paths.'}


def roles(e):
    """Conservative operand access; unknown operations stay unknown, never guessed."""
    n = len(e['operands'])
    if e['kind'] == 'contact':
        return ['read'] * n
    op = e.get('opcode', '').split('__')[0]
    if op.endswith('P') and op not in {'STL', 'SRET'}:
        op = op[:-1]
    if op in {'c', 'SET', 'RST'}:
        return ['write'] * n
    if op in {'INC', 'DINC', 'DEC', 'DDEC'}:
        return ['read_write'] * n
    if op in {'OUT', 'OUTH'}:
        return ['write'] + ['read'] * (n - 1)
    if op in {'MOV', '$MOV'}:
        return ['read', 'write'] if n == 2 else ['unknown'] * n
    if op in {'ADD', 'SUB', 'MUL', 'DIV'}:
        return ['read', 'read', 'write'] if n == 3 else ['unknown'] * n
    if op == 'ZRST':
        return ['write_range_start', 'write_range_end']
    if op == 'CALL':
        return ['call_target'] * n
    if op == 'STL':
        return ['step_guard'] * n
    return ['unknown'] * n
