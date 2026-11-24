#include "sentence.h"

#include <string.h>

namespace speech {
namespace {

// 空白：段首缩进、换行、全角空格
bool isSpace(uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == 0x3000 || cp == 0x00A0;
}

// 句末标点
bool isTerminator(uint32_t cp) {
  switch (cp) {
    case 0x3002:  // 。
    case 0xFF01:  // ！
    case 0xFF1F:  // ？
    case 0xFF1B:  // ；
    case '!':
    case '?':
    case ';':
    case 0x2026:  // …
      return true;
    default:
      return false;
  }
}

// 句中的停顿：太长时优先在这里断
bool isSecondary(uint32_t cp) {
  switch (cp) {
    case 0xFF0C:  // ，
    case 0x3001:  // 、
    case 0xFF1A:  // ：
    case ',':
    case ':':
    case 0x2014:  // —
    case 0x2013:  // –
      return true;
    default:
      return false;
  }
}

// 右引号、右括号：跟在句末标点后面时算这一句
bool isCloser(uint32_t cp) {
  switch (cp) {
    case 0x201D:  // ”
    case 0x2019:  // ’
    case '"':
    case '\'':
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0xFF09:  // ）
    case ')':
    case 0x3011:  // 】
    case 0x300B:  // 》
    case 0x3009:  // 〉
    case 0x3015:  // 〕
    case ']':
      return true;
    default:
      return false;
  }
}

// 真正有内容的字（不是空白、标点、符号）。全是标点的句子不合成
bool isMeaningful(uint32_t cp) {
  if (isSpace(cp) || isTerminator(cp) || isSecondary(cp) || isCloser(cp)) return false;
  switch (cp) {
    case 0x00B7:  // ·
    case 0x2027:  // ‧
    case 0xFF5E:  // ～
    case '~':
    case '-':
    case 0x3000:
    case 0x00A0:
      return false;
    default:
      return true;
  }
}

// 带缓冲的 UTF-8 读取：从 off 开始一个字一个字取
class Cursor {
 public:
  Cursor(TextSource &src, uint32_t total, uint32_t off) : src_(src), total_(total), pos_(off > total ? total : off) {}

  // 读一个字：cp、占的字节数、在正文里的起点。到末尾返回 false
  bool next(uint32_t &cp, uint32_t &nbytes, uint32_t &startOff) {
    uint8_t b;
    startOff = pos_;
    if (!get(b)) return false;
    int extra = 0;
    if (b >= 0xF0 && b < 0xF8) extra = 3;
    else if (b >= 0xE0) extra = 2;
    else if (b >= 0xC0) extra = 1;
    uint8_t tmp[4] = {b, 0, 0, 0};
    int got = 1;
    for (int i = 0; i < extra; i++) {
      uint8_t c;
      if (!get(c)) break;
      if ((c & 0xC0) != 0x80) { unget(); break; }  // 不是续字节：这个字节属于下一个字
      tmp[got++] = c;
    }
    nbytes = got;
    cp = decode(tmp, got);
    return true;
  }

 private:
  bool get(uint8_t &c) {
    if (i_ >= n_) {
      if (pos_ >= total_) return false;
      uint32_t want = total_ - pos_;
      if (want > sizeof(buf_)) want = sizeof(buf_);
      uint32_t got = src_.read(pos_, buf_, want);
      if (!got) return false;
      base_ = pos_;
      n_ = got;
      i_ = 0;
    }
    c = (uint8_t)buf_[i_++];
    pos_ = base_ + i_;
    return true;
  }

  // 刚读的那个字节还回去（只可能在同一块缓冲里）
  void unget() {
    if (i_ > 0) {
      i_--;
      pos_--;
    }
  }

  static uint32_t decode(const uint8_t *p, int n) {
    if (n <= 1) return p[0] ? p[0] : 0xFFFD;
    uint32_t cp = n == 2 ? (p[0] & 0x1Fu) : n == 3 ? (p[0] & 0x0Fu) : (p[0] & 0x07u);
    for (int i = 1; i < n; i++) cp = (cp << 6) | (p[i] & 0x3Fu);
    return cp ? cp : 0xFFFD;
  }

