#include "tts_client.h"

#include <ctype.h>
#include <esp_http_client.h>

#include "config.h"

// IDF 自带的根证书包（编进了框架的 libmbedtls.a）。不 include esp_crt_bundle.h：
// WiFiClientSecure 里有个同名头文件，声明的是 arduino_ 开头的另一套函数（net/update.cpp 里也是这么写的）
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

namespace tts {
namespace {

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

// 服务端没走流式（返回了一整个 wav 的网址）：再下载一遍，照样喂给 PCM 解析
bool download(const String &url, speech::StreamPcm &sp, const volatile bool *stop, String &err) {
  esp_http_client_config_t cfg = baseConfig(url.c_str());
  cfg.timeout_ms = 15000;
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (!h) {
    err = "内存不够";
    return false;
  }
  bool ok = false;
  if (esp_http_client_open(h, 0) != ESP_OK) {
    err = "下载音频连不上";
  } else {
    esp_http_client_fetch_headers(h);
    int code = esp_http_client_get_status_code(h);
    if (code != 200) {
      err = "下载音频返回 " + String(code);
    } else {
      ok = true;
      char buf[1024];
      for (;;) {
        if (*stop) {
          ok = false;
          err = "已停止";
          break;
        }
        int n = esp_http_client_read(h, buf, sizeof(buf));
        if (n < 0) {
          ok = false;
          err = "下载音频中断";
          break;
        }
        if (n == 0) break;
        sp.feedPcm((const uint8_t *)buf, n);
      }
    }
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  return ok;
}

}  // namespace

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

SynthResult synthesize(const String &text, const Params &p, speech::PcmSink &sink, const volatile bool *stop) {
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

  const bool plus = p.model == TTS_MODEL_PLUS;
  String voice = p.voice;
  if (!voice.length()) voice = plus ? TTS_VOICE_PLUS : TTS_VOICE;

  // 两个模型走不同端点、不同请求体：
  //   flash：多模态生成接口，language_type；返回 WAV/PCM（我们的解析器都认）
  //   plus ：SpeechSynthesizer 接口，地址里要带业务空间 ID；直接要 24k 单声道 PCM
  String url, body;
  if (plus) {
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
    url = String("https://") + ws + TTS_PLUS_HOST_SUFFIX + TTS_PLUS_PATH;
    body = String("{\"model\":\"") + TTS_MODEL_PLUS + "\",\"input\":{\"text\":\"" + jsonEscape(text) + "\",\"voice\":\"" +
           jsonEscape(voice) + "\",\"format\":\"pcm\",\"sample_rate\":" + String(TTS_RATE) + ",\"volume\":100}}";
  } else {
    url = TTS_API_URL;
    body = String("{\"model\":\"") + TTS_MODEL + "\",\"input\":{\"text\":\"" + jsonEscape(text) + "\",\"voice\":\"" +
           jsonEscape(voice) + "\",\"language_type\":\"" + TTS_LANGUAGE + "\"}}";
  }
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
  sp.flushNow();  // 很短的一句也要先交出去，才能判断"到底有没有声音"
  bool downloaded = false;
  if (!stopped && !err.length() && !sp.sawAudio()) {
    // 服务端忽略了 SSE：返回的是 {"output":{"audio":{"url":"https://…wav"}}}，去下载
    String url = findUrl(raw);
    if (url.length()) {
      Serial.printf("[朗读] 服务端返回的是音频网址，下载 %s\n", url.c_str());
      downloaded = download(url, sp, stop, err);
      if (!downloaded && *stop) stopped = true;
    } else {
      err = "服务没有返回声音";
    }
  }
  sp.finish();  // 收尾放在下载之后：下载的 WAV 头要能认出来
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

}  // namespace tts
