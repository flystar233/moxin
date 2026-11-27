// 在电脑上测"按句切分"和"流式 PCM 解析"（不需要板子、不需要网络、不需要 API Key）。
//
//   cl /nologo /EHsc /std:c++17 /Isrc tools\ttstest\main.cpp src\voice\sentence.cpp src\voice\sse_pcm.cpp
//   ttstest.exe [书.txt]
//
// 不给文件名就用内置的几段测试文本。
#include <cstdio>
#include <cstring>
#include <string>

#include <algorithm>

#include "voice/sentence.h"
#include "voice/sse_pcm.h"

struct MemSource : speech::TextSource {
  std::string d;
  uint32_t read(uint32_t off, char *buf, uint32_t n) override {
    if (off >= d.size()) return 0;
    size_t k = std::min<size_t>(n, d.size() - off);
    memcpy(buf, d.data() + off, k);
    return (uint32_t)k;
  }
};

struct VecSink : speech::PcmSink {
  std::string got;
  void pcm(const uint8_t *d, size_t n) override { got.append((const char *)d, n); }
};

static int failures = 0;
#define CHECK(cond, ...)                       \
  do {                                         \
    if (!(cond)) {                             \
      printf("  ✗ " __VA_ARGS__);              \
      printf("\n");                            \
      failures++;                              \
    }                                          \
  } while (0)

// ---------------------------------------------------------------------
static int u8chars(const std::string &s) {
  int n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) n++;
  return n;
}

static int splitAll(const std::string &text, int maxChars, int minChars, bool verbose, int *skippedBytes) {
  MemSource src;
  src.d = text;
  uint32_t off = 0, prevEnd = 0, prevOff = 0;
  int count = 0, prevBad = 0;
  uint32_t biggestGap = 0;
  speech::Sentence s;
  while (speech::nextSentence(src, (uint32_t)text.size(), off, maxChars, minChars, s)) {
    if (s.len == 0 || s.off + s.len > text.size() || s.off < prevEnd) {
      printf("  ✗ 范围不对：off=%u len=%u（上一句结束 %u）\n", s.off, s.len, prevEnd);
      failures++;
      break;
    }
    std::string t = text.substr(s.off, s.len);
    biggestGap = std::max(biggestGap, s.off - prevEnd);
    if (!t.empty() && (t.back() == ' ' || t.back() == '\n' || t.back() == '\t' || t.back() == '\r')) {
      printf("  ✗ 句子以空白结尾：[%s]\n", t.c_str());
      failures++;
    }
    if (verbose) printf("  [%6u +%3u %2d字]%s %s\n", s.off, s.len, u8chars(t), s.last ? "*" : " ", t.c_str());
    // 往前找上一句：起点应该对得上
    if (count > 0) {
      uint32_t back = 0;
      if (!speech::prevSentenceStart(src, (uint32_t)text.size(), s.off, maxChars, minChars, back) || back != prevOff) {
        if (++prevBad == 1) printf("  ✗ 上一句没对上：这次 %u，上次 %u\n", back, prevOff);
        failures++;
      }
    }
    prevEnd = s.off + s.len;
    prevOff = s.off;
    count++;
    if (s.last) break;
    off = prevEnd;
    if (count > 200000) break;
  }
  if (skippedBytes) *skippedBytes = (int)biggestGap;
  return count;
}

static void testSentences() {
  printf("== 按句切分 ==\n");
  struct Case {
    const char *text;
    int maxChars;
  };
  const Case cases[] = {
      {"第一句。第二句！“引号里的句子？”第三句……\n\n第四段没有标点，长长的一行也要读\n第五段。", 120},
      {"这是一个很长的句子，里面有很多逗号，一直都没有句号，所以我们必须在逗号或者字数上限的地方把它断开，不能让它超过一次请求的长度限制，否则模型会报错。", 40},
      {"Hello world! How are you? Fine.\nOK", 60},
      {"　　段首缩进用全角空格。\n\n\n\n空行之后。", 120},
      {"“对话一。”\n“对话二！”\n……\n（完）", 120},
  };
  for (const Case &c : cases) {
    printf("-- %s\n", c.text);
    int maxGap = 0;
    int n = splitAll(c.text, c.maxChars, 2, true, &maxGap);
    printf("   %d 句，最大空隙 %d 字节\n", n, maxGap);
    CHECK(n > 0, "一句都没切出来");
  }
}

