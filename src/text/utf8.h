#pragma once
#include <stdint.h>

// 解码一个字符，i 前进；非法字节返回 '?' 并前进 1
inline uint32_t utf8Next(const char *s, uint32_t n, uint32_t &i) {
  uint8_t c = s[i];
  int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
  if (i + len > n) len = 1;
  uint32_t cp = len == 1 ? (c < 0x80 ? c : '?') : (c & (0xFF >> (len + 1)));
  for (int k = 1; k < len; k++) {
    uint8_t b = s[i + k];
    if ((b & 0xC0) != 0x80) { len = 1; cp = '?'; break; }
    cp = (cp << 6) | (b & 0x3F);
  }
  i += len;
  return cp;
}

// 编码一个字符，返回字节数（1～4）
inline int utf8Encode(uint32_t cp, char *o) {
  if (cp < 0x80) { o[0] = cp; return 1; }
  if (cp < 0x800) { o[0] = 0xC0 | (cp >> 6); o[1] = 0x80 | (cp & 0x3F); return 2; }
  if (cp < 0x10000) {
    o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 0x3F); o[2] = 0x80 | (cp & 0x3F);
    return 3;
  }
  o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 0x3F); o[2] = 0x80 | ((cp >> 6) & 0x3F);
  o[3] = 0x80 | (cp & 0x3F);
  return 4;
}

// 空白：半角空格、制表、全角空格、不换行空格
inline bool isSpaceCp(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == 0x3000 || cp == 0xA0; }
