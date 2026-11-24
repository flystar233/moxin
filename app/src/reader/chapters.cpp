#include "chapters.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "text/utf8.h"

static bool isNumeral(uint32_t cp) {
  if ((cp >= '0' && cp <= '9') || (cp >= 0xFF10 && cp <= 0xFF19)) return true;
  static const char NUM[] = "零一二三四五六七八九十百千万两〇壹贰叁肆伍陆柒捌玖拾佰仟廿卅萬兩";
  uint32_t i = 0, n = sizeof(NUM) - 1;
  while (i < n)
    if (utf8Next(NUM, n, i) == cp) return true;
  return false;
}

static bool isUnit(uint32_t cp) {
  static const char U[] = "章回节卷集部篇幕话册節話冊";
  uint32_t i = 0, n = sizeof(U) - 1;
  while (i < n)
    if (utf8Next(U, n, i) == cp) return true;
  return false;
}

// 整行（去掉首尾空白）不长，并且像"第十二章 xxx""Chapter 3""序章""番外"
bool isChapterHeading(const char *p, uint32_t n) {
  if (n == 0 || n > 90) return false;
  uint32_t i = 0;
  uint32_t cp = utf8Next(p, n, i);
  if (cp == 0x7B2C || cp == 0x5377) {  // 第 / 卷
    int digits = 0;
    while (i < n) {
      uint32_t k = i;
      uint32_t c = utf8Next(p, n, k);
      if (!isNumeral(c)) break;
      i = k;
      if (++digits > 12) return false;
    }
    if (!digits) return false;
    if (cp == 0x5377) return true;  // 卷一、卷十二
    return i < n && isUnit(utf8Next(p, n, i));
  }
  if ((cp == 'C' || cp == 'c') && n >= 8 && strncasecmp(p, "chapter", 7) == 0) return p[7] == ' ' || isdigit((uint8_t)p[7]);
  static const char *const W[] = {"序章", "楔子", "引子", "序言", "序幕", "前言", "后记", "尾声", "番外", "终章", "结局", "引言", "自序", "跋"};
  if (n > 36) return false;
  for (const char *w : W) {
    size_t l = strlen(w);
    if (n >= l && memcmp(p, w, l) == 0) return true;
  }
  return false;
}

static const uint32_t LINE_KEEP = 4096;

void ChapterScanner::feed(const char *p, uint32_t n) {
  uint32_t i = 0;
  while (i < n) {
    const char *nl = (const char *)memchr(p + i, '\n', n - i);
    uint32_t e = nl ? (uint32_t)(nl - p) : n;
    uint32_t take = e - i;
    uint32_t room = LINE_KEEP - line_.size();  // line_ 从不超过 LINE_KEEP
    line_.append(p + i, take < room ? take : room);
    if (take > room) lineLong_ = true;  // 超过 4KB 的行不可能是标题
    pos_ += take;
    i = e;
    if (nl) {
      endLine();
      pos_++;  // 换行符
      i++;
      lineStart_ = pos_;
    }
  }
}

void ChapterScanner::endLine() {
  if (!lineLong_ && ch_.size() < max_ && !line_.empty()) {
    const char *t = line_.data();
    uint32_t le = line_.size(), s = 0;
    while (s < le) {  // 去掉行首空白
      uint32_t k = s;
      if (!isSpaceCp(utf8Next(t, le, k))) break;
      s = k;
    }
    uint32_t e = le;
    while (e > s) {  // 去掉行尾空白（半角空格、制表、全角空格）
      if (t[e - 1] == ' ' || t[e - 1] == '\t') e--;
      else if (e >= s + 3 && memcmp(t + e - 3, "\xE3\x80\x80", 3) == 0) e -= 3;
      else break;
    }
    if (e > s && isChapterHeading(t + s, e - s)) tmp_.addChapter(lineStart_ + s, t + s, e - s);
  }
  line_.clear();
  lineLong_ = false;
}

void ChapterScanner::finish(BookMeta &m) {
  if (pos_ > lineStart_ || !line_.empty()) endLine();  // 最后一行没有换行符
  m.ch.clear();
  m.titles.clear();
  m.heads.clear();
  if (!ch_.empty() && pos_ / ch_.size() < 400) return;  // 太密了，不算
  m.ch = tmp_.ch;
  m.titles = tmp_.titles;
  for (auto &c : m.ch) m.heads.push_back(c.off);
}
