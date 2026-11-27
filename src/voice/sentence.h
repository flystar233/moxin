#pragma once
// 按句切分：把正文切成适合一次语音合成请求的句子（"按句流式"就是一发一句）。
//
// 规则：
//   - 跳过段首空白、缩进、空行；
//   - 在 。！？…； 和英文 ! ? ; 处断句，句末标点后面的右引号、右括号算在这一句里；
//   - 没有标点的行（诗、对话）在换行处断（至少 minChars 个字），不然会和下一段粘在一起；
//   - 一句最多 maxChars 个字：太长就在最后的 ，、： 处断，没有就硬断；
//   - 只有标点的句子（"……"、"——"）跳过，不发请求。
//
// 纯 C++（只依赖 std），不依赖 Arduino，电脑上可以单独编译测（tools/ttstest）。
#include <stdint.h>

#include <string>

namespace speech {

// 正文取值：读 [off, off + n) 到 buf，返回实际读到的字节数（到全书末尾会少）。
// 设备上是阅读窗口（reader/book.cpp 的 bookTextAt），电脑上是文件。
struct TextSource {
  virtual ~TextSource() = default;
  virtual uint32_t read(uint32_t off, char *buf, uint32_t n) = 0;
};

struct Sentence {
  uint32_t off = 0;   // 在正文里的起点（第一个非空白字节）
  uint32_t len = 0;   // 字节数
  bool last = false;  // 后面已经没有正文了
};

// 从 off 开始找下一句。找不到（到末尾了）返回 false。
bool nextSentence(TextSource &src, uint32_t total, uint32_t off, int maxChars, int minChars, Sentence &out);

// 把这一句读成 UTF-8 字符串
bool sentenceText(TextSource &src, const Sentence &s, std::string &out);

// 找 off 之前那一句的起点（当前句的起点传进来）。没有上一句返回 false。
bool prevSentenceStart(TextSource &src, uint32_t total, uint32_t off, int maxChars, int minChars, uint32_t &outOff);

}  // namespace speech
