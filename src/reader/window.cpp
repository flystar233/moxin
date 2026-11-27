#include "window.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

void TextWindow::release() {
  free(buf_);
  buf_ = nullptr;
  cap_ = 0;
  span_ = TextSpan();
  span_.total = total_;
}

void TextWindow::reset(uint32_t total, uint32_t cap) {
  total_ = total;
  release();
  want_ = cap < total ? cap : total;
}

bool TextWindow::allocate() {
  if (buf_) return true;
  for (uint32_t w = want_;; w /= 2) {  // PSRAM 不够就少要一点
    buf_ = (char *)alloc_(w + 1);
    if (buf_) { cap_ = w; break; }
    if (w <= 256 * 1024) return false;
  }
  span_ = TextSpan();
  span_.p = buf_;
  span_.total = total_;
  return true;
}

bool TextWindow::covers(uint32_t from, uint32_t to) const {
  if (to > total_) to = total_;
  if (from > to) from = to;
  return buf_ && (span_.len || total_ == 0) && span_.covers(from, to);
}

bool TextWindow::ensure(uint32_t from, uint32_t to, TextReader &rd) {
  if (to > total_) to = total_;
  if (from > to) from = to;
  if (covers(from, to)) return true;
  if (!allocate()) return false;
  const uint32_t W = cap_, len = total_;
  // 新窗口：以要的这段为中心，到了两头就靠边
  uint32_t start = 0;
  if (len > W) {
    uint32_t mid = from / 2 + to / 2;
    start = mid > W / 2 ? mid - W / 2 : 0;
    if (start + W > len) start = len - W;
    if (from < start) start = from;  // 要的比窗口还大（不该发生）：至少从 from 开始
  }
  const uint32_t n = len - start < W ? len - start : W;
  // 和旧窗口重叠的部分 [a, b) 在内存里挪到新位置，其余从卡上读
  uint32_t a = 0, b = 0;
  if (span_.len) {
    a = std::max(start, span_.base);
    b = std::min(start + n, span_.end());
    if (a < b) memmove(buf_ + (a - start), buf_ + (a - span_.base), b - a);
    else a = b = 0;
  }
  bool ok;
  if (a == b) {
    ok = n == 0 || rd.read(start, buf_, n);
  } else {
    ok = (a == start || rd.read(start, buf_, a - start)) && (b == start + n || rd.read(b, buf_ + (b - start), start + n - b));
  }
  if (!ok) {
    span_.len = 0;
    return false;
  }
  span_.base = start;
  span_.len = n;
  buf_[n] = 0;
  return true;
}
