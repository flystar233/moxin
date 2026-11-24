#include "ui.h"

#include <U8g2_for_Adafruit_GFX.h>
#include <map>

#include "config.h"
#include "epd_ssd1619a.h"
#include "font.h"
#include "guoqinlun.h"  // EXTRA_GLYPHS：字库里没有的几个字

static EpdSsd1619a epd(EPD_PIN_SCK, EPD_PIN_MOSI, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY, EPD_SPI_HZ);
static U8G2_FOR_ADAFRUIT_GFX u8g2;

#define FONT_BODY  u8g2_font_wqy16_t_gb2312
#define FONT_VALUE u8g2_font_logisoso28_tf
#define FONT_SMALL u8g2_font_helvR12_tf

static const int M = 12;           // 左右边距
static const int LINE_H = 24;      // 正文行高
static const int HEAD_H = 30;      // 阅读页眉
static const int FOOT_H = 22;      // 阅读页脚
static const int CJK_W = 16;
static const int INDENT = 2 * CJK_W;

static uint8_t s_asciiW[128];
static std::map<uint32_t, uint8_t> s_wCache;
static const char *WEEK[] = {"日", "一", "二", "三", "四", "五", "六"};

// ---------------------------------------------------------------------
static const ExtraGlyph *extraGlyph(uint32_t cp) {
  for (const ExtraGlyph &g : EXTRA_GLYPHS)
    if (g.cp == cp) return &g;
  return nullptr;
}

// 正文字号相关：卡上有 .efnt 字库就按它，没有就是固件里的文泉驿 16
static int bodySize() { return fontLoaded() ? fontSize() : CJK_W; }
static int lineH() { return fontLoaded() ? (fontSize() * 29 + 10) / 20 : LINE_H; }  // 行距约 1.45 倍字号
static int indentW() { return 2 * bodySize(); }

static int bodyWidth(uint32_t cp) {
  if (fontLoaded()) {
    Glyph g;
    if (fontGlyph(cp, g)) return g.adv;
    if (cp == '\t') return fontSize();
    return cp < 128 ? fontSize() / 2 : fontSize();  // 字库里没有的字退回文泉驿画，宽度按字号占位
  }
  if (cp < 128) return s_asciiW[cp];
  if (cp >= 0x2E80) return CJK_W;  // 中日韩文字、全角标点
  if (extraGlyph(cp)) return CJK_W;
  auto it = s_wCache.find(cp);
  if (it != s_wCache.end()) return it->second;
  char b[5] = {0};
  if (cp < 0x800) { b[0] = 0xC0 | (cp >> 6); b[1] = 0x80 | (cp & 0x3F); }
  else { b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F); b[2] = 0x80 | (cp & 0x3F); }
  u8g2.setFont(FONT_BODY);
  int w = u8g2.getUTF8Width(b);
  if (w <= 0) w = CJK_W / 2;
  s_wCache[cp] = w;
  return w;
}

// 注意：U8g2_for_Adafruit_GFX 每次换字体都会把透明模式关掉，背景色默认 0（= EPD_WHITE），
// 所以每次 setFont 之后都要重新 setFontMode(1)，否则反白的字会连字框一起涂成白块
static void useFont(const uint8_t *font, uint16_t fg) {
  u8g2.setFont(font);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(fg);
  u8g2.setBackgroundColor(fg == EPD_BLACK ? EPD_WHITE : EPD_BLACK);
}

static int text(int x, int y, const char *s, const uint8_t *font = FONT_BODY, uint16_t fg = EPD_BLACK) {
  useFont(font, fg);
  return u8g2.drawUTF8(x, y, s);
}

static int textW(const char *s, const uint8_t *font = FONT_BODY) {
  u8g2.setFont(font);
  return u8g2.getUTF8Width(s);
}

static void textCenter(int y, const char *s, const uint8_t *font = FONT_BODY) {
  text((epd.width() - textW(s, font)) / 2, y, s, font);
}

