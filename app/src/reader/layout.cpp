#include "layout.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "text/utf8.h"

// 行尾允许挂出去一个的标点，避免它们出现在行首
static bool isClosingPunct(uint32_t cp) {
  static const uint32_t P[] = {0xFF0C, 0x3002, 0x3001, 0xFF1B, 0xFF1A, 0xFF1F, 0xFF01, 0xFF09, 0x201D, 0x2019,
                               0x300B, 0x300D, 0x300F, 0x3011, 0x2026, ',', '.', ';', ':', '!', '?', ')'};
  for (uint32_t p : P)
    if (p == cp) return true;
  return false;
}

static bool isOpeningPunct(uint32_t cp) {
  static const uint32_t P[] = {0x201C, 0x2018, 0x300A, 0x300C, 0x300E, 0x3010, 0xFF08, 0x3008, 0x3014, '(', '['};
  for (uint32_t p : P)
    if (p == cp) return true;
  return false;
}

static bool isWordByte(uint32_t c) { return isalnum(c) || c == 0x27 || c == '-' || c == '_'; }

static uint32_t skipSpaces(const char *t, uint32_t i, uint32_t end) {
  while (i < end) {
    uint32_t k = i;
    if (!isSpaceCp(utf8Next(t, end, k))) break;
    i = k;
  }
  return i;
}

// 下面的位置都是相对 span.p 的（pos = 正文位置 - span.base）
uint32_t layoutPage(const TextSpan &span, const BookMeta &m, const Layout &L, uint32_t start, TextLine *out,
                    int maxLines, int *outCount) {
  const char *t = span.p;
  const uint32_t n = span.len, base = span.base;
  if (start < base || start > span.end()) {  // 调用方没准备好这一段：什么也不排
    if (outCount) *outCount = 0;
    return start;
  }
  uint32_t pos = start - base;
  bool para = start == 0 || (pos > 0 ? t[pos - 1] == '\n' : true);
  bool inHeading = false;
  size_t ci = std::lower_bound(m.ch.begin(), m.ch.end(), start, [](const Chapter &c, uint32_t o) { return c.off < o; }) -
              m.ch.begin();
  size_t hi = std::lower_bound(m.heads.begin(), m.heads.end(), start) - m.heads.begin();
  int cnt = 0;
  while (pos < n && cnt < maxLines) {
    if (t[pos] == '\n') { pos++; para = true; inHeading = false; continue; }
    if (para) {  // 段首：跳过原文自带的空格、全角空格缩进
      pos = skipSpaces(t, pos, n);
      if (pos >= n || t[pos] == '\n') continue;  // 空行
      const uint32_t abs = pos + base;
      while (ci < m.ch.size() && m.ch[ci].off < abs) ci++;
      while (hi < m.heads.size() && m.heads[hi] < abs) hi++;
      if (ci < m.ch.size() && m.ch[ci].off == abs) {
        if (cnt > 0) break;  // 新的一章换页
        ci++;
      }
      if (hi < m.heads.size() && m.heads[hi] == abs) {  // 标题段：居中加粗
        inHeading = true;
        hi++;
      }
    }
    TextLine ln{pos, 0, (uint8_t)(inHeading ? LINE_HEADING : para ? LINE_PARA : LINE_TEXT), false, 0};
    const int avail = L.maxWidth - (ln.kind == LINE_PARA ? L.indentWidth : 0);
    para = false;
    int w = 0;
    bool hung = false;
    while (pos < n && t[pos] != '\n') {
      uint32_t j = pos;
      uint32_t cp = utf8Next(t, n, j);
      int cw = L.adv(cp);
      if (w + cw > avail && pos > ln.start) {
        if (!hung && isClosingPunct(cp)) {
          hung = true;
        } else {
          // 英文单词不从中间断：退回到单词开头（单词比一行还长就只好断开）
          if (cp < 0x80 && isWordByte(cp)) {
            uint32_t k = pos;
            int back = 0;
            while (k > ln.start && isWordByte((uint8_t)t[k - 1])) { k--; back += L.adv((uint8_t)t[k]); }
            if (k > ln.start) { pos = k; w -= back; }
          }
          // 左引号、左括号这类不留在行尾，带到下一行
          uint32_t ps = pos;
          while (ps > ln.start && ((uint8_t)t[ps - 1] & 0xC0) == 0x80) ps--;
          if (ps > ln.start) ps--;
          if (ps > ln.start) {
            uint32_t q = ps;
            uint32_t prev = utf8Next(t, n, q);
            if (isOpeningPunct(prev)) { w -= L.adv(prev); pos = ps; }
          }
          break;
        }
      }
      w += cw;
      pos = j;
    }
    // 到了这一段的末尾但全书还没完：当成这一行还没断（调用方保证一页以内不会这样）
    bool endsPara = (pos >= n && span.end() >= span.total) || (pos < n && t[pos] == '\n');
    ln.len = pos - ln.start;
    ln.width = w;
    ln.justify = !endsPara && ln.kind != LINE_HEADING && w <= avail;
    ln.start += base;
    if (out) out[cnt] = ln;
    cnt++;
    if (inHeading && endsPara && cnt < maxLines) {  // 标题后空一行
      if (out) out[cnt] = TextLine{pos + base, 0, LINE_BLANK, false, 0};
      cnt++;
    }
  }
  if (outCount) *outCount = cnt;
  return pos + base;
}

