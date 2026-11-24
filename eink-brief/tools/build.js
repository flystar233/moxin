// 客户版构建：给合作的硬件 / 结构工程师和工厂看的规格书（最终版）。
// 数据和图都从 ../eink-docs 读（data.js、board-data.js、tools/*.js），这里不另存数字；eink-docs 改了以后重新跑一次即可同步。
// 用法：node tools/build.js
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.resolve(__dirname, '..');
const docs = path.resolve(root, '..', 'eink-docs');
const read = f => fs.readFileSync(path.join(docs, f), 'utf8');
const load = f => { const ctx = { window: {} }; vm.runInNewContext(read(f), ctx); return ctx.window; };
const D = load('assets/data.js').DOC;
const BRD = load('assets/board-data.js').BOARD;
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const must = (v, what) => { if (!v) throw new Error('eink-docs 里找不到：' + what); return v; };

// 设计笔记里的修订标记、评审编号、页面互引对客户没有意义，取数据时去掉
const clean = s => String(s == null ? '' : s)
  .replace(/（v\d+(?:\.\d+)+[^（）]*）/g, '')
  .replace(/v\d+(?:\.\d+)+ ?(?:新增|起)[：:，、]?/g, '')
  .replace(/（(?:已确认|已核对|已选定)）/g, '')
  .replace(/（F\d+(?:[、，]F\d+)*）/g, '')
  .replace(/（见[^（）]*）/g, '')
  .replace(/（原[^（）]*）/g, '')
  .replace(/[^，；。\s<>]*v\d+(?:\.\d+)+[^，；。<>]*[，；。]?/g, '')
  .replace(/[，；]去掉了[^，；。<>]*/g, '')
  .replace(/（）/g, '')
  .replace(/[，；]\s*(?=[。；]|$)/g, '')
  .trim();
