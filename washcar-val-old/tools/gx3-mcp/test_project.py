"""Regression checks for the supplied snapshot and structurally tricky cases."""
import collections
import json
import pathlib
import sys
import unittest
from gx3.project import Project, logical_signature, page, digest
from gx3.syntax import decode, fields, walk, FormatError
from gx3.model import normalize, topology, operand


ROOT = pathlib.Path(__file__).resolve().parent
# 工程按厂商目录拆分: 默认跑驿公里; 换厂商用 `python test_project.py <工程路径>` 覆盖
SOURCE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 \
    else ROOT / 'projects' / 'yigongli' / 'source_snapshot.gx3'


def pack(strings, body):
    sizes = [str(len(s.encode('utf-16-le')) // 2) for s in strings]
    return ':'.join(['V1', str(len(strings))] + sizes + strings + [body])


class SyntaxTests(unittest.TestCase):
    def test_delimiters_unicode_and_hash_in_strings(self):
        text = '中文:包含{结构}[符号]#以及😀\n第二行'
        root = decode(pack([text], 'st{st=c{t=#:st=s}:dim=0x1}'))['root']
        self.assertEqual(fields(fields(root)['st'])['t'], text)

    def test_nested_operand_order_without_opcode_whitelist(self):
        data = pack(['NEW_INSTRUCTION', 'K_1', 'M', 'Ks', 'D'],
                    'cb{fg=fg{dim=5x1:es=[e{s=ce{op=cl{op=#:ct=a:as=[as{vt=A16}:as{vt=A16}:as{vt=A16}]}:args=[c{s=#:v=7:si=s}:M{b=d{s=#:a=250:vt=nn}:m=c{s=#:v=4}}:d{s=#:a=1:vt=nn}]}:pos=0,0}]}}')
        b = normalize(decode(data)['root'], 'test')
        e = b['elements'][0]
        self.assertEqual(e['opcode'], 'NEW_INSTRUCTION')
        self.assertEqual([a['text'] for a in e['operands']], ['K7', 'K4M250', 'D1'])

    def test_corrupt_length_and_placeholder_rejected(self):
        for data in ['V1:1:2:A:st{st=c{t=#:st=s}:dim=0x1}', 'V1:0:st{st=c{t=#:st=s}:dim=0x1}', 'V1:1:1:A:end{type=end:dim=1x1}', 'V1:0:end{type=end:dim=1x1}garbage']:
            with self.subTest(data=data), self.assertRaises((FormatError, ValueError)):
                decode(data)

    def test_hex_constant_uses_value_not_decimal_text(self):
        a = operand({'type': 'c', 'fields': {'s': 'H_1', 'v': '255', 'si': 'u'}})
        self.assertEqual(a['text'], 'HFF')

    def test_pagination_bounds(self):
        for kw in [{'offset': -1}, {'limit': 0}, {'limit': 201}, {'offset': True}]:
            with self.assertRaises(ValueError):
                page([], **kw)


class ProjectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.p = Project(SOURCE)
        cls.bs = list(cls.p.blocks.values())

    def test_snapshot_identity_and_source_read_only(self):
        self.assertEqual(self.p.sha256, '358473951ae0b2d22bf91934d63340783cd1a15bcb4b1ed2eb2b146e8a5b5447')
        self.assertEqual(digest(SOURCE.read_bytes()), self.p.sha256)

    def test_deduplicate_by_connectivity_not_size(self):
        self.assertEqual(len(self.p.programs), 1)
        p = self.p.programs[0]
        self.assertEqual(p['member'], 'f147681f1865ec6a_LDDB.db')
        self.assertEqual(p['step_matches'], 678)
        self.assertEqual(p['aliases'][0]['verified_blocks'], 678)
        self.assertEqual(p['aliases'][0]['member'], 'c9906ee5decb30c2_LDDB.db')

    def test_all_source_structures_covered(self):
        self.assertEqual(len(self.bs), 678)
        self.assertEqual(collections.Counter(b['kind'] for b in self.bs), {'ladder': 644, 'statement': 33, 'end': 1})
        self.assertEqual(sum(e['kind'] in {'instruction', 'contact'} for b in self.bs for e in b['elements']), 2604)
        self.assertEqual(self.p.issues, [])
        for b in self.bs:
            self.assertEqual(len(b['parsed']['strings']), len(b['parsed']['references']))
            nodes = [n for _, n in walk(b['parsed']['root']) if n['type'] == 'ce']
            self.assertEqual(len(nodes), sum(e['kind'] in {'contact', 'instruction'} for e in b['elements']))
            source_operands = [fields(n).get('args', []) for n in nodes]
            parsed_operands = [[a['raw'] for a in e['operands']] for e in b['elements'] if e['kind'] in {'contact', 'instruction'}]
            self.assertEqual(source_operands, parsed_operands)

    def test_screenshot_first_four_rungs(self):
        self.assertEqual(self.bs[0]['comments'][0]['text'], '洗车时长')
        first = self.bs[1]
        es = first['elements']
        self.assertEqual([e['operands'][0]['text'] for e in es], ['S0', 'S1', 'S2', 'S3', 'S4', 'S5', 'S81', 'SM8013', 'D2000'])
        self.assertEqual([e['contact_mode'] for e in es[:7]], ['normally_closed'] * 7)
        self.assertEqual(es[7]['contact_mode'], 'rising_edge')
        self.assertEqual(es[8]['opcode'], 'INC')
        self.assertEqual([e['operands'][0]['comments'][0]['text'] for e in es], ['停止中', '归位1', '归位2', '报警归位', '中止洗车', '待机中', '洗车完成', '1秒时钟', '洗车时长'])
        self.assertEqual([b['step']['start'] for b in self.bs[1:5]], [0, 22, 29, 36])
        for b, src, dst, comment in zip(self.bs[2:5], ['Y6', 'Y7', 'Y14'], ['D2001', 'D2002', 'D2003'], ['顶刷上', '顶刷下', '左刷开']):
            self.assertEqual([e['operands'][0]['text'] for e in b['elements']], [src, dst])
            self.assertEqual(b['elements'][0]['contact_mode'], 'rising_edge')
            self.assertEqual(b['elements'][0]['operands'][0]['comments'][0]['text'], comment)
        links = first['topology']['adjacency']
        self.assertEqual([(x['from'], x['to']) for x in links], [(es[i]['id'], es[i+1]['id']) for i in range(8)])

    def test_parallel_strings_and_no_double_string_operand(self):
        b = self.bs[15]
        contacts = [e for e in b['elements'] if e['kind'] == 'contact']
        output = next(e for e in b['elements'] if e['kind'] == 'instruction')
        self.assertEqual([a['text'] for a in output['operands']], ['"work"', 'D1210'])
        edges = b['topology']['contacts']
        self.assertEqual(len(contacts), 2)
        self.assertEqual(edges[0]['from'], edges[1]['from'])
        self.assertEqual(edges[0]['to'], edges[1]['to'])
        self.assertEqual(len(b['topology']['adjacency']), 2)

    def test_modifier_not_flattened_or_omitted(self):
        a = self.bs[33]['elements'][0]['operands'][0]
        self.assertEqual(a['text'], 'K4M250')
        result = self.p.search_device('M255', limit=200)
        self.assertTrue(any(r.get('basis') == 'K digit group' for r in result['items']))
        self.assertGreater(self.p.search_device('K4M250')['total'], 0)
        modified = [a for b in self.bs for e in b['elements'] for a in e['operands'] if a.get('mode') == 'bit_select']
        self.assertTrue(modified)
        for a in modified:
            self.assertGreater(self.p.search_device(a['text'])['total'], 0)

    def test_every_comment_preserved_with_identity(self):
        source = self.p.tables['2c025637ae046c69_DC.db']['COMMENT_DATA']['rows']
        result = [c for c in self.p.comments if c['kind'] == 'device']
        self.assertEqual(len(source), 3521)
        self.assertEqual([c['raw_comment'] for c in result], source)
        self.assertEqual(len([c for c in self.p.comments if c['kind'] == 'statement']), 33)
        self.assertEqual(len({c['id'] for c in self.p.comments}), len(self.p.comments))
        self.assertEqual(self.p.search_device('Y14')['comments'][0]['text'], '左刷开')

    def test_all_pointers_and_calls(self):
        self.assertEqual(len(self.p.pointer_entries), 17)
        for e in self.p.elements.values():
            if e.get('opcode') in {'CALL', 'CALLP'}:
                self.assertIn(e['operands'][0]['text'], self.p.pointer_entries)

    def test_unknown_access_is_not_invented(self):
        for e in self.p.elements.values():
            if e.get('opcode') in {'ADPRW', 'RS2', 'CRC'}:
                self.assertTrue(all(v == 'unknown' for v in e['operand_access']))

    def test_dependency_edges_keep_provenance_and_partial_scope(self):
        from gx3.dependencies import trace
        r = trace(self.p, 'D2000', direction='upstream', limit=200)
        first = [e for e in r['items'] if e['block_id'] == self.bs[1]['id']]
        self.assertEqual({e['from'] for e in first if e['kind'] == 'control'}, {'S0','S1','S2','S3','S4','S5','S81','SM8013'})
        self.assertTrue(any(e['from'] == 'D2000' and e['kind'] == 'data' for e in first))
        self.assertEqual(r['coverage'], 'partial_semantics')

    def test_source_tables_and_archive_chunks_roundtrip(self):
        import base64
        data = self.p.members['Config.xml']
        chunks, off = [], 0
        while off is not None:
            r = self.p.raw_member('Config.xml', off, 31)
            chunks.append(base64.b64decode(r['data']))
            off = r['next_offset']
        self.assertEqual(b''.join(chunks), data)
        rows, off = [], 0
        while off is not None:
            r = self.p.raw_rows('2c025637ae046c69_DC.db', 'COMMENT_DATA', off, 200)
            rows.extend(r['items'])
            off = r['next_offset']
        self.assertEqual(len(rows), 3521)


if __name__ == '__main__':
    # 第一个位置参数(非 - 开头)是工程路径, 从 argv 剔除, 不传给 unittest
    if len(sys.argv) > 1 and not sys.argv[1].startswith('-'):
        sys.argv = sys.argv[:1] + sys.argv[2:]
    unittest.main(verbosity=2)
