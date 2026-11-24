#pragma once
// 阅读窗口：正文里一段连续的字放在内存里。要的范围不在窗口里，就以它为中心重新取一段；
// 和旧窗口重叠的部分在内存里挪过去，只从卡上读新的部分（顺着往后读时每次只读半个窗口）。
// 纯 C++，电脑上能测（tools/readertest）。
#include "layout.h"

class TextWindow {
 public:
  typedef void *(*AllocFn)(size_t);
  explicit TextWindow(AllocFn alloc) : alloc_(alloc) {}
  ~TextWindow() { release(); }
  // 换书：total = 正文长度，cap = 窗口大小（会按 total 截）
  void reset(uint32_t total, uint32_t cap);
  void release();                   // 放掉内存（以后 ensure 会重新分配）
  // 内存不够时窗口会缩小（每次减半，最小 256KB）；返回实际大小，0 = 分不到
  uint32_t capacity() const { return cap_; }
  bool covers(uint32_t from, uint32_t to) const;
  // 保证 [from, to) 在窗口里（to 超过全书就截到全书末尾）
  bool ensure(uint32_t from, uint32_t to, TextReader &rd);
  const TextSpan &span() const { return span_; }
  bool whole() const { return span_.len && span_.base == 0 && span_.len == total_; }

 private:
  bool allocate();
  AllocFn alloc_;
  char *buf_ = nullptr;
  uint32_t total_ = 0, want_ = 0, cap_ = 0;
  TextSpan span_;
};
