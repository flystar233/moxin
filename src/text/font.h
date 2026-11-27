#pragma once
// TF 卡上的点阵字库（.efnt，由 tools/makefont.py 从 TTF 生成），文件名形如 /fonts/wenkai_18.efnt
// （"字体名_字号"）。选中的字库整个读进 PSRAM。另建一张 64K 的前进宽度表，分页时查宽度是 O(1)。
#include <Arduino.h>

#include <vector>

struct Glyph {
  uint8_t w, h;         // 点阵宽高
  int8_t xo, yo;        // xo：左边离笔位；yo：顶边在基线以上多少像素
  uint8_t adv;          // 前进宽度
  const uint8_t *bits;  // 每行按字节补齐，高位在左，1 = 黑
};

class EFont {
 public:
  bool load(const String &path);  // 调用方不用先挂载卡
  bool loadMem(const uint8_t *data, uint32_t size);  // 固件里嵌的字库（在 flash 里，不拷贝）
  // 以后 load() 都读进这块固定内存（开机分一次，不再释放），adv 是 64KB 的宽度表
  void useArena(uint8_t *buf, uint32_t cap, uint8_t *adv);
  void unload();
  bool loaded() const { return buf_ != nullptr; }
  const String &path() const { return path_; }
  int size() const { return size_; }
  int ascent() const { return asc_; }
  int descent() const { return desc_; }
  uint32_t glyphCount() const { return count_; }
  bool glyph(uint32_t cp, Glyph &g) const;  // 二分查找，没有这个字返回 false
  // 前进宽度，没有这个字返回 0
  int advance(uint32_t cp) const {
    if (cp < 0x10000) return adv_ ? adv_[cp] : 0;
    Glyph g;
    return glyph(cp, g) ? g.adv : 0;
  }

 private:
  bool parse(const uint8_t *buf, uint32_t size);
  const uint8_t *buf_ = nullptr;
  bool own_ = false;
  uint8_t *adv_ = nullptr;
  uint32_t len_ = 0, count_ = 0, idxOff_ = 0, bmpOff_ = 0;
  uint8_t size_ = 0, asc_ = 0, desc_ = 0;
  String path_;
  uint8_t *arena_ = nullptr, *arenaAdv_ = nullptr;
  uint32_t arenaCap_ = 0;
};

// 卡上的字库文件
struct FontFile {
  String path;
  String family;  // 文件名里 "_" 前面的部分
  int size;       // "_" 后面的数字
};
void fontScan();                             // 列出 /fonts/*.efnt
const std::vector<FontFile> &fontFiles();    // 按字体名、字号排好
std::vector<String> fontFamilies();
std::vector<int> fontSizes(const String &family);
String fontFindPath(const String &family, int size);  // 没有返回 ""
String fontFamilyLabel(const String &family);         // wenkai → 霞鹜文楷
