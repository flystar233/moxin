#include "tts.h"

#include <Arduino.h>
#include <WiFi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app/app.h"
#include "board/audio.h"
#include "config.h"
#include "core/settings.h"
#include "net/wifi.h"
#include "reader/book.h"
#include "sentence.h"
#include "tts_client.h"
#include "ui/views.h"  // reader::saveProgress()

namespace tts {
namespace {

const int MAX_QUEUE = 3;      // 最多预排几句文本
const int KEEP_RANGES = 8;    // 记住最近几段 PCM 是哪一句
const int RING_MIN_KB = 32;

// ---- 模型 / 音色 ----
struct ModelDef {
  const char *id;
  const char *label;
};
const ModelDef MODELS[] = {{TTS_MODEL, "qwen3-tts-flash"}, {TTS_MODEL_PLUS, "qwen-audio-3.1-tts-plus"}};

struct VoiceDef {
  const char *id;
  const char *label;
};
const VoiceDef FLASH_VOICES[] = {
    {"Cherry", "芊悦（女）"},   {"Serena", "苏瑶（女）"}, {"Ethan", "晨煦（男）"},  {"Chelsie", "千雪（女）"},
    {"Dylan", "晓东（京腔）"}, {"Jada", "阿珍（沪语）"}, {"Sunny", "小雅（川话）"},
};
const VoiceDef PLUS_VOICES[] = {
    {"longanlingxin_v3.1", "龙安灵心（女）"},
    {"xunanchuan_v3.1", "许南川（男）"},
    {"longanhuan_v3.1", "龙安欢（女）"},
    {"longanfengyue_v3.1", "龙安风悦（女）"},
};

bool isPlusModelId(const String &m) { return m == TTS_MODEL_PLUS; }

const VoiceDef *voicesOf(const String &model, int &count) {
  if (isPlusModelId(model)) {
    count = (int)(sizeof(PLUS_VOICES) / sizeof(PLUS_VOICES[0]));
    return PLUS_VOICES;
  }
  count = (int)(sizeof(FLASH_VOICES) / sizeof(FLASH_VOICES[0]));
  return FLASH_VOICES;
}

const char *defaultVoiceId(const String &model) { return isPlusModelId(model) ? TTS_VOICE_PLUS : TTS_VOICE; }

bool voiceKnown(const String &model, const String &name) {
  int n = 0;
  const VoiceDef *v = voicesOf(model, n);
  for (int i = 0; i < n; i++)
    if (name == v[i].id) return true;
  return false;
}

uint32_t utf8Chars(const String &s) {
  uint32_t n = 0;
  for (size_t i = 0; i < s.length(); i++)
    if (((uint8_t)s[i] & 0xC0) != 0x80) n++;
  return n;
}

struct Item {
  String text;
  uint32_t off = 0;
  uint32_t endOff = 0;
};

// 一段 PCM 在整条流里的字节范围，对应正文里的一句
struct Range {
  uint32_t off, endOff;
  uint32_t startByte, endByte;
};

enum : uint8_t { ST_IDLE, ST_PLAY, ST_PAUSE };

// ---- 共享状态 ----
SemaphoreHandle_t s_mtx = nullptr;   // 保护队列、ranges、环形缓冲、s_taskErr、s_voice/s_key
SemaphoreHandle_t s_done = nullptr;  // 两个任务退出时各给一次
TaskHandle_t s_netTask = nullptr, s_audioTask = nullptr;

volatile bool s_stop = false;
volatile bool s_paused = false;
volatile bool s_failed = false;
volatile bool s_netBusy = false;
volatile uint32_t s_written = 0;  // 合成任务写进环的累计字节
volatile uint32_t s_played = 0;   // 播放任务送进 I2S 的累计字节

uint8_t s_state = ST_IDLE;
int s_tasks = 0;                  // 这次会话建了几个任务（收尾时等它们各给一次）
bool s_say = false;               // 念一段临时文本（试听）
uint32_t s_total = 0;             // 正文长度（试听时 0）
uint32_t s_nextOff = 0;           // 下一句从哪里取
bool s_srcDone = false;           // 正文取完了
uint32_t s_curOff = 0, s_curEndOff = 0;
uint32_t s_idleAt = 0, s_pauseAt = 0, s_lastSave = 0;
int s_vol = TTS_VOL_DEFAULT;

String s_taskErr;
String s_model, s_ws, s_voice, s_key;
volatile uint32_t s_chars = 0;  // 本次会话已经合成多少字

Event s_event = EV_NONE;
String s_eventMsg;

std::vector<Item> s_queue;
std::vector<Range> s_ranges;

uint8_t *s_ring = nullptr;
size_t s_ringCap = 0, s_ringHead = 0, s_ringUsed = 0;

// ---- 正文取字（只在主线程用） ----
struct BookSource : speech::TextSource {
  uint32_t read(uint32_t off, char *buf, uint32_t n) override {
    const char *p = bookTextAt(off, n);
    if (!p) return 0;
    memcpy(buf, p, n);
    return n;
  }
};
BookSource s_book;

// =====================================================================
// 环形缓冲（单生产者 / 单消费者）
// =====================================================================
size_t ringFreeLocked() { return s_ringCap - s_ringUsed; }
size_t ringUsed() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  size_t n = s_ringUsed;
  xSemaphoreGive(s_mtx);
  return n;
}
bool ringEmpty() { return ringUsed() == 0; }

void ringClear() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_ringHead = s_ringUsed = 0;
  xSemaphoreGive(s_mtx);
}