// 电池图标：w×h 外框 + 正极小块，按百分比填充
static void batteryIcon(int x, int y, int w, int h, int pct) {
  epd.drawRect(x, y, w, h, EPD_BLACK);
  epd.fillRect(x + w, y + h / 4, 2, h / 2, EPD_BLACK);
  int fill = (w - 4) * constrain(pct, 0, 100) / 100;
  if (fill > 0) epd.fillRect(x + 2, y + 2, fill, h - 4, EPD_BLACK);
}

// 右上角：电量（插着 USB 时加"充电"）
static void drawBatteryCorner(const Battery &bat, bool usb, int baseline) {
  const int W = epd.width();
  char buf[24];
  if (bat.ok) snprintf(buf, sizeof(buf), "%d%%", bat.soc);
  else snprintf(buf, sizeof(buf), "--");
  int tw = textW(buf, FONT_SMALL);
  int x = W - M - 26 - 6 - tw;
  text(x, baseline, buf, FONT_SMALL);
  batteryIcon(W - M - 26, baseline - 12, 24, 12, bat.ok ? bat.soc : 0);
  if (usb) {
    int cw = textW("充电");
    text(x - cw - 6, baseline, "充电");
  }
}

// ---------------------------------------------------------------------
void uiBegin() {
  epd.begin();
  u8g2.begin(epd);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);
  u8g2.setFont(FONT_BODY);
  for (int c = 0; c < 128; c++) {
    char b[2] = {(char)c, 0};
    s_asciiW[c] = c < 32 ? 0 : u8g2.getUTF8Width(b);
  }
}

void uiSetRotation(uint8_t rot) { epd.setRotation(rot & 3); }
void uiSetPartialFrames(uint8_t black, uint8_t whiteExtra) { epd.setPartialFrames(black, whiteExtra); }

bool uiShow(bool full) {
  uint32_t t0 = millis();
  bool ok = full ? epd.refresh() : epd.refreshPartial();
  Serial.printf("[屏] %s %lu ms%s\n", full ? "全刷" : "局刷", (unsigned long)(millis() - t0), ok ? "" : "（BUSY 超时）");
  return ok;
}

void uiResetPartialBaseline() {}

// ---------------------------------------------------------------------
// 桌面（横放）
// ---------------------------------------------------------------------
void uiDrawDesk(const DeskData &d) {
  const int W = epd.width(), H = epd.height();
  const bool big = W >= 600;
  char buf[48];
  epd.fillScreen(EPD_WHITE);

  // 页眉：日期、星期、电量
  snprintf(buf, sizeof(buf), "%d月%d日 星期%s", d.now.month, d.now.day, WEEK[d.now.weekday % 7]);
  text(M, 22, buf);
  drawBatteryCorner(d.bat, d.usb, 22);
  epd.drawFastHLine(M, 30, W - 2 * M, EPD_BLACK);

  // 大字时间
  const uint8_t *clockFont = big ? u8g2_font_logisoso92_tn : u8g2_font_logisoso62_tn;
  snprintf(buf, sizeof(buf), "%02d:%02d", d.now.hour, d.now.minute);
  u8g2.setFont(clockFont);
  int asc = u8g2.getFontAscent();
  int clockY = 30 + 18 + asc;
  textCenter(clockY, buf, clockFont);
  if (!d.timeOk) {
    const char *s = "未对时";
    text((W + textW(buf, clockFont)) / 2 + 6, clockY, s);
  }

  // 环境：CO2、温度、湿度
  int y0 = H - 92;
  epd.drawFastHLine(M, y0 - 6, W - 2 * M, EPD_BLACK);
  const char *labels[3] = {"二氧化碳", "温度", "湿度"};
  const char *units[3] = {"ppm", "\xC2\xB0" "C", "%"};
  for (int i = 0; i < 3; i++) {
    int cx = M + (W - 2 * M) * i / 3;
    text(cx + 4, y0 + 16, labels[i]);
    bool ok = d.co2Present && d.clim.ok;
    if (!ok) snprintf(buf, sizeof(buf), "--");
    else if (i == 0) snprintf(buf, sizeof(buf), "%d", d.clim.co2);
    else if (i == 1) snprintf(buf, sizeof(buf), "%.1f", d.clim.temp);
    else snprintf(buf, sizeof(buf), "%.0f", d.clim.rh);
    int vw = text(cx + 4, y0 + 56, buf, FONT_VALUE);
    text(cx + 4 + vw + 4, y0 + 56, units[i], FONT_SMALL);
  }
  if (d.co2Present && d.clim.ok && d.clim.co2 >= 1000) text(M + 4, y0 + 74, "该开窗了");

  // 页脚
  epd.drawFastHLine(M, H - 24, W - 2 * M, EPD_BLACK);
  text(M, H - 6, d.co2Present ? "电脑：未连接" : "电脑：未连接　　CO2 传感器未安装");
}

