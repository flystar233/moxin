#include "wifi.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "app/app.h"
#include "board/sensors.h"
#include "config.h"
#include "voice/tts.h"

namespace net {

bool busy();

static Preferences s_nvs;
static bool s_nvsOpen = false;

static void nvs() {
  if (!s_nvsOpen) s_nvsOpen = s_nvs.begin("wifi", false);
}

static int8_t s_hasCred = -1;  // 缓存：主循环每轮都会问

bool hasCredentials() {
  if (s_hasCred < 0) {
    nvs();
    s_hasCred = s_nvs.getString("ssid", "").length() > 0;
  }
  return s_hasCred;
}

String savedSsid() {
  nvs();
  return s_nvs.getString("ssid", "");
}

void forget() {
  nvs();
  s_nvs.remove("ssid");
  s_nvs.remove("pass");
  s_hasCred = 0;
}

static void save(const String &ssid, const String &pass) {
  nvs();
  s_nvs.putString("ssid", ssid);
  s_nvs.putString("pass", pass);
  s_hasCred = 1;
}

void off() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

static String statusWhy(wl_status_t st) {
  switch (st) {
    case WL_NO_SSID_AVAIL: return "找不到这个 Wi-Fi（只支持 2.4GHz）";
    case WL_CONNECT_FAILED: return "连不上，密码可能不对";
    case WL_CONNECTION_LOST: return "连接断了";
    default: return "连接超时（密码不对或信号太弱）";
  }
}

bool connect(uint32_t timeoutMs, String *why) {
  nvs();
  String ssid = s_nvs.getString("ssid", ""), pass = s_nvs.getString("pass", "");
  if (!ssid.length()) {
    if (why) *why = "还没配网";
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[网] 连上 %s，IP %s，%lu ms\n", ssid.c_str(), WiFi.localIP().toString().c_str(),
                  (unsigned long)(millis() - t0));
    return true;
  }
  if (why) *why = statusWhy(WiFi.status());
  Serial.printf("[网] 连不上 %s：%s\n", ssid.c_str(), statusWhy(WiFi.status()).c_str());
  return false;
}

// 北京时间；阿里云的 NTP 在国内最稳，再备两个。只写时钟芯片，不碰界面（后台任务里也能调）
static bool ntpToRtc(String *why, DateTime *out) {
  configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
  struct tm tm;
  if (!getLocalTime(&tm, 8000) || tm.tm_year + 1900 < 2024) {
    if (why) *why = "网络对时没有响应";
    return false;
  }
  DateTime t{(uint16_t)(tm.tm_year + 1900), (uint8_t)(tm.tm_mon + 1), (uint8_t)tm.tm_mday, (uint8_t)tm.tm_hour,
             (uint8_t)tm.tm_min, (uint8_t)tm.tm_sec, 0};
  rtcWrite(t);
  if (out) *out = t;
  Serial.printf("[网] 对时 %04d-%02d-%02d %02d:%02d:%02d\n", t.year, t.month, t.day, t.hour, t.minute, t.second);
  return true;
}

static uint32_t dayOf(const DateTime &t) { return (uint32_t)t.year * 10000 + t.month * 100 + t.day; }

static void markSynced(const DateTime &t) {
  nvs();
  s_nvs.putUInt("syncDay", dayOf(t));
}

bool syncTime(String *why) {
  DateTime t;
  if (!ntpToRtc(why, &t)) return false;
  markSynced(t);
  app::refreshSensors(true);
  return true;
}

// =====================================================================
// 每天自动对时：后台任务里连网、对时、断开，主循环照常跑
// =====================================================================
static volatile bool s_syncBusy = false, s_syncDone = false, s_syncOk = false;
RTC_DATA_ATTR static uint32_t r_failStamp = 0;  // 上次失败的 年月日时：同一小时不再试（深睡醒来也记得）

static void syncTask(void *) {
  String why;
  DateTime t;
  bool ok = connect(12000, &why) && ntpToRtc(&why, &t);
  off();
  if (ok) markSynced(t);
  s_syncOk = ok;
  s_syncDone = true;
  s_syncBusy = false;
  vTaskDelete(nullptr);
}

void autoSyncTick(const DateTime &now, bool timeOk, bool allowed) {
  if (s_syncBusy || !allowed || busy()) return;
  nvs();
  uint32_t day = dayOf(now);
  if (timeOk && s_nvs.getUInt("syncDay", 0) == day) return;  // 今天对过了
  uint32_t stamp = day * 100 + now.hour;
  if (r_failStamp == stamp) return;
  r_failStamp = stamp;  // 先记上；成功了也无妨（今天不会再进来）
  s_syncBusy = true;
  Serial.println("[网] 每天自动对时：后台联网");
  xTaskCreatePinnedToCore(syncTask, "ntp", 6144, nullptr, 2, nullptr, 0);
}

