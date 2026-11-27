#pragma once
// 流式合成客户端：发一句文本，边收边解出 PCM。
//
// 两个模型：
//   Edge 朗读（免费）：微软 Edge 的在线朗读接口，WebSocket + 按时间算的令牌；
//     整个朗读会话复用一条连接（每句新建连接要 1 秒多，合成跟不上就会卡）。
//   qwen-audio-3.1-tts-plus：阿里云百炼，HTTP 流式，要 API Key + 业务空间 ID。
#include <Arduino.h>

#include "sse_pcm.h"

namespace tts {

// 这一次请求用哪个模型、音色、语速、Key、业务空间（plus 需要）
struct Params {
  String model;        // TTS_MODEL_EDGE 或 TTS_MODEL_PLUS
  String voice;
  float speed = 1.0f;  // 语速倍率（0.5～2.0）
  String key;
  String workspace;  // 业务空间 ID：只有 qwen-audio-3.1-tts-plus 用
};

struct SynthResult {
  bool ok = false;
  bool stopped = false;  // 被 stop 打断
  String error;          // ok 为 false 时给屏上显示
  uint32_t bytes = 0;    // 解出来的 PCM 字节数
};

// 一个朗读会话用一个实例：Edge 内部保持 WebSocket 连接和 MP3 解码器。
// 参数由调用方传进来（这些值可能在主线程里改）。
// stop 变 true 时尽快返回（最多等一次网络读超时）。
class Client {
 public:
  SynthResult speak(const String &text, const Params &p, speech::PcmSink &sink, const volatile bool *stop);
  void close();

 private:
  SynthResult speakEdge(const String &text, const Params &p, speech::PcmSink &sink, const volatile bool *stop);
  bool connectEdge();
  bool sendConfig();
  bool sendSsml(const String &ssml);

  void *tls_ = nullptr;            // WiFiClientSecure*（放在 .cpp 里，免得头文件牵着 TLS 库）
  void *mp3_ = nullptr;            // libhelix::MP3DecoderHelix*
  void *out_ = nullptr;            // EdgeOut*
  bool up_ = false;                // 连接已建立、speech.config 已发
  String err_;
};

// 从服务端返回的 JSON 里取 "message"（出错时给用户看）
String jsonMessage(const String &body);

}  // namespace tts
