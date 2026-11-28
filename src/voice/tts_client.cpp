#include "tts_client.h"

#include <ctype.h>
#include <math.h>
#include <time.h>

#include <MP3DecoderHelix.h>
#include <WiFiClientSecure.h>
#include <esp_http_client.h>
#include <esp_random.h>
#include <mbedtls/base64.h>

#include "config.h"
#include "edge_drm.h"

// IDF 自带的根证书包（编进了框架的 libmbedtls.a）。不 include esp_crt_bundle.h：
// WiFiClientSecure 里有个同名头文件，声明的是 arduino_ 开头的另一套函数（net/update.cpp 里也是这么写的）
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

namespace tts {
namespace {

// =====================================================================
// 公共小工具
// =====================================================================
esp_http_client_config_t baseConfig(const char *url) {
  esp_http_client_config_t c = {};
  c.url = url;
  c.crt_bundle_attach = esp_crt_bundle_attach;  // 校验证书，别把 API Key 发给冒充的服务器
  c.timeout_ms = 8000;                          // 单次 socket 读超时：停了也能很快退出
  c.buffer_size = 2048;
  c.buffer_size_tx = 2048;
  c.user_agent = "InkReader/" FW_VERSION;
  return c;
}

String jsonEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if ((uint8_t)c < 0x20) {
      o += ' ';
    } else {
      o += c;
    }
  }
  return o;
}

String readSome(esp_http_client_handle_t h, int limit) {
  String o;
  char b[256];
  int n;
  while ((int)o.length() < limit && (n = esp_http_client_read(h, b, sizeof(b))) > 0) o.concat(b, n);
  return o;
}

String findUrl(const String &s) {
  int i = s.indexOf("\"url\":\"");
  if (i < 0) return "";
  i += 7;
  int j = s.indexOf('"', i);
  if (j < 0) return "";
  String u = s.substring(i, j);
  return u.startsWith("http") ? u : String();
}

// =====================================================================
// Edge 朗读：微软 Edge 在线朗读接口
//
//   连接  wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1
//         ?TrustedClientToken=6A5A…&ConnectionId=<32 位十六进制>
//         &Sec-MS-GEC=<v2 令牌>&Sec-MS-GEC-Version=2-143.0.3650.75
//   流程  连接后先发一次 speech.config（说明要 MP3），之后每句发一条 ssml；
//         服务端回的二进制帧里前两个字节是大端头长度，头里写着 Path:audio，
//         剩下的是 MP3，用 helix 解成 24k 16bit 单声道 PCM。
//   一条连接可以连发多句（edge-tts 也是这么用的），每句新建连接要 1 秒多，会卡。
// =====================================================================
String hex32() {
  char b[33];
  for (int i = 0; i < 16; i++) snprintf(b + i * 2, 3, "%02x", (unsigned)(esp_random() & 0xFF));
  b[32] = 0;
  return String(b);
}

String isoNow() {
  time_t t = time(nullptr);
  struct tm tm;
  gmtime_r(&t, &tm);
  char b[32];
  snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02d.000Z", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
           tm.tm_min, tm.tm_sec);
  return String(b);
}

String xmlEscape(const String &s) {
  String o;
  o.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&apos;"; break;
      default: o += c; break;
    }
  }
  return o;
}

// 语速倍率 → SSML 的 rate（1.25 倍 = +25%，0.75 倍 = -25%）
String rateArg(float speed) {
  int pct = (int)lroundf((speed - 1.0f) * 100.0f);
  if (pct > 100) pct = 100;
  if (pct < -50) pct = -50;
  char b[16];
  snprintf(b, sizeof(b), "%+d%%", pct);
  return String(b);
}

// 音色名是「语种-名字Neural」，xml:lang 取最后一段横线之前
String langOf(const String &voice) {
  int i = voice.lastIndexOf('-');
  return i > 0 ? voice.substring(0, i) : String("zh-CN");
}

