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
<meta name="viewport" content="width=device-width,initial-scale=1"><title>墨水屏阅读器</title>
<style>:root{--ink:#222;--sub:#777;--line:#e2e0da;--paper:#f7f6f2}
body{font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;max-width:640px;margin:0 auto;padding:22px 16px 48px;color:var(--ink);background:var(--paper)}
h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:26px 0 10px}.sub{color:var(--sub);font-size:14px;line-height:1.6}
.card{background:#fff;border:1px solid var(--line);border-radius:12px;padding:6px 16px 14px}
ul{list-style:none;margin:0;padding:0}li{display:flex;align-items:center;gap:10px;padding:10px 0;border-bottom:1px solid #eee}
.n{flex:1;word-break:break-all}.s{color:var(--sub);font-size:13px;white-space:nowrap}
.btn{display:inline-block;font-size:15px;padding:8px 14px;border-radius:8px;border:1px solid var(--ink);background:#fff;color:var(--ink);cursor:pointer}
.del{font-size:13px;padding:4px 10px;border-radius:7px;border:1px solid #c44;background:#fff;color:#c44;cursor:pointer}
.drop{border:2px dashed #ccc;border-radius:10px;padding:18px;text-align:center;color:var(--sub);margin-top:12px;font-size:14px}
.drop.on{border-color:var(--ink);color:var(--ink)}progress{width:100%;height:10px;margin-top:8px}</style></head><body>
<h1>墨水屏阅读器</h1><div class=sub id=info>…</div>
<h2>书（TXT / EPUB）</h2><div class=card><ul id=books><li class=sub>…</li></ul>
<div class=drop id=dropB>把书拖到这里，或 <label class=btn>选择文件<input type=file id=fB multiple accept=".txt,.epub" hidden></label></div></div>
<h2>字库（.efnt）</h2><div class=card><ul id=fonts><li class=sub>…</li></ul>
<div class=drop id=dropF>把字库拖到这里，或 <label class=btn>选择文件<input type=file id=fF multiple accept=".efnt" hidden></label></div></div>
<h2>语音朗读</h2><div class=card><p class=sub>阿里云百炼的 API Key（sk- 开头，存在设备里，只用来说书）。<br>第二个模型 <b>qwen-audio-3.1-tts-plus</b> 还要填业务空间 ID（百炼控制台右上角图标里复制），它更贵但音质更好、有免费额度。</p>
<div style="display:flex;gap:8px;margin-bottom:8px"><input id=ttsKey type=password placeholder="sk-..." autocomplete=off style="flex:1;padding:9px;border:1px solid #ccc;border-radius:8px;font-size:15px">
<button class=btn id=ttsSave>保存 Key</button></div>
<div style="display:flex;gap:8px;align-items:center"><select id=ttsModel style="flex:1;padding:9px;border:1px solid #ccc;border-radius:8px;font-size:15px">
<option value="qwen3-tts-flash">qwen3-tts-flash（0.8 元/万字符）</option>
<option value="qwen-audio-3.1-tts-plus">qwen-audio-3.1-tts-plus（约 1.44 元/万字符）</option></select>
<button class=btn id=ttsSaveModel>保存模型</button></div>
<input id=ttsWs placeholder="业务空间 ID（plus 用）" style="width:100%;box-sizing:border-box;margin-top:8px;padding:9px;border:1px solid #ccc;border-radius:8px;font-size:15px">
<p class=sub id=ttsSt>…</p></div>
<h2>固件升级</h2><div class=card><p class=sub>选 firmware.bin（电脑上 app/.pio/build/pcb1/ 里）。传完设备自动重启，书、进度和设置都不会丢；传坏了也还是原来的固件。</p>
<label class=btn>选择 firmware.bin<input type=file id=fW accept=".bin" hidden></label></div>
<p id=st class=sub style="margin-top:18px"></p><progress id=pg value=0 max=1 hidden></progress>
<script>
const $=id=>document.getElementById(id);
const mb=b=>b>=1048576?(b/1048576).toFixed(1)+' MB':Math.max(1,Math.round(b/1024))+' KB';
function list(dir,el){fetch('/api/list?dir='+dir).then(r=>r.json()).then(j=>{el.innerHTML='';
 if(!j.files.length){el.innerHTML='<li class=sub>（空）</li>';return}
 j.files.forEach(f=>{const li=document.createElement('li');li.innerHTML='<span class=n></span><span class=s>'+mb(f.s)+'</span><button class=del>删除</button>';
  li.querySelector('.n').textContent=f.n;li.querySelector('.del').onclick=()=>{if(confirm('删除 '+f.n+'？'))
  fetch('/api/delete?path='+encodeURIComponent(dir+'/'+f.n),{method:'POST'}).then(refresh)};el.appendChild(li)})})}
function refresh(){list('/books',$('books'));list('/fonts',$('fonts'));
 fetch('/api/info').then(r=>r.json()).then(j=>{$('info').textContent='固件 '+j.fw+' · 电量 '+j.bat+(j.usb?'（插着电）':'')+' · TF 卡剩 '+j.free})}
function up(url,files,i){if(i>=files.length){$('pg').hidden=true;refresh();return}
 const f=files[i],x=new XMLHttpRequest(),fd=new FormData();fd.append('f',f,f.name);x.open('POST',url);$('pg').hidden=false;
 x.upload.onprogress=e=>{$('pg').value=e.loaded/e.total;$('st').textContent='上传 '+f.name+'（'+(i+1)+'/'+files.length+'）'+Math.round(100*e.loaded/e.total)+'%'};
 x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}$('st').textContent=(j.ok?'✓ ':'✗ ')+(j.msg||('出错 '+x.status));up(url,files,i+1)};
 x.onerror=()=>{$('pg').hidden=true;$('st').textContent='✗ '+f.name+' 没传完（设备断开了？）'};x.send(fd)}
$('fB').onchange=e=>up('/upload?dir=/books',[...e.target.files],0);
$('fF').onchange=e=>up('/upload?dir=/fonts',[...e.target.files],0);
$('fW').onchange=e=>{const f=e.target.files[0];if(f&&confirm('用 '+f.name+' 升级固件？传完设备会重启'))up('/fw',[f],0)};
function ttsInfo(){fetch('/api/tts').then(r=>r.json()).then(j=>{
 $('ttsSt').textContent=(j.key?('Key 已设置（…'+j.tail+'）'):'Key 未设置')+' · 模型 '+j.model+' · 音色 '+j.voice+' · 音量 '+j.vol+'%';
 if(j.model)$('ttsModel').value=j.model; $('ttsWs').value=j.ws||'';});}
$('ttsSave').onclick=()=>{const k=$('ttsKey').value.trim();if(!k)return;
 fetch('/api/tts',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key='+encodeURIComponent(k)})
  .then(r=>r.json()).then(j=>{$('ttsSt').textContent=j.msg;if(j.ok)$('ttsKey').value='';ttsInfo()});};
$('ttsSaveModel').onclick=()=>{
 fetch('/api/tts',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'model='+encodeURIComponent($('ttsModel').value)+'&ws='+encodeURIComponent($('ttsWs').value.trim())})
  .then(r=>r.json()).then(j=>{$('ttsSt').textContent=j.msg;ttsInfo()});};
ttsInfo();
function drop(el,url,ex){el.ondragover=e=>{e.preventDefault();el.classList.add('on')};el.ondragleave=()=>el.classList.remove('on');
 el.ondrop=e=>{e.preventDefault();el.classList.remove('on');up(url,[...e.dataTransfer.files].filter(f=>ex.some(x=>f.name.toLowerCase().endsWith(x))),0)}}
drop($('dropB'),'/upload?dir=/books',['.txt','.epub']);drop($('dropF'),'/upload?dir=/fonts',['.efnt']);refresh();
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
    if (model == TTS_MODEL || model == TTS_MODEL_PLUS) {
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
