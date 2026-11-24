#include "audio.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"
#include "driver/i2s.h"
#include "power.h"

static const i2s_port_t PORT = I2S_NUM_0;
static const int TONE_RATE = 16000;
static const int FRAMES = 256;   // 每次写 256 帧（16ms）
static const int FADE_MS = 8;    // 起止淡入淡出，避免"咔"一声

static bool s_open = false;
static int s_rate = TONE_RATE;
static int s_vol = 100;          // 语音的音量（%）
static int s_fade = 0;           // 还要淡入多少个采样
static int s_fadeTotal = 1;
static int16_t s_stereo[2 * 1024];

static bool start(int rate);
static void stop();

static float gainOf(int vol) {
  if (vol < 0) vol = 0;
  if (vol > 100) vol = 100;
  float a = vol / 100.0f;
  if (usbPresent() && a > 0.7f) a = 0.7f;  // 插 USB 时 VSYS 约 4.65V，满幅会超过 1W 喇叭额定功率（文档 F8：−3dB）
  return a;
}

static bool start(int rate) {
  if (s_open && s_rate == rate) return true;
  if (s_open) stop();
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = rate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = FRAMES;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;  // 没数据时自动送 0，不会重复最后一段
  esp_err_t e = i2s_driver_install(PORT, &cfg, 0, nullptr);
  if (e != ESP_OK) { Serial.printf("[声] I2S 初始化失败 %d\n", e); return false; }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRCLK;
  pins.data_out_num = PIN_I2S_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(PORT, &pins);
  i2s_zero_dma_buffer(PORT);
  delay(20);                       // 时钟先跑起来再开功放
  pinMode(PIN_AMP_CTRL, OUTPUT);
  digitalWrite(PIN_AMP_CTRL, HIGH);
  delay(30);                       // NS4168 开启后有软启动
  s_open = true;
  s_rate = rate;
  s_fadeTotal = rate * FADE_MS / 1000;
  if (s_fadeTotal < 1) s_fadeTotal = 1;
  s_fade = s_fadeTotal;
  return true;
}

static void stop() {
  if (!s_open) return;
  i2s_zero_dma_buffer(PORT);
  delay(30);
  ampOff();
  delay(5);
  i2s_driver_uninstall(PORT);
  for (int p : {PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DIN}) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  s_open = false;
  s_fade = 0;
}

// ---------------------------------------------------------------------
// 测试音
// ---------------------------------------------------------------------
// 频率随时间变化：hzAt(t) 给出 0～1 进度处的频率
template <typename F>
static void play(int ms, float a, F hzAt) {
  static int16_t buf[FRAMES * 2];
  const int total = (int)((int64_t)TONE_RATE * ms / 1000);
  const int fade = TONE_RATE * FADE_MS / 1000;
  float phase = 0;
  for (int n = 0; n < total;) {
    int k = min(FRAMES, total - n);
    for (int i = 0; i < k; i++, n++) {
      float g = 1.0f;
      if (n < fade) g = (float)n / fade;
      else if (total - n < fade) g = (float)(total - n) / fade;
      phase += 2.0f * (float)M_PI * hzAt((float)n / total) / TONE_RATE;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
      int16_t s = (int16_t)(a * g * sinf(phase));
      buf[2 * i] = s;
      buf[2 * i + 1] = s;
    }
    size_t w;
    i2s_write(PORT, buf, k * 4, &w, portMAX_DELAY);
  }
}

bool audioTone(int hz, int ms, int vol) {
  if (!start(TONE_RATE)) return false;
  play(ms, gainOf(vol) * 32767.0f, [hz](float) { return (float)hz; });
  stop();
  return true;
}

bool audioSweep(int fromHz, int toHz, int ms, int vol) {
  if (!start(TONE_RATE)) return false;
  const float r = logf((float)toHz / fromHz);
  play(ms, gainOf(vol) * 32767.0f, [=](float t) { return fromHz * expf(r * t); });
  stop();
  return true;
}

bool audioScale(int vol) {
  static const int NOTES[] = {523, 587, 659, 698, 784, 880, 988, 1047};  // C5～C6
  if (!start(TONE_RATE)) return false;
  float a = gainOf(vol) * 32767.0f;
  for (int hz : NOTES) {
    play(260, a, [hz](float) { return (float)hz; });
    play(40, 0, [](float) { return 0.0f; });
  }
  stop();
  return true;
}

// ---------------------------------------------------------------------
// 流式 PCM
// ---------------------------------------------------------------------
bool audioBegin(uint32_t rate) {
  if (rate < 8000 || rate > 48000) rate = 24000;
  return start((int)rate);
}

void audioSetVolume(int vol) { s_vol = constrain(vol, 0, 100); }

bool audioWriteMono(const int16_t *pcm, size_t frames) {
  if (!s_open) return false;
  float g = gainOf(s_vol);
  size_t done = 0;
  while (done < frames) {
    size_t k = frames - done;
    if (k > 1024) k = 1024;
    for (size_t i = 0; i < k; i++) {
      float f = g;
      if (s_fade > 0) {
        f *= 1.0f - (float)s_fade / (float)s_fadeTotal;
        s_fade--;
      }
      int32_t v = (int32_t)(pcm[done + i] * f);
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      s_stereo[2 * i] = (int16_t)v;
      s_stereo[2 * i + 1] = (int16_t)v;
    }
    size_t w = 0;
    i2s_write(PORT, s_stereo, k * 4, &w, portMAX_DELAY);
    done += k;
  }
  return true;
}

void audioEnd() { stop(); }