// speech.platform.bing.com 用的根证书（Microsoft TLS RSA Root G2，DigiCert 交叉签名）。
// 服务端会带上中间证书，链是通的。
static const char EDGE_ROOT_CA[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIFiTCCBHGgAwIBAgIQCwxrLEZpF7BHc8ZH1K/AyDANBgkqhkiG9w0BAQwFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0yNTA1MjEwMDAwMDBaFw0yOTA2MTkyMzU5NTlaMFExCzAJBgNVBAYTAlVT
MR4wHAYDVQQKExVNaWNyb3NvZnQgQ29ycG9yYXRpb24xIjAgBgNVBAMTGU1pY3Jv
c29mdCBUTFMgUlNBIFJvb3QgRzIwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIK
AoICAQDf6oufR+EoEHGvQdYZ25JX3mur5i7erTpgg7cTmKxbuTILe+ufcidrXUCr
vhgGk7IN0hLtuHT1fy/qqBeU9jMWV4reIHwh3bfarN5OZLBazUt18+8CZE3tUtqj
jwTokfjX+z8Z/U5FOV7oKcPW8mevswCUwY3h8EoYmDn6wAmEM0EFAwWr9HXhU6Uh
klxETOZgV6SQApfH1diTBDJK7YVR7dbFuqA/Noovb0w5qARpIoQ7dRT32T60qdAH
QTiBfkZIHegZ5nC4oKoY3XK/fn21bE4ZcBGEBBOB1GL9nGvxHN3/7Kfg5seNMUu/
8mszzNGMtv6xG6NKqF8OfzF2OD8HR2wBqKylFNqCsF8fbLyJGsASKst7lx8oLjEW
ilNMdWb5fQHWwmCqZY8xnnLLzJst5UQZk1erbo7C2S5lsHIt56HDoX5JHVln1gnU
GBJtwJVFeMnxYGrk9u4GJDtzSloRwj6XYcB47u8TpzDiSjgt7lgXEyC3NirfCzK0
wjixkd0SsEW2fMCxHWKhnd1xEhWWAZ0KCfWx3bPZ4DhCNPZptsOvFnP+1EP4Q+RY
+U+z8+zWPZQ6QDgVqwyG0GTOGmPohJRVCVq2BLbRPpoVx2QRgNAbgg5N/0WesmUH
JR/bmsjG7NZbhVAEnxzLXSCCZ5554t/o8uhvxCByMIblnXUnNQIDAQABo4IBSzCC
AUcwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQU3pGGSLehMVkx8UtfB6nciHna
qHYwHwYDVR0jBBgwFoAUTiJUIBiV5uNu5g/6+rkS7QYXjzkwDgYDVR0PAQH/BAQD
AgGGMBMGA1UdJQQMMAoGCCsGAQUFBwMBMHYGCCsGAQUFBwEBBGowaDAkBggrBgEF
BQcwAYYYaHR0cDovL29jc3AuZGlnaWNlcnQuY29tMEAGCCsGAQUFBzAChjRodHRw
Oi8vY2FjZXJ0cy5kaWdpY2VydC5jb20vRGlnaUNlcnRHbG9iYWxSb290RzIuY3J0
MEIGA1UdHwQ7MDkwN6A1oDOGMWh0dHA6Ly9jcmwzLmRpZ2ljZXJ0LmNvbS9EaWdp
Q2VydEdsb2JhbFJvb3RHMi5jcmwwEwYDVR0gBAwwCjAIBgZngQwBAgIwDQYJKoZI
hvcNAQEMBQADggEBAAu8tCs3dMVLpzYCNsav4RPMipqXG/zjRIzuVADl5EEaRvAL
djT/mVViNaqtipwMWmLMQ8DL6kodvWsdr7EZJWac93luWyWAJIGFx3ktNV9CCXjt
n+Jl1cQgUIIQj2o67RiOSImrpgn44YD8BnUWJyVaj7g6cGwYR/Bj9FMO2RU1IPOR
PRMBoOL6JAhFVnfRZ6kxQtBX/xomvsVD2FepY/+v8zrY9ntLEKKXoc9mvmdnCfm1
TOerGSu/Ij193sb372M4LN1WxPkJUtrf44hv1W1r9whBL44+hjGf8XxK9dZhpEZG
KO9XurBvktjSdyXte6YpzjtyeRHU4KdUbTUrpHo=
-----END CERTIFICATE-----
)PEM";