// ---------------------------------------------------------------------
static std::string b64(const std::string &in) {
  static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  size_t i = 0;
  while (i + 2 < in.size()) {
    uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
    o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    i += 3;
  }
  if (i + 1 == in.size()) {
    uint32_t v = (uint8_t)in[i] << 16;
    o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += "==";
  } else if (i + 2 == in.size()) {
    uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
    o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '=';
  }
  return o;
}

static void testSse() {
  printf("== 流式 PCM 解析 ==\n");
  // 造一段"音频"：300 字节
  std::string pcm;
  for (int i = 0; i < 300; i++) pcm.push_back((char)(i * 7 + 3));
  std::string a = pcm.substr(0, 150), b = pcm.substr(150);
  std::string sse =
      "data:{\"status_code\":200,\"output\":{\"finish_reason\":\"null\",\"audio\":{\"data\":\"" + b64(a) +
      "\",\"url\":\"\"}},\"usage\":{}}\n\ndata:{\"status_code\":200,\"output\":{\"finish_reason\":\"stop\",\"audio\":{\"data\":\"" +
      b64(b) + "\",\"url\":\"\"}}}\n\n";
  VecSink sink;
  speech::StreamPcm sp(sink);
  // 故意用奇怪的块大小喂，检查跨块的 base64 / 窗口匹配
  for (size_t i = 0; i < sse.size(); i += 7) sp.feed((const uint8_t *)sse.data() + i, std::min<size_t>(7, sse.size() - i));
  sp.finish();
  CHECK(sink.got == pcm, "SSE 解出来的 PCM 和原来不一样（%u vs %zu 字节）", sp.bytes(), pcm.size());
  CHECK(sp.sawAudio(), "没解出音频");
  printf("  SSE：%u 字节，%s\n", sp.bytes(), sink.got == pcm ? "一致" : "不一致");

  // WAV：头 + PCM，头里带一个没用的 LIST 块
  std::string wav;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) wav.push_back((char)((v >> (8 * i)) & 0xFF)); };
  auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; i++) wav.push_back((char)((v >> (8 * i)) & 0xFF)); };
  wav += "RIFF"; u32(36 + 8 + 4 + (uint32_t)pcm.size()); wav += "WAVE";
  wav += "fmt "; u32(16); u16(1); u16(1); u32(24000); u32(48000); u16(2); u16(16);
  wav += "LIST"; u32(4); wav += "INFO";
  wav += "data"; u32((uint32_t)pcm.size());
  wav += pcm;
  VecSink sink2;
  speech::StreamPcm sp2(sink2);
  for (size_t i = 0; i < wav.size(); i += 5) sp2.feedPcm((const uint8_t *)wav.data() + i, std::min<size_t>(5, wav.size() - i));
  sp2.finish();
  CHECK(sink2.got == pcm, "WAV 解出来的 PCM 不对（%u vs %zu）", sp2.bytes(), pcm.size());
  CHECK(sp2.wavRate() == 24000, "WAV 采样率读错了：%u", sp2.wavRate());
  CHECK(sp2.wavChannels() == 1 && sp2.wavBits() == 16, "WAV 声道/位深读错了");
  printf("  WAV：%u 字节，%u Hz %d 声道 %d bit，%s\n", sp2.bytes(), sp2.wavRate(), sp2.wavChannels(), sp2.wavBits(),
         sink2.got == pcm ? "一致" : "不一致");

  // 裸 PCM（不是 WAV，也不是 SSE）
  VecSink sink3;
  speech::StreamPcm sp3(sink3);
  sp3.feedPcm((const uint8_t *)pcm.data(), pcm.size());
  sp3.finish();
  CHECK(sink3.got == pcm, "裸 PCM 输出不对");

  // 非流式返回（只有 url）：不能吐出假音频
  VecSink sink4;
  speech::StreamPcm sp4(sink4);
  std::string js = "{\"output\":{\"audio\":{\"data\":\"\",\"url\":\"https://x/y.wav\"}}}";
  sp4.feed((const uint8_t *)js.data(), js.size());
  sp4.finish();
  CHECK(!sp4.sawAudio(), "非流式 JSON 被当成了音频");

  // 冒号后面有空格、data 里带 '=' 补齐
  std::string odd = pcm.substr(0, 5);  // 5 字节：base64 会带 '='
  VecSink sink5;
  speech::StreamPcm sp5(sink5);
  std::string sse2 = "data:{\"output\":{\"audio\":{\"data\": \"" + b64(odd) + "\"}},\"x\":1}\n\n";
  sp5.feed((const uint8_t *)sse2.data(), sse2.size());
  sp5.finish();
  printf("  空格/补齐：%u 字节（期望 4）\n", sp5.bytes());
  CHECK(sink5.got.size() >= 4 && sink5.got.compare(0, 4, odd.substr(0, 4)) == 0, "带空格/补齐的 base64 解错了");
}