// 从设计笔记的页面里取一段 HTML：链接只留文字，位号保留灰底标签，评审编号去掉
const strip = html => clean(html
  .replace(/<a class="fx"[^>]*>[^<]*<\/a>/g, '')
  .replace(/<a (?:class="ref"|href="[^"]*")[^>]*>([^<]*)<\/a>/g, (m, t) => /class="ref"/.test(m) ? `<span class="ref">${t}</span>` : t)
  .replace(/<tr class="is-new">/g, '<tr>')
  .replace(/<span class="tag [^"]*">[^<]*<\/span>/g, ''));
// 取 marker 之后第一个 open 开头的元素（按 tag 配对嵌套）
const after = (src, marker, open, tag, what) => {
  const i = must(src.indexOf(marker) + 1, what) - 1, a = must(src.indexOf(open, i) + 1, what) - 1;
  const re = new RegExp(`<${tag}[\\s>]|</${tag}>`, 'g'); re.lastIndex = a;
  let depth = 0;
  for (let m; (m = re.exec(src));) { depth += m[0][1] === '/' ? -1 : 1; if (!depth) return src.slice(a, m.index + m[0].length); }
  return must(null, what);
};

/* ---------- 图 ---------- */
const dims = require(path.join(docs, 'tools', 'dims.js'))();
const svg = {
  arch: require('./arch.js')(D),
  stack: require(path.join(docs, 'tools', 'stack.js'))(),
  pcb: dims.pcb, front: dims.front, edges: dims.edges, keys: dims.keys, sw: dims.sw,
  plan: planSvg(), sch: schematicFigs()
};

// 器件布局平面图：直接跑 eink-docs 布局页里的 draw2d，去掉交互属性
function planSvg() {
  const src = read('src/layout.html');
  const a = must(src.indexOf('(function draw2d() {') + 1, '布局页 draw2d') - 1;
  const b = src.indexOf('})();', a) + 5;
  let out = '';
  vm.runInNewContext(src.slice(a, b), { window: { BOARD: BRD }, document: { getElementById: () => ({ set innerHTML(v) { out = v; } }) } });
  return must(out, '平面图输出').replace(/ tabindex="0" role="button"/g, '');
}
// 原理图：在沙盒里跑 schematic.js，用最小的假 DOM 接住它生成的每张图；墨水屏那张用 epd.js 出图
function schematicFigs() {
  const mk = (tag, attrs, ...kids) => ({ tag, attrs: attrs || {}, kids: kids.flat().filter(k => k != null), innerHTML: '', append(...k) { this.kids.push(...k.flat().filter(x => x != null)); }, addEventListener() {} });
  const host = mk('div');
  const document = { getElementById: id => (id === 'schSheets' ? host : null), querySelectorAll: () => [], querySelector: () => null, addEventListener() {} };
  vm.runInNewContext(read('assets/schematic.js'), { window: { DOC: D, APP: { el: mk, refMap: new Map() }, addEventListener() {} }, document });
  const epd = require(path.join(docs, 'tools', 'epd.js'))(false);
  must(host.kids.length, '原理图');
  return host.kids.map(fig => {
    const [title, surf, notes] = fig.kids;
    const label = title.kids.map(k => (typeof k === 'string' ? k : k.attrs.text)).join('');
    const body = surf.innerHTML || epd;
    const li = notes && notes.tag === 'ul' ? notes.kids.map(n => `<li>${esc(clean(n.attrs.text))}</li>`).join('') : '';
    return `<figure class="fig sch"><div class="sch-title">${esc(label)}</div><div class="fig-surface">${body}</div>${li ? `<ul class="plain sch-notes">${li}</ul>` : ''}</figure>`;
  }).join('\n');
}

/* ---------- 从设计笔记取的表格和列表 ---------- */
const bomOf = ref => must(D.bom.find(b => b.refs.includes(ref)), '器件清单里的 ' + ref);
const ov = read('src/overview.html'), ly = read('src/layout.html');
const html = {
  rails: strip(after(ov, '电压域', '<div class="tbl-wrap">', 'div', '概览页电压域表')),
  thick: strip(after(ly, 'id="stackup"', '<div class="tbl-wrap">', 'div', '布局页厚度表')).replace(/<\/table>\s*<\/table>/, '</table>'),
  heights: strip(after(ly, '元件区限高 4mm</h3>', '<div class="tbl-wrap">', 'div', '布局页限高表')),
  assembly: strip(after(ly, '装配顺序', '<ol class="plain">', 'ol', '布局页装配顺序')),
  bomTable: bomTable(), pinTable: pinTable(), connTable: connTable(), stepTable: stepTable(), tpTable: tpTable()
};
function bomTable() {
  const groups = [...new Set(D.bom.map(b => b.g))];
  return groups.map(g => `<h3>${esc(g)}</h3><div class="tbl-wrap"><table class="bom"><colgroup><col style="width:17%"><col style="width:20%"><col style="width:31%"><col style="width:25%"><col style="width:7%"></colgroup><thead><tr><th>位号</th><th>功能</th><th>型号</th><th>封装</th><th class="r">数量</th></tr></thead><tbody>` +
    D.bom.filter(b => b.g === g).map(b => `<tr><td class="m">${b.refs.map(r => `<span class="ref">${esc(r)}</span>`).join(' ')}</td><td>${esc(clean(b.fn))}</td><td>${esc(clean(b.part))}</td><td>${esc(clean(b.pkg))}</td><td class="r">${b.qty}</td></tr>`).join('') +
    '</tbody></table></div>').join('\n');
}
function pinTable() {
  return '<div class="tbl-wrap tall"><table><thead><tr><th class="r">脚</th><th>名称</th><th>网络</th><th>连到</th><th>方向 / 上下拉</th><th>分组</th><th>说明</th></tr></thead><tbody>' +
    D.pins.map(p => `<tr${p.group === 'free' ? ' class="muted"' : ''}><td class="r">${p.pin}</td><td class="m">${esc(p.name)}</td><td class="m">${esc(p.net || '—')}</td><td>${esc(clean(p.to || ''))}</td><td>${esc([p.dir, p.pull].filter(Boolean).join('，'))}</td><td>${esc((D.pinGroups[p.group] || {}).label || '')}</td><td>${esc(clean(p.note || ''))}</td></tr>`).join('') +
    '</tbody></table></div>';
}
function connTable() {
  const refs = ['USB0', 'BT1', 'U3', 'LS1', 'H4', 'FPC1', 'CARD1', 'H3', 'SW1', 'RESET', 'SW3'];
  return '<div class="tbl-wrap"><table><thead><tr><th>位号</th><th>接口</th><th>型号</th><th>封装 / 尺寸</th></tr></thead><tbody>' +
    refs.map(r => { const b = bomOf(r); return `<tr><td class="m">${b.refs.map(x => `<span class="ref">${esc(x)}</span>`).join(' ')}</td><td>${esc(clean(b.fn))}</td><td>${esc(clean(b.part))}</td><td>${esc(clean(b.pkg))}</td></tr>`; }).join('') +
    '</tbody></table></div>';
}
function stepTable() {
  return '<div class="tbl-wrap"><table><thead><tr><th class="r">步</th><th>做什么</th><th>通过标准</th></tr></thead><tbody>' +
    D.steps.map((s, i) => `<tr><td class="r">${i + 1}</td><td><b>${esc(clean(s.title))}</b><div class="sub">${esc(clean(s.do))}</div></td><td>${esc(clean(s.pass))}</td></tr>`).join('') +
    '</tbody></table></div>';
}
function tpTable() {
  const m = must(/const tps = (\[[\s\S]*?\]);/.exec(ly), '布局页测试点表');
  const tps = vm.runInNewContext(m[1]);
  return '<div class="tbl-wrap"><table><thead><tr><th>点在哪</th><th>网络</th><th>用途</th></tr></thead><tbody>' +
    tps.map(([id, net, use]) => `<tr><td class="m"><span class="ref">${id}</span></td><td class="m">${esc(net)}</td><td>${esc(clean(use).replace(/（验收第 \d+ 步）/, ''))}</td></tr>`).join('') +
    '</tbody></table></div>';
}

/* ---------- 页面外壳 ---------- */
// 网站页（产品介绍）：自己一套版式（assets/site.css），顶部是网站导航
const SITE = [
  { id: 'index', file: 'index.html', title: '首页', desc: '一本会朗读的电子书，也是桌上一块安静的状态屏：4.26 英寸墨水屏，9.5 毫米。' },
  { id: 'reader', file: 'reader.html', title: '阅读', desc: '书在 TF 卡里，翻页不用联网；电脑端排版、合成朗读，进度跨设备同步。' },
  { id: 'desk', file: 'desk.html', title: '桌面', desc: '放回支架上，显示 Claude Code 的进度、电脑负载、时间和室内空气。' },
  { id: 'design', file: 'design.html', title: '设计', desc: '从原理图、四层电路板到亚克力面板外壳：九毫米半里的设计。' },
  { id: 'specs', file: 'specs.html', title: '规格', desc: '屏幕、主控、电池、接口、尺寸等技术规格。' },
  { id: 'docs', file: 'docs.html', title: '资料', desc: '原理图、尺寸图、外壳生产文件、器件清单和打样测试。' }
];
// 工程资料页：沿用设计笔记的版式（style.css + brief.css）
const PAGES = [
  { id: 'electrical', file: 'electrical.html', title: '电气' },
  { id: 'mechanical', file: 'mechanical.html', title: '结构' },
  { id: 'case', file: 'case.html', title: '外壳制造' },
  { id: 'bom', file: 'bom.html', title: '器件清单' },
  { id: 'test', file: 'test.html', title: '打样测试' }
];
const pad = n => String(n).padStart(2, '0');
const brand = '墨水屏监控器 · 工程资料';
const schStyle = (/<style>([\s\S]*?)<\/style>/.exec(read('src/schematic.html')) || [])[1] || '';
const planStyle = ly.split('\n').filter(l => /^\s*#board[ .:]/.test(l)).join('\n');

function sitePage(p, body) {
  const nav = SITE.filter(q => q.id !== 'index').map(q => `<a href="${q.file}"${q.id === p.id ? ' aria-current="page"' : ''}>${q.title}</a>`).join('');
  const foot = SITE.map(q => `<a href="${q.file}">${q.title}</a>`).join('');
  return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>${p.id === 'index' ? '墨水屏阅读器' : p.title + ' · 墨水屏阅读器'}</title>
<meta name="description" content="${esc(p.desc)}">
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500;600&family=Noto+Serif+SC:wght@600;900&display=swap" rel="stylesheet">
<link rel="stylesheet" href="assets/site.css">
<script>try{var t=JSON.parse(localStorage.getItem('eink.theme'));if(t==='light'||t==='dark')document.documentElement.dataset.theme=t}catch(e){}</script>
</head>
<body class="museum" data-page="${p.id}">
<header class="m-bar"><div class="m-wrap"><a class="m-brand" href="index.html">墨水屏阅读器</a><nav class="m-nav" aria-label="网站">${nav}</nav><button class="m-theme" type="button" id="themeBtn" aria-label="切换主题">主题：跟随系统</button></div></header>
${body}
<footer class="m-foot"><div class="m-wrap"><nav class="m-footnav" aria-label="网站">${foot}</nav><div>${esc(D.meta.project)} · ${esc(D.meta.rev)} · ${esc(D.meta.date)} · ${esc(D.meta.status)}</div></div></footer>
<script src="assets/brief.js"></script>
</body>
</html>
`;
}
function page(p, i, body) {
  // 段落编号和本页目录
  const toc = []; let k = 0;
  body = body.replace(/(<section class="sec" id="([\w-]+)">\s*<h2>)([\s\S]*?)(<\/h2>)/g, (all, open, sid, inner, close) => {
    k++; toc.push(`<li><a href="#${sid}"><span class="n">${i + 1}.${k}</span>${inner.replace(/<[^>]+>/g, '')}</a></li>`);
    return `${open}<span class="n">${i + 1}.${k}</span>${inner}${close}`;
  });
  body = body.replace(/<header class="sheet-head">([\s\S]*?)<\/header>/, '<header class="sheet-head"><div class="sh-text">$1</div></header>');
  const tabs = '<a href="docs.html"><span class="n">←</span>网站</a>' + PAGES.map((q, j) => `<a href="${q.file}"${q.id === p.id ? ' aria-current="page"' : ''}><span class="n">${pad(j + 1)}</span>${q.title}</a>`).join('');
  const prev = PAGES[i - 1], next = PAGES[i + 1];
  const pager = `<nav class="pager" aria-label="上一页与下一页">${prev ? `<a href="${prev.file}"><span>← ${pad(i)}</span>${prev.title}</a>` : ''}${next ? `<a class="next" href="${next.file}"><span>${pad(i + 2)} →</span>${next.title}</a>` : ''}</nav>`;
  const extraStyle = (p.id === 'electrical' ? schStyle : '') + (p.id === 'mechanical' ? planStyle : '');
  return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>${p.title} · ${brand}</title>
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500;600&family=Noto+Serif+SC:wght@600;900&display=optional" rel="stylesheet">
<link rel="stylesheet" href="assets/style.css">
<link rel="stylesheet" href="assets/brief.css">
<script>try{var t=JSON.parse(localStorage.getItem('eink.theme'));if(t==='light'||t==='dark')document.documentElement.dataset.theme=t}catch(e){}</script>
${extraStyle ? `<style>${extraStyle}</style>` : ''}
</head>
<body data-page="${p.id}">
<header class="bar"><div class="wrap"><a class="brand" href="docs.html"><span class="brand-mark" aria-hidden="true"></span><span class="brand-name">${brand}</span></a><nav class="tabs" aria-label="页面">${tabs}</nav><button class="theme-btn" type="button" id="themeBtn" aria-label="切换主题">主题：跟随系统</button></div></header>
<div class="wrap">
${body}
${pager}
<footer class="foot">${esc(D.meta.project)} · ${esc(D.meta.rev)} · ${esc(D.meta.date)} · ${esc(D.meta.status)}。本文由设计笔记自动生成，数值以最新版本为准。</footer>
</div>
<script src="assets/brief.js"></script>
</body>
</html>
`.replace('<aside class="rail" aria-label="本页导航"></aside>', toc.length > 1 ? `<aside class="rail" aria-label="本页导航"><div class="toc-box"><h4>本页</h4><ol class="toc">${toc.join('')}</ol></div></aside>` : '');
}

// 模板里的占位：<!--SVG:名--> 图、<!--HTML:名--> 表格、{{part:位号}} 器件型号、{{meta:字段}}
const fill = s => s
  .replace(/<!--SVG:(\w+)-->/g, (m, k) => must(svg[k], '图 ' + k))
  .replace(/<!--HTML:(\w+)-->/g, (m, k) => must(html[k], '表 ' + k))
  .replace(/\{\{part:([\w]+)\}\}/g, (m, r) => esc(clean(bomOf(r).part)))
  .replace(/\{\{meta:(\w+)\}\}/g, (m, k) => esc(must(D.meta[k], 'meta.' + k)));

fs.mkdirSync(path.join(root, 'assets'), { recursive: true });
// 外壳制造页的下载文件：从 ../enclosure 复制，和 3D 模型保持一致；渲染图由 tools/shoot.js 生成，这里只检查在不在
const enc = path.resolve(root, '..', 'enclosure'), caseDir = path.join(root, 'files', 'case');
fs.mkdirSync(caseDir, { recursive: true });
['stl/master-frame.stl', 'stl/master-back.stl', 'stl/master-keys.stl', 'stl/fdm-print.stl', 'acrylic-printed.dxf', 'acrylic-cutwindow.dxf', 'acrylic-preview.svg',
 'eink-case.scad', 'eink-case-master.scad', 'eink-case-fdm.scad'].forEach(f => fs.copyFileSync(path.join(enc, f), path.join(caseDir, path.basename(f))));
['reader', 'readerf', 'hero', 'front', 'explode', 'pcb', 'back', 'keys', 'usb'].forEach(n => must(fs.existsSync(path.join(root, 'assets', 'img', 'studio-' + n + '.png')), '渲染图 studio-' + n + '.png（先跑 node tools/shoot.js）'));
fs.copyFileSync(path.join(docs, 'assets', 'style.css'), path.join(root, 'assets', 'style.css'));
const write = (file, out) => {
  if (/<!--(SVG|HTML):|{{/.test(out)) throw new Error(file + ' 还有没填的占位');
  fs.writeFileSync(path.join(root, file), out);
  console.log('built', file, Buffer.byteLength(out));
};
SITE.forEach(p => write(p.file, sitePage(p, fill(fs.readFileSync(path.join(root, 'src', p.file), 'utf8')))));
PAGES.forEach((p, i) => {
  const out = page(p, i, fill(fs.readFileSync(path.join(root, 'src', p.file), 'utf8')));
  if (/<!--(SVG|HTML):|\{\{/.test(out)) throw new Error(p.file + ' 还有没填的占位');
  fs.writeFileSync(path.join(root, p.file), out);
  console.log('built', p.file, Buffer.byteLength(out));
});
// 嘉立创格式的 BOM，方便直接导入
const csv = [['Comment', 'Designator', 'Footprint', 'Quantity', '功能']].concat(D.bom.filter(b => !b.dnp).map(b => [clean(b.part), b.refs.join(','), clean(b.pkg), b.qty, clean(b.fn)]))
  .map(r => r.map(c => `"${String(c).replace(/"/g, '""')}"`).join(',')).join('\r\n');
fs.writeFileSync(path.join(root, 'bom.csv'), '﻿' + csv);
console.log('built bom.csv', D.bom.length, 'rows');