// ---------------------------------------------------------------------
// 阅读（竖拿）
// ---------------------------------------------------------------------
Layout uiReadingLayout() {
  Layout L;
  L.maxWidth = epd.width() - 2 * M;
  L.linesPerPage = (epd.height() - HEAD_H - FOOT_H - 8) / lineH();
  L.indentWidth = indentW();
  L.width = bodyWidth;
  return L;
}

void uiDrawReading(const DateTime &now, const Battery &bat) {
  const int W = epd.width(), H = epd.height();
  char buf[40];
  epd.fillScreen(EPD_WHITE);

  // 页眉：书名 …… 页码
  snprintf(buf, sizeof(buf), "%d / %d", bookPage() + 1, bookPageCount());
  int pw = textW(buf, FONT_SMALL);
  text(W - M - pw, 21, buf, FONT_SMALL);
  String title = bookTitle();
  // 书名太长就截断
  while (title.length() > 3 && textW(title.c_str()) > W - 2 * M - pw - 12) {
    uint32_t i = 0, last = 0, n = title.length();
    while (i < n) { last = i; utf8Next(title.c_str(), n, i); }
    title = title.substring(0, last);
  }
  text(M, 21, title.c_str());
  epd.drawFastHLine(M, HEAD_H - 2, W - 2 * M, EPD_BLACK);

  // 正文
  Layout L = uiReadingLayout();
  static TextLine lines[64];
  int n = bookPageLines(bookPage(), lines, min(L.linesPerPage, 64));
  const char *t = bookText();
  const uint32_t len = bookSize();
  useFont(FONT_BODY, EPD_BLACK);
  const bool efnt = fontLoaded();
  const int asc = efnt ? fontAscent() : u8g2.getFontAscent();
  const int lh = lineH();
  for (int k = 0; k < n; k++) {
    int x = M + (lines[k].indent ? indentW() : 0);
    int y = HEAD_H + 4 + asc + k * lh;   // 基线
    uint32_t i = lines[k].start, end = lines[k].start + lines[k].len;
    while (i < end && i < len) {
      uint32_t cp = utf8Next(t, len, i);
      int w = bodyWidth(cp);
      Glyph fg;
      if (efnt && fontGlyph(cp, fg)) {
        const int stride = (fg.w + 7) / 8;
        for (int r = 0; r < fg.h; r++) {
          const uint8_t *row = fg.bits + r * stride;
          for (int c = 0; c < fg.w; c++)
            if (row[c >> 3] & (0x80 >> (c & 7))) epd.drawPixel(x + fg.xo + c, y - fg.yo + r, EPD_BLACK);
        }
      } else if (const ExtraGlyph *g = extraGlyph(cp)) {
        for (int r = 0; r < 16; r++)
          for (int c = 0; c < 16; c++)
            if (g->rows[r] & (0x8000 >> c)) epd.drawPixel(x + c, y - asc + r, EPD_BLACK);
      } else if (cp <= 0xFFFF && cp >= 32) {
        u8g2.drawGlyph(x, y, (uint16_t)cp);
      }
      x += w;
    }
  }

  // 页脚：进度条、时间、电量
  int fy = H - FOOT_H;
  int barW = W - 2 * M - 110;
  epd.drawRect(M, fy + 8, barW, 6, EPD_BLACK);
  int pc = bookPageCount() > 1 ? (barW - 4) * bookPage() / (bookPageCount() - 1) : barW - 4;
  epd.fillRect(M + 2, fy + 10, pc, 2, EPD_BLACK);
  snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
  text(M + barW + 8, fy + 16, buf, FONT_SMALL);
  batteryIcon(W - M - 26, fy + 5, 24, 12, bat.ok ? bat.soc : 0);
}