// ---- 一个够用就好的 WebSocket 客户端（客户端发出的帧要掩码） ----
String b64(const uint8_t *d, size_t n) {
  size_t outLen = 0;
  uint8_t buf[64];
  if (mbedtls_base64_encode(buf, sizeof(buf), &outLen, d, n) != 0) return String();
  return String((const char *)buf, outLen);
}

// 网络读写要能随时被 stop 打断：任务卡在 TLS 里的话，停止朗读会等 20 秒超时，
// 那个分支会留下麻烦（旧代码就是在这里把信号量置空导致断言重启的）。
const volatile bool *g_netStop = nullptr;
inline bool netStopped() { return g_netStop && *g_netStop; }

bool readExact(WiFiClientSecure &tls, uint8_t *buf, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    if (netStopped()) return false;
    int r = tls.read(buf + got, n - got);
    if (r > 0) {
      got += r;
      t0 = millis();
      continue;
    }
    if (millis() - t0 > timeoutMs) return false;
    delay(5);
  }
  return true;
}

bool discardBytes(WiFiClientSecure &tls, size_t n) {
  uint8_t buf[128];
  while (n) {
    size_t k = n > sizeof(buf) ? sizeof(buf) : n;
    if (!readExact(tls, buf, k, 8000)) return false;
    n -= k;
  }
  return true;
}

bool wsSendText(WiFiClientSecure &tls, const char *d, size_t n) {
  uint8_t h[14];
  size_t hl = 0;
  h[hl++] = 0x81;  // FIN + text
  if (n < 126) {
    h[hl++] = (uint8_t)(0x80 | n);
  } else if (n < 65536) {
    h[hl++] = 0x80 | 126;
    h[hl++] = (uint8_t)(n >> 8);
    h[hl++] = (uint8_t)n;
  } else {
    return false;
  }
  uint8_t mask[4] = {(uint8_t)esp_random(), (uint8_t)esp_random(), (uint8_t)esp_random(), (uint8_t)esp_random()};
  memcpy(h + hl, mask, 4);
  hl += 4;
  if (tls.write(h, hl) != hl) return false;

  uint8_t buf[512];
  size_t off = 0;
  while (off < n) {
    size_t k = n - off > sizeof(buf) ? sizeof(buf) : n - off;
    for (size_t i = 0; i < k; i++) buf[i] = (uint8_t)d[off + i] ^ mask[(off + i) & 3];
    if (tls.write(buf, k) != k) return false;
    off += k;
  }
  return true;
}

struct EdgeOut {
  speech::StreamPcm *sp;
  size_t pcmBytes;
};

// 库的 data callback 传的是内部 p_caller_data（没有 setter，永远是空），
// 所以 ref 参数不能用，只能在这边记一个当前会话的指针。
EdgeOut *g_edgeOut = nullptr;

void onMp3Pcm(MP3FrameInfo &info, short *pcm, size_t len, void *ref) {
  auto *o = g_edgeOut;
  if (!o || !o->sp || !len) return;
  o->sp->feedPcm((const uint8_t *)pcm, len * sizeof(short));
  o->pcmBytes += len * sizeof(short);
  (void)info;
  (void)ref;
}

