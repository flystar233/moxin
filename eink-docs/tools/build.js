// 构建：把原设计笔记里的四张 SVG（架构、屏驱动原理图、PCB 布局、叠层）拼进 src/ 下的页面，
// 并叠加可点击的热区，输出到 eink-docs/ 根目录。用法：node tools/build.js
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const original = path.resolve(root, '..', 'eink-desk-monitor.html');
const svgs = fs.readFileSync(original, 'utf8').match(/<svg\b[\s\S]*?<\/svg>/g);
if (!svgs || svgs.length !== 4) throw new Error('expected 4 svgs in ' + original);
// v2.0 系统主电源统一叫 VSYS（和 VBUS、VBAT 一样带 V 前缀），原图里的 SYS 一并替换
const [arch0, epd0, layout0] = svgs.map(s => s.replace(/(?<![A-Za-z_])SYS(?![A-Za-z_])/g, 'VSYS'));
// v4.13 位号按 EDA 原理图重排，原布局图里屏驱动的标注跟着改
const layout = layout0.replace('L1 · Q1 · D1～D3', 'L1 · U13 · D4～D6');
// v3.3 叠层图重画：原图还是 v0.6 电池叠在元件上的画法，改用 tools/stack.js 按当前尺寸生成
const stack = require('./stack.js')();
const close = (svg, extra) => svg.replace(/<\/svg>$/, extra + '</svg>');
const font = 'font-family="IBM Plex Mono, Consolas, monospace"';

// ---- architecture: v3.6 起由 tools/arch.js 生成（原图缺电量计、TF 卡、侧键、总开关），方框自带链接 ----
const archOut = require('./arch.js')();

// ---- EPD schematic: v3.6 起由 tools/epd.js 生成（原图行距太挤），墨水屏页带点击热区，原理图页不带 ----
const epdOut = require('./epd.js')(true);

// ---- PCB layout: zones, test points, F9 tag ----
const zones = {
  rf: [0.4, 0.4, 30, 26, '射频与主控'], keys: [41, 0, 42, 8.5, '侧按键'], rtc: [39, 10, 17, 12, '时钟'],
  acc: [58, 12, 7, 9.5, '姿态传感器'], aht: [86, 1, 18.5, 18.5, 'CO2 · 温湿度'], power: [0, 32, 40.5, 20.5, '电源区'],
  usb: [7.5, 54.5, 24, 8.5, 'USB 输入'], debug: [45, 32, 18, 17, '调试'], epd: [68, 21, 41, 23, '屏驱动'], audio: [68, 48, 27, 10.5, '音频']
};
let lay = '<g class="zones">';
for (const [k, [x, y, w, h, label]] of Object.entries(zones)) lay += `<rect class="zone" data-zone="${k}" tabindex="0" role="button" aria-label="${label}" x="${x}" y="${y}" width="${w}" height="${h}" rx=".8"/>`;
lay += '</g><g class="tps">';
const tps = []; // v4.42 起不设测试点，测量直接点在器件焊盘上
for (const [id, x, y, sug] of tps) lay += `<circle class="tp${sug ? ' sug' : ''}" data-tp="${id}" cx="${x}" cy="${y}" r="1.05"/><text class="tpl" x="${x}" y="${y + (sug ? -1.6 : 2.6)}" text-anchor="middle">${id}</text>`;
lay += '</g>';
lay += `<a href="review.html#F9"><rect x="23" y="1.2" width="6.2" height="3.2" rx=".6" fill="#A3341F"/><text x="26.1" y="3.5" text-anchor="middle" fill="#fff" ${font} font-size="2" font-weight="700">F9</text></a>`;
const layoutOut = close(layout.replace('<svg ', '<svg id="board" '), lay);

// 原理图页复用同一张屏驱动图，不带墨水屏页的点击热区
const epdPlain = require('./epd.js')(false);
// v4.9 尺寸图：PCB 外形、外壳正视、外壳开孔边侧视，由 tools/dims.js 从 board-data.js 算出
const dims = require('./dims.js')();
// L3 电源层（3V3 + VBUS）分区图，由 tools/l2.js 从 board-data.js 生成（v4.24 起层号按 EDA）
const l2 = require('./l2.js')();
const parts = { arch: archOut, epd: epdOut, epdPlain, layout: layoutOut, stack, dimsPcb: dims.pcb, dimsFront: dims.front, dimsEdges: dims.edges, dimsKeys: dims.keys, l2 };
// ---- static chrome: sheet bar, eyebrow, finding chips, sheet card, section numbers, rail ----
// 这些原来由 app.js 在加载后生成，会造成内容跳动；改为构建时直接写进 HTML，脚本只负责更新进度。
const vm = require('vm');
const ctx = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(root, 'assets', 'data.js'), 'utf8'), ctx);
const D = ctx.window.DOC;
const pad = n => String(n).padStart(2, '0');
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const strip = s => s.replace(/<[^>]+>/g, '').replace(/\s+/g, ' ').trim();