// ---------------------------------------------------------------------
void uiDrawMenu(const char *title, const std::vector<MenuItem> &items, int cursor) {
  const int W = epd.width(), H = epd.height();
  epd.fillScreen(EPD_WHITE);
  text(M, 24, title);
  epd.drawFastHLine(M, 32, W - 2 * M, EPD_BLACK);
  const int itemH = 30, top = 40;
  int visible = (H - top - 30) / itemH;
  int first = cursor >= visible ? cursor - visible + 1 : 0;
  for (int i = first; i < (int)items.size() && i < first + visible; i++) {
    int y = top + (i - first) * itemH;
    bool sel = i == cursor;
    if (sel) epd.fillRect(M, y, W - 2 * M, itemH - 4, EPD_BLACK);
    text(M + 8, y + 20, items[i].text.c_str(), FONT_BODY, sel ? EPD_WHITE : EPD_BLACK);
  }
  epd.drawFastHLine(M, H - 26, W - 2 * M, EPD_BLACK);
  text(M, H - 8, "翻页键选择  菜单键确定  长按菜单返回");
}

void uiDrawMessage(const char *line1, const char *line2) {
  epd.fillScreen(EPD_WHITE);
  int H = epd.height();
  textCenter(H / 2 - (line2 ? 6 : -8), line1);
  if (line2) textCenter(H / 2 + 24, line2);
}

void uiDrawShutdown(const Battery &bat, const DateTime &now, bool timeOk, const String &book, int page, int pages) {
  const int W = epd.width(), H = epd.height();
  char buf[80];
  epd.fillScreen(EPD_WHITE);
  epd.drawRect(8, 8, W - 16, H - 16, EPD_BLACK);
  epd.drawRect(11, 11, W - 22, H - 22, EPD_BLACK);

  // 正中：已关机 + 电源键图标
  int cy = H / 2 - 30;
  int r = 14, ix = W / 2, iy = cy - 30;
  epd.drawCircle(ix, iy, r, EPD_BLACK);
  epd.drawCircle(ix, iy, r - 1, EPD_BLACK);
  epd.fillRect(ix - 4, iy - r - 2, 8, r, EPD_WHITE);
  epd.fillRect(ix - 1, iy - r - 2, 3, r + 2, EPD_BLACK);
  textCenter(cy + 10, "已关机");
  textCenter(cy + 36, "长按电源键 3 秒开机");

  // 读到哪
  if (pages > 0 && book.length()) {
    snprintf(buf, sizeof(buf), "《%s》 第 %d / %d 页", book.c_str(), page + 1, pages);
    if (textW(buf) > W - 40) snprintf(buf, sizeof(buf), "读到第 %d / %d 页", page + 1, pages);
    textCenter(H - 66, buf);
  }
  // 关机时间、电量
  if (timeOk) snprintf(buf, sizeof(buf), "%d月%d日 %02d:%02d 关机", now.month, now.day, now.hour, now.minute);
  else snprintf(buf, sizeof(buf), "关机");
  if (bat.ok) {
    size_t l = strlen(buf);
    snprintf(buf + l, sizeof(buf) - l, "  电量 %d%%", bat.soc);
  }
  textCenter(H - 38, buf);
}

void uiBanner(const char *msg, bool screenUnknown) {
  const int W = epd.width(), H = epd.height();
  if (screenUnknown) epd.fillScreen(EPD_WHITE);
  int bw = textW(msg) + 48, bh = 52;
  if (bw > W - 20) bw = W - 20;
  int bx = (W - bw) / 2, by = (H - bh) / 2;
  epd.fillRect(bx, by, bw, bh, EPD_WHITE);
  epd.drawRect(bx, by, bw, bh, EPD_BLACK);
  epd.drawRect(bx + 2, by + 2, bw - 4, bh - 4, EPD_BLACK);
  text((W - textW(msg)) / 2, by + bh / 2 + 6, msg);
  if (screenUnknown) epd.forcePartialRect(bx, by, bw, bh);
  uiShow(false);
}