// ---------------------------------------------------------------------
// 用真实生成的 WAV 验证：头必须被完整剥掉，PCM 一个字节都不能少
static int testWavFile(const std::string &d) {
  printf("== WAV 文件 ==\n");
  if (d.size() < 44 || d.compare(0, 4, "RIFF") != 0 || d.compare(8, 4, "WAVE") != 0) {
    printf("  不是 WAV\n");
    return 1;
  }
  // 在电脑上自己也走一遍块，找出 data 的起点，和固件解析结果对比
  size_t pos = 12, dataOff = 0;
  uint32_t rate = 0;
  int ch = 0, bits = 0;
  while (pos + 8 <= d.size()) {
    uint32_t sz = (uint8_t)d[pos + 4] | ((uint8_t)d[pos + 5] << 8) | ((uint8_t)d[pos + 6] << 16) |
                  ((uint32_t)(uint8_t)d[pos + 7] << 24);
    if (d.compare(pos, 4, "data") == 0) {
      dataOff = pos + 8;
      break;
    }
    if (d.compare(pos, 4, "fmt ") == 0 && sz >= 16 && pos + 24 <= d.size()) {
      const unsigned char *f = (const unsigned char *)d.data() + pos + 8;
      ch = f[2] | (f[3] << 8);
      rate = (uint32_t)f[4] | ((uint32_t)f[5] << 8) | ((uint32_t)f[6] << 16) | ((uint32_t)f[7] << 24);
      bits = f[14] | (f[15] << 8);
    }
    pos += 8 + sz + (sz & 1);
  }
  if (!dataOff) {
    printf("  找不到 data 块\n");
    return 1;
  }
  printf("  文件 %zu 字节，%u Hz %d 声道 %d bit，data 从 %zu 开始，PCM %zu 字节\n", d.size(), rate, ch, bits, dataOff,
         d.size() - dataOff);
  VecSink sink;
  speech::StreamPcm sp(sink);
  // 分成奇怪的块喂，模拟网络一段一段到
  for (size_t i = 0; i < d.size(); i += 137) sp.feedPcm((const uint8_t *)d.data() + i, std::min<size_t>(137, d.size() - i));
  sp.finish();
  std::string want = d.substr(dataOff);
  CHECK(sink.got.size() == want.size(), "PCM 长度不对：%zu，应该 %zu", sink.got.size(), want.size());
  CHECK(sink.got == want, "PCM 内容不对");
  CHECK(sp.wavRate() == rate || rate == 0, "采样率没读出来：%u（头里 %u）", sp.wavRate(), rate);
  printf("  解出 %u 字节（%.2f 秒），%s\n", sp.bytes(), sp.bytes() / 48000.0, sink.got == want ? "一致" : "不一致");
  return 0;
}

// ---------------------------------------------------------------------
int main(int argc, char **argv) {
  testSentences();
  testSse();
  if (argc > 1) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
      printf("打不开 %s\n", argv[1]);
      return 1;
    }
    std::string data;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    if (data.size() >= 4 && data.compare(0, 4, "RIFF") == 0) {
      testWavFile(data);
    } else {
      printf("== 真实文件：%s ==\n", argv[1]);
      int maxGap = 0;
      int cnt = splitAll(data, 120, 2, true, &maxGap);
      printf("   %zu 字节 → %d 句，最大空隙 %d 字节\n", data.size(), cnt, maxGap);
      CHECK(cnt > 0, "一句都没切出来");
      CHECK(maxGap < 64, "有空隙 %d 字节，可能漏字", maxGap);
    }
  }
  printf(failures ? "\n有 %d 项不对\n" : "\n全部通过\n", failures);
  return failures ? 1 : 0;
}
