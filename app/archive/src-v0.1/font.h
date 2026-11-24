#pragma once
// TF 卡上的点阵字库（.efnt，由 tools/makefont.py 从 TTF 生成）。选中的字号整个读进 PSRAM，正文用它显示。
// 卡里没有字库时 fontLoaded() 为 false，界面退回固件里的文泉驿 16 点阵。
#include <Arduino.h>
#include <vector>

struct Glyph {
  uint8_t w, h;      // 点阵宽高
  int8_t xo, yo;     // xo：左边离笔位；yo：顶边在基线以上多少像素
  uint8_t adv;       // 前进宽度
  const uint8_t *bits;  // 每行按字节补齐，高位在左，1 = 黑
};

void fontScan();                       // 列出 /fonts/*.efnt（打开菜单时也会调一次）
const std::vector<String> &fontFiles();
bool fontLoad(const String &path);     // 读进 PSRAM；path 为空 = 不用卡上的字库
bool fontLoaded();
String fontPath();
int fontSize();                        // 字号（像素）
int fontAscent();
int fontDescent();
bool fontGlyph(uint32_t cp, Glyph &g); // 二分查找，字库里没有返回 false