function barHTML(id) {
  // v4.0 首页用简洁的网站导航，其余页面保留全部图纸页签
  if (id === 'index') return `<header class="bar bar-home"><div class="wrap"><a class="brand" href="index.html"><span class="brand-mark" aria-hidden="true"></span><span class="brand-name">墨水屏监控器 · 阅读器</span></a><nav class="home-nav" aria-label="主导航"><a href="overview.html">概览</a><a href="#docs">文档</a><a href="layout.html">3D 电路板</a><a href="review.html">评审</a></nav><button class="theme-btn" type="button" id="themeBtn" aria-label="切换主题">主题：跟随系统</button></div><div class="bar-prog" aria-hidden="true"></div></header>
<script>(function(){try{var v=JSON.parse(localStorage.getItem('eink.theme')),L={light:'浅色',dark:'深色'};if(L[v])document.getElementById('themeBtn').textContent='主题：'+L[v];}catch(e){}})();</script>`;
  const tabs = D.sheets.map((s, i) => `<a href="${s.file}"${s.id === id ? ' aria-current="page"' : ''}><span class="n">${pad(i + 1)}</span>${esc(s.title)}</a>`).join('');
  return `<header class="bar"><div class="wrap"><a class="brand" href="index.html"><span class="brand-mark" aria-hidden="true"></span><span class="brand-name">墨水屏监控器 · 阅读器</span></a><nav class="tabs" aria-label="图纸">${tabs}</nav><button class="theme-btn" type="button" id="themeBtn" aria-label="切换主题">主题：跟随系统</button></div><div class="bar-prog" aria-hidden="true"></div></header>
<script>(function(){var t=document.querySelector('.tabs'),c=t&&t.querySelector('[aria-current]');if(c){var o=c.offsetLeft-t.offsetLeft+c.offsetWidth-t.clientWidth;if(o>0)t.scrollLeft=o+40;}try{var v=JSON.parse(localStorage.getItem('eink.theme')),L={light:'浅色',dark:'深色'};if(L[v])document.getElementById('themeBtn').textContent='主题：'+L[v];}catch(e){}})();</script>`;
}

function chrome(html, f) {
  const m = html.match(/<body data-sheet="(\w+)">/);
  if (!m) return html;
  const id = m[1], idx = D.sheets.findIndex(s => s.id === id), no = idx + 1;
  const fs_ = id === 'review' ? D.findings : D.findings.filter(x => x.sheet === id);
  const own = D.findings.filter(x => x.sheet === id);

  html = html.replace(m[0], m[0] + '\n' + barHTML(id));
  html = html.replace('<p class="eyebrow" data-auto></p>',
    `<p class="eyebrow">图纸 <b>${pad(no)}</b> / ${pad(D.sheets.length)} · ${esc(D.meta.rev)} · ${esc(D.meta.status)}</p>`);
  html = html.replace('<div class="head-chips" data-auto></div>', own.length
    ? `<div class="head-chips">${own.map(x => { const sv = D.severities[x.sev]; return `<a class="sev ${sv.cls}" href="#${x.anchor || ''}" data-f="${x.id}" style="text-decoration:none">${x.id} ${sv.label}</a>`; }).join('')}</div>`
    : '');

  // section numbers + toc
  const toc = [];
  let i = 0;
  html = html.replace(/(<section class="sec" id="([\w-]+)">\s*<h2>)([\s\S]*?)(<\/h2>)/g, (all, open, sid, inner, close) => {
    i++;
    toc.push({ sid, n: no + '.' + i, t: strip(inner) });
    return `${open}<span class="n">${no}.${i}</span>${inner}${close}`;
  });

  // sheet card
  const secs = toc.length;
  const inter = (html.match(/class="panel"/g) || []).length + (/class="pinfig"/.test(html) ? 1 : 0)
    + (/data-hit=/.test(html) ? 1 : 0) + (/class="zone"/.test(html) ? 1 : 0) + (/id="stage"/.test(html) ? 1 : 0);
  const tables = (html.match(/<table[\s>]/g) || []).length;
  const third = inter ? ['交互', inter] : ['表格', tables];
  const card = `<aside class="sh-card" aria-label="本页概况"><div class="sh-no"${fs_.length ? '' : ' style="grid-row: span 1"'}>${pad(no)}</div><dl class="sh-stats"${fs_.length ? '' : ' style="border-bottom:0"'}><div><dt>章节</dt><dd>${secs}</dd></div><div><dt>评审项</dt><dd>${fs_.length}</dd></div><div><dt>${third[0]}</dt><dd>${third[1]}</dd></div></dl>${fs_.length ? `<div class="sh-prog" data-f="${fs_.map(x => x.id).join(' ')}"><span>本页评审已处理<b>0 / ${fs_.length}</b></span><div class="sh-bar" aria-hidden="true"></div></div>` : ''}</aside>`;
  html = html.replace(/<header class="sheet-head">([\s\S]*?)<\/header>/, (all, inner) => `<header class="sheet-head"><div class="sh-text">${inner}</div>${card}</header>`);

  // rail
  const railToc = toc.length > 1 ? `<div class="toc-box"><h4>本页</h4><ol class="toc">${toc.map(t => `<li><a href="#${t.sid}"><span class="n">${t.n}</span>${esc(t.t)}</a></li>`).join('')}</ol></div>` : '';
  const railF = own.length ? `<div><h4>本页评审项</h4><div class="rail-findings">${own.map(x => `<a href="review.html#${x.id}" data-f="${x.id}"><span class="sev ${D.severities[x.sev].cls}">${x.id}</span><span>${esc(x.title)}<span class="muted st"></span></span></a>`).join('')}</div></div>` : '';
  html = html.replace('<aside class="rail" aria-label="本页导航"></aside>', `<aside class="rail" aria-label="本页导航" data-static>${railToc}${railF}</aside>`);
  return html;
}

const srcDir = path.join(root, 'src');
for (const f of fs.readdirSync(srcDir).filter(f => f.endsWith('.html'))) {
  let html = fs.readFileSync(path.join(srcDir, f), 'utf8');
  html = html.replace(/<!--SVG:(\w+)-->/g, (m, k) => {
    if (!parts[k]) throw new Error('unknown svg ' + k + ' in ' + f);
    return parts[k];
  });
  html = chrome(html, f);
  fs.writeFileSync(path.join(root, f), html);
  console.log('built', f, Buffer.byteLength(html));
}
