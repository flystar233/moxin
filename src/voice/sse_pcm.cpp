#include "sse_pcm.h"

namespace speech {
namespace {

// JSON 里 audio.data 前面的那 7 个字节（冒号后面可能还有空格）
const char PAT[7] = {'"', 'd', 'a', 't', 'a', '"', ':'};

inline int b64val(uint8_t c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

inline uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

}  // namespace

void StreamPcm::feed(const uint8_t *data, size_t n) {
  for (size_t k = 0; k < n; k++) {
    uint8_t c = data[k];
    if (inData_) {
      if (c == '"') {  // 这一段 base64 结束了
        inData_ = false;
        winLen_ = 0;
        acc_ = 0;
        bits_ = 0;
        continue;
      }
      int v = b64val(c);
      if (v < 0) continue;  // '=' 和空白：忽略
      acc_ = (acc_ << 6) | (uint32_t)v;
      bits_ += 6;
      if (bits_ >= 8) {
        bits_ -= 8;
        dataByte((uint8_t)((acc_ >> bits_) & 0xFF));
      }
      continue;
    }
    // 已经看到 "data":，等值的开始引号（中间可能有空白）
    if (afterColon_) {
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
      afterColon_ = false;
      if (c == '"') {  // 这一段 base64 开始了
        inData_ = true;
        winLen_ = 0;
        acc_ = 0;
        bits_ = 0;
        continue;
      }
      // 不是引号：当作普通字节，重新找
    }
    // 还没进 base64：在字节流里找 "data":
    if (winLen_ == 7) {
      memmove(win_, win_ + 1, 6);
      winLen_ = 6;
    }
    win_[winLen_++] = c;
    if (winLen_ == 7 && memcmp(win_, PAT, 7) == 0) {
      afterColon_ = true;
      winLen_ = 0;
      acc_ = 0;
      bits_ = 0;
    }
  }
}

void StreamPcm::dataByte(uint8_t b) {
  push(&b, 1);
}

void StreamPcm::finish() {
  inData_ = false;
  afterColon_ = false;
  winLen_ = 0;
  acc_ = 0;
  bits_ = 0;
  // 攒在 WAV 头缓冲里还没放出去的数据：不是 WAV 就当裸 PCM 放掉（太短的一段也要放出来）
  if (hdrState_ != HDR_DONE) {
    bool wav = headLen_ >= 12 && memcmp(head_, "RIFF", 4) == 0 && memcmp(head_ + 8, "WAVE", 4) == 0;
    hdrState_ = HDR_DONE;
    if (!wav && headLen_) {
      size_t n = headLen_;
      headLen_ = 0;
      push(head_, n);
    }
    headLen_ = 0;
  }
  flushOut();
  if (hasOdd_) {
    hasOdd_ = false;  // 单字节凑不成一个采样，丢掉
  }
}

void StreamPcm::flushOut() {
  if (!outLen_) return;
  // 保证交给 sink 的是整采样（16bit），单字节留到下一次
  size_t n = outLen_;
  if (hasOdd_) {
    // 把上次留下的那个字节放到最前面（out_ 最多用 1023 字节，加一个正好放得下）
    memmove(out_ + 1, out_, outLen_);
    out_[0] = odd_;
    n = outLen_ + 1;
  }
  size_t even = n & ~(size_t)1;
  if (even) sink_.pcm(out_, even);
  bytes_ += (uint32_t)even;
  hasOdd_ = (n > even);
  if (hasOdd_) odd_ = out_[even];
  outLen_ = 0;
}

// WAV 头处理：第一次进来先攒一段，是 RIFF 就找 data 块，不是就当裸 PCM 直接放出去
void StreamPcm::push(const uint8_t *data, size_t n) {
  if (hdrState_ == HDR_DONE) {
    while (n) {
      size_t room = sizeof(out_) - 1 - outLen_;
      if (!room) {
        flushOut();
        continue;
      }
      size_t k = n < room ? n : room;
      memcpy(out_ + outLen_, data, k);
      outLen_ += k;
      data += k;
      n -= k;
    }
    return;
  }

  // 还在攒头
  while (n && headLen_ < sizeof(head_)) {
    head_[headLen_++] = *data++;
    n--;
    if (parseHead()) break;
  }
  if (hdrState_ == HDR_DONE && n) push(data, n);  // 头后面的数据接着走
}

// 返回 true = 头已经处理完（或确定不是 WAV）
bool StreamPcm::parseHead() {
  if (headLen_ < 12) return false;
  if (memcmp(head_, "RIFF", 4) != 0 || memcmp(head_ + 8, "WAVE", 4) != 0) {
    // 不是 WAV：前面攒的字节全是 PCM
    hdrState_ = HDR_DONE;
    const uint8_t *p = head_;
    size_t n = headLen_;
    headLen_ = 0;
    push(p, n);
    return true;
  }
  // 走一遍块：fmt 记参数，data 后面就是 PCM
  size_t p = 12;
  while (p + 8 <= headLen_) {
    uint32_t sz = le32(head_ + p + 4);
    if (memcmp(head_ + p, "data", 4) == 0) {
      size_t start = p + 8;
      hdrState_ = HDR_DONE;
      const uint8_t *rest = head_ + start;
      size_t restLen = headLen_ - start;
      headLen_ = 0;
      if (restLen) push(rest, restLen);
      return true;
    }
    if (memcmp(head_ + p, "fmt ", 4) == 0 && sz >= 16 && p + 8 + 16 <= headLen_) {
      const uint8_t *f = head_ + p + 8;
      wavChannels_ = (int)(f[2] | (f[3] << 8));
      wavRate_ = le32(f + 4);
      wavBits_ = (int)(f[14] | (f[15] << 8));
    }
    // 未知块：跳过（块长度是奇数时后面有一个填充字节）
    uint64_t next = (uint64_t)p + 8 + sz + (sz & 1);
    if (next > sizeof(head_)) {  // 头太长/认不出来：当裸 PCM
      hdrState_ = HDR_DONE;
      const uint8_t *all = head_;
      size_t n = headLen_;
      headLen_ = 0;
      push(all, n);
      return true;
    }
    if (next > headLen_) return false;  // 还没收全
    p = (size_t)next;
  }
  if (headLen_ >= sizeof(head_)) {  // 攒满了还不是 WAV：当裸 PCM
    hdrState_ = HDR_DONE;
    const uint8_t *all = head_;
    size_t n = headLen_;
    headLen_ = 0;
    push(all, n);
    return true;
  }
  return false;
}

}  // namespace speech