// =====================================================================
// 分页
// =====================================================================
namespace {

// 一段一段读正文的滑动缓冲
class Window {
 public:
  Window(const BookMeta &m, const TextSpan *whole, TextReader *rd, uint32_t cap) : total_(m.len), rd_(rd) {
    if (whole) {
      span_ = *whole;
    } else {
      cap_ = cap;
      buf_ = (char *)malloc(cap);
      span_.p = buf_;
      span_.total = total_;
    }
  }
  ~Window() { free(buf_); }
  bool ok() const { return span_.p != nullptr; }
  const TextSpan &span() const { return span_; }
  // 一页比缓冲还长（几百 KB 的空行）：缓冲临时放大到能装下 need
  bool grow(uint32_t pos, uint32_t need) {
    if (!buf_) return false;
    if (need + 1 > cap_) {
      char *nb = (char *)realloc(buf_, need + 1);
      if (!nb) return false;
      buf_ = nb;
      cap_ = need + 1;
      span_.p = buf_;
      span_.len = 0;  // 缓冲换了地方，重新读
    }
    return ensure(pos, need);
  }
  // 保证 [pos - 1, pos + need) 在缓冲里（到全书末尾为止）
  bool ensure(uint32_t pos, uint32_t need) {
    uint32_t from = pos > 0 ? pos - 1 : 0;
    uint32_t to = pos + need < total_ ? pos + need : total_;
    if (span_.covers(from, to)) return true;
    if (!buf_) return true;  // 整本在内存，本来就全有
    uint32_t len = cap_ < total_ - from ? cap_ : total_ - from;
    uint32_t keep = 0;
    if (from >= span_.base && from < span_.end()) {  // 和现在的缓冲有重叠：挪过去，只读新的部分
      keep = span_.end() - from;
      if (keep > len) keep = len;
      memmove(buf_, buf_ + (from - span_.base), keep);
    }
    if (len > keep && !rd_->read(from + keep, buf_ + keep, len - keep)) return false;
    span_.base = from;
    span_.len = len;
    return true;
  }

 private:
  uint32_t total_, cap_ = 0;
  TextReader *rd_;
  char *buf_ = nullptr;
  TextSpan span_;
};

}  // namespace

static const uint32_t LOOKAHEAD = 64 * 1024;  // 一页最多这么多字节（再多是成片的空行，退化处理）

bool paginate(const BookMeta &m, const Layout &L, const TextSpan *whole, TextReader *rd, uint32_t bufBytes,
              std::vector<uint32_t> &pages, ProgressFn progress, void *ctx) {
  pages.clear();
  const uint32_t total = m.len;
  if (!whole && bufBytes < 4 * LOOKAHEAD) bufBytes = 4 * LOOKAHEAD;
  Window win(m, whole, rd, bufBytes);
  if (!win.ok()) return false;
  pages.reserve(total / 400 + 16);
  uint32_t pos = 0, lastReport = 0;
  while (pos < total) {
    if (!win.ensure(pos, LOOKAHEAD)) return false;
    pages.push_back(pos);
    uint32_t next = layoutPage(win.span(), m, L, pos, nullptr, L.linesPerPage, nullptr);
    // 一页超过了预读的长度（大片空行）：从这页开头多读一些再排，直到这一页排完（最多放大到 8MB）
    for (uint32_t need = 2 * LOOKAHEAD; next >= win.span().end() && win.span().end() < total; need *= 2) {
      if (need > 8u * 1024 * 1024 || !win.grow(pos, need)) break;
      next = layoutPage(win.span(), m, L, pos, nullptr, L.linesPerPage, nullptr);
    }
    if (next <= pos) break;
    pos = next;
    // 剩下的只有空白就不再开新页
    uint32_t k = pos;
    bool more = false;
    while (k < total) {
      if (!win.ensure(k, 16)) return false;
      const TextSpan &s = win.span();
      uint32_t r = k - s.base, n = s.len;
      uint32_t c = r;
      uint32_t cp = utf8Next(s.p, n, c);
      if (!(isSpaceCp(cp) || cp == '\n')) { more = true; break; }
      k = c + s.base;
    }
    if (!more) break;
    if (progress && pos - lastReport >= 256 * 1024) {
      lastReport = pos;
      if (!progress(ctx, pos, total)) return false;
    }
  }
  if (pages.empty()) pages.push_back(0);
  return true;
}