bool wsConnect(WiFiClientSecure &tls, const String &path, String &err) {
  tls.setCACert(EDGE_ROOT_CA);
  tls.setTimeout(8000);
  if (!tls.connect(TTS_EDGE_HOST, 443)) {
    err = "TLS 连接失败";
    return false;
  }
  uint8_t rnd[16];
  for (int i = 0; i < 16; i++) rnd[i] = (uint8_t)esp_random();
  String key = b64(rnd, sizeof(rnd));
  String req = String("GET ") + path +
               " HTTP/1.1\r\nHost: " TTS_EDGE_HOST "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
               key + "\r\nSec-WebSocket-Version: 13\r\nOrigin: " TTS_EDGE_ORIGIN +
               "\r\nPragma: no-cache\r\nCache-Control: no-cache\r\nUser-Agent: " TTS_EDGE_UA "\r\n\r\n";
  tls.print(req);

  String head;
  uint32_t t0 = millis();
  while (millis() - t0 < 8000 && head.length() < 2048) {
    if (netStopped()) {
      tls.stop();
      err = "已停止";
      return false;
    }
    if (!tls.available()) {
      if (!tls.connected()) break;
      delay(10);
      continue;
    }
    head += (char)tls.read();
    if (head.endsWith("\r\n\r\n")) break;
  }
  if (!head.startsWith("HTTP/1.1 101")) {
    int nl = head.indexOf("\r\n");
    err = String("握手失败：") + (nl > 0 ? head.substring(0, nl) : head.substring(0, 40));
    tls.stop();
    return false;
  }
  return true;
}

// 读一帧。返回 false 表示连接结束/出错
bool wsRecvFrame(WiFiClientSecure &tls, libhelix::MP3DecoderHelix &mp3, size_t &audioBytes, bool &turnEnd, String &err) {
  uint8_t h2[2];
  if (!readExact(tls, h2, 2, 15000)) {
    err = "接收超时";
    return false;
  }
  uint8_t op = h2[0] & 0x0F;
  uint64_t len = h2[1] & 0x7F;
  if (len == 126) {
    uint8_t b[2];
    if (!readExact(tls, b, 2, 8000)) return false;
    len = ((uint64_t)b[0] << 8) | b[1];
  } else if (len == 127) {
    uint8_t b[8];
    if (!readExact(tls, b, 8, 8000)) return false;
    len = 0;
    for (int i = 0; i < 8; i++) len = (len << 8) | b[i];
  }

  if (op == 0x8) {  // close
    err = "服务端关闭连接";
    return false;
  }
  if (op == 0x9 || op == 0xA) {  // ping / pong：丢掉
    return discardBytes(tls, (size_t)len);
  }

  if (op == 0x1) {  // 文本：服务端的事件说明
    String s;
    size_t left = (size_t)len;
    while (left) {
      size_t k = left > 256 ? 256 : left;
      uint8_t buf[256];
      if (!readExact(tls, buf, k, 8000)) return false;
      s.concat((const char *)buf, k);
      left -= k;
    }
    if (s.indexOf("Path:turn.end") >= 0) turnEnd = true;
    else if (s.indexOf("error") >= 0) Serial.printf("[朗读] 服务端：%s\n", s.substring(0, 200).c_str());
    return true;
  }

  if (op != 0x2 && op != 0x0) return discardBytes(tls, (size_t)len);

  // 二进制：2 字节大端头长度 + 头部文本 + 音频
  uint8_t hl2[2];
  if (!readExact(tls, hl2, 2, 8000)) return false;
  size_t hlen = ((size_t)hl2[0] << 8) | hl2[1];
  if (hlen > 1024 || len < 2 + hlen) return false;
  String header;
  header.reserve(hlen + 1);
  uint8_t hb[1024];
  if (!readExact(tls, hb, hlen, 8000)) return false;
  header.concat((const char *)hb, hlen);

  size_t remaining = (size_t)len - 2 - hlen;
  if (header.indexOf("Path:audio") >= 0) {
    uint8_t buf[512];
    while (remaining) {
      size_t k = remaining > sizeof(buf) ? sizeof(buf) : remaining;
      if (!readExact(tls, buf, k, 8000)) return false;
      mp3.write(buf, k);
      audioBytes += k;
      remaining -= k;
    }
  } else if (!discardBytes(tls, remaining)) {
    return false;
  }
  return true;
}

