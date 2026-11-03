"""Snapshot-based, read-only project loader and query model."""
import base64
import collections
import hashlib
import io
import json
import pathlib
import sqlite3
import tempfile
import zipfile
import xml.etree.ElementTree as ET
from .syntax import decode, fields, walk
from .model import normalize, topology, roles, referenced_devices, DEV_CODES


def digest(value):
    if not isinstance(value, bytes):
        value = json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    return hashlib.sha256(value).hexdigest()


def guid(value):
    return value.lower().replace('_guid/', '').strip('{}')


def json_value(value):
    if isinstance(value, bytes):
        return {'encoding': 'base64', 'data': base64.b64encode(value).decode('ascii'), 'bytes': len(value)}
    return value


def page(items, offset=0, limit=50):
    if type(offset) is not int or offset < 0 or type(limit) is not int or not 1 <= limit <= 200:
        raise ValueError('offset must be >= 0; limit must be 1..200')
    end = min(offset + limit, len(items))
    return {'total': len(items), 'offset': offset, 'items': items[offset:end],
            'next_offset': end if end < len(items) else None}


def logical_signature(block):
    """Compare conditional net structure, instructions, operands, and statement text.

    IDs, draw coordinates, dimensions, wire subdivision and source order do not
    affect the signature. Unresolved topology is never eligible for deduplication.
    """
    if block['issues']:
        return None
    g = block['topology']
    elements = {e['id']: e for e in block['elements']}
    incoming = collections.defaultdict(list)
    for e in g['contacts']:
        incoming[e['to']].append(e)
    cache, visiting = {}, set()

    def net_signature(net):
        if net == g['left_rail']:
            return 'LEFT_RAIL'
        if net in cache:
            return cache[net]
        if net in visiting:
            raise ValueError('Cyclic contact graph')
        visiting.add(net)
        parts = []
        for edge in incoming[net]:
            parts.append(digest([net_signature(edge['from']), fields(elements[edge['element']]['raw'])['s']]))
        result = digest(sorted(parts))
        visiting.remove(net)
        cache[net] = result
        return result

    try:
        contacts = sorted(digest([net_signature(e['from']), fields(elements[e['element']]['raw'])['s']]) for e in g['contacts'])
        sinks = sorted(digest([net_signature(e['input']), fields(elements[e['element']]['raw'])['s']]) for e in g['sinks'])
    except ValueError:
        return None
    output_order = [fields(e['raw'])['s'] for e in sorted(block['elements'], key=lambda e: (e['pos'][1], e['pos'][0])) if e['kind'] == 'instruction']
    return digest({'kind': block['kind'], 'contacts': contacts, 'sinks': sinks, 'output_order': output_order,
                   'comments': [c['text'] for c in block['comments']],
                   'pointers': [p['operand']['raw'] for p in block['pointers']]})