bool autoSyncResult(bool &ok) {
  if (!s_syncDone) return false;
  s_syncDone = false;
  ok = s_syncOk;
  return true;
}

// =====================================================================
// 热点配网
// =====================================================================
static DNSServer *s_dns = nullptr;
static WebServer *s_web = nullptr;
static ProvState s_state = PROV_OFF;
static String s_apSsid, s_apPass, s_msg, s_trySsid, s_tryPass;
static uint32_t s_tryAt = 0;
static std::vector<std::pair<String, int>> s_nets;  // 扫到的网络（名字，信号）
static const IPAddress AP_IP(192, 168, 4, 1);

ProvState provState() { return s_state; }
String provApSsid() { return s_apSsid; }
String provApPass() { return s_apPass; }
String provMessage() { return s_msg; }

static void setState(ProvState st, const String &msg) {
  s_state = st;
  s_msg = msg;
  Serial.printf("[配网] %s\n", msg.c_str());
}

static String html(const String &s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String json(const String &s) {
  String o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

static const char PAGE_HEAD[] = R"(<!doctype html><html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>墨水屏阅读器 · 配网</title>
<style>body{font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;max-width:420px;margin:28px auto;padding:0 18px;color:#222;background:#faf9f6}
h1{font-size:21px;font-weight:600;margin:0 0 6px}label{display:block;margin:16px 0 6px;font-size:15px}
select,input{width:100%;font-size:17px;padding:11px;box-sizing:border-box;border:1px solid #bbb;border-radius:9px;background:#fff}
button{margin-top:20px;width:100%;padding:13px;font-size:17px;border:0;border-radius:9px;background:#222;color:#fff}
.tip{color:#777;font-size:14px;line-height:1.6}a{color:#222}</style></head><body>)";

static void pageForm() {
  String opts;
  for (auto &n : s_nets) {
    const char *q = n.second > -60 ? "信号强" : n.second > -75 ? "信号中" : "信号弱";
    opts += "<option value=\"" + html(n.first) + "\">" + html(n.first) + "（" + q + "）</option>";
  }
  String body = String(PAGE_HEAD) +
                "<h1>连接 Wi-Fi</h1><p class=tip>选择家里的 Wi-Fi，输入密码。只支持 2.4GHz。</p>"
                "<form method=post action=/save><label>Wi-Fi 名称</label>"
                "<select name=s id=s onchange=\"o.style.display=this.value==''?'block':'none'\">" + opts +
                "<option value=\"\">手动输入…</option></select>"
                "<input name=o id=o placeholder=\"输入 Wi-Fi 名称\" style=\"display:none;margin-top:8px\">"
                "<label>密码</label><input name=p id=p type=password autocomplete=off>"
                "<label class=tip><input type=checkbox style=\"width:auto\" onclick=\"p.type=this.checked?'text':'password'\"> 显示密码</label>"
                "<button>连接</button></form></body></html>";
  if (!s_nets.size()) body.replace("onchange=", "data-x=");  // 没扫到网络：直接手动输入
  if (!s_nets.size()) body.replace("style=\"display:none;margin-top:8px\"", "style=\"margin-top:8px\"");
  s_web->send(200, "text/html; charset=utf-8", body);
}

static void pageSave() {
  String ssid = s_web->arg("s");
  if (!ssid.length()) ssid = s_web->arg("o");
  ssid.trim();
  String pass = s_web->arg("p");
  if (!ssid.length()) {
    s_web->send(200, "text/html; charset=utf-8", String(PAGE_HEAD) + "<h1>没填 Wi-Fi 名称</h1><p><a href=/>返回</a></p></body></html>");
    return;
  }
  s_trySsid = ssid;
  s_tryPass = pass;
  s_tryAt = millis();
  WiFi.begin(ssid.c_str(), pass.c_str());
  setState(PROV_CONNECTING, "正在连接 " + ssid + "…");
  app::invalidate(Refresh::Partial);
  String body = String(PAGE_HEAD) +
                "<h1 id=t>正在连接 " + html(ssid) + "…</h1>"
                "<p id=m class=tip>大约 10 秒。手机和设备的连接可能会断一下，页面断开的话看设备屏幕上的结果。</p>"
                "<script>function q(){fetch('/status').then(r=>r.json()).then(j=>{t.textContent=j.title;m.innerHTML=j.msg;"
                "if(j.state=='connecting')setTimeout(q,1000)}).catch(()=>setTimeout(q,1500))}setTimeout(q,1500)</script></body></html>";
  s_web->send(200, "text/html; charset=utf-8", body);
}

static void pageStatus() {
  String st = s_state == PROV_CONNECTING ? "connecting" : s_state == PROV_OK ? "ok" : s_state == PROV_FAIL ? "fail" : "wait";
  String title = s_state == PROV_OK ? "连上了" : s_state == PROV_FAIL ? "没连上" : "正在连接…";
  String msg = json(html(s_msg));
  if (s_state == PROV_FAIL) msg += " <a href=/>重新输入</a>";
  if (s_state == PROV_OK) msg += "。可以关掉这个页面了。";
  s_web->send(200, "application/json", "{\"state\":\"" + st + "\",\"title\":\"" + title + "\",\"msg\":\"" + msg + "\"}");
}

// 手机连上热点后会访问各种"联网检测"地址：一律跳到配网页，系统就会弹出它
static void pageRedirect() {
  s_web->sendHeader("Location", "http://192.168.4.1/", true);
  s_web->send(302, "text/plain", "");
}

static void scan() {
  s_nets.clear();
  int n = WiFi.scanNetworks(false, false);
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    auto it = std::find_if(s_nets.begin(), s_nets.end(), [&](const std::pair<String, int> &p) { return p.first == s; });
    if (it == s_nets.end()) s_nets.push_back({s, WiFi.RSSI(i)});
    else it->second = max(it->second, (int)WiFi.RSSI(i));
  }
  WiFi.scanDelete();
  std::sort(s_nets.begin(), s_nets.end(), [](const std::pair<String, int> &a, const std::pair<String, int> &b) {
    return a.second > b.second;
  });
  if (s_nets.size() > 20) s_nets.resize(20);
  Serial.printf("[配网] 扫到 %u 个 Wi-Fi\n", (unsigned)s_nets.size());
}

// 开设备热点：名字 InkReader-xxxx（MAC 后两字节），密码每次随机 8 位
bool startAp() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "InkReader-%02X%02X", mac[4], mac[5]);
  s_apSsid = ssid;
  static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";  // 去掉容易看错的 i l o 0 1
  s_apPass = "";
  for (int i = 0; i < 8; i++) s_apPass += A[esp_random() % (sizeof(A) - 1)];
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  return WiFi.softAP(s_apSsid.c_str(), s_apPass.c_str());
}

bool provStart() {
  if (s_state != PROV_OFF) return true;
  if (tts::active()) tts::stop();  // 语音朗读也在用 Wi-Fi，先停掉
  while (s_syncBusy) delay(50);  // 后台对时正在用 Wi-Fi，等它完
  setCpuFrequencyMhz(240);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  scan();
  if (!startAp()) {
    setState(PROV_OFF, "开不了热点");
    off();
    setCpuFrequencyMhz(CPU_MHZ);
    return false;
  }
  s_dns = new DNSServer();
  s_dns->setErrorReplyCode(DNSReplyCode::NoError);
  s_dns->start(53, "*", AP_IP);
  s_web = new WebServer(80);
  s_web->on("/", HTTP_GET, [] {
    if (s_state == PROV_WAITING) { setState(PROV_PHONE, "手机打开了配网页面，在网页里选 Wi-Fi、输密码"); app::invalidate(Refresh::Partial); }
    app::markActivity();
    pageForm();
  });
  s_web->on("/save", HTTP_POST, [] { app::markActivity(); pageSave(); });
  s_web->on("/status", HTTP_GET, [] { app::markActivity(); pageStatus(); });
  s_web->onNotFound([] { app::markActivity(); pageRedirect(); });
  s_web->begin();
  setState(PROV_WAITING, "等手机连上热点：用 iPhone 相机、安卓相机或 WLAN 设置里的扫码功能扫 ①");
  return true;
}

void provStop() {
  if (s_state == PROV_OFF) return;
  if (s_web) { s_web->stop(); delete s_web; s_web = nullptr; }
  if (s_dns) { s_dns->stop(); delete s_dns; s_dns = nullptr; }
  WiFi.softAPdisconnect(true);
  off();
  s_state = PROV_OFF;
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.println("[配网] 热点关了");
}

void provLoop() {
  if (s_state == PROV_OFF) return;
  s_dns->processNextRequest();
  s_web->handleClient();
  if (s_state == PROV_WAITING && WiFi.softAPgetStationNum() > 0) {
    setState(PROV_PHONE, "手机连上热点了，等它弹出配网页面（没弹出就扫码 ②）");
    app::invalidate(Refresh::Partial);
  }
  if (s_state == PROV_CONNECTING) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      save(s_trySsid, s_tryPass);
      String why;
      bool t = syncTime(&why);
      setState(PROV_OK, "已连上 " + s_trySsid + "（IP " + WiFi.localIP().toString() + "）" + (t ? "，时间已校准" : "，" + why));
      app::invalidate(Refresh::Partial);
    } else if ((millis() - s_tryAt > 2000 && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL)) ||
               millis() - s_tryAt > 20000) {
      String why = statusWhy(st);
      WiFi.disconnect(false, false);  // 热点留着，可以在网页上重试
      setState(PROV_FAIL, "没连上 " + s_trySsid + "：" + why + "，请在手机网页上重新输入");
      app::invalidate(Refresh::Partial);
    }
  }
}

bool busy() { return s_syncBusy || s_state != PROV_OFF || transferActive() || tts::active(); }

}  // namespace net