// 写：满了就等（等的时候看 stop）。返回实际写进去的字节数
size_t ringWrite(const uint8_t *p, size_t n) {
  size_t done = 0;
  while (done < n && !s_stop) {
    bool space = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    size_t room = ringFreeLocked();
    if (room) {
      size_t k = std::min(n - done, room);
      size_t wp = (s_ringHead + s_ringUsed) % s_ringCap;
      if (k > s_ringCap - wp) k = s_ringCap - wp;
      memcpy(s_ring + wp, p + done, k);
      s_ringUsed += k;
      done += k;
      space = true;
    }
    xSemaphoreGive(s_mtx);
    if (!space) vTaskDelay(pdMS_TO_TICKS(10));
  }
  return done;
}

// 读：最多 n 字节；waitMs 内没有就返回 0
size_t ringRead(uint8_t *p, size_t n, int waitMs) {
  uint32_t t0 = millis();
  for (;;) {
    size_t got = 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_ringUsed) {
      size_t k = std::min(n, s_ringUsed);
      if (k > s_ringCap - s_ringHead) k = s_ringCap - s_ringHead;
      memcpy(p, s_ring + s_ringHead, k);
      s_ringHead = (s_ringHead + k) % s_ringCap;
      s_ringUsed -= k;
      got = k;
    }
    xSemaphoreGive(s_mtx);
    if (got) return got;
    if (s_stop || (int)(millis() - t0) >= waitMs) return 0;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

struct RingSink : speech::PcmSink {
  void pcm(const uint8_t *data, size_t n) override {
    size_t w = ringWrite(data, n);
    s_written += (uint32_t)w;
  }
};
RingSink s_sink;

// =====================================================================
// 句子队列（主线程放、合成任务取）
// =====================================================================
bool queuePush(const Item &it) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  bool ok = (int)s_queue.size() < MAX_QUEUE;
  if (ok) s_queue.push_back(it);
  xSemaphoreGive(s_mtx);
  return ok;
}

bool queuePop(Item &it) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  bool ok = !s_queue.empty();
  if (ok) {
    it = s_queue.front();
    s_queue.erase(s_queue.begin());
  }
  xSemaphoreGive(s_mtx);
  return ok;
}

int queueSize() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  int n = (int)s_queue.size();
  xSemaphoreGive(s_mtx);
  return n;
}

// =====================================================================
// 任务
// =====================================================================
void netTask(void *) {
  RingSink sink;
  while (!s_stop) {
    Item it;
    if (!queuePop(it)) {
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }
    if (s_stop) break;
    Params pr;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    pr.model = s_model;
    pr.voice = s_voice;
    pr.key = s_key;
    pr.workspace = s_ws;
    xSemaphoreGive(s_mtx);

    s_netBusy = true;
    uint32_t before = s_written;
    SynthResult r = synthesize(it.text, pr, sink, &s_stop);
    s_netBusy = false;
    if (s_stop) break;
    if (r.ok) s_chars += utf8Chars(it.text);
    uint32_t after = s_written;
    if (after > before) {
      xSemaphoreTake(s_mtx, portMAX_DELAY);
      s_ranges.push_back({it.off, it.endOff, before, after});
      while ((int)s_ranges.size() > KEEP_RANGES) s_ranges.erase(s_ranges.begin());
      xSemaphoreGive(s_mtx);
    }
    if (!r.ok) {
      xSemaphoreTake(s_mtx, portMAX_DELAY);
      s_taskErr = r.error.length() ? r.error : String("合成失败");
      xSemaphoreGive(s_mtx);
      s_failed = true;
      s_stop = true;
      break;
    }
  }
  xSemaphoreGive(s_done);
  vTaskDelete(nullptr);
}