class Project:
    def __init__(self, path):
        self.path = str(pathlib.Path(path).resolve())
        self.archive_bytes = pathlib.Path(path).read_bytes()
        self.sha256 = digest(self.archive_bytes)
        self.tables, self.manifest, self.members = {}, [], {}
        self.programs, self.comments, self.issues = [], [], []
        with zipfile.ZipFile(io.BytesIO(self.archive_bytes)) as z, tempfile.TemporaryDirectory(prefix='gx3_read_') as temp:
            for i, member in enumerate(z.infolist()):
                if member.is_dir():
                    continue
                if member.filename in self.members:
                    raise ValueError('Duplicate archive entry: ' + member.filename)
                if member.file_size > 128 * 1024 * 1024:
                    raise ValueError('Archive member exceeds 128 MiB safety limit')
                data = z.read(member)
                self.members[member.filename] = data
                entry = {'member': member.filename, 'bytes': len(data), 'sha256': digest(data), 'format': 'opaque'}
                if data.startswith(b'SQLite format 3\x00'):
                    entry['format'] = 'sqlite'
                    file = pathlib.Path(temp) / f'{i}.db'
                    file.write_bytes(data)
                    con = sqlite3.connect(file.as_uri() + '?mode=ro', uri=True)
                    con.row_factory = sqlite3.Row
                    try:
                        check = con.execute('PRAGMA integrity_check').fetchone()[0]
                        entry['integrity_check'] = check
                        if check != 'ok':
                            raise ValueError(f'SQLite integrity failed: {member.filename}: {check}')
                        tables = {}
                        for row in con.execute("SELECT name,sql FROM sqlite_master WHERE type='table'"):
                            name = row['name']
                            quoted = '"' + name.replace('"', '""') + '"'
                            rows = [{k: json_value(v) for k, v in dict(r).items()} for r in con.execute('SELECT * FROM ' + quoted)]
                            tables[name] = {'schema': row['sql'], 'rows': rows}
                        self.tables[member.filename] = tables
                        entry['tables'] = {name: len(t['rows']) for name, t in tables.items()}
                    finally:
                        con.close()
                self.manifest.append(entry)
        self.cpu = ET.fromstring(self.members['Config.xml']).find('Config').get('Unit')
        if self.cpu != 'FX5U':
            raise ValueError(f'Unvalidated CPU {self.cpu}: this version supports the supplied FX5U encoding only')
        self._load_comments()
        self._load_programs()
        self._index()

    def _load_comments(self):
        for member, tables in self.tables.items():
            if 'COMMENT_DATA' in tables:
                devices = {r['SEQ']: r for r in tables.get('DEVICE_DATA', {}).get('rows', [])}
                for r in tables['COMMENT_DATA']['rows']:
                    d = devices.get(r['DeviceSEQ'], {})
                    family = DEV_CODES.get(d.get('DevCode'))
                    n = d.get('DevNoLow')
                    name = None
                    if family and n is not None and not any(d.get(k, 0) for k in ['ExtCode', 'ExtNo', 'IsLocal', 'DevNoHigh', 'BitNo']):
                        name = family + (format(n, 'o') if family in {'X', 'Y'} else str(n))
                    self.comments.append({'id': f'{member}:COMMENT_DATA:{r["SEQ"]}', 'kind': 'device',
                                          'device': name, 'text': r['CmtData'], 'comment_number': r['CmtNo'],
                                          'deleted_flag': r['DelFlag'], 'raw_device': d, 'raw_comment': r,
                                          'source': {'member': member, 'table': 'COMMENT_DATA', 'key': r['SEQ']}})
            # Preserve label comments and all textual label values without assuming
            # a ColumnID is a comment. The original schema remains queryable.
            for table in ['CommentDataTbl', 'ColumnDataTbl', 'RowAdditionalTbl', 'LabelAdditionalTbl']:
                for i, row in enumerate(tables.get(table, {}).get('rows', [])):
                    for column in ['CommentStrValue', 'ColumnStrValue', 'Value']:
                        if isinstance(row.get(column), str) and row[column]:
                            self.comments.append({'id': f'{member}:{table}:{i}:{column}',
                                                  'kind': 'label_comment' if table == 'CommentDataTbl' else 'label_text_unclassified',
                                                  'device': None, 'text': row[column], 'raw_row': row,
                                                  'source': {'member': member, 'table': table, 'row_index': i, 'column': column}})
        self.comments_by_device = collections.defaultdict(list)
        for c in self.comments:
            if c.get('device') and c.get('deleted_flag') in (None, 0):
                self.comments_by_device[c['device']].append(c)

    def _load_programs(self):
        step_sets = []
        for member, tables in self.tables.items():
            if 'T_Block' in tables and tables['T_Block']['rows']:
                offset, mapping = 0, {}
                for r in sorted(tables['T_Block']['rows'], key=lambda r: r['Pos']):
                    mapping[guid(r['BlockID'])] = {'start': offset, 'size': r['StepSize'], 'source': member, 'raw': r}
                    offset += r['StepSize']
                step_sets.append(mapping)
        candidates = []
        for member, tables in self.tables.items():
            if 'LadderBlocks' not in tables:
                continue
            blocks, active_statement, active_stl, active_pointer = [], None, None, None
            for i, row in enumerate(sorted(tables['LadderBlocks']['rows'], key=lambda r: (r['pos'], r['id']))):
                bid = f'{member}:b{i+1}'
                try:
                    parsed = decode(row['data'])
                    b = normalize(parsed['root'], bid)
                except (ValueError, KeyError, IndexError) as error:
                    parsed = None
                    b = {'kind': 'unresolved', 'elements': [], 'comments': [], 'pointers': [], 'verticals': [],
                         'issues': [str(error)], 'dimensions': None}
                if row['blocktype'] == 5:
                    b['kind'] = 'end'
                b.update(id=bid, ordinal=i+1, source={'member': member, 'table': 'LadderBlocks', 'key': row['id'], 'pos': row['pos']},
                         raw_row=row, parsed=parsed, step=None)
                for steps in step_sets:
                    if guid(row['id']) in steps:
                        if b['step'] is not None:
                            b['issues'].append('Multiple step tables match block')
                        b['step'] = steps[guid(row['id'])]
                if b['kind'] == 'statement':
                    active_statement = '\n'.join(c['text'] for c in b['comments'])
                if b['pointers']:
                    active_pointer = [p['operand']['text'] for p in b['pointers']]
                ops = {e.get('opcode') for e in b['elements']}
                if 'STL' in ops:
                    active_stl = [a['text'] for e in b['elements'] if e.get('opcode') == 'STL' for a in e['operands']]
                elif 'RETSTL' in ops or 'FEND' in ops:
                    active_stl = None
                b['section'] = active_statement
                b['execution_context'] = {'stl_guard': active_stl, 'subroutine_entry': active_pointer,
                                          'note': 'Lexical STL/subroutine scope; calls and scan order affect execution. Not a PLC simulator.'}
                b['topology'] = topology(b)
                b['logical_signature'] = logical_signature(b)
                blocks.append(b)
                if 'SRET' in ops:
                    active_pointer = None
            candidates.append({'member': member, 'blocks': blocks,
                               'step_matches': sum(b['step'] is not None for b in blocks), 'aliases': []})
        # Prefer actual GUID-matched step metadata, not file size.
        candidates.sort(key=lambda p: (-p['step_matches'], p['member']))
        for candidate in candidates:
            signatures = [b['logical_signature'] for b in candidate['blocks']]
            match = next((p for p in self.programs if None not in signatures and signatures == [b['logical_signature'] for b in p['blocks']]), None)
            if match is not None:
                match['aliases'].append({'member': candidate['member'], 'reason': 'All ordered block conditional-net signatures, instructions, output order, operands, pointers and comments match',
                                         'block_count': len(signatures), 'verified_blocks': len(signatures)})
            else:
                candidate['id'] = f'program{len(self.programs)+1}'
                self.programs.append(candidate)
        self.blocks = {}
        for program in self.programs:
            for b in program['blocks']:
                b['program'] = program['id']
                self.blocks[b['id']] = b
                for i, c in enumerate(b['comments']):
                    self.comments.append({'id': f'{b["id"]}:comment{i}', 'kind': 'statement', 'device': None,
                                          'text': c['text'], 'block_id': b['id'], 'source': dict(b['source'], ast_path=c['ast_path'])})
                for e in b['elements']:
                    for a in e['operands']:
                        a['comments'] = [dict(id=c['id'], device=c['device'], text=c['text'], comment_number=c['comment_number'])
                                         for d in referenced_devices(a) for c in self.comments_by_device.get(d, [])]
                self.issues.extend({'block_id': b['id'], 'issue': s} for s in b['issues'])

    def _index(self):
        self.references, self.elements = [], {}
        self.pointer_entries = collections.defaultdict(list)
        for b in self.blocks.values():
            for pointer in b['pointers']:
                self.pointer_entries[pointer['operand']['text']].append(b['id'])
                self.references.append({'device': pointer['operand']['text'], 'role': 'entry', 'block_id': b['id'], 'element_id': None,
                                        'operand': pointer['operand']['text'], 'source': b['source'], 'step': b['step']['start'] if b['step'] else None})
            for e in b['elements']:
                self.elements[e['id']] = e
                access = roles(e)
                e['operand_access'] = access
                for i, (arg, role) in enumerate(zip(e['operands'], access)):
                    for d in referenced_devices(arg):
                        self.references.append({'device': d, 'role': role, 'block_id': b['id'], 'element_id': e['id'],
                                                'operand_index': i, 'operand': arg['text'], 'opcode': e.get('opcode'),
                                                'section': b['section'], 'pos': e['pos'], 'source': b['source'],
                                                'step': b['step']['start'] if b['step'] else None, 'match': 'explicit_operand'})
                    if arg['kind'] == 'modified_device' and arg.get('text'):
                        self.references.append({'device': arg['text'].upper(), 'role': role, 'block_id': b['id'], 'element_id': e['id'],
                                                'operand_index': i, 'operand': arg['text'], 'opcode': e.get('opcode'),
                                                'section': b['section'], 'pos': e['pos'], 'source': b['source'],
                                                'step': b['step']['start'] if b['step'] else None, 'match': 'qualified_operand'})
        self._add_implicit_references()

    def _add_implicit_references(self):
        # Range membership is separately labelled; never confused with explicit operands.
        extra = []
        for b in self.blocks.values():
            for e in b['elements']:
                args, op = e['operands'], e.get('opcode', '').split('__')[0]
                if op.endswith('P'):
                    op = op[:-1]
                ranges = []
                if op == 'ZRST' and len(args) == 2 and all(a['kind'] == 'device' for a in args):
                    a, z = args
                    if a['family'] == z['family'] and a['family'] not in {'X', 'Y'}:
                        ranges.append((a, a['raw_address'], z['raw_address'], 'write', 'ZRST inclusive range'))
                for i, arg in enumerate(args):
                    if arg['kind'] == 'modified_device' and arg['mode'] == 'digit_group':
                        a = arg['base']
                        if a['family'] not in {'X', 'Y'}:
                            ranges.append((a, a['raw_address'], a['raw_address']+4*int(arg['modifier']['value'])-1, e['operand_access'][i], 'K digit group'))
                    width = 2 if (op == 'DINC' and i == 0) or (op in {'MUL', 'DIV'} and i == 2) else 1
                    if arg['kind'] == 'device' and width > 1:
                        ranges.append((arg, arg['raw_address'], arg['raw_address']+width-1, e['operand_access'][i], 'multiword operand'))
                    if op == '$MOV' and i == 1 and arg['kind'] == 'device' and args[0].get('encoding') == 'String':
                        text = args[0]['value']
                        if text.isascii():
                            width = (len(text.encode('ascii')) + 2)//2
                            ranges.append((arg, arg['raw_address'], arg['raw_address']+width-1, 'write', 'ASCII string including terminator'))
                for a, start, end, role, basis in ranges:
                    if not 0 <= end-start <= 65535:
                        self.issues.append({'block_id': b['id'], 'issue': 'Unsupported range length', 'element': e['id']})
                        continue
                    for n in range(start, end+1):
                        extra.append({'device': a['family'] + str(n), 'role': role, 'block_id': b['id'], 'element_id': e['id'],
                                      'operand': a['text'], 'opcode': e['opcode'], 'section': b['section'], 'pos': e['pos'],
                                      'step': b['step']['start'] if b['step'] else None,
                                      'match': 'range_member', 'basis': basis, 'source': b['source']})
        self.references.extend(extra)

    def overview(self):
        elements = list(self.elements.values())
        unknown_access = collections.Counter(e.get('opcode') for e in elements if 'unknown' in e.get('operand_access', []))
        return {'source_path': self.path, 'snapshot_sha256': self.sha256, 'cpu': self.cpu,
                'programs': [{'id': p['id'], 'member': p['member'], 'block_count': len(p['blocks']), 'step_matches': p['step_matches'], 'aliases': p['aliases']} for p in self.programs],
                'counts': {'blocks': len(self.blocks), 'block_types': dict(collections.Counter(b['kind'] for b in self.blocks.values())),
                           'instructions_and_contacts': sum(e['kind'] in {'contact', 'instruction'} for e in elements),
                           'comments': len(self.comments), 'comment_types': dict(collections.Counter(c['kind'] for c in self.comments)),
                           'unique_referenced_devices': len({r['device'] for r in self.references}), 'reference_records': len(self.references)},
                'issues': self.issues, 'unclassified_access_opcodes': dict(unknown_access),
                'verification': {'syntax': 'Every string length, placeholder and structure delimiter is checked; no unconsumed data accepted',
                                 'topology': 'Static connectivity derived from explicit grid wires and conditional contacts; visual verification pending',
                                 'snapshot': 'Saved project only; unsaved GX Works3 changes and live PLC state are not included',
                                 'comments': 'All device comment rows and ladder statements retained, including languages/deletion flags; label text preserved separately',
                                 'limitations': ['Opaque project metadata and binary parameter files retained as original bytes, not semantically decoded',
                                                'ADPRW/RS2/CRC operand access and variable buffer effects are unresolved; explicit operands remain searchable',
                                                'Program name association in _Project.txc is unresolved; database member is the stable program identifier',
                                                'STL scope and CALL links are exposed, not executed; no scan-cycle or interlock simulation']}}

    def block_view(self, block_id, raw=False):
        if block_id not in self.blocks:
            raise ValueError('Unknown block_id; use list_blocks')
        b = self.blocks[block_id]
        if raw:
            return b
        def strip(v):
            if isinstance(v, dict):
                return {k: strip(x) for k, x in v.items() if k not in {'raw', 'raw_row', 'parsed', 'logical_signature'}}
            if isinstance(v, list):
                return [strip(x) for x in v]
            return v
        return strip(b)

    def list_blocks(self, program=None, query='', offset=0, limit=50):
        rows = []
        for b in self.blocks.values():
            if program and b['program'] != program:
                continue
            texts = [b['section'] or ''] + [str(e.get('opcode', '')) + ' ' + ' '.join(a.get('text') or '?' for a in e['operands']) for e in b['elements']]
            if query and query.casefold() not in '\n'.join(texts).casefold():
                continue
            rows.append({'block_id': b['id'], 'ordinal': b['ordinal'], 'kind': b['kind'], 'section': b['section'],
                         'step': b['step']['start'] if b['step'] else None, 'element_count': len(b['elements']),
                         'summary': texts[1:], 'issues': b['issues']})
        return page(rows, offset, limit)

    def search_device(self, device, offset=0, limit=50):
        device = device.strip().upper()
        result = page([r for r in self.references if r['device'] == device], offset, limit)
        result.update(device=device, comments=self.comments_by_device.get(device, []),
                      coverage_note='Explicit operands plus supported fixed ranges. Unknown-access instructions may have additional indirect/buffer effects; see project_info.')
        return result

    def search_comments(self, query='', kind=None, offset=0, limit=50):
        return page([c for c in self.comments if (not kind or c['kind'] == kind) and query.casefold() in (c.get('text') or '').casefold()], offset, limit)

    def device_relations(self, device, offset=0, limit=50):
        device = device.strip().upper()
        hits = [r for r in self.references if r['device'] == device]
        result = []
        seen = set()
        for hit in hits:
            eid = hit['element_id']
            if eid is None or eid in seen:
                continue
            seen.add(eid)
            b, e = self.blocks[hit['block_id']], self.elements[eid]
            adj = b['topology']['adjacency']
            direct_up = sorted({x['from'] for x in adj if x['to'] == eid})
            direct_down = sorted({x['to'] for x in adj if x['from'] == eid})
            def describe(ids):
                return [{'element_id': i, 'opcode': self.elements[i].get('opcode'), 'operands': [a['text'] for a in self.elements[i]['operands']]} for i in ids]
            ancestors, queue = set(), list(direct_up)
            while queue:
                other = queue.pop()
                if other in ancestors:
                    continue
                ancestors.add(other)
                queue.extend(x['from'] for x in adj if x['to'] == other)
            result.append({'block_id': b['id'], 'element_id': eid, 'opcode': e.get('opcode'), 'step': hit['step'],
                           'operands': e['operands'], 'operand_access': e['operand_access'],
                           'upstream_adjacent': describe(direct_up), 'downstream_adjacent': describe(direct_down),
                           'all_upstream_contacts': describe(sorted(ancestors)), 'execution_context': b['execution_context'],
                           'call_entries': [target for a in e['operands'] for target in self.pointer_entries.get(a['text'], [])] if e.get('opcode') in {'CALL', 'CALLP'} else [],
                           'note': 'Upstream is graph reachability, NOT an all-AND condition. Use get_block.topology nets to retain parallel branches.'})
        return page(result, offset, limit)

    def raw_rows(self, member, table, offset=0, limit=50):
        try:
            t = self.tables[member][table]
        except KeyError:
            raise ValueError('Unknown member/table; use source_inventory') from None
        return dict(page(t['rows'], offset, limit), schema=t['schema'])

    def raw_member(self, member, offset=0, length=16384):
        if member not in self.members:
            raise ValueError('Unknown archive member')
        if type(offset) is not int or offset < 0 or type(length) is not int or not 1 <= length <= 65536:
            raise ValueError('offset >= 0; length 1..65536')
        data = self.members[member]
        end = min(offset+length, len(data))
        return {'member': member, 'bytes': len(data), 'sha256': digest(data), 'offset': offset,
                'encoding': 'base64', 'data': base64.b64encode(data[offset:end]).decode('ascii'),
                'next_offset': end if end < len(data) else None}
