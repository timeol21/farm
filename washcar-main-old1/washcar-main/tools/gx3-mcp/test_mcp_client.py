"""Optional integration test using the independently installed official MCP SDK."""
import asyncio
import json
import pathlib
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main():
    root = pathlib.Path(__file__).resolve().parent
    params = StdioServerParameters(command=sys.executable, args=[str(root/'server.py'), '--project', str(root/'source_snapshot.gx3')])
    results = []
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            init = await session.initialize()
            listed = await session.list_tools()
            assert len(listed.tools) == 10
            info = await session.call_tool('project_info', {})
            assert not info.isError
            payload = json.loads(info.content[0].text)
            assert payload['counts']['blocks'] == 678
            results.append({'check': 'initialize_and_tools', 'protocol': init.protocolVersion, 'tools': [t.name for t in listed.tools]})
            blocks = await session.call_tool('list_blocks', {'query': '洗车时长'})
            block = json.loads(blocks.content[0].text)['items'][1]['block_id']
            for name, args in [
                ('get_block', {'block_id': block}),
                ('get_block', {'block_id': block, 'raw': True}),
                ('search_device', {'device': 'Y14'}),
                ('device_relations', {'device': 'D2000'}),
                ('trace_device', {'device': 'D2000', 'direction': 'upstream'}),
                ('source_inventory', {}),
                ('read_source_rows', {'member': '2c025637ae046c69_DC.db', 'table': 'COMMENT_DATA'}),
                ('read_source_bytes', {'member': 'Config.xml'}),
            ]:
                r = await session.call_tool(name, args)
                assert not r.isError, r
                result = json.loads(r.content[0].text)
                assert result['snapshot_sha256'] == payload['snapshot_sha256']
                if hasattr(r, 'structuredContent'):
                    assert r.structuredContent == result
                results.append({'check': name, 'ok': True})
            off, ids = 0, set()
            while off is not None:
                r = await session.call_tool('search_comments', {'kind': 'device', 'offset': off, 'limit': 200})
                assert not r.isError
                d = json.loads(r.content[0].text)
                for c in d['items']:
                    assert c['id'] not in ids
                    ids.add(c['id'])
                off = d['next_offset']
            assert len(ids) == 3521
            results.append({'check': 'all_device_comments_through_mcp', 'count': len(ids)})
            for name, args in [('get_block', {'block_id': 'nonexistent'}), ('search_device', {'device': 'D0', 'limit': 0}), ('read_source_bytes', {'member': '../outside'}), ('search_comments', {'query': 12})]:
                r = await session.call_tool(name, args)
                assert r.isError
            results.append({'check': 'invalid_arguments_return_tool_errors', 'ok': True})
    target = root/'data'/'mcp-test-results.json'
    target.parent.mkdir(exist_ok=True)
    target.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(results, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    asyncio.run(main())
