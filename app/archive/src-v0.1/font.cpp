#include "font.h"

#include <FS.h>
#include <SD_MMC.h>

#include <algorithm>

#include "book.h"

static uint8_t *s_buf = nullptr;    // 整个字库文件（PSRAM）
static uint32_t s_len = 0;
static uint32_t s_count = 0, s_idxOff = 0, s_bmpOff = 0;
static uint8_t s_size = 0, s_asc = 0, s_desc = 0;
static String s_path;
static std::vector<String> s_files;

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void fontScan() {
  s_files.clear();
  if (!storageMount()) return;
  File d = SD_MMC.open("/fonts");
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      String n = f.name();
      String lower = n;
      lower.toLowerCase();
      if (!f.isDirectory() && lower.endsWith(".efnt")) s_files.push_back(String("/fonts/") + n);
    }
  }
  storageUnmount();
  // 按字号从小到大排：文件名形如 wenkai_24.efnt
  std::sort(s_files.begin(), s_files.end(), [](const String &a, const String &b) {
    auto num = [](const String &s) { int u = s.lastIndexOf('_'); return u >= 0 ? s.substring(u + 1).toInt() : 0; };
    return num(a) != num(b) ? num(a) < num(b) : a < b;
  });
  Serial.printf("[字库] 卡上有 %u 个\n", (unsigned)s_files.size());
}

const std::vector<String> &fontFiles() { return s_files; }

static void unload() {
  if (s_buf) free(s_buf);
  s_buf = nullptr;
  s_len = s_count = 0;
  s_size = 0;
  s_path = "";
}

bool fontLoad(const String &path) {
  if (path.length() == 0) { unload(); return true; }
  if (path == s_path && s_buf) return true;
  if (!storageMount()) { Serial.println("[字库] 没有 TF 卡"); return false; }
  File f = SD_MMC.open(path);
  if (!f) { storageUnmount(); Serial.printf("[字库] 打不开 %s\n", path.c_str()); return false; }
  uint32_t size = f.size();
  uint8_t *buf = (uint8_t *)ps_malloc(size);
  if (!buf) { f.close(); storageUnmount(); Serial.println("[字库] PSRAM 不够"); return false; }
  uint32_t t0 = millis(), got = 0;
  while (got < size) {
    int r = f.read(buf + got, min<uint32_t>(32768, size - got));
    if (r <= 0) break;
    got += r;
  }
  f.close();
  storageUnmount();
  bool ok = got == size && size >= 32 && memcmp(buf, "EFNT", 4) == 0 && buf[4] == 1;
  uint32_t n = ok ? rd32(buf + 12) : 0, io = ok ? rd32(buf + 16) : 0, bo = ok ? rd32(buf + 20) : 0;
  ok = ok && io + 16 * n <= size && bo <= size;
  if (!ok) { free(buf); Serial.printf("[字库] %s 格式不对\n", path.c_str()); return false; }
  unload();
  s_buf = buf;
  s_len = size;
  s_size = buf[5];
  s_asc = buf[6];
  s_desc = buf[7];
  s_count = n;
  s_idxOff = io;
  s_bmpOff = bo;
  s_path = path;
  Serial.printf("[字库] 载入 %s：%u 字，字号 %u，%.2f MB，%lu ms\n", path.c_str(), (unsigned)n, s_size,
                size / 1048576.0, (unsigned long)(millis() - t0));
  return true;
}

bool fontLoaded() { return s_buf != nullptr; }
String fontPath() { return s_path; }
int fontSize() { return s_size; }
int fontAscent() { return s_asc; }
int fontDescent() { return s_desc; }

bool fontGlyph(uint32_t cp, Glyph &g) {
  if (!s_buf) return false;
  uint32_t lo = 0, hi = s_count;
  const uint8_t *idx = s_buf + s_idxOff;
  while (lo < hi) {
    uint32_t mid = (lo + hi) / 2;
    uint32_t c = rd32(idx + 16 * mid);
    if (c < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo >= s_count) return false;
  const uint8_t *e = idx + 16 * lo;
  if (rd32(e) != cp) return false;
  uint32_t off = rd32(e + 4);
  g.w = e[8];
  g.h = e[9];
  g.xo = (int8_t)e[10];
  g.yo = (int8_t)e[11];
  g.adv = e[12];
  g.bits = s_buf + s_bmpOff + off;
  return s_bmpOff + off + ((g.w + 7) / 8) * g.h <= s_len;
}