// =====================================================================
// qwen-audio-3.1-tts-flash：阿里云百炼，HTTP 流式
// =====================================================================
SynthResult flashSynth(const String &text, const Params &p, speech::PcmSink &sink, const volatile bool *stop) {
  SynthResult r;
  String key = p.key;
  key.trim();
  if (!key.length()) {
    r.error = "还没设置 API Key";
    return r;
  }
  if (!text.length()) {
    r.error = "没有要读的字";
    return r;
  }
  String ws = p.workspace;
  ws.trim();
  if (!ws.length()) {
    r.error = "还没填业务空间 ID";
    return r;
  }
  for (size_t i = 0; i < ws.length(); i++) {
    char c = ws[i];
    if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) {
      r.error = "业务空间 ID 格式不对";
      return r;
    }
  }

  String voice = p.voice;
  if (!voice.length()) voice = TTS_VOICE_FLASH;

  String url = String("https://") + ws + TTS_FLASH_HOST_SUFFIX + TTS_FLASH_PATH;
  String body = String("{\"model\":\"") + TTS_MODEL_FLASH + "\",\"input\":{\"text\":\"" + jsonEscape(text) +
                "\",\"voice\":\"" + jsonEscape(voice) + "\",\"format\":\"pcm\",\"sample_rate\":" + String(TTS_RATE) +
                ",\"volume\":100}}";
  String auth = "Bearer " + key;

  esp_http_client_config_t cfg = baseConfig(url.c_str());
  cfg.method = HTTP_METHOD_POST;
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (!h) {
    r.error = "内存不够";
    return r;
  }
  esp_http_client_set_header(h, "Authorization", auth.c_str());
  esp_http_client_set_header(h, "Content-Type", "application/json");
  esp_http_client_set_header(h, "X-DashScope-SSE", "enable");
  esp_http_client_set_header(h, "Accept", "text/event-stream");

  esp_err_t e = esp_http_client_open(h, body.length());
  if (e != ESP_OK) {
    r.error = String("连不上语音服务（") + esp_err_to_name(e) + "）";
    esp_http_client_cleanup(h);
    return r;
  }
  int wrote = esp_http_client_write(h, body.c_str(), body.length());
  if (wrote < (int)body.length()) {
    r.error = "请求发不出去";
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return r;
  }
  esp_http_client_fetch_headers(h);
  int code = esp_http_client_get_status_code(h);
  if (code != 200) {
    if (code <= 0) {
      r.error = "连不上语音服务（没有响应）";
      esp_http_client_close(h);
      esp_http_client_cleanup(h);
      return r;
    }
    String eb = readSome(h, 1024);
    String msg = jsonMessage(eb);
    if (code == 401 || code == 403) r.error = "API Key 不对或没有权限";
    else if (code == 429) r.error = "请求太频繁，稍后再试";
    else r.error = "语音服务返回 " + String(code) + (msg.length() ? "：" + msg : String());
    Serial.printf("[朗读] HTTP %d：%s\n", code, eb.c_str());
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return r;
  }

  speech::StreamPcm sp(sink);
  char buf[1024];
  String raw;     // 开头 4KB，非流式时从里面找 wav 网址
  String tail;    // 最后几十个字节，用来查 finish_reason 跨包
  bool stopped = false;
  String err;
  for (;;) {
    if (*stop) {
      stopped = true;
      break;
    }
    int n = esp_http_client_read(h, buf, sizeof(buf));
    if (n < 0) {  // 连接断了：已经收到声音就当读完（有的服务端直接关连接）
      if (!sp.sawAudio()) err = "接收中断（网络不稳）";
      break;
    }
    if (n == 0) {
      if (!sp.sawAudio()) err = "接收超时（网络不稳）";
      break;
    }
    sp.feed((const uint8_t *)buf, n);
    if (raw.length() < 4096) raw.concat(buf, n);
    // 流结束标记：末尾那个事件的 finish_reason 是 stop
    String probe = tail;
    probe.concat(buf, n);
    tail = probe.substring(probe.length() > 48 ? probe.length() - 48 : 0);
    if (probe.indexOf("\"finish_reason\":\"stop\"") >= 0) break;
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);

  r.stopped = stopped;
  sp.flushNow();
  bool downloaded = false;
  if (!stopped && !err.length() && !sp.sawAudio()) {
    String u = findUrl(raw);
    if (u.length()) {
      Serial.printf("[朗读] 服务端返回的是音频网址，下载 %s\n", u.c_str());
      downloaded = false;
      // 走 http 下载
      esp_http_client_config_t cfg2 = baseConfig(u.c_str());
      cfg2.timeout_ms = 15000;
      esp_http_client_handle_t h2 = esp_http_client_init(&cfg2);
      if (h2 && esp_http_client_open(h2, 0) == ESP_OK) {
        esp_http_client_fetch_headers(h2);
        if (esp_http_client_get_status_code(h2) == 200) {
          downloaded = true;
          char b2[1024];
          for (;;) {
            if (*stop) break;
            int n = esp_http_client_read(h2, b2, sizeof(b2));
            if (n <= 0) break;
            sp.feedPcm((const uint8_t *)b2, n);
          }
        }
        esp_http_client_close(h2);
        esp_http_client_cleanup(h2);
      }
      if (!downloaded && *stop) stopped = true;
    } else {
      err = "服务没有返回声音";
    }
  }
  sp.finish();
  if (!stopped && !err.length() && !sp.sawAudio()) err = downloaded ? "下载的音频是空的" : "没有声音";
  if (sp.wavRate() && sp.wavRate() != TTS_RATE) {
    Serial.printf("[朗读] 注意：服务端 WAV 是 %u Hz，按 %d Hz 播（可能变调）\n", sp.wavRate(), TTS_RATE);
  }
  r.bytes = sp.bytes();
  r.stopped = stopped;
  r.error = err;
  r.ok = !stopped && !err.length() && sp.sawAudio();
  return r;
}

}  // namespace

