#include "decode.h"

#include "gbk_table.h"
#include "utf8.h"

const char *textEncName(TextEnc e) {
  switch (e) {
    case TextEnc::Gbk: return "GBK";
    case TextEnc::Utf16le: return "UTF-16LE";
    case TextEnc::Utf16be: return "UTF-16BE";
    default: return "UTF-8";
  }
}

static uint16_t gbkLookup(uint8_t lead, uint8_t trail) {
  if (lead < 0x81 || lead > 0xFE || trail < 0x40 || trail > 0xFE) return 0;
  return GBK_TABLE[(lead - 0x81) * 191 + (trail - 0x40)];
}

TextEnc detectEncoding(const uint8_t *p, uint32_t n, uint32_t &bomLen) {
  bomLen = 0;
  if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) { bomLen = 3; return TextEnc::Utf8; }
  if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { bomLen = 2; return TextEnc::Utf16le; }
  if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { bomLen = 2; return TextEnc::Utf16be; }
  // 没有 BOM 的 UTF-16：英文、数字、换行的高字节是 0
  uint32_t zEven = 0, zOdd = 0, m = n < 4096 ? n : 4096;
  for (uint32_t i = 0; i < m; i++)
    if (!p[i]) (i & 1 ? zOdd : zEven)++;
  if (m >= 64 && zOdd > m / 8 && zEven < m / 64) return TextEnc::Utf16le;
  if (m >= 64 && zEven > m / 8 && zOdd < m / 64) return TextEnc::Utf16be;
  // UTF-8 检查：多字节序列都合法才算；样本末尾可能截断一个字，不计
  uint32_t i = 0, multi = 0, bad = 0, end = n > 4 ? n - 4 : 0;
  while (i < end) {
    uint8_t c = p[i];
    if (c < 0x80) { i++; continue; }
    int len = (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    bool ok = len > 0;
    for (int k = 1; ok && k < len; k++) ok = (p[i + k] & 0xC0) == 0x80;
    if (ok) { multi++; i += len; }
    else { bad++; i++; }
  }
  if (bad == 0 || bad * 50 < multi) return TextEnc::Utf8;  // 坏字节不到 2%
  return TextEnc::Gbk;
}

bool TextDecoder::put(uint32_t cp) {
  if (cp == '\r') { lastCr_ = true; cp = '\n'; }
  else if (cp == '\n' && lastCr_) { lastCr_ = false; return true; }  // \r\n 已经出过一个 \n
  else lastCr_ = false;
  if (cp == 0xFEFF || cp == 0) return true;
  if (cp < 0x20 && cp != '\n' && cp != '\t') return true;
  if (len_ + 4 > cap_) { full_ = true; return false; }
  len_ += utf8Encode(cp, out_ + len_);
  return true;
}

uint32_t TextDecoder::feed(const uint8_t *p, uint32_t n, bool last) {
  uint32_t i = 0;
  while (i < n) {
    uint32_t cp, start = i;
    if (enc_ == TextEnc::Utf8) {
      uint8_t c = p[i];
      if (c < 0x80) { cp = c; i++; }
      else {
        int len = (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + len > n && !last) break;  // 半个字，留到下一段
        uint32_t j = i;
        cp = utf8Next((const char *)p, n, j);
        if (cp == '?' && c != '?') bad_++;
        i = j;
      }
    } else if (enc_ == TextEnc::Gbk) {
      uint8_t c = p[i];
      if (c < 0x80) { cp = c; i++; }
      else if (i + 1 >= n) {
        if (!last) break;
        cp = '?'; bad_++; i++;
      } else {
        cp = gbkLookup(c, p[i + 1]);
        if (cp) i += 2;
        else { cp = '?'; bad_++; i += (c == 0x80 || c == 0xFF) ? 1 : 2; }
      }
    } else {
      if (i + 1 >= n) { if (!last) break; i = n; continue; }
      bool le = enc_ == TextEnc::Utf16le;
      cp = le ? (p[i] | p[i + 1] << 8) : (p[i] << 8 | p[i + 1]);
      i += 2;
      if (cp >= 0xD800 && cp <= 0xDBFF) {  // 代理对
        if (i + 1 >= n) { if (!last) { i = start; break; } cp = '?'; }
        else {
          uint32_t lo = le ? (p[i] | p[i + 1] << 8) : (p[i] << 8 | p[i + 1]);
          if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
          else { cp = '?'; bad_++; }
        }
      }
    }
    if (!put(cp)) return start;
  }
  return i;
}
