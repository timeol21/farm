"""Convenience static dependency queries; wiring truth remains in get_block."""
import collections
from .project import page


def trace(project, device, direction='downstream', depth=1, offset=0, limit=50):
    if direction not in {'upstream', 'downstream'} or type(depth) is not int or not 1 <= depth <= 5:
        raise ValueError('direction upstream/downstream; depth 1..5')
    by_element = collections.defaultdict(list)
    for ref in project.references:
        if ref['element_id']:
            by_element[ref['element_id']].append(ref)
    edges = {}
    for b in project.blocks.values():
        previous = collections.defaultdict(list)
        for edge in b['topology']['adjacency']:
            previous[edge['to']].append(edge['from'])
        for e in b['elements']:
            if e['kind'] != 'instruction':
                continue
            destinations = {r['device'] for r in by_element[e['id']] if r['role'] in {'write', 'read_write', 'write_range_start', 'write_range_end'}}
            sources = {(r['device'], 'data', e['id']) for r in by_element[e['id']] if r['role'] in {'read', 'read_write'}}
            seen, queue = set(), list(previous[e['id']])
            while queue:
                i = queue.pop()
                if i in seen:
                    continue
                seen.add(i)
                sources.update((r['device'], 'control', i) for r in by_element[i])
                queue.extend(previous[i])
            sources.update((d, 'stl_scope', None) for d in b['execution_context']['stl_guard'] or [])
            for src, kind, source_element in sources:
                for dst in destinations:
                    key = (src, dst, e['id'], source_element, kind)
                    edges[key] = {'from': src, 'to': dst, 'kind': kind, 'source_element': source_element,
                                  'target_element': e['id'], 'block_id': b['id'], 'opcode': e.get('opcode')}
    device = device.strip().upper()
    key, dest = ('from', 'to') if direction == 'downstream' else ('to', 'from')
    seen, frontier, found = {device}, {device}, []
    ordered = sorted(edges.values(), key=lambda e: (e['block_id'], e['target_element'], e['from'], e['to'], e['kind'], e['source_element'] or ''))
    for level in range(1, depth+1):
        matches = [dict(e, depth=level) for e in ordered if e[key] in frontier]
        found.extend(matches)
        frontier = {e[dest] for e in matches} - seen
        seen.update(frontier)
        if not frontier:
            break
    return dict(page(found, offset, limit),
                semantics='POTENTIAL static influence, not proof of activation. Paths can be mutually exclusive or span scans/subroutines. Use get_block for exact parallel wiring and contact polarity.',
                exclusions='Unknown-access ADPRW/RS2/CRC buffer effects, dynamic addressing, and CALL caller-to-callee guards are NOT expanded.',
                coverage='partial_semantics')
