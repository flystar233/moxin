#pragma once
// 把 TXT 的原始字节转成 UTF-8，边读边转（不用整份原文再占一块内存）。
// 支持 UTF-8（有没有 BOM 都行）、GBK / GB2312、UTF-16 LE/BE（要有 BOM 或能从 0 字节看出来）。
// 顺便规整：\r\n、\r → \n；去掉 BOM、0 字节和别的控制字符。
#include <stdint.h>

enum class TextEnc : uint8_t { Utf8, Gbk, Utf16le, Utf16be };
const char *textEncName(TextEnc e);

// 看文件开头一段，猜编码；bomLen 返回要跳过的 BOM 字节数
TextEnc detectEncoding(const uint8_t *p, uint32_t n, uint32_t &bomLen);

class TextDecoder {
 public:
  // out：输出缓冲（PSRAM），cap：容量
  TextDecoder(TextEnc enc, char *out, uint32_t cap) : enc_(enc), out_(out), cap_(cap) {}
  // 解一段原始字节，返回用掉的字节数。末尾不够一个字的字节不用（last = true 时当坏字节）；
  // 调用方把没用掉的几个字节挪到下一段开头。输出满了 full() 为 true
  uint32_t feed(const uint8_t *p, uint32_t n, bool last);
  uint32_t length() const { return len_; }
  uint32_t badChars() const { return bad_; }
  bool full() const { return full_; }
  void rewind() { len_ = 0; full_ = false; }  // 输出写走了，从缓冲开头接着写（状态保留）

 private:
  bool put(uint32_t cp);
  TextEnc enc_;
  char *out_;
  uint32_t cap_, len_ = 0, bad_ = 0;
  bool lastCr_ = false, full_ = false;
};
