#pragma once
// 画图：界面文字（固件里的点阵字）、正文（卡上的 .efnt，没有就用文泉驿 16）、图标、刷屏。
// 所有界面都只通过这里画，换屏、换界面字体只改这里。
#include <Arduino.h>

#include "display/epd_panel.h"

enum class Font : uint8_t {
  Ui,        // 菜单、标题、提示（大屏：新晰黑 26）
  Small,     // 页眉页脚、说明（大屏：新晰黑 20）
  Value,     // 读数、跳转进度（大屏：Barlow 54，只有数字和 . : % - +）
  Clock,     // 桌面大时钟（大屏：Barlow 224，只有数字、冒号、减号）
  Num,       // 中号数字（大屏：Barlow 34）
  NumSmall,  // 小号数字（大屏：Barlow 24）
  Note,      // 桌面"还没对时"（大屏：新晰黑 60，只有这四个字）
};

namespace gfx {

void begin();
EpdPanel &d();
int W();
int H();
// 按屏幕大小缩放一个"在 300 像素宽的小屏上"定的尺寸（4.26 寸竖屏 480 宽 → ×1.6）
int px(int v);
void setRotation(uint8_t rot);
void clear();

// 文字：y 是基线；返回宽度
int text(int x, int y, const char *s, Font f = Font::Ui, uint16_t color = EPD_BLACK);
inline int text(int x, int y, const String &s, Font f = Font::Ui, uint16_t color = EPD_BLACK) {
  return text(x, y, s.c_str(), f, color);
}
int textW(const char *s, Font f = Font::Ui);
inline int textW(const String &s, Font f = Font::Ui) { return textW(s.c_str(), f); }
void textCenter(int cx, int y, const String &s, Font f = Font::Ui, uint16_t color = EPD_BLACK);
void textRight(int xr, int y, const String &s, Font f = Font::Ui, uint16_t color = EPD_BLACK);
int ascent(Font f);
int lineH(Font f);
int glyphTop(Font f, uint32_t cp);  // 这个字最高处在基线以上多少像素（数字字库按字形算，比 ascent 准）
String fit(const String &s, Font f, int maxW);  // 太长就截断加"…"

void battery(int x, int y, int w, int h, int pct, bool charging);
void hline(int x, int y, int w);

// 正文字体
bool bodyLoad(const String &path);  // "" = 固件里的文泉驿 16
bool bodyIsEfnt();
String bodyPath();
int bodySize();
int bodyAscent();
int bodyAdvance(uint32_t cp);
void bodyGlyph(int x, int baseline, uint32_t cp, bool bold);

// 刷屏；full = 全刷
bool show(bool full);
bool showClean();  // 标准全刷，清残影
// 截图：按当前旋转输出 PBM（P4）到串口
void screenshot(Stream &out);

}  // namespace gfx