  TextSource &src_;
  uint32_t total_;
  uint32_t base_ = 0, pos_ = 0, n_ = 0, i_ = 0;
  char buf_[512];
};

struct Scan {
  bool started = false;
  uint32_t startOff = 0;
  uint32_t endOff = 0;     // 最后一个非空白字之后
  uint32_t lastBreak = 0;  // 最近的次要标点之后
  int lastBreakChars = 0;
  int chars = 0;           // 字数（含标点）
  int meaningful = 0;      // 有内容的字数
};

// 把扫到的一句交出去；后面还有正文就不是最后一句
bool emit(TextSource &src, uint32_t total, const Scan &st, Sentence &out) {
  if (!st.started || st.meaningful == 0 || st.endOff <= st.startOff) return false;
  out.off = st.startOff;
  out.len = st.endOff - st.startOff;
  out.last = true;
  Cursor c(src, total, out.off + out.len);
  uint32_t cp, nb, at;
  while (c.next(cp, nb, at)) {
    if (!isSpace(cp)) {
      out.last = false;
      break;
    }
  }
  return true;
}

}  // namespace

bool nextSentence(TextSource &src, uint32_t total, uint32_t off, int maxChars, int minChars, Sentence &out) {
  if (maxChars < 16) maxChars = 16;
  if (minChars < 2) minChars = 2;
  if (off >= total) return false;
  Cursor c(src, total, off);
  Scan st;
  uint32_t cp, nb, at;
  while (c.next(cp, nb, at)) {
    if (!st.started) {
      if (isSpace(cp)) continue;
      st.started = true;
      st.startOff = at;
    }
    // 换行：够长就断（诗、对话、标题），不够长就当空白，和下一行接起来
    if (cp == '\n' || cp == '\r') {
      if (st.chars >= minChars && emit(src, total, st, out)) return true;
      continue;
    }
    if (isSpace(cp)) {
      // 半角空格是英文的断词点：太长时在这里断，不把单词劈开
      if (cp == ' ' && st.chars > 0) {
        st.lastBreak = at;
        st.lastBreakChars = st.chars;
      }
      continue;
    }
    st.chars++;
    st.endOff = at + nb;
    if (isMeaningful(cp)) st.meaningful++;
    if (isTerminator(cp)) {
      // 后面的右引号、右括号、重复的省略号/叹号（"……"、"？！"）算这一句的
      Cursor c2(src, total, st.endOff);
      uint32_t cp2, nb2, at2;
      while (c2.next(cp2, nb2, at2) && (isCloser(cp2) || isTerminator(cp2))) st.endOff = at2 + nb2;
      if (emit(src, total, st, out)) return true;
      st = Scan();  // 只有标点，跳过
      continue;
    }
    if (isSecondary(cp)) {
      st.lastBreak = st.endOff;
      st.lastBreakChars = st.chars;
    }
    if (st.chars >= maxChars) {
      uint32_t save = st.endOff;
      // 断点离上限太远（不到 3/5）就硬断，不然句子会碎得很短
      if (st.lastBreak > st.startOff + 8 && st.lastBreakChars * 5 >= maxChars * 3) st.endOff = st.lastBreak;
      if (emit(src, total, st, out)) return true;
      st.endOff = save;
      st = Scan();  // 理论上走不到（有 meaningful 才会到 maxChars）
    }
  }
  // 末尾没标点的最后一句
  return emit(src, total, st, out);
}

bool sentenceText(TextSource &src, const Sentence &s, std::string &out) {
  out.clear();
  if (!s.len) return false;
  out.reserve(s.len);
  char buf[512];
  uint32_t done = 0;
  while (done < s.len) {
    uint32_t want = s.len - done;
    if (want > sizeof(buf)) want = sizeof(buf);
    uint32_t got = src.read(s.off + done, buf, want);
    if (!got) return false;
    out.append(buf, got);
    done += got;
  }
  return true;
}

bool prevSentenceStart(TextSource &src, uint32_t total, uint32_t off, int maxChars, int minChars, uint32_t &outOff) {
  if (off == 0) return false;
  uint32_t span = (uint32_t)maxChars * 8 + 64;
  for (int attempt = 0; attempt < 3; attempt++) {
    uint32_t from = off > span ? off - span : 0;
    uint32_t cur = from, last = 0;
    bool have = false;
    Sentence s;
    while (cur < off && nextSentence(src, total, cur, maxChars, minChars, s)) {
      if (s.off >= off) break;
      last = s.off;
      have = true;
      uint32_t nxt = s.off + s.len;
      if (nxt <= cur) break;
      cur = nxt;
    }
    // 起点正好是回看窗口的开头：可能从半句开始，往回再找远一点
    if (have && !(from > 0 && last == from)) {
      outOff = last;
      return true;
    }
    if (from == 0) break;
    span *= 3;
  }
  return false;
}

}  // namespace speech
