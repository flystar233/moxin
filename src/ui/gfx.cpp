#include "gfx.h"

#include <U8g2_for_Adafruit_GFX.h>

#include "config.h"
#include "reader/guoqinlun.h"  // EXTRA_GLYPHS：文泉驿里没有的几个字
#include "text/font.h"
#include "text/utf8.h"

namespace gfx {

static U8G2_FOR_ADAFRUIT_GFX u8g2;
static EFont s_body;
static uint8_t *s_wqyW = nullptr;  // 文泉驿 16 每个字的宽度，0xFF = 还没量
// 大屏（800×480）的界面字体：霞鹜新晰黑 GB2312 子集 26 / 20 号，嵌在固件里（platformio.ini 的 embed_files，见 data/FONTS.md）。
// 黑体笔画均匀，小字号比楷体清楚；正文仍用卡上的楷体等字库。
// 小屏还是用 U8g2 的文泉驿 16 / 12
static EFont s_ui, s_small, s_clock, s_num54, s_num34, s_num24, s_note;
static EFont s_uiHan, s_smallHan;  // 固件补字，不依赖 TF 卡或当前正文字体
static bool s_bigUi = false;

}  // namespace gfx
extern const uint8_t uiSmall_start[] asm("_binary_data_ui_small_efnt_start");
extern const uint8_t uiSmall_end[] asm("_binary_data_ui_small_efnt_end");
extern const uint8_t uiBig_start[] asm("_binary_data_ui_big_efnt_start");
extern const uint8_t uiBig_end[] asm("_binary_data_ui_big_efnt_end");
#define EMBED(n)                                                         extern const uint8_t n##_start[] asm("_binary_data_" #n "_efnt_start");   extern const uint8_t n##_end[] asm("_binary_data_" #n "_efnt_end");
EMBED(desk_clock)
EMBED(desk_num54)
EMBED(desk_num34)
EMBED(desk_num24)
EMBED(desk_note)
EMBED(ui_han_20)
EMBED(ui_han_26)
// U8g2_for_Adafruit_GFX.cpp 里的函数（头文件没声明）：字体里没有这个字返回 nullptr
const uint8_t *u8g2_font_get_glyph_data(u8g2_font_t *u8g2, uint16_t encoding);
namespace gfx {

static bool bigScreen() { return max(d().width(), d().height()) >= 600; }

static const EFont *efontFor(Font f) {
  if (!s_bigUi) return nullptr;
  if (f == Font::Ui) return &s_ui;
  if (f == Font::Small) return &s_small;
  const EFont *e = f == Font::Clock ? &s_clock : f == Font::Value ? &s_num54 : f == Font::Num ? &s_num34
                 : f == Font::NumSmall ? &s_num24 : f == Font::Note ? &s_note : nullptr;
  return e && e->loaded() ? e : nullptr;
}

static const uint8_t *u8font(Font f) {
  bool big = bigScreen();
  switch (f) {
    case Font::Small: return u8g2_font_wqy12_t_gb2312;
    case Font::Value: return big ? u8g2_font_logisoso46_tf : u8g2_font_logisoso28_tf;
    case Font::Clock: return big ? u8g2_font_logisoso92_tn : u8g2_font_logisoso62_tn;
    case Font::Num: return u8g2_font_logisoso28_tf;
    case Font::NumSmall: return u8g2_font_wqy12_t_gb2312;
    default: return u8g2_font_wqy16_t_gb2312;
  }
}

// U8g2_for_Adafruit_GFX 每次 setFont 都会把透明模式关掉、背景色默认 0（= EPD_WHITE），
// 所以每次都要重新 setFontMode(1)，否则反白的字会连字框一起涂成白块
static void use(Font f, uint16_t fg) {
  u8g2.setFont(u8font(f));
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(fg);
  u8g2.setBackgroundColor(fg == EPD_BLACK ? EPD_WHITE : EPD_BLACK);
}

void begin() {
  d().begin();
  u8g2.begin(d());
  u8g2.setFontDirection(0);
  use(Font::Ui, EPD_BLACK);
  s_wqyW = (uint8_t *)ps_malloc(65536);
  if (s_wqyW) memset(s_wqyW, 0xFF, 65536);
  // 正文字库的固定区：开机趁 PSRAM 还完整时分一次，以后换字号都读进这里（不会被碎片卡住）
  uint8_t *arena = (uint8_t *)ps_malloc(BODY_FONT_ARENA);
  uint8_t *adv = (uint8_t *)ps_malloc(65536);
  if (arena && adv) s_body.useArena(arena, BODY_FONT_ARENA, adv);
  else Serial.println("[字库] 分不到固定区，换字号时可能因为内存碎片失败");
  if (bigScreen()) {
    s_bigUi = s_ui.loadMem(uiBig_start, uiBig_end - uiBig_start) && s_small.loadMem(uiSmall_start, uiSmall_end - uiSmall_start);
    if (!s_bigUi) Serial.println("[界面] 固件里的界面字体坏了，用文泉驿");
    s_smallHan.loadMem(ui_han_20_start, ui_han_20_end - ui_han_20_start);
    s_uiHan.loadMem(ui_han_26_start, ui_han_26_end - ui_han_26_start);
    s_clock.loadMem(desk_clock_start, desk_clock_end - desk_clock_start);
    s_num54.loadMem(desk_num54_start, desk_num54_end - desk_num54_start);
    s_num34.loadMem(desk_num34_start, desk_num34_end - desk_num34_start);
    s_num24.loadMem(desk_num24_start, desk_num24_end - desk_num24_start);
    s_note.loadMem(desk_note_start, desk_note_end - desk_note_start);
  }
}

EpdPanel &d() { return panel(); }
int W() { return d().width(); }
int H() { return d().height(); }
int px(int v) { return (v * min(d().width(), d().height()) + 150) / 300; }
void setRotation(uint8_t rot) { d().setRotation(rot & 3); }
void clear() { d().fillScreen(EPD_WHITE); }

// 界面字体里没有全角空格（U+3000），换成两个半角空格
static const char *norm(const char *s, String &tmp) {
  if (!strstr(s, "\xE3\x80\x80")) return s;
  tmp = s;
  tmp.replace("\xE3\x80\x80", "  ");
  return tmp.c_str();
}

static void drawBits(const Glyph &g, int x, int y, uint16_t color) {
  const int stride = (g.w + 7) / 8;
  for (int r = 0; r < g.h; r++)
    for (int c = 0; c < g.w; c++)
      if (g.bits[r * stride + (c >> 3)] & (0x80 >> (c & 7))) d().drawPixel(x + g.xo + c, y - g.yo + r, color);
}

// 大屏界面：先用主字库，再找同字号的固件补字；罕见字仍可用已加载的正文补。
static int runE(const EFont &F, int x, int y, const char *s, uint16_t color, bool draw) {
  const EFont *fallback = &F == &s_small ? &s_smallHan : &F == &s_ui ? &s_uiHan : nullptr;
  uint32_t n = strlen(s), i = 0;
  int x0 = x;
  while (i < n) {
    uint32_t cp = utf8Next(s, n, i);
    Glyph g;
    if (F.glyph(cp, g) || (fallback && fallback->glyph(cp, g)) ||
        (s_body.loaded() && s_body.glyph(cp, g))) {
      if (draw) drawBits(g, x, y, color);
      x += g.adv;
    } else {
      int b = F.ascent();
      if (draw) d().drawRect(x + 1, y - b + 1, b - 2, b - 1, color);
      x += b;
    }
  }
  return x - x0;
}

// 界面字体（文泉驿，只有 GB2312 简体）里没有的字——繁体、生僻字、〇——用正文字库的字形补上；
// 正文字库也没有就画个小方框。当前字体要先 setFont
static bool uiHas(uint32_t cp) { return cp < 0x10000 && u8g2_font_get_glyph_data(&u8g2.u8g2, cp) != nullptr; }

static int boxW() { return u8g2.getFontAscent(); }

// 用 U8g2 画（draw = true）或只量宽度
static int run(int x, int y, const char *s, uint16_t color, bool draw) {
  uint32_t n = strlen(s), i = 0;
  // 先看有没有缺字，没有就整串交给 U8g2（快）
  bool missing = false;
  while (i < n) {
    uint32_t cp = utf8Next(s, n, i);
    if (cp >= 0x80 && !uiHas(cp)) { missing = true; break; }
  }
  if (!missing) return draw ? u8g2.drawUTF8(x, y, s) : u8g2.getUTF8Width(s);
  int x0 = x;
  String seg;
  auto flush = [&]() {
    if (!seg.length()) return;
    x += draw ? u8g2.drawUTF8(x, y, seg.c_str()) : u8g2.getUTF8Width(seg.c_str());
    seg = "";
  };
  i = 0;
  while (i < n) {
    uint32_t st = i;
    uint32_t cp = utf8Next(s, n, i);
    if (cp < 0x80 || uiHas(cp)) { seg.concat(s + st, i - st); continue; }
    flush();
    Glyph g;
    if (s_body.loaded() && s_body.glyph(cp, g)) {
      if (draw) drawBits(g, x, y, color);
      x += g.adv;
    } else {
      int b = boxW();
      if (draw) d().drawRect(x + 1, y - b + 1, b - 2, b - 1, color);
      x += b;
    }
  }
  flush();
  return x - x0;
}

int text(int x, int y, const char *s, Font f, uint16_t color) {
  String tmp;
  if (const EFont *F = efontFor(f)) return runE(*F, x, y, norm(s, tmp), color, true);
  use(f, color);
  return run(x, y, norm(s, tmp), color, true);
}

int textW(const char *s, Font f) {
  String tmp;
  if (const EFont *F = efontFor(f)) return runE(*F, 0, 0, norm(s, tmp), EPD_BLACK, false);
  u8g2.setFont(u8font(f));
  return run(0, 0, norm(s, tmp), EPD_BLACK, false);
}

void textCenter(int cx, int y, const String &s, Font f, uint16_t color) { text(cx - textW(s, f) / 2, y, s, f, color); }
void textRight(int xr, int y, const String &s, Font f, uint16_t color) { text(xr - textW(s, f), y, s, f, color); }

int ascent(Font f) {
  if (const EFont *F = efontFor(f)) return F->ascent();
  u8g2.setFont(u8font(f));
  return u8g2.getFontAscent();
}

int glyphTop(Font f, uint32_t cp) {
  Glyph g;
  if (const EFont *F = efontFor(f))
    if (F->glyph(cp, g)) return g.yo;
  return ascent(f);
}

int lineH(Font f) {
  if (const EFont *F = efontFor(f)) return F->ascent() + F->descent();
  u8g2.setFont(u8font(f));
  return u8g2.getFontAscent() - u8g2.getFontDescent();
}

String fit(const String &s, Font f, int maxW) {
  if (textW(s, f) <= maxW) return s;
  const int ell = textW("…", f);
  String out;
  uint32_t i = 0, n = s.length();
  while (i < n) {
    uint32_t j = i;
    utf8Next(s.c_str(), n, j);
    String t = s.substring(0, j);
    if (textW(t, f) + ell > maxW) break;
    out = t;
    i = j;
  }
  return out + "…";
}

void battery(int x, int y, int w, int h, int pct, bool charging) {
  EpdPanel &g = d();
  g.drawRect(x, y, w, h, EPD_BLACK);
  g.fillRect(x + w, y + h / 4, 2, h - 2 * (h / 4), EPD_BLACK);
  int fill = (w - 4) * constrain(pct, 0, 100) / 100;
  if (fill > 0) g.fillRect(x + 2, y + 2, fill, h - 4, EPD_BLACK);
  if (charging) {  // 闪电：在电池中间画一个白边的折线
    int cx = x + w / 2, cy = y + h / 2;
    for (int dx = -1; dx <= 1; dx++) {
      g.drawLine(cx + 2 + dx, y + 1, cx - 2 + dx, cy, EPD_WHITE);
      g.drawLine(cx - 2 + dx, cy, cx + 2 + dx, cy, EPD_WHITE);
      g.drawLine(cx + 2 + dx, cy, cx - 2 + dx, y + h - 2, EPD_WHITE);
    }
    g.drawLine(cx + 2, y + 1, cx - 2, cy, EPD_BLACK);
    g.drawLine(cx - 2, cy, cx + 2, cy, EPD_BLACK);
    g.drawLine(cx + 2, cy, cx - 2, y + h - 2, EPD_BLACK);
  }
}

void hline(int x, int y, int w) { d().drawFastHLine(x, y, w, EPD_BLACK); }

// ---------------------------------------------------------------------
// 正文
// ---------------------------------------------------------------------
static const ExtraGlyph *extraGlyph(uint32_t cp) {
  for (const ExtraGlyph &g : EXTRA_GLYPHS)
    if (g.cp == cp) return &g;
  return nullptr;
}

static int wqyWidth(uint32_t cp) {
  if (cp >= 0x2E80 && cp < 0xFFF0 && !(cp >= 0xFF61 && cp <= 0xFFDC)) return 16;  // 中日韩文字、全角标点
  if (cp == '\t') return 16;
  if (cp >= 0x10000) return 16;
  if (s_wqyW && s_wqyW[cp] != 0xFF) return s_wqyW[cp];
  int w;
  if (extraGlyph(cp)) w = 16;
  else {
    char b[5] = {0};
    utf8Encode(cp, b);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    w = u8g2.getUTF8Width(b);
    if (w <= 0) w = 8;
  }
  if (s_wqyW) s_wqyW[cp] = w;
  return w;
}

bool bodyLoad(const String &path) {
  if (path.length() == 0) { s_body.unload(); return true; }
  return s_body.load(path);
}
bool bodyIsEfnt() { return s_body.loaded(); }
String bodyPath() { return s_body.path(); }
int bodySize() { return s_body.loaded() ? s_body.size() : 16; }
int bodyAscent() {
  if (s_body.loaded()) return s_body.ascent();
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  return u8g2.getFontAscent();
}

int bodyAdvance(uint32_t cp) {
  if (s_body.loaded()) {
    int a = s_body.advance(cp);
    if (a) return a;
    if (cp == '\t') return s_body.size();
    return cp < 0x2E80 ? (s_body.size() + 1) / 2 : s_body.size();  // 字库里没有的字退回文泉驿画，宽度按字号占位
  }
  return wqyWidth(cp);
}

void bodyGlyph(int x, int baseline, uint32_t cp, bool bold) {
  EpdPanel &g = d();
  Glyph fg;
  if (s_body.loaded() && s_body.glyph(cp, fg)) {
    const int stride = (fg.w + 7) / 8;
    for (int r = 0; r < fg.h; r++) {
      const uint8_t *row = fg.bits + r * stride;
      for (int c = 0; c < fg.w; c++)
        if (row[c >> 3] & (0x80 >> (c & 7))) {
          g.drawPixel(x + fg.xo + c, baseline - fg.yo + r, EPD_BLACK);
          if (bold) g.drawPixel(x + fg.xo + c + 1, baseline - fg.yo + r, EPD_BLACK);
        }
    }
    return;
  }
  if (cp < 32 || isSpaceCp(cp)) return;
  if (const ExtraGlyph *e = extraGlyph(cp)) {
    int top = baseline - bodyAscent();
    for (int r = 0; r < 16; r++)
      for (int c = 0; c < 16; c++)
        if (e->rows[r] & (0x8000 >> c)) g.drawPixel(x + c, top + r, EPD_BLACK);
    return;
  }
  if (cp <= 0xFFFF) {
    use(Font::Ui, EPD_BLACK);
    u8g2.drawGlyph(x, baseline, (uint16_t)cp);
    if (bold) u8g2.drawGlyph(x + 1, baseline, (uint16_t)cp);
  }
}

// ---------------------------------------------------------------------
bool show(bool full) {
  uint32_t t0 = millis();
  bool ok = full ? d().refreshFull() : d().refreshPartial();
  Serial.printf("[屏] %s %lu ms%s\n", full ? "全刷" : "局刷", (unsigned long)(millis() - t0), ok ? "" : "（BUSY 超时）");
  return ok;
}

bool showClean() {
  uint32_t t0 = millis();
  bool ok = d().refreshClean();
  Serial.printf("[屏] 全刷（清残影）%lu ms%s\n", (unsigned long)(millis() - t0), ok ? "" : "（BUSY 超时）");
  return ok;
}

void screenshot(Stream &out) {
  const int w = W(), h = H();
  out.printf("P4\n%d %d\n", w, h);
  uint8_t row[(800 + 7) / 8];
  for (int y = 0; y < h; y++) {
    memset(row, 0, sizeof(row));
    for (int x = 0; x < w; x++)
      if (d().pixelBlack(x, y)) row[x >> 3] |= 0x80 >> (x & 7);
    out.write(row, (w + 7) / 8);
  }
}

}  // namespace gfx
