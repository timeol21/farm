"""Export a self-contained human review page and lossless JSON evidence."""
import argparse
import json
import pathlib
from gx3.project import Project


HTML = r'''<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>GX Works3 梯形图核对</title>
<style>
*{box-sizing:border-box}body{margin:0;color:#152331;background:#f4f6f8;font:15px/1.5 "Microsoft YaHei",sans-serif}header{background:#102c40;color:#fff;padding:20px 28px}h1{font-size:24px;margin:0 0 6px}header p{margin:5px 0;color:#cee0eb}main{padding:18px 24px}.metrics{display:flex;gap:12px;flex-wrap:wrap}.metric{padding:12px 20px;background:white;border:1px solid #d9e2ea;border-radius:8px}.metric b{display:block;font-size:24px;color:#12618b}.notice{background:#fff5d6;border-left:4px solid #d5a224;padding:12px 16px;margin:16px 0}nav{display:flex;gap:10px;flex-wrap:wrap;margin:16px 0}button,select,input{font:inherit;padding:8px 12px;border:1px solid #bdcdd9;border-radius:5px;background:#fff}button{cursor:pointer}button.active{background:#12618b;color:white}input{min-width:280px}.layout{display:grid;grid-template-columns:270px minmax(0,1fr);gap:16px}.list{max-height:76vh;overflow:auto;background:white;border:1px solid #d9e2ea}.row{display:block;width:100%;text-align:left;border:0;border-bottom:1px solid #e3e9ee;border-radius:0;padding:10px}.row small{display:block;color:#567}.row.active small{color:#eee}.panel{background:white;border:1px solid #d9e2ea;border-radius:6px;padding:16px;min-width:0}h2{font-size:19px;margin:0 0 8px}.diagram{overflow:auto;border:1px solid #e2e7ed;background:#fff;margin:12px 0}svg text{font-family:"Microsoft YaHei",sans-serif}table{border-collapse:collapse;width:100%;font-size:14px}th,td{padding:9px;border-bottom:1px solid #e3e9ee;vertical-align:top;text-align:left}th{background:#edf3f7;position:sticky;top:0}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f1f4f7;padding:14px;font:12px/1.5 Consolas,monospace;max-height:500px;overflow:auto}.muted{color:#627685;font-size:13px}.badge{display:inline-block;padding:2px 7px;border-radius:4px;background:#edf4f9;margin:2px}details{margin:12px 0}summary{cursor:pointer}.pages{margin:12px 0;display:flex;gap:8px;align-items:center}@media(max-width:900px){.layout{grid-template-columns:1fr}.list{max-height:240px}main{padding:12px}}
</style>
<header><h1>GX Works3 梯形图 · 核对版</h1><p>单一程序视图 · 原始数据可追溯 · 保存工程的静态快照</p><p id="hash" style="font:12px Consolas;overflow-wrap:anywhere"></p></header>
<main><div class="metrics" id="metrics"></div><div class="notice">解析结构检查已通过；与 GX Works3 的完整人工核对尚未完成。图中坐标为数据库逻辑坐标，排版不等同于 GX 界面。连线保留串并联，通信指令的缓冲区作用范围暂未展开。这里不包含 PLC 实时值。</div>
<nav><button id="ladderTab" class="active" onclick="tab('ladder')">梯形图</button><button id="commentTab" onclick="tab('comment')">全部注释 / 标签文本</button><button id="auditTab" onclick="tab('audit')">覆盖与来源</button></nav>
<section id="ladder"><nav><input id="search" placeholder="搜索软元件、指令、注释或步号" oninput="filterBlocks()"><span id="matched"></span></nav><div class="layout"><div class="list" id="blocks"></div><article class="panel" id="detail"></article></div></section>
<section id="comment" hidden><nav><input id="csearch" placeholder="搜索注释或软元件" oninput="commentPage=0;showComments()"><select id="ckind" onchange="commentPage=0;showComments()"><option value="">全部类型</option><option value="device">软元件注释</option><option value="statement">程序说明</option><option value="label_text_unclassified">标签文本（类别未判定）</option></select></nav><div class="pages"><button onclick="commentPage=Math.max(0,commentPage-1);showComments()">上一页</button><span id="cpage"></span><button onclick="commentPage++;showComments()">下一页</button></div><div class="panel" style="overflow:auto"><table><thead><tr><th>类型</th><th>软元件</th><th>完整文本</th><th>语言/删除标记</th><th>来源</th></tr></thead><tbody id="comments"></tbody></table></div></section>
<section id="audit" hidden class="panel"><h2>核对范围</h2><p>包含 644 个梯形图块、33 条程序说明和结束块。两个数据库的 678 个有序块，经指令、操作数、条件连接、入口及注释签名核对一致，主视图去重。主库全部块与步号表按 GUID 对应。</p><p>3,521 条软元件注释全部保留；20 条标签文本单独列出，不计作已确认注释。非数据库二进制元数据保留原始字节，可由 MCP 查询，但未解码为业务字段。</p><p>读取角色未判定：ADPRW、RS2、CRC。它们的完整指令、参数和原始结构仍在数据中。拓扑为静态结构，不代表运行时必然导通；STL 及子程序上下文单独显示。</p><h2>原工程信息</h2><pre id="info"></pre><h2>原始成员与数据库表计数</h2><pre id="manifest"></pre></section>
</main><script>
const DATA=__DATA__;
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
let selected=null,commentPage=0;
document.getElementById('hash').textContent='SHA-256  '+DATA.info.snapshot_sha256;
const c=DATA.info.counts;
document.getElementById('metrics').innerHTML=[[c.block_types.ladder||0,'梯形图块'],[c.instructions_and_contacts,'触点 / 指令'],[c.comment_types.device||0,'软元件注释'],[c.comment_types.statement||0,'程序说明'],[DATA.info.programs.reduce((n,p)=>n+p.step_matches,0)+' / '+c.blocks,'步号对应']].map(([v,l])=>`<div class="metric"><b>${esc(v)}</b>${esc(l)}</div>`).join('');
document.getElementById('info').textContent=JSON.stringify(DATA.info,null,2);document.getElementById('manifest').textContent=JSON.stringify(DATA.manifest,null,2);
function tab(name){for(const t of ['ladder','comment','audit']){document.getElementById(t).hidden=t!==name;document.getElementById(t+'Tab').classList.toggle('active',t===name)}if(name==='comment')showComments()}
function searchable(b){return [b.ordinal,b.step?.start,b.section,...b.elements.flatMap(e=>[e.opcode,...e.operands.flatMap(a=>[a.text,...(a.comments||[]).map(c=>c.text)])])].join(' ').toLowerCase()}
function filterBlocks(){let q=document.getElementById('search').value.toLowerCase();let bs=DATA.blocks.filter(b=>searchable(b).includes(q));document.getElementById('matched').textContent=`${bs.length} / ${DATA.blocks.length} 条记录`;document.getElementById('blocks').innerHTML=bs.map(b=>`<button class="row ${selected===b.id?'active':''}" data-id="${esc(b.id)}">#${b.ordinal} · 步 ${b.step?.start??'—'}<small>${esc(b.section||b.kind)} · ${b.elements.filter(e=>e.kind!=='wire').length} 元素</small></button>`).join('');document.querySelectorAll('.row').forEach(el=>el.onclick=()=>showBlock(el.dataset.id));if(!bs.some(b=>b.id===selected)&&bs.length)showBlock(bs[0].id);else if(!bs.length)document.getElementById('detail').textContent='没有匹配记录。'}
function svgBlock(b){
 if(b.kind!=='ladder')return `<p>${esc(b.kind==='end'?'END / 程序结束':b.comments.map(c=>c.text).join('\n'))}</p>`;
 const cw=115,maxArgs=Math.max(1,...b.elements.map(e=>e.operands.length)),rh=Math.max(135,70+maxArgs*25),pad=40;
 const maxX=Math.max(1,...b.elements.map(e=>e.pos[0]+(e.kind==='instruction'?3:(e.width||1)))),maxY=Math.max(0,...b.elements.map(e=>e.pos[1]));
 const W=pad*2+cw*(maxX+1),H=70+(maxY+1)*rh;
 const px=x=>pad+x*cw,py=y=>65+y*rh;
 let s=`<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" role="img" aria-label="梯形图逻辑拓扑">`;
 const line=(x1,y1,x2,y2,color='#344b5b')=>`<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${color}" stroke-width="1.5"/>`;
 const text=(x,y,t,color='#223744',size=14,anchor='middle')=>`<text x="${x}" y="${y}" text-anchor="${anchor}" fill="${color}" font-size="${size}">${esc(t)}</text>`;
 s+=line(px(0),30,px(0),H-25,'#102c40');
 for(let y=0;y<=maxY;y++)s+=line(20,py(y)+rh-40,W-15,py(y)+rh-40,'#e8eef3');
 for(const seg of b.topology.wire_segments)s+=line(px(seg.from[0]),py(seg.from[1]),px(seg.to[0]),py(seg.to[1]));
 for(const e of b.elements){const x=px(e.pos[0]),y=py(e.pos[1]),args=e.operands.map(a=>a.text??'?');
  if(e.kind==='contact'){
   const width=(e.width||1)*cw;
   if(e.contact_mode==='comparison'){s+=line(x,y,x+10,y)+`<rect x="${x+10}" y="${y-19}" width="${width-20}" height="38" fill="#f3f8fc" stroke="#344b5b"/>`+text(x+width/2,y+5,`${args[0]} ${e.opcode} ${args[1]}`)+line(x+width-10,y,x+width,y)}
   else{let cx=x+cw/2;s+=line(x,y,cx-9,y)+line(cx+9,y,x+cw,y)+line(cx-9,y-12,cx-9,y+12)+line(cx+9,y-12,cx+9,y+12);if(e.contact_mode==='normally_closed')s+=line(cx-16,y+15,cx+16,y-15);if(e.activation==='p'||e.activation==='f'){s+=text(cx,y+5,e.activation==='p'?'↑':'↓','#172e43',23)}s+=text(cx,y-24,args.join(' ')||e.display_opcode);}
   let comments=e.operands.flatMap(a=>(a.comments||[]).map(c=>c.text));comments.slice(0,3).forEach((t,i)=>s+=text(x+width/2,y+32+i*20,t,'#218134',12));
  }else if(e.kind==='instruction'){
   const w=2.6*cw,h=Math.max(48,30+args.length*25);s+=line(x,y,x+12,y)+`<rect x="${x+12}" y="${y-20}" width="${w}" height="${h}" rx="3" fill="#edf4f9" stroke="#48657a"/>`+text(x+28,y+1,e.display_opcode,'#124e73',15,'start');
   e.operands.forEach((a,i)=>{s+=text(x+28,y+26+i*25,a.text??'?','#243b4c',13,'start');let cm=(a.comments||[]).map(c=>c.text).join(' / ');s+=text(x+120,y+26+i*25,cm,'#218134',12,'start')});
  }
 }
 return s+'</svg>';
}
function showBlock(id){selected=id;const b=DATA.blocks.find(x=>x.id===id);document.querySelectorAll('.row').forEach(el=>el.classList.toggle('active',el.dataset.id===id));
const symbols={normally_open:'常开',normally_closed:'常闭',rising_edge:'上升沿',falling_edge:'下降沿',comparison:'比较'};
document.getElementById('detail').innerHTML=`<h2>#${b.ordinal} · ${esc(b.section||b.kind)} <span class="badge">步 ${b.step?.start??'—'}</span></h2><div class="muted">${esc(b.source.member)} · ${esc(b.source.key)}</div><p>STL 条件：${esc((b.execution_context.stl_guard||[]).join(', ')||'无')}　子程序入口：${esc((b.execution_context.subroutine_entry||[]).join(', ')||'主程序 / 未处于子程序')}</p><div class="diagram">${svgBlock(b)}</div><table><thead><tr><th>坐标 x,y</th><th>类型 / 指令</th><th>全部操作数（保持次序）</th><th>注释</th></tr></thead><tbody>${b.elements.filter(e=>e.kind!=='wire').map(e=>`<tr><td>${e.pos}</td><td>${esc(symbols[e.contact_mode]||e.display_opcode||e.kind)}<br><small>${esc(e.opcode)}</small></td><td>${e.operands.map((a,i)=>`<div>${i+1}. ${esc(a.text)} <span class="muted">${esc(e.operand_access[i])}</span></div>`).join('')}</td><td>${e.operands.flatMap(a=>(a.comments||[]).map(c=>`<div>${esc(c.device)}：${esc(c.text)}</div>`)).join('')}</td></tr>`).join('')}</tbody></table><details><summary>完整连接关系（节点与条件边）</summary><pre>${esc(JSON.stringify(b.topology,null,2))}</pre></details><details><summary>原始梯形图记录</summary><pre>${esc(DATA.raw[b.id])}</pre></details>`;
}
function showComments(){let q=document.getElementById('csearch').value.toLowerCase(),k=document.getElementById('ckind').value;let cs=DATA.comments.filter(c=>(!k||c.kind===k)&&((c.device||'')+' '+(c.text||'')).toLowerCase().includes(q));let pages=Math.max(1,Math.ceil(cs.length/100));commentPage=Math.min(commentPage,pages-1);document.getElementById('cpage').textContent=`${cs.length} 条 · 第 ${commentPage+1} / ${pages} 页`;document.getElementById('comments').innerHTML=cs.slice(commentPage*100,(commentPage+1)*100).map(c=>`<tr><td>${esc({device:'软元件注释',statement:'程序说明',label_text_unclassified:'标签文本'}[c.kind]||c.kind)}</td><td>${esc(c.device||'—')}</td><td style="white-space:pre-wrap">${esc(c.text)}</td><td>${esc(c.comment_number??'—')} / ${esc(c.deleted_flag??'空')}</td><td class="muted">${esc(c.source.member)}<br>${esc(c.source.table)}</td></tr>`).join('')}
filterBlocks();
</script></html>'''


def export(project, directory):
    out = pathlib.Path(directory)
    out.mkdir(parents=True, exist_ok=True)
    evidence = {'info': project.overview(), 'manifest': project.manifest,
                'blocks': list(project.blocks.values()), 'comments': project.comments,
                'references': project.references, 'source_tables': project.tables}
    (out/'project.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    data = {'info': project.overview(), 'manifest': project.manifest, 'comments': project.comments,
            'blocks': [project.block_view(b) for b in project.blocks],
            'raw': {b['id']: b['raw_row']['data'] for b in project.blocks.values()}}
    serialized = json.dumps(data, ensure_ascii=False).replace('<', '\\u003c').replace('\u2028','\\u2028').replace('\u2029','\\u2029')
    (out/'核对页面.html').write_text(HTML.replace('__DATA__', serialized), encoding='utf-8')
    (out/'coverage.json').write_text(json.dumps(project.overview(), ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--project', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    p = Project(args.project)
    export(p, args.out)
    print(json.dumps(p.overview()['counts'], ensure_ascii=False))