// =====================================================================
// Client
// =====================================================================
bool Client::connectEdge() {
  close();
  auto *tls = new WiFiClientSecure();
  auto *mp3 = new libhelix::MP3DecoderHelix();
  auto *out = new EdgeOut{nullptr, 0};
  if (!tls || !mp3 || !out) {
    delete tls;
    delete mp3;
    delete out;
    err_ = "内存不够";
    return false;
  }
  tls_ = tls;
  mp3_ = mp3;
  out_ = out;

  String token = edgeSecMsGec((uint64_t)time(nullptr));
  String path = String(TTS_EDGE_PATH) + "?TrustedClientToken=" + TTS_EDGE_TRUSTED_TOKEN +
                "&ConnectionId=" + hex32() + "&Sec-MS-GEC=" + token +
                "&Sec-MS-GEC-Version=" + TTS_EDGE_GEC_VERSION;
  if (!wsConnect(*tls, path, err_)) {
    close();
    return false;
  }
  if (!mp3->begin()) {
    err_ = "音频解码器初始化失败（内存不够）";
    close();
    return false;
  }
  mp3->setDataCallback(onMp3Pcm);
  if (!sendConfig()) {
    close();
    return false;
  }
  up_ = true;
  return true;
}

bool Client::sendConfig() {
  String stamp = isoNow();
  String cfgMsg = "X-Timestamp:" + stamp +
                  "\r\nContent-Type:application/json; charset=utf-8\r\nPath:speech.config\r\n\r\n"
                  "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":{\"sentenceBoundaryEnabled\":\"false\","
                  "\"wordBoundaryEnabled\":\"false\"},\"outputFormat\":\"" TTS_EDGE_FORMAT "\"}}}}";
  return wsSendText(*(WiFiClientSecure *)tls_, cfgMsg.c_str(), cfgMsg.length());
}

bool Client::sendSsml(const String &ssml) {
  String stamp = isoNow();
  String ssmlMsg = "X-RequestId:" + hex32() + "\r\nContent-Type:application/ssml+xml\r\nX-Timestamp:" + stamp +
                   "\r\nPath:ssml\r\n\r\n" + ssml;
  return wsSendText(*(WiFiClientSecure *)tls_, ssmlMsg.c_str(), ssmlMsg.length());
}

SynthResult Client::speakEdge(const String &text, const Params &p, speech::PcmSink &sink, const volatile bool *stop) {
  SynthResult r;
  if (time(nullptr) < 1700000000) {  // 2023 年之前 = 没对时
    r.error = "设备时间不对（先联网对时）";
    return r;
  }
  if (!text.length()) {
    r.error = "没有要读的字";
    return r;
  }

  String voice = p.voice.length() ? p.voice : String(TTS_VOICE_EDGE);
  speech::StreamPcm sp(sink);
  String ssml = String("<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='") + langOf(voice) +
                "'><voice name='" + voice + "'><prosody pitch='+0Hz' rate='" + rateArg(p.speed) +
                "' volume='+0%'>" + xmlEscape(text) + "</prosody></voice></speak>";

  g_netStop = stop;  // 让 TLS 读写随时可中断

  bool turnEnd = false;
  size_t audioBytes = 0;

  for (int attempt = 0; attempt < 2 && !*stop; attempt++) {
    if (!up_ && !connectEdge()) continue;
    auto *out = (EdgeOut *)out_;
    out->sp = &sp;
    out->pcmBytes = 0;
    g_edgeOut = out;

    if (!sendSsml(ssml)) {
      up_ = false;
      close();
      continue;
    }

    uint32_t t0 = millis();
    while (!turnEnd && !*stop && millis() - t0 < 25000) {
      if (!wsRecvFrame(*(WiFiClientSecure *)tls_, *(libhelix::MP3DecoderHelix *)mp3_, audioBytes, turnEnd, err_)) break;
    }
    if (turnEnd || *stop) break;
    // 连接断了：没收到声音就重连重试一次；已经收到声音就当这句读完了
    up_ = false;
    close();
    if (sp.sawAudio()) break;
  }

  g_edgeOut = nullptr;
  g_netStop = nullptr;
  auto *mp3 = (libhelix::MP3DecoderHelix *)mp3_;
  if (mp3) mp3->flush();  // 把最后几帧解完
  sp.flushNow();
  sp.finish();
  r.bytes = sp.bytes();
  r.stopped = *stop;
  if (r.stopped) {
    up_ = false;
    close();
    return r;
  }
  if (!sp.sawAudio()) {
    Serial.printf("[朗读] edge 没声音：MP3 %u 字节，%s\n", (unsigned)audioBytes, err_.c_str());
    r.error = err_.length() ? err_ : String("服务没有返回声音（可能被限流，稍后再试）");
    return r;
  }
  r.ok = true;
  return r;
}

SynthResult Client::speak(const String &text, const Params &p, speech::PcmSink &sink, const volatile bool *stop) {
  if (p.model == TTS_MODEL_FLASH) return flashSynth(text, p, sink, stop);
  return speakEdge(text, p, sink, stop);
}

void Client::close() {
  if (tls_) {
    ((WiFiClientSecure *)tls_)->stop();
    delete (WiFiClientSecure *)tls_;
    tls_ = nullptr;
  }
  if (mp3_) {
    delete (libhelix::MP3DecoderHelix *)mp3_;
    mp3_ = nullptr;
  }
  if (out_) {
    delete (EdgeOut *)out_;
    out_ = nullptr;
  }
  up_ = false;
}

String jsonMessage(const String &body) {
  int i = body.indexOf("\"message\":\"");
  if (i < 0) return "";
  i += 11;
  int j = body.indexOf('"', i);
  if (j < 0) return "";
  String m = body.substring(i, j);
  m.trim();
  if (m.length() > 60) m = m.substring(0, 60) + "…";
  return m;
}

}  // namespace tts
