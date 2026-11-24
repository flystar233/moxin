#pragma once
// 阿里云百炼 qwen3-tts-flash 的流式合成客户端：发一句文本，边收边解出 PCM。
// 这里只管"一句"：连服务、发请求、把 SSE 里的 base64 音频剥出来（见 sse_pcm）。
// 排队、预合成、播放、翻页都在 voice/tts.cpp。
#include <Arduino.h>

#include "sse_pcm.h"

namespace tts {

// 这一次请求用哪个模型、音色、Key、业务空间（plus 需要）
struct Params {
  String model;      // TTS_MODEL 或 TTS_MODEL_PLUS
  String voice;
  String key;
  String workspace;  // 业务空间 ID：只有 qwen-audio-3.1-tts-plus 用
};

struct SynthResult {
  bool ok = false;
  bool stopped = false;  // 被 stop 打断
  String error;          // ok 为 false 时给屏上显示
  uint32_t bytes = 0;    // 解出来的 PCM 字节数
};

// 合成一句。参数由调用方传进来（这些值可能在主线程里改）。
// stop 变 true 时尽快返回（最多等一次网络读超时）。
SynthResult synthesize(const String &text, const Params &p, speech::PcmSink &sink, const volatile bool *stop);

// 从服务端返回的 JSON 里取 "message"（出错时给用户看）
String jsonMessage(const String &body);

}  // namespace tts
