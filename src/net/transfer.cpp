// 无线传输：设备开一个网页，电脑或手机浏览器打开就能上传书、字库，升级固件。
// 先连家里的 Wi-Fi；连不上（不在家）就开设备热点。会话期间 TF 卡一直挂着。
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app/app.h"
#include "board/storage.h"
#include "config.h"
#include "core/ota.h"
#include "core/settings.h"
#include "voice/tts.h"
#include "wifi.h"

namespace net {

static WebServer *s_srv = nullptr;
static bool s_on = false, s_ap = false, s_sd = false, s_reboot = false;
static String s_url, s_msg;
static uint32_t s_ver = 0, s_rebootAt = 0;

// 上传中的文件
static const size_t WBUF = 32768;  // 攒满 32KB 再写卡（网页每次只给 1.4KB，逐块写卡太慢）
static uint8_t *s_wbuf = nullptr;
static size_t s_wlen = 0;
static File s_up;
static String s_upName, s_upPath, s_upTmp, s_upErr;
static bool s_upOk = false;
static uint32_t s_upBytes = 0;
static bool s_fwOk = false;
static String s_fwErr;

bool transferActive() { return s_on; }
bool transferViaAp() { return s_ap; }
String transferUrl() { return s_url; }
String transferMessage() { return s_msg; }
uint32_t transferVersion() { return s_ver; }
bool transferRebooting() { return s_reboot; }

static void note(const String &m) {
  s_msg = m;
  s_ver++;
  Serial.printf("[传输] %s\n", m.c_str());
}

static String jsonStr(const String &s) {
  String o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o + "\"";
}

static String sizeText(uint64_t b) {
  char t[24];
  if (b >= 1073741824ULL) snprintf(t, sizeof(t), "%.1f GB", b / 1073741824.0);
  else if (b >= 1048576) snprintf(t, sizeof(t), "%.1f MB", b / 1048576.0);
  else snprintf(t, sizeof(t), "%u KB", (unsigned)((b + 1023) / 1024));
  return t;
}

static bool allowedDir(const String &d) { return d == "/books" || d == "/fonts"; }

static bool okExt(const String &dir, const String &name) {
  String l = name;
  l.toLowerCase();
  if (dir == "/fonts") return l.endsWith(".efnt");
  return l.endsWith(".txt") || l.endsWith(".epub");
}

static String baseName(String n) {
  int k = max(n.lastIndexOf('/'), n.lastIndexOf('\\'));
  if (k >= 0) n = n.substring(k + 1);
  n.trim();
  return n;
}

// =====================================================================
static const char INDEX_HTML[] = R"HTML(<!doctype html><html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>墨芯 · 无线传输</title>
<style>
:root{--bg:#f6f4ee;--card:#fff;--ink:#1b1b1b;--muted:#6f6a60;--line:#e4dfd3;--accent:#2f6f4f;--accent-ink:#fff;--danger:#b03a2e;--track:#ece8dd;--shadow:0 1px 2px rgba(0,0,0,.04),0 10px 30px rgba(0,0,0,.05)}
@media (prefers-color-scheme:dark){:root{--bg:#141412;--card:#1c1c19;--ink:#ece9e1;--muted:#9a948a;--line:#2c2b26;--accent:#6fbf92;--accent-ink:#10231a;--danger:#e07a6a;--track:#262520;--shadow:none}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.6 -apple-system,"Segoe UI","Microsoft YaHei","PingFang SC",sans-serif;-webkit-font-smoothing:antialiased}
code{font-family:ui-monospace,Consolas,monospace;font-size:.92em;background:var(--track);padding:1px 5px;border-radius:5px}
.wrap{max-width:1080px;margin:0 auto;padding:28px 20px 52px}
header.top{display:flex;align-items:center;gap:16px;margin-bottom:20px;flex-wrap:wrap}
.brand{display:flex;align-items:center;gap:14px;min-width:0}
.mark{width:46px;height:46px;border-radius:14px;display:grid;place-items:center;background:var(--accent);color:var(--accent-ink);font-size:22px;font-weight:600;box-shadow:var(--shadow);flex:none}
h1{font-size:20px;margin:0;letter-spacing:.3px}
.tag{margin:2px 0 0;color:var(--muted);font-size:13px}
.pill{margin-left:auto;display:inline-flex;align-items:center;gap:8px;padding:7px 13px;border-radius:999px;background:var(--card);border:1px solid var(--line);font-size:13px;color:var(--muted);white-space:nowrap}
.pill i{width:8px;height:8px;border-radius:50%;background:#c9a227;flex:none}
.pill.ok i{background:var(--accent)}
.pill.bad i{background:var(--danger)}
.grid{display:grid;grid-template-columns:1.12fr .88fr;gap:18px;align-items:start}
.col{display:flex;flex-direction:column;gap:18px;min-width:0}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:var(--shadow)}
.head{display:flex;align-items:center;gap:10px;margin-bottom:12px}
.head h2{font-size:14px;margin:0;font-weight:600}
.count{font-size:12px;color:var(--muted);background:var(--track);border-radius:999px;padding:1px 8px;min-width:22px;text-align:center}
.ghost{margin-left:auto;border:1px solid var(--line);background:transparent;color:var(--muted);border-radius:9px;padding:4px 10px;font-size:12px;cursor:pointer}
.ghost:hover{color:var(--ink);border-color:var(--muted)}
.covers{display:grid;grid-template-columns:repeat(3,1fr);gap:14px 12px;margin-bottom:14px}
.book{perspective:900px}
.cover{position:relative;aspect-ratio:3/4.35;border-radius:3px 8px 8px 3px;background:linear-gradient(100deg,color-mix(in srgb,var(--track) 72%,var(--card)) 0%,var(--card) 20%,var(--card) 86%,color-mix(in srgb,var(--track) 52%,var(--card)) 100%);border:1px solid color-mix(in srgb,var(--line) 75%,var(--card));box-shadow:0 2px 3px rgba(0,0,0,.14),0 10px 18px -6px rgba(0,0,0,.18);transform:rotateY(-7deg);transition:transform .18s ease,box-shadow .18s ease;display:flex;align-items:center;justify-content:center;padding:14px 12px 28px 19px;overflow:hidden}
.book:hover .cover{transform:rotateY(-2deg) translateY(-3px);box-shadow:0 4px 6px rgba(0,0,0,.16),0 18px 28px -8px rgba(0,0,0,.24)}
.cover::before{content:"";position:absolute;left:0;top:0;bottom:0;width:9px;background:linear-gradient(90deg,color-mix(in srgb,var(--line) 90%,var(--card)) 0 55%,transparent 100%);box-shadow:inset -1px 0 0 color-mix(in srgb,var(--line) 65%,transparent)}
.cover::after{content:"";position:absolute;right:1px;top:8px;bottom:8px;width:3px;background:repeating-linear-gradient(180deg,color-mix(in srgb,var(--line) 78%,var(--card)) 0 1px,transparent 1px 3px);opacity:.85}
.cover .t{position:relative;z-index:1;font-size:12.5px;line-height:1.45;text-align:center;word-break:break-word;display:-webkit-box;-webkit-line-clamp:4;-webkit-box-orient:vertical;overflow:hidden}
.cover .m{position:absolute;z-index:1;left:0;right:0;bottom:8px;text-align:center;font-size:11px;color:var(--muted)}
.cover .x{position:absolute;z-index:2;top:3px;right:4px;border:0;background:transparent;color:var(--muted);font-size:15px;line-height:1;cursor:pointer;opacity:.35;padding:2px 4px}
.cover:hover .x{opacity:1;color:var(--danger)}
.rows{list-style:none;margin:0 0 14px;padding:0;display:flex;flex-direction:column;gap:8px}
.rows li{display:flex;align-items:center;gap:12px;padding:9px 10px;border-radius:12px;background:var(--card)}
.rows li:hover{background:color-mix(in srgb,var(--track) 55%,transparent)}
.badge{width:38px;height:38px;border-radius:10px;flex:none;display:grid;place-items:center;font-size:10.5px;font-weight:700;letter-spacing:.4px;background:var(--track);color:var(--muted)}
.badge.epub{background:color-mix(in srgb,#7a5cc0 18%,var(--track));color:#7a5cc0}
.badge.efnt{background:color-mix(in srgb,#b07d2a 20%,var(--track));color:#a5761f}
.main{flex:1;min-width:0}
.name{font-size:14px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.meta{font-size:12px;color:var(--muted)}
.del{border:1px solid var(--line);background:transparent;color:var(--muted);border-radius:8px;padding:3px 9px;font-size:12px;cursor:pointer}
.del:hover{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 45%,var(--line))}
.drop{border:1.5px dashed #cfc7b6;border-radius:14px;padding:16px;text-align:center;color:var(--muted);font-size:13px;transition:.15s}
.drop.on{border-color:var(--accent);background:color-mix(in srgb,var(--accent) 7%,transparent)}
.btn{display:inline-flex;align-items:center;gap:6px;padding:8px 15px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--ink);font-size:14px;cursor:pointer;user-select:none}
.btn:hover{border-color:var(--muted)}
.btn.primary{background:var(--accent);border-color:var(--accent);color:var(--accent-ink)}
.btn.primary:hover{filter:brightness(1.05)}
.upload{margin-top:10px;display:flex;flex-direction:column;gap:8px}
.upRow{display:flex;align-items:center;gap:10px;font-size:12px;color:var(--muted)}
.upName{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--ink)}
.bar{width:120px;height:6px;background:var(--track);border-radius:3px;overflow:hidden;flex:none}
.bar i{display:block;height:100%;width:0;background:var(--accent);border-radius:3px;transition:width .2s}
.upPct{width:38px;text-align:right;font-variant-numeric:tabular-nums}
.field{display:block;font-size:13px;color:var(--muted);margin-bottom:10px}
.field input,.field select{display:block;width:100%;margin-top:4px;padding:9px 11px;border:1px solid var(--line);border-radius:10px;background:var(--card);color:var(--ink);font-size:15px;font-family:inherit}
.field input:focus,.field select:focus{outline:none;border-color:var(--accent)}
.actions{display:flex;align-items:center;gap:12px;margin:4px 0 10px}
.note{color:var(--muted);font-size:12.5px;line-height:1.6;margin:6px 0}
.msg{font-size:12.5px;color:var(--accent)}
.msg.bad{color:var(--danger)}
footer{margin-top:24px;text-align:center;color:var(--muted);font-size:12px}
@media (max-width:860px){.grid{grid-template-columns:1fr}.pill{margin-left:0}}
</style></head><body>
<div class="wrap">
<header class="top">
<div class="brand"><div class="mark">墨</div><div><h1>墨芯 · 无线传输</h1><p class="tag">同一 Wi-Fi，拖拽传书</p></div></div>
<div class="pill" id="pill"><i></i><span id="infoText">读取设备…</span></div>
</header>
<div class="grid">
<div class="col">
<section class="card">
<div class="head"><h2>书</h2><span class="count" id="bookCount">0</span><button class="ghost" id="refresh">刷新</button></div>
<div class="covers" id="books"></div>
<div class="drop" id="dropBooks">把 TXT / EPUB 拖到这里，或 <label class="btn">选择文件<input id="fB" type="file" multiple accept=".txt,.epub" hidden></label></div>
<div class="upload" id="upBooks"></div>
</section>
<section class="card">
<div class="head"><h2>字库</h2><span class="count" id="fontCount">0</span></div>
<ul class="rows" id="fonts"></ul>
<div class="drop" id="dropFonts">把 .efnt 拖到这里，或 <label class="btn">选择文件<input id="fF" type="file" accept=".efnt" hidden></label></div>
<div class="upload" id="upFonts"></div>
</section>
</div>
<div class="col">
<section class="card">
<div class="head"><h2>朗读设置</h2><span class="tag" id="ttsVoice" style="margin-left:auto"></span></div>
<label class="field">模型<select id="ttsModel"><option value="edge">Edge 朗读（免费）</option><option value="qwen-audio-3.1-tts-plus">qwen-audio-3.1-tts-plus（付费）</option></select></label>
<label class="field">API Key（只有 plus 用）<input id="ttsKey" type="password" placeholder="sk-..." autocomplete="off"></label>
<label class="field">业务空间 ID（只有 plus 用）<input id="ttsWs" placeholder="百炼控制台右上角复制"></label>
<label class="field">语速（0.5～2.0）<input id="ttsSpeed" type="number" step="0.05" min="0.5" max="2"></label>
<div class="actions"><button class="btn primary" id="ttsSave">保存</button><span class="msg" id="ttsMsg"></span></div>
<p class="note">Edge 免费，但要设备时间准确（开机自动联网对时）；plus 走阿里云百炼，按字符计费。</p>
</section>
<section class="card">
<div class="head"><h2>固件升级</h2></div>
<p class="note">选 firmware.bin。传完设备自动重启，书、进度和设置都不会丢；传坏了仍留在旧固件。</p>
<label class="btn primary">选择固件<input id="fW" type="file" accept=".bin" hidden></label>
<div class="upload" id="upFw"></div>
</section>
</div>
</div>
<footer>墨芯阅读器 · 同一局域网 · 传完自动刷新</footer>
</div>
<script>
const $=id=>document.getElementById(id);
const fmt=b=>b>=1048576?(b/1048576).toFixed(1)+' MB':b>=1024?(b/1024).toFixed(0)+' KB':b+' B';
const extOf=n=>(n.split('.').pop()||'').toLowerCase();
function badge(ext){const d=document.createElement('div');d.className='badge '+(ext==='epub'?'epub':ext==='efnt'?'efnt':'');d.textContent=(ext||'?').toUpperCase();return d}

function info(){fetch('/api/info').then(r=>r.json()).then(j=>{
  $('infoText').textContent='固件 '+j.fw+' · 电量 '+j.bat+(j.usb?'（插电）':'')+' · TF 剩 '+j.free;
  $('pill').className='pill ok';
}).catch(()=>{$('infoText').textContent='设备没响应';$('pill').className='pill bad'})}

function cover(it){
  const wrap=document.createElement('div');wrap.className='book';
  const c=document.createElement('div');c.className='cover';
  const t=document.createElement('div');t.className='t';t.textContent=it.n.replace(/\.(txt|epub)$/i,'');
  const m=document.createElement('div');m.className='m';m.textContent=fmt(it.s);
  const x=document.createElement('button');x.className='x';x.textContent='×';x.title='删除';
  x.onclick=()=>{if(!confirm('删除 '+it.n+'？'))return;fetch('/api/delete?path='+encodeURIComponent('/books/'+it.n),{method:'POST'}).then(()=>{books();info()})};
  c.append(t,m,x);wrap.append(c);return wrap;
}
function books(){fetch('/api/list?dir=/books').then(r=>r.json()).then(j=>{
  const el=$('books');el.innerHTML='';$('bookCount').textContent=j.files.length;
  if(!j.files.length){el.innerHTML='<div class="note" style="grid-column:1/-1">还没有书</div>';return}
  j.files.forEach(f=>el.append(cover(f)));
})}
function fonts(){fetch('/api/list?dir=/fonts').then(r=>r.json()).then(j=>{
  const el=$('fonts');el.innerHTML='';$('fontCount').textContent=j.files.length;
  if(!j.files.length){el.innerHTML='<li class="note">还没有字库</li>';return}
  j.files.forEach(f=>{const li=document.createElement('li');li.append(badge('efnt'));
    const m=document.createElement('div');m.className='main';
    const n=document.createElement('div');n.className='name';n.textContent=f.n;
    const s=document.createElement('div');s.className='meta';s.textContent=fmt(f.s);m.append(n,s);
    const d=document.createElement('button');d.className='del';d.textContent='删除';
    d.onclick=()=>{if(!confirm('删除 '+f.n+'？'))return;fetch('/api/delete?path='+encodeURIComponent('/fonts/'+f.n),{method:'POST'}).then(()=>{fonts();info()})};
    li.append(m,d);el.append(li)});
})}

function upRow(container,name){const row=document.createElement('div');row.className='upRow';
  const n=document.createElement('span');n.className='upName';n.textContent=name;
  const bar=document.createElement('span');bar.className='bar';const fill=document.createElement('i');bar.append(fill);
  const pct=document.createElement('span');pct.className='upPct';pct.textContent='0%';
  row.append(n,bar,pct);container.prepend(row);return{row,fill,pct}}

function upload(files,dir,container,after){
  const list=[...files];if(!list.length)return;
  let i=0;
  const next=()=>{
    if(i>=list.length){after&&after();return}
    const f=list[i++],ui=upRow(container,f.name);
    const x=new XMLHttpRequest();x.open('POST','/upload?dir='+encodeURIComponent(dir));
    const fd=new FormData();fd.append('f',f,f.name);
    x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(100*e.loaded/e.total);ui.fill.style.width=p+'%';ui.pct.textContent=p+'%'}};
    x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
      ui.fill.style.width='100%';ui.pct.textContent=j.ok?'完成':'失败';
      if(!j.ok){ui.pct.style.color='var(--danger)';ui.pct.style.width='auto'}
      setTimeout(()=>{ui.row.remove();next()},j.ok?350:1400)};
    x.onerror=()=>{ui.pct.textContent='断开';ui.pct.style.color='var(--danger)';setTimeout(()=>{ui.row.remove();next()},1200)};
    x.send(fd);
  };
  next();
}

$('fB').onchange=e=>{upload(e.target.files,'/books','upBooks',()=>{books();info()});e.target.value=''};
$('fF').onchange=e=>{upload(e.target.files,'/fonts','upFonts',()=>{fonts();info()});e.target.value=''};
$('fW').onchange=e=>{
  const f=e.target.files[0];if(!f)return;
  if(!confirm('用 '+f.name+' 升级固件？传完设备会重启')){e.target.value='';return}
  const ui=upRow($('upFw'),f.name);
  const x=new XMLHttpRequest();x.open('POST','/fw');
  const fd=new FormData();fd.append('f',f,f.name);
  x.upload.onprogress=ev=>{if(ev.lengthComputable){const p=Math.round(100*ev.loaded/ev.total);ui.fill.style.width=p+'%';ui.pct.textContent=p+'%'}};
  x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
    ui.pct.textContent=j.ok?'升级中…':'失败';if(!j.ok)ui.pct.style.color='var(--danger)'};
  x.onerror=()=>{ui.pct.textContent='断开'};
  x.send(fd);e.target.value='';
};

function tts(){fetch('/api/tts').then(r=>r.json()).then(j=>{
  $('ttsModel').value=j.model;$('ttsWs').value=j.ws||'';$('ttsSpeed').value=j.speed;
  $('ttsKey').placeholder=j.key?'已设置（…'+j.tail+'），留空不改':'sk-...';
  $('ttsVoice').textContent=j.voice||'';
})}
$('ttsSave').onclick=()=>{
  const body=new URLSearchParams();
  body.set('model',$('ttsModel').value);
  const k=$('ttsKey').value.trim();if(k)body.set('key',k);
  body.set('ws',$('ttsWs').value.trim());
  body.set('speed',$('ttsSpeed').value);
  fetch('/api/tts',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body}).then(r=>r.json()).then(j=>{
    const m=$('ttsMsg');m.textContent=j.msg||'';m.className='msg '+(j.ok?'':'bad');tts();
  });
};

function bindDrop(el,dir,container,after){
  el.ondragover=e=>{e.preventDefault();el.classList.add('on')};
  el.ondragleave=()=>el.classList.remove('on');
  el.ondrop=e=>{e.preventDefault();el.classList.remove('on');
    const fs=[...e.dataTransfer.files].filter(f=>dir==='/fonts'?/\.efnt$/i.test(f.name):/\.(txt|epub)$/i.test(f.name));
    upload(fs,dir,container,after)};
}
bindDrop($('dropBooks'),'/books','upBooks',()=>{books();info()});
bindDrop($('dropFonts'),'/fonts','upFonts',()=>{fonts();info()});
document.addEventListener('dragover',e=>e.preventDefault());
document.addEventListener('drop',e=>{
  e.preventDefault();
  const fs=[...e.dataTransfer.files];if(!fs.length)return;
  const bin=fs.find(f=>/\.bin$/i.test(f.name));
  if(bin){$('fW').files=e.dataTransfer.files;$('fW').onchange({target:$('fW')});return}
  const fonts=fs.filter(f=>/\.efnt$/i.test(f.name));
  const books=fs.filter(f=>/\.(txt|epub)$/i.test(f.name));
  if(books.length)upload(books,'/books','upBooks',()=>{books();info()});
  if(fonts.length)upload(fonts,'/fonts','upFonts',()=>{fonts();info()});
});
$('refresh').onclick=()=>{books();fonts();info()};
info();books();fonts();tts();
</script></body></html>)HTML";

static void listDir(const String &dir, const String &prefix, String &out, int depth) {
  for (auto &e : sdList(dir.c_str(), 300)) {
    if (e.name.startsWith(".")) continue;
    if (e.dir) {
      if (depth > 0) listDir(e.path, prefix + e.name + "/", out, depth - 1);
      continue;
    }
    if (e.name.endsWith(".part")) continue;
    if (out.length() > 1) out += ",";
    out += "{\"n\":" + jsonStr(prefix + e.name) + ",\"s\":" + String(e.size) + "}";
  }
}

static void apiList() {
  String dir = s_srv->arg("dir");
  String files = "[";
  if (s_sd && allowedDir(dir)) listDir(dir, "", files, 2);
  files += "]";
  s_srv->send(200, "application/json", "{\"files\":" + files + "}");
}

static void apiInfo() {
  const SysState &s = app::sys();
  String fr = s_sd ? sizeText(SD_MMC.totalBytes() - SD_MMC.usedBytes()) : String("（没有卡）");
  s_srv->send(200, "application/json",
              String("{\"fw\":") + jsonStr(FW_VERSION) + ",\"bat\":" + jsonStr(s.bat.ok ? String(s.bat.soc) + "%" : "?") +
                  ",\"usb\":" + (s.usb ? "true" : "false") + ",\"free\":" + jsonStr(fr) + "}");
}

static void apiDelete() {
  String p = s_srv->arg("path");
  bool ok = s_sd && (p.startsWith("/books/") || p.startsWith("/fonts/")) && p.indexOf("..") < 0 && SD_MMC.remove(p);
  if (ok) note("删掉了 " + baseName(p));
  s_srv->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ---- 语音朗读的 API Key / 模型 / 业务空间 ID（Key 只回后 4 位） ----
static void apiTts() {
  const Settings &st = settings();
  String tail = st.ttsKey.length() > 4 ? st.ttsKey.substring(st.ttsKey.length() - 4) : String();
  s_srv->send(200, "application/json",
              String("{\"key\":") + (st.ttsKey.length() ? "true" : "false") + ",\"tail\":" + jsonStr(tail) +
                  ",\"voice\":" + jsonStr(st.ttsVoice) + ",\"vol\":" + String(st.ttsVolume) +
                  ",\"speed\":" + String(st.ttsSpeed, 2) +
                  ",\"model\":" + jsonStr(st.ttsModel) + ",\"ws\":" + jsonStr(st.ttsWs) + "}");
}

static void apiTtsSet() {
  Settings &st = settings();
  bool ok = true;
  String msg;

  if (s_srv->hasArg("key")) {
    String key = s_srv->arg("key");
    key.trim();
    if (!key.length()) {
      // 留空 = 不改
    } else if (key.length() >= 16 && key.startsWith("sk-")) {
      st.ttsKey = key;
      tts::setKey(key);
      msg += "Key 已保存（…" + key.substring(key.length() - 4) + "）；";
    } else {
      ok = false;
      msg += "Key 看着不对（sk- 开头的一长串）；";
    }
  }
  if (s_srv->hasArg("model")) {
    String model = s_srv->arg("model");
    model.trim();
    if (model == TTS_MODEL_EDGE || model == TTS_MODEL_PLUS) {
      if (model != st.ttsModel) {  // 只在真的换了模型时才重置音色（音色名不能跨模型）
        st.ttsModel = model;
        st.ttsVoice = tts::defaultVoice(model);
        tts::setVoice(st.ttsVoice);
      }
      tts::setModel(st.ttsModel, st.ttsWs);
      msg += "模型 " + tts::modelLabel(st.ttsModel) + "；";
    } else {
      ok = false;
      msg += "模型不对；";
    }
  }
  if (s_srv->hasArg("speed")) {
    String sp = s_srv->arg("speed");
    sp.trim();
    float v = sp.toFloat();
    if (!sp.length() || v < TTS_SPEED_MIN - 0.001f || v > TTS_SPEED_MAX + 0.001f) {
      ok = false;
      msg += "语速要在 0.5～2 之间；";
    } else {
      st.ttsSpeed = v;
      tts::setSpeed(v);
      msg += "语速 " + String(v, 2) + " 倍；";
    }
  }
  if (s_srv->hasArg("ws")) {
    String ws = s_srv->arg("ws");
    ws.trim();
    st.ttsWs = ws;
    tts::setModel(st.ttsModel, st.ttsWs);
    msg += ws.length() ? "业务空间 ID 已保存；" : "业务空间 ID 已清除；";
  }
  settingsSave();
  if (msg.endsWith("；")) msg = msg.substring(0, msg.length() - 3);
  if (ok) note("语音朗读设置已更新");
  s_srv->send(200, "application/json",
              String("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":" + jsonStr(msg) + "}");
}

// ---- 上传文件 ----
static void flushBuf() {
  if (s_wlen && s_up && s_up.write(s_wbuf, s_wlen) != s_wlen) s_upOk = false;
  s_wlen = 0;
}

static void onUpload() {
  HTTPUpload &u = s_srv->upload();
  app::markActivity();
  if (u.status == UPLOAD_FILE_START) {
    String dir = s_srv->arg("dir");
    s_upName = baseName(u.filename);
    s_upErr = "";
    s_upOk = s_sd && s_wbuf && allowedDir(dir) && s_upName.length() && !s_upName.startsWith(".") && okExt(dir, s_upName);
    if (!s_upOk) { s_upErr = s_sd ? "只能传 TXT、EPUB（书）和 EFNT（字库）" : "没有 TF 卡"; return; }
    if (!SD_MMC.exists(dir)) SD_MMC.mkdir(dir);
    s_upPath = dir + "/" + s_upName;
    s_upTmp = s_upPath + ".part";  // 先写临时文件，传完再改名，断了不会留下半本书
    if (SD_MMC.exists(s_upTmp)) SD_MMC.remove(s_upTmp);
    s_up = SD_MMC.open(s_upTmp, FILE_WRITE);
    if (!s_up) { s_upOk = false; s_upErr = "建不了文件"; }
    s_upBytes = 0;
    s_wlen = 0;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!s_upOk) return;
    const uint8_t *p = u.buf;
    size_t n = u.currentSize;
    while (n) {
      size_t k = min(n, WBUF - s_wlen);
      memcpy(s_wbuf + s_wlen, p, k);
      s_wlen += k;
      p += k;
      n -= k;
      if (s_wlen == WBUF) flushBuf();
    }
    s_upBytes += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END) {
    if (!s_upOk) return;
    flushBuf();
    s_up.close();
    if (s_upOk) {
      if (SD_MMC.exists(s_upPath)) SD_MMC.remove(s_upPath);
      s_upOk = SD_MMC.rename(s_upTmp, s_upPath);
    }
    if (!s_upOk) {
      SD_MMC.remove(s_upTmp);
      s_upErr = "写卡失败（卡满了？）";
      return;
    }
    note("收到 " + s_upName + "（" + sizeText(s_upBytes) + "）");
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (s_up) s_up.close();
    if (s_upTmp.length()) SD_MMC.remove(s_upTmp);
    s_upOk = false;
    s_upErr = "传输中断";
  }
}

static void uploadDone() {
  String msg = s_upOk ? s_upName + " 已存到卡上（" + sizeText(s_upBytes) + "）" : s_upName + "：" + s_upErr;
  s_srv->send(200, "application/json", String("{\"ok\":") + (s_upOk ? "true" : "false") + ",\"msg\":" + jsonStr(msg) + "}");
}

// ---- 固件升级 ----
static void onFirmware() {
  HTTPUpload &u = s_srv->upload();
  app::markActivity();
  if (u.status == UPLOAD_FILE_START) {
    s_fwOk = true;
    s_fwErr = "";
    String l = u.filename;
    l.toLowerCase();
    if (!l.endsWith(".bin")) { s_fwOk = false; s_fwErr = "要选 firmware.bin"; return; }
    ota::confirm("开始网页升级");  // 试运行中不能开始下一次升级（esp_ota_begin 会拒绝）
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { s_fwOk = false; s_fwErr = Update.errorString(); return; }
    note("正在接收新固件…");
    app::invalidate(Refresh::Partial);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (s_fwOk && Update.write(u.buf, u.currentSize) != u.currentSize) { s_fwOk = false; s_fwErr = Update.errorString(); }
  } else if (u.status == UPLOAD_FILE_END) {
    if (s_fwOk && !Update.end(true)) { s_fwOk = false; s_fwErr = String("校验不过：") + Update.errorString(); }
    if (!s_fwOk) Update.abort();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    s_fwOk = false;
    s_fwErr = "传输中断";
  }
}

static void firmwareDone() {
  String msg = s_fwOk ? String("新固件收好了，设备正在重启（十几秒后刷新这个页面）") : "升级失败：" + s_fwErr + "（还是原来的固件）";
  s_srv->send(200, "application/json", String("{\"ok\":") + (s_fwOk ? "true" : "false") + ",\"msg\":" + jsonStr(msg) + "}");
  if (s_fwOk) {
    note("新固件收好了，马上重启");
    s_reboot = true;
    s_rebootAt = millis() + 1500;  // 让网页先收到回复
  } else {
    note("固件升级失败：" + s_fwErr);
  }
}

// =====================================================================
bool transferStart(String *why) {
  if (s_on) return true;
  if (tts::active()) tts::stop();  // 语音朗读也在用 Wi-Fi，先停掉
  while (busy()) delay(50);  // 后台对时正在用 Wi-Fi
  setCpuFrequencyMhz(240);
  String w;
  bool sta = hasCredentials() && connect(12000, &w);
  if (sta) {
    s_ap = false;
    s_url = "http://" + WiFi.localIP().toString() + "/";
  } else {
    WiFi.mode(WIFI_AP);
    if (!startAp()) {
      off();
      setCpuFrequencyMhz(CPU_MHZ);
      if (why) *why = "开不了热点";
      return false;
    }
    s_ap = true;
    s_url = "http://192.168.4.1/";
  }
  WiFi.setSleep(false);
  s_sd = sdMount();
  if (!s_wbuf) s_wbuf = (uint8_t *)malloc(WBUF);
  if (MDNS.begin("inkreader")) MDNS.addService("http", "tcp", 80);
  s_srv = new WebServer(80);
  s_srv->on("/", HTTP_GET, [] { app::markActivity(); s_srv->send(200, "text/html; charset=utf-8", INDEX_HTML); });
  s_srv->on("/api/list", HTTP_GET, [] { app::markActivity(); apiList(); });
  s_srv->on("/api/info", HTTP_GET, [] { apiInfo(); });
  s_srv->on("/api/delete", HTTP_POST, [] { app::markActivity(); apiDelete(); });
  s_srv->on("/api/tts", HTTP_GET, [] { apiTts(); });
  s_srv->on("/api/tts", HTTP_POST, [] { app::markActivity(); apiTtsSet(); });
  s_srv->on("/upload", HTTP_POST, uploadDone, onUpload);
  s_srv->on("/fw", HTTP_POST, firmwareDone, onFirmware);
  s_srv->onNotFound([] { s_srv->sendHeader("Location", "/", true); s_srv->send(302, "text/plain", ""); });
  s_srv->begin();
  s_on = true;
  s_reboot = false;
  note(sta ? "已连上 " + savedSsid() + "，等浏览器打开" : (hasCredentials() ? "连不上家里的 Wi-Fi（" + w + "），改用设备热点" : "还没配网，用设备热点"));
  if (!s_sd) note("没有 TF 卡：只能升级固件");
  return true;
}

void transferStop() {
  if (!s_on) return;
  s_srv->stop();
  delete s_srv;
  s_srv = nullptr;
  MDNS.end();
  if (s_up) s_up.close();
  if (s_wbuf) { free(s_wbuf); s_wbuf = nullptr; }
  if (s_sd) sdUnmount();
  s_sd = false;
  if (s_ap) WiFi.softAPdisconnect(true);
  off();
  s_on = false;
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.println("[传输] 结束，Wi-Fi 关了");
}

void transferLoop() {
  if (!s_on) return;
  s_srv->handleClient();
  if (s_reboot && (int32_t)(millis() - s_rebootAt) > 0) {
    Serial.println("[传输] 重启进新固件");
    Serial.flush();
    ESP.restart();  // 软件重启：PWR_HOLD 的保持不会丢
  }
}

}  // namespace net
