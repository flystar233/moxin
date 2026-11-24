#include "font.h"

#include <algorithm>

#include <esp_heap_caps.h>

#include "board/storage.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void EFont::unload() {
  if (buf_ && own_) free((void *)buf_);
  own_ = false;
  if (adv_ && adv_ != arenaAdv_) free(adv_);
  buf_ = adv_ = nullptr;
  len_ = count_ = 0;
  size_ = asc_ = desc_ = 0;
  path_ = "";
}

void EFont::useArena(uint8_t *buf, uint32_t cap, uint8_t *adv) {
  unload();
  arena_ = buf;
  arenaCap_ = cap;
  arenaAdv_ = adv;
}

// 有固定区（useArena）时：字库总是读进同一块内存，换字号不再申请 / 释放，不会被碎片卡住。
// 读新字库失败就把原来的读回来；都不行才变成"没有字库"（界面退回系统点阵）
bool EFont::load(const String &path) {
  if (path.length() == 0) { unload(); return true; }
  if (path == path_ && buf_) return true;
  uint32_t t0 = millis(), size = 0;
  String old = buf_ ? path_ : String();
  SdSession sd;
  if (!sd.ok) { Serial.println("[字库] 没有 TF 卡"); return false; }

  if (arena_) {
    File f = SD_MMC.open(path);
    if (!f) { Serial.printf("[字库] 找不到 %s\n", path.c_str()); return false; }
    uint32_t fileSize = f.size();
    f.close();
    if (fileSize > arenaCap_) {
      Serial.printf("[字库] %s 太大（%u KB，最多 %u KB）\n", path.c_str(), (unsigned)(fileSize / 1024),
                    (unsigned)(arenaCap_ / 1024));
      return false;
    }
    unload();
    if (sdReadInto(path, arena_, arenaCap_, size) && parse(arena_, size)) {
      path_ = path;
      Serial.printf("[字库] 载入 %s：%u 字，字号 %u，%.2f MB，%lu ms\n", path.c_str(), (unsigned)count_, size_,
                    size / 1048576.0, (unsigned long)(millis() - t0));
      return true;
    }
    Serial.printf("[字库] 读不了 %s\n", path.c_str());
    if (old.length() && sdReadInto(old, arena_, arenaCap_, size) && parse(arena_, size)) path_ = old;
    return false;
  }

  // 没有固定区（开机时没分到）：临时申请
  uint8_t *buf = sdReadAll(path, size, 6 * 1024 * 1024);
  if (!buf || !parse(buf, size)) {
    if (buf) free(buf);
    Serial.printf("[字库] 读不了 %s（PSRAM 最大块 %u KB）\n", path.c_str(),
                  (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    return false;
  }
  own_ = true;
  path_ = path;
  Serial.printf("[字库] 载入 %s：%u 字，字号 %u，%.2f MB，%lu ms\n", path.c_str(), (unsigned)count_, size_,
                size / 1048576.0, (unsigned long)(millis() - t0));
  return true;
}

bool EFont::loadMem(const uint8_t *data, uint32_t size) {
  unload();
  if (!parse(data, size)) return false;
  path_ = "<固件>";
  return true;
}

// 检查文件头、建前进宽度表（PSRAM 64KB）。成功后 buf 由这个对象引用
bool EFont::parse(const uint8_t *buf, uint32_t size) {
  bool ok = size >= 32 && memcmp(buf, "EFNT", 4) == 0 && buf[4] == 1;
  uint32_t n = ok ? rd32(buf + 12) : 0, io = ok ? rd32(buf + 16) : 0, bo = ok ? rd32(buf + 20) : 0;
  ok = ok && io + 16 * n <= size && bo <= size;
  if (!ok) return false;
  uint8_t *adv = arenaAdv_ ? arenaAdv_ : (uint8_t *)ps_calloc(65536, 1);
  if (!adv) return false;
  if (arenaAdv_) memset(adv, 0, 65536);
  if (buf_ && own_) free((void *)buf_);  // 临时申请模式：换下来的旧字库
  if (adv_ && adv_ != arenaAdv_ && adv_ != adv) free(adv_);
  own_ = false;
  for (uint32_t k = 0; k < n; k++) {
    const uint8_t *e = buf + io + 16 * k;
    uint32_t cp = rd32(e);
    if (cp < 0x10000) adv[cp] = e[12] ? e[12] : 1;
  }
  buf_ = buf;
  adv_ = adv;
  len_ = size;
  size_ = buf[5];
  asc_ = buf[6];
  desc_ = buf[7];
  count_ = n;
  idxOff_ = io;
  bmpOff_ = bo;
  return true;
}

bool EFont::glyph(uint32_t cp, Glyph &g) const {
  if (!buf_) return false;
  uint32_t lo = 0, hi = count_;
  const uint8_t *idx = buf_ + idxOff_;
  while (lo < hi) {
    uint32_t mid = (lo + hi) / 2;
    if (rd32(idx + 16 * mid) < cp) lo = mid + 1;
    else hi = mid;
  }
  if (lo >= count_) return false;
  const uint8_t *e = idx + 16 * lo;
  if (rd32(e) != cp) return false;
  uint32_t off = rd32(e + 4);
  g.w = e[8];
  g.h = e[9];
  g.xo = (int8_t)e[10];
  g.yo = (int8_t)e[11];
  g.adv = e[12];
  g.bits = buf_ + bmpOff_ + off;
  return bmpOff_ + off + ((g.w + 7) / 8) * g.h <= len_;
}

// ---------------------------------------------------------------------
static std::vector<FontFile> s_files;

void fontScan() {
  s_files.clear();
  SdSession sd;
  if (!sd.ok) return;
  for (auto &e : sdList("/fonts")) {
    String lower = e.name;
    lower.toLowerCase();
    if (e.dir || !lower.endsWith(".efnt")) continue;
    String base = e.name.substring(0, e.name.length() - 5);
    int u = base.lastIndexOf('_');
    FontFile f{e.path, u > 0 ? base.substring(0, u) : base, u > 0 ? (int)base.substring(u + 1).toInt() : 0};
    if (f.size > 0) s_files.push_back(f);
  }
  std::sort(s_files.begin(), s_files.end(), [](const FontFile &a, const FontFile &b) {
    return a.family != b.family ? a.family < b.family : a.size < b.size;
  });
  Serial.printf("[字库] 卡上有 %u 个\n", (unsigned)s_files.size());
}

const std::vector<FontFile> &fontFiles() { return s_files; }

std::vector<String> fontFamilies() {
  std::vector<String> out;
  for (auto &f : s_files)
    if (std::find(out.begin(), out.end(), f.family) == out.end()) out.push_back(f.family);
  return out;
}

std::vector<int> fontSizes(const String &family) {
  std::vector<int> out;
  for (auto &f : s_files)
    if (f.family == family) out.push_back(f.size);
  return out;
}

String fontFindPath(const String &family, int size) {
  for (auto &f : s_files)
    if (f.family == family && f.size == size) return f.path;
  return "";
}

String fontFamilyLabel(const String &family) {
  static const struct { const char *key, *label; } K[] = {
      {"wenkai", "霞鹜文楷"}, {"wenkaimono", "霞鹜文楷等宽"}, {"xinhei", "霞鹜新晰黑"}, {"noto", "思源黑体"},
      {"sourcehan", "思源黑体"}, {"songti", "宋体"}, {"fangsong", "仿宋"}, {"heiti", "黑体"}, {"kaiti", "楷体"},
  };
  String l = family;
  l.toLowerCase();
  for (auto &k : K)
    if (l == k.key) return k.label;
  return family;
}
