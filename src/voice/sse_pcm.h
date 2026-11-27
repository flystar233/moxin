#pragma once
// 把 qwen3-tts-flash 的流式响应（SSE）变成一段段 PCM。
//
// 服务端开流以后返回的是 SSE：每行 `data:{"output":{"audio":{"data":"<base64>"}}}`。
// 实测 qwen3-tts-flash 的 base64 解出来是 WAV：44 字节 RIFF 头（24kHz、16bit、单声道）+ PCM，
// 而且 data 块的长度字段是假的（流式不知道总长）。这里不解析整个 JSON（省内存、也快）：
// 只在字节流里找 "data":"，后面的 base64 边收边解，遇到引号结束；WAV 头剥掉，裸 PCM 也认。
// 纯 C++，电脑上能单独编译测（tools/ttstest）。
#include <stdint.h>
#include <string.h>

namespace speech {

struct PcmSink {
  virtual ~PcmSink() = default;
  virtual void pcm(const uint8_t *data, size_t n) = 0;  // 24kHz 16bit 单声道，小端
};

class StreamPcm {
 public:
  explicit StreamPcm(PcmSink &sink) : sink_(sink) {}

  // 喂 SSE 原始字节
  void feed(const uint8_t *data, size_t n);
  // 直接喂 PCM / WAV 字节（非流式返回的音频文件走这里）
  void feedPcm(const uint8_t *data, size_t n) { push(data, n); }
  // 收尾：把不满 4 个字符的 base64、半个采样冲出去
  void finish();
  // 先把已经解出来的 PCM 交出去（不结束流，后面还可以接着 feed）
  void flushNow() { flushOut(); }

  uint32_t bytes() const { return bytes_; }       // 已经交给 sink 的 PCM 字节数
  bool sawAudio() const { return bytes_ > 0; }
  // 如果是 WAV，这里是从头里读到的参数（0 = 不是 WAV 或还没读到）
  uint32_t wavRate() const { return wavRate_; }
  int wavChannels() const { return wavChannels_; }
  int wavBits() const { return wavBits_; }

 private:
  void push(const uint8_t *data, size_t n);   // WAV 头处理之后交给 sink
  void emit(const uint8_t *data, size_t n);
  void dataByte(uint8_t b);                   // base64 解出来的一个字节
  void flushOut();
  bool parseHead();

  PcmSink &sink_;

  // ---- SSE 里找 "data":"（冒号后面允许有空白） ----
  uint8_t win_[8] = {0};
  int winLen_ = 0;
  bool afterColon_ = false;
  bool inData_ = false;

  // ---- base64 ----
  uint32_t acc_ = 0;
  int bits_ = 0;
  uint8_t out_[1024];
  size_t outLen_ = 0;
  bool hasOdd_ = false;
  uint8_t odd_ = 0;

  // ---- WAV 头 ----
  enum : uint8_t { HDR_UNDECIDED, HDR_DONE };
  uint8_t hdrState_ = HDR_UNDECIDED;
  uint8_t head_[1024];
  size_t headLen_ = 0;
  uint32_t wavRate_ = 0;
  int wavChannels_ = 0, wavBits_ = 0;

  uint32_t bytes_ = 0;
};

}  // namespace speech