void audioTask(void *) {
  static int16_t pcm[1024];  // 最长 512 采样 = 21ms
  bool opened = false;
  uint32_t lastData = 0, firstAt = 0;
  while (!s_stop) {
    if (s_paused) {
      if (opened) {
        audioEnd();
        opened = false;
        firstAt = 0;
      }
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    // 一句刚开始：先攒 0.3 秒再出声，免得服务端头几段来得慢、一放就断
    if (!opened) {
      size_t have = ringUsed();
      if (have && !firstAt) firstAt = millis();
      if (have < 16384 && firstAt && millis() - firstAt < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
    }
    size_t got = ringRead((uint8_t *)pcm, sizeof(pcm), 20);
    if (got >= 2) {
      if (!opened) {
        if (!audioBegin(TTS_RATE)) {
          xSemaphoreTake(s_mtx, portMAX_DELAY);
          s_taskErr = "打不开功放";
          xSemaphoreGive(s_mtx);
          s_failed = true;
          s_stop = true;
          break;
        }
        audioSetVolume(s_vol);
        opened = true;
      }
      audioWriteMono(pcm, got / 2);
      s_played += (uint32_t)(got & ~(size_t)1);
      lastData = millis();
    } else {
      // 两句之间有一小段空：先不关功放（开着安静），超过 3 秒才关（省电、也没底噪）
      if (opened && millis() - lastData > 3000) {
        audioEnd();
        opened = false;
        firstAt = 0;
      } else {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
  }
  if (opened) audioEnd();
  xSemaphoreGive(s_done);
  vTaskDelete(nullptr);
}

// =====================================================================
// 会话
// =====================================================================
bool beginSession(uint32_t off, bool say, const String &text);

void stopInternal(bool wifiOff) {
  if (s_state == ST_IDLE && s_tasks == 0 && !s_ring && !s_mtx && !s_done) {
    if (wifiOff) {
      net::off();
      setCpuFrequencyMhz(CPU_MHZ);
    }
    return;
  }
  s_stop = true;
  s_paused = false;
  // 等任务退出：HTTP 读超时 8 秒，最多等 20 秒（正常几十毫秒）
  bool allDone = true;
  if (s_done) {
    for (int i = 0; i < s_tasks; i++)
      if (xSemaphoreTake(s_done, pdMS_TO_TICKS(20000)) != pdTRUE) {
        Serial.println("[朗读] 任务没退干净（超时），这次不回收缓冲");
        allDone = false;
        break;
      }
  }
  s_tasks = 0;
  s_netTask = s_audioTask = nullptr;
  audioEnd();  // 兜底：正常情况下播放任务退出时已经关了

  if (allDone) {
    if (s_mtx) {
      xSemaphoreTake(s_mtx, portMAX_DELAY);
      s_queue.clear();
      s_ranges.clear();
      s_ringHead = s_ringUsed = 0;
      s_taskErr = "";
      xSemaphoreGive(s_mtx);
      vSemaphoreDelete(s_mtx);
      s_mtx = nullptr;
    }
    if (s_done) {
      vSemaphoreDelete(s_done);
      s_done = nullptr;
    }
    if (s_ring) {
      free(s_ring);
      s_ring = nullptr;
    }
    s_ringCap = 0;
  } else {
    // 任务还卡在网络里：缓冲、信号量都不动（宁可漏一点内存，也不能在任务还在用的时候 free）
    s_mtx = nullptr;
    s_done = nullptr;
    s_ring = nullptr;
    s_ringCap = 0;
  }
  s_state = ST_IDLE;
  s_say = false;
  s_stop = false;
  s_paused = false;
  s_failed = false;
  s_netBusy = false;
  if (wifiOff) {
    net::off();
    setCpuFrequencyMhz(CPU_MHZ);
  }
  Serial.println("[朗读] 结束");
}

// 取当前播到哪一句（按已经送进 I2S 的字节数，在 ranges 里找）
uint32_t currentOffset() {
  uint32_t played = s_played;
  uint32_t off = s_curOff;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  for (auto &r : s_ranges) {
    if (played >= r.startByte && played < r.endByte) {
      off = r.off;
      s_curEndOff = r.endOff;
      break;
    }
  }
  // 播过头的旧记录删掉（留最后一条，用来兜底）
  while (s_ranges.size() > 1 && s_ranges.front().endByte <= played) s_ranges.erase(s_ranges.begin());
  xSemaphoreGive(s_mtx);
  return off;
}

bool beginSession(uint32_t off, bool say, const String &text) {
  if (s_state != ST_IDLE) stopInternal(true);
  Settings &st = settings();
  if (!st.wifiOn) {
    s_taskErr = "Wi-Fi 已关闭（系统设置 → Wi-Fi）";
    return false;
  }
  if (!st.ttsKey.length()) {
    s_taskErr = "还没设置语音 API Key";
    return false;
  }
  String model = st.ttsModel.length() ? st.ttsModel : String(TTS_MODEL);
  if (isPlusModelId(model) && !st.ttsWs.length()) {
    s_taskErr = "这个模型要先填业务空间 ID";
    return false;
  }
  if (!net::hasCredentials()) {
    s_taskErr = "还没配网（Wi-Fi → 配网）";
    return false;
  }
  if (!say && !bookIsOpen()) {
    s_taskErr = "没有打开的书";
    return false;
  }
  if (net::busy()) {
    s_taskErr = "Wi-Fi 正忙（配网、传书或后台对时），过一会儿再试";
    return false;
  }
  if (!say && off >= bookSize()) off = 0;

  if (WiFi.status() != WL_CONNECTED) {
    app::busy("正在连接 Wi-Fi…");
    String why;
    if (!net::connect(15000, &why)) {
      net::off();
      s_taskErr = why.length() ? why : String("连不上 Wi-Fi");
      return false;
    }
  }

  // 预读缓冲：PSRAM 不够就往下降
  s_ring = nullptr;
  s_ringCap = 0;
  for (size_t kb = TTS_RING_KB; kb >= RING_MIN_KB; kb /= 2) {
    s_ring = (uint8_t *)ps_malloc(kb * 1024);
    if (s_ring) {
      s_ringCap = kb * 1024;
      break;
    }
  }
  if (!s_ring) {
    s_taskErr = "内存不够（朗读缓冲）";
    net::off();
    return false;
  }

  s_mtx = xSemaphoreCreateMutex();
  s_done = xSemaphoreCreateCounting(2, 0);
  if (!s_mtx || !s_done) {
    if (s_mtx) vSemaphoreDelete(s_mtx);
    if (s_done) vSemaphoreDelete(s_done);
    s_mtx = s_done = nullptr;
    free(s_ring);
    s_ring = nullptr;
    s_ringCap = 0;
    s_taskErr = "内存不够（朗读）";
    net::off();
    return false;
  }

  setCpuFrequencyMhz(240);
  s_say = say;
  s_total = say ? 0 : bookSize();
  s_nextOff = off;
  s_srcDone = false;
  s_curOff = off;
  s_curEndOff = 0;
  s_written = s_played = 0;
  s_idleAt = s_pauseAt = s_lastSave = 0;
  s_stop = s_paused = s_failed = s_netBusy = false;
  s_tasks = 0;
  s_taskErr = "";
  String voice = st.ttsVoice;
  if (!voiceKnown(model, voice)) voice = defaultVoiceId(model);
  s_model = model;
  s_ws = st.ttsWs;
  s_voice = voice;
  s_key = st.ttsKey;
  s_vol = st.ttsVolume;
  s_chars = 0;
  ringClear();
  if (say) {
    Item it;
    it.text = text;
    queuePush(it);
    s_srcDone = true;
  }
  if (xTaskCreatePinnedToCore(netTask, "tts_net", 16384, nullptr, 3, &s_netTask, 0) != pdPASS) {
    s_netTask = nullptr;
    stopInternal(true);
    s_taskErr = "建不了合成任务（内存不够）";
    return false;
  }
  s_tasks = 1;
  if (xTaskCreatePinnedToCore(audioTask, "tts_audio", 4096, nullptr, 4, &s_audioTask, 1) != pdPASS) {
    s_audioTask = nullptr;
    stopInternal(true);
    s_taskErr = "建不了播放任务（内存不够）";
    return false;
  }
  s_tasks = 2;
  s_state = ST_PLAY;
  s_event = EV_NONE;
  s_eventMsg = "";
  app::invalidate(Refresh::Partial);  // 把"正在连接 Wi-Fi…"的提示框盖掉
  Serial.printf("[朗读] 开始（%s），缓冲 %u KB\n", say ? "试听" : "正文", (unsigned)(s_ringCap / 1024));
  return true;
}

}  // namespace

// =====================================================================
// 对外接口
// =====================================================================
bool ready(String *why) {
  const Settings &st = settings();
  String w;
  String model = st.ttsModel.length() ? st.ttsModel : String(TTS_MODEL);
  if (!st.wifiOn) w = "Wi-Fi 已关闭";
  else if (!st.ttsKey.length()) w = "还没设置 API Key";
  else if (isPlusModelId(model) && !st.ttsWs.length()) w = "这个模型要填业务空间 ID";
  else if (!net::hasCredentials()) w = "还没配网";
  if (why) *why = w;
  return w.length() == 0;
}

bool start(uint32_t off) {
  if (!bookIsOpen()) {
    s_taskErr = "没有打开的书";
    return false;
  }
  return beginSession(off, false, String());
}

bool say(const String &text) {
  if (!text.length()) {
    s_taskErr = "没有要读的字";
    return false;
  }
  return beginSession(0, true, text);
}

void stop() {
  if (s_state == ST_IDLE) return;
  bool wasSay = s_say;
  stopInternal(true);
  if (!wasSay && bookIsOpen()) reader::saveProgress();
  app::invalidate(Refresh::Partial);  // 页脚的"朗读中"要擦掉
}

void togglePause() {
  if (s_state == ST_PLAY) {
    s_state = ST_PAUSE;
    s_paused = true;
    s_pauseAt = millis();
    app::invalidate(Refresh::Partial);
  } else if (s_state == ST_PAUSE) {
    s_state = ST_PLAY;
    s_paused = false;
    app::invalidate(Refresh::Partial);
  }
}

bool active() { return s_state != ST_IDLE; }
bool paused() { return s_state == ST_PAUSE; }
uint32_t offset() { return s_curOff; }
uint32_t charsUsed() { return s_chars; }
String lastError() { return s_taskErr; }

String statusText() {
  if (s_state == ST_PAUSE) return "朗读暂停";
  if (s_state == ST_PLAY) return "朗读中";
  return "";
}

bool jumpTo(uint32_t off) {
  if (s_say) return false;
  if (s_state == ST_IDLE) return start(off);
  stopInternal(false);  // Wi-Fi 留着
  return beginSession(off, false, String());
}

bool nextSentence() {
  if (s_state == ST_IDLE || s_say) return false;
  uint32_t from = s_curEndOff > s_curOff ? s_curEndOff : s_curOff;
  speech::Sentence s;
  if (!speech::nextSentence(s_book, s_total, from, TTS_MAX_CHARS, TTS_MIN_CHARS, s)) return false;
  return jumpTo(s.off);
}

bool prevSentence() {
  if (s_state == ST_IDLE || s_say) return false;
  uint32_t back = 0;
  if (!speech::prevSentenceStart(s_book, s_total, s_curOff, TTS_MAX_CHARS, TTS_MIN_CHARS, back)) return false;
  return jumpTo(back);
}

void setVolume(int vol) {
  s_vol = constrain(vol, 0, 100);
  audioSetVolume(s_vol);
}

void setVoice(const String &voice) {
  if (!s_mtx) {
    s_voice = voice;
    return;
  }
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_voice = voice;
  xSemaphoreGive(s_mtx);
}

void setKey(const String &key) {
  if (!s_mtx) {
    s_key = key;
    return;
  }
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_key = key;
  xSemaphoreGive(s_mtx);
}

void setModel(const String &model, const String &workspace) {
  String m = model.length() ? model : String(TTS_MODEL);
  String v = s_voice;
  if (!voiceKnown(m, v)) v = defaultVoiceId(m);
  if (!s_mtx) {
    s_model = m;
    s_ws = workspace;
    s_voice = v;
    return;
  }
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_model = m;
  s_ws = workspace;
  s_voice = v;
  xSemaphoreGive(s_mtx);
}

Event pollEvent(String &msg) {
  Event e = s_event;
  s_event = EV_NONE;
  msg = s_eventMsg;
  s_eventMsg = "";
  return e;
}

void tick() {
  if (s_state == ST_IDLE) return;
  uint32_t now = millis();

  if (s_failed) {
    String msg;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    msg = s_taskErr;
    xSemaphoreGive(s_mtx);
    stopInternal(true);
    s_event = EV_ERROR;
    s_eventMsg = msg;
    return;
  }

  // 1. 从正文里按句取，排队等合成（暂停时也预取，恢复时不用等）
  while (!s_srcDone && queueSize() < MAX_QUEUE) {
    if (s_say) {
      s_srcDone = true;
      break;
    }
    speech::Sentence s;
    if (!speech::nextSentence(s_book, s_total, s_nextOff, TTS_MAX_CHARS, TTS_MIN_CHARS, s)) {
      s_srcDone = true;
      break;
    }
    std::string text;
    if (speech::sentenceText(s_book, s, text) && !text.empty()) {
      Item it;
      it.text = String(text.c_str());
      it.off = s.off;
      it.endOff = s.off + s.len;
      if (!queuePush(it)) break;
    }
    s_nextOff = s.off + s.len;
    if (s.last) s_srcDone = true;
  }

  // 2. 跟着正在播的那一句翻页
  if (!s_say) {
    uint32_t cur = currentOffset();
    if (cur != s_curOff) s_curOff = cur;
    // 每一轮都看一下页码：菜单开着的这段时间句子会往下走，关掉菜单后要能追上
    if (settings().ttsAutoPage && s_state == ST_PLAY && app::rootIsReader() && app::top() && app::top()->isRoot()) {
      int p = bookPageOf(s_curOff);
      if (p != bookPage()) {
        bookSetPage(p);
        app::invalidate(Refresh::Partial);
        if (now - s_lastSave > 20000) {
          s_lastSave = now;
          reader::saveProgress();
        }
      }
    }
  }

  // 3. 读完了：正文取完、队列空、缓冲放空、合成任务闲着
  bool done = s_srcDone && queueSize() == 0 && ringEmpty() && !s_netBusy && !s_paused;
  if (done) {
    if (!s_idleAt) {
      s_idleAt = now;
    } else if (now - s_idleAt > 700) {
      bool wasSay = s_say;
      stopInternal(true);
      if (!wasSay) {
        reader::saveProgress();
        s_event = EV_DONE;
        s_eventMsg = "";
      }
      return;
    }
  } else {
    s_idleAt = 0;
  }

  // 4. 暂停太久：自动结束，关 Wi-Fi 省电
  if (s_state == ST_PAUSE && s_pauseAt && now - s_pauseAt > TTS_PAUSE_STOP_MS) {
    stopInternal(true);
    s_event = EV_DONE;
    s_eventMsg = "暂停太久，已停止";
  }
}

// =====================================================================
// 模型、音色
// =====================================================================
int modelCount() { return (int)(sizeof(MODELS) / sizeof(MODELS[0])); }
const char *modelId(int i) { return MODELS[constrain(i, 0, modelCount() - 1)].id; }

String modelLabel(const String &model) {
  for (const ModelDef &m : MODELS)
    if (model == m.id) return m.label;
  return model.length() ? model : String(TTS_MODEL);
}

String defaultVoice(const String &model) { return defaultVoiceId(model); }
float modelPrice(const String &model) { return isPlusModelId(model) ? TTS_PRICE_PLUS : TTS_PRICE_FLASH; }

int voiceCount(const String &model) {
  int n = 0;
  voicesOf(model, n);
  return n;
}

const char *voiceName(const String &model, int i) {
  int n = 0;
  const VoiceDef *v = voicesOf(model, n);
  return v[constrain(i, 0, n - 1)].id;
}

String voiceLabel(const String &model, const String &name) {
  int n = 0;
  const VoiceDef *v = voicesOf(model, n);
  for (int i = 0; i < n; i++)
    if (name == v[i].id) return String(v[i].id) + " " + v[i].label;
  return name.length() ? name : String(defaultVoiceId(model));
}

}  // namespace tts
