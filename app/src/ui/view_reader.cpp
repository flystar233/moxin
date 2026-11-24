// 阅读界面（竖拿）：页眉章节名，正文，页脚页码、时间、电量
#include <algorithm>

#include "core/settings.h"
#include "gfx.h"
#include "reader/book.h"
#include "reader/library.h"
#include "text/font.h"
#include "text/utf8.h"
#include "views.h"
#include "voice/tts.h"

// 阅读版面的几何参数（竖屏）
struct ReadGeom {
  int W, H;
  int mx;        // 左右边距
  int headH;     // 页眉高（含分隔线）
  int top;       // 正文第一行顶
  int lh;        // 行高
  int lines;     // 每页行数
};

static ReadGeom geom() {
  ReadGeom g;
  int a = gfx::W(), b = gfx::H();
  g.W = min(a, b);  // 阅读总是竖屏；不依赖当前旋转，切界面时就能先分好页
  g.H = max(a, b);
  static const int MARGIN[] = {6, 12, 20};
  g.mx = MARGIN[settings().margin % 3] * g.W / 300;
  g.headH = gfx::lineH(Font::Small) + gfx::px(9);
  int footH = gfx::lineH(Font::Small) + gfx::px(8);
  g.top = g.headH + gfx::px(8);
  int size = gfx::bodySize();
  g.lh = (size * lineSpacingX10(settings().lineSpacing) + 5) / 10;
  int bottom = g.H - footH - gfx::px(6);
  // 最后一行连同下伸部分（约字号的 1/4）都要在 bottom 以内，离页脚留出空
  g.lines = max(1, (bottom - g.top - size - size / 4) / g.lh + 1);
  if (g.lines > 64) g.lines = 64;
  return g;
}

namespace reader {

static bool s_fontApplied = false;

bool applyFont() {
  Settings &s = settings();
  static bool scanned = false;
  if (!scanned) { fontScan(); scanned = true; }
  String path;
  if (s.fontFamily.length()) {
    auto fams = fontFamilies();
    if (std::find(fams.begin(), fams.end(), s.fontFamily) == fams.end()) s.fontFamily = fams.empty() ? "" : fams.front();
  }
  if (s.fontFamily.length()) {
    // 没有这个字号就用最接近的
    auto sizes = fontSizes(s.fontFamily);
    int best = sizes.front();
    for (int z : sizes)
      if (abs(z - s.fontSize) < abs(best - s.fontSize)) best = z;
    s.fontSize = best;
    path = fontFindPath(s.fontFamily, best);
  }
  s_fontApplied = true;
  if (gfx::bodyLoad(path)) return true;
  // 换不成（多半是 PSRAM 不够：大书 + 大字号）：正文还用原来那个字库，设置也改回去，不要悄悄退回系统点阵
  if (gfx::bodyIsEfnt()) {
    for (auto &f : fontFiles())
      if (f.path == gfx::bodyPath()) { s.fontFamily = f.family; s.fontSize = f.size; }
  } else if (!path.length()) {
    s.fontFamily = "";
  }
  return false;
}

Layout layout() {
  ReadGeom g = geom();
  Layout L;
  L.maxWidth = g.W - 2 * g.mx;
  L.linesPerPage = g.lines;
  L.indentWidth = 2 * gfx::bodySize();
  L.adv = gfx::bodyAdvance;
  // 字库指纹：字号一样、字体不同，字宽也不同，分页要重排（分页表缓存也按它区分）
  String fp = gfx::bodyPath() + "#" + String(gfx::bodySize());
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < fp.length(); i++) h = (h ^ (uint8_t)fp[i]) * 16777619u;
  L.fontId = h;
  return L;
}

// 导入、排版要几秒：在当前画面中间报进度
static void showProgress(const char *what, int pct) {
  char b[48];
  snprintf(b, sizeof(b), "%s %d%%", what, pct);
  app::busy(b);
}

void ensureLayout(bool showBusy) {
  if (!bookIsOpen()) return;
  Layout L = layout();
  if (bookPaginatedWith(L)) return;
  uint32_t keep = bookPageCount() ? bookOffset() : progressLoad(bookPath());
  // 大书分页要几秒（排过的版面有缓存，很快），先给个提示
  bookSetProgress(showBusy ? showProgress : nullptr);
  if (showBusy && bookPageCount() && bookSize() > 600 * 1024) app::busy("正在排版…");
  bookPaginate(L, keep);
  bookSetProgress(showProgress);
}

bool ensureOpen() {
  bookSetProgress(showProgress);
  if (!s_fontApplied) applyFont();
  if (bookIsOpen()) return true;
  String last = libraryLastPath();
  if (!bookOpen(last)) {
    bookOpen(BUILTIN_PATH);
  }
  ensureLayout();
  return true;
}

bool openBook(const String &path) {
  if (tts::active()) tts::stop();  // 换书就别念旧的了
  bookSetProgress(showProgress);
  if (!s_fontApplied) applyFont();
  if (!bookOpen(path)) return false;
  librarySetLast(path);
  ensureLayout();
  return true;
}

void saveProgress() {
  if (!bookIsOpen() || !bookPageCount()) return;
  progressSave(bookPath(), bookOffset(), bookPermille());
  // 章节变了才记（桌面上显示"在读 第几章"），NVS 少写
  static int lastCh = -2;
  static String lastPath;
  int ci = bookChapterAt(bookOffset());
  if (ci != lastCh || lastPath != bookPath()) {
    lastCh = ci;
    lastPath = bookPath();
    librarySetChapter(ci >= 0 ? bookChapterTitle(ci) : String(""));
  }
}

void gotoOffset(uint32_t off) {
  if (tts::active()) tts::stop();  // 手动跳转就重新来（TOC、跳转进度）
  ensureLayout();
  bookSetPage(bookPageOf(off));
  saveProgress();
  app::invalidate(Refresh::Partial);
}

void turn(int d) {
  int before = bookPage();
  bookSetPage(before + d);
  if (bookPage() == before) {
    app::toast(d > 0 ? "已经是最后一页" : "已经是第一页", "", 1000);
    return;
  }
  saveProgress();
  app::invalidate(Refresh::Partial);
}

static void nextChapter() {
  const auto &ch = bookChapters();
  int i = bookChapterAt(bookOffset());
  if (i + 1 < (int)ch.size()) gotoOffset(ch[i + 1].off);
  else app::toast("已经是最后一章", "", 1000);
}

// 不在本章开头：回到本章开头；已经在开头：上一章
static void prevChapter() {
  const auto &ch = bookChapters();
  int i = bookChapterAt(bookOffset());
  if (i < 0) { gotoOffset(0); return; }
  if (bookPage() > bookPageOf(ch[i].off)) gotoOffset(ch[i].off);
  else if (i > 0) gotoOffset(ch[i - 1].off);
  else gotoOffset(0);
}

}  // namespace reader

// =====================================================================
class ReaderView : public View {
 public:
  ReaderView() { reader::ensureOpen(); }
  const char *name() const override { return "reader"; }
  bool isRoot() const override { return true; }
  uint32_t idleMs() const override { return IDLE_READ_MS; }
  int fullEvery() const override { return settings().fullEvery; }
  void onShow() override { reader::ensureLayout(); }
  void onTap() override {
    if (tts::active()) tts::togglePause();  // 朗读时双击后背 = 暂停 / 继续
    else reader::turn(+1);
  }
  void draw() override;
  void onKey(Key k, Press p) override;

 private:
  uint32_t lastShort_ = 0;
};

View *newReaderView() { return new ReaderView(); }

void ReaderView::onKey(Key k, Press p) {
  // 正在朗读：短按上一页/下一页 = 上一句/下一句，长按 = 上一章/下一章（接着念）；
  // 菜单键 = 菜单，长按菜单键 = 停止朗读
  if (tts::active()) {
    if (k == Key::Menu) {
      if (p == Press::Short) app::push(newReaderMenu());
      else if (p == Press::Long) tts::stop();
      return;
    }
    if (p == Press::Repeat) return;  // 一直按着不要连续跳句，每次都要重新请求
    int dir = k == Key::Next ? 1 : -1;
    if (p == Press::Long) {
      const auto &ch = bookChapters();
      int ci = bookChapterAt(tts::offset());
      if (dir > 0) {
        if (ci + 1 < (int)ch.size()) tts::jumpTo(ch[ci + 1].off);
      } else if (ci >= 0) {
        tts::jumpTo(ch[ci].off);
      }
    } else {
      if (dir > 0) {
        if (!tts::nextSentence()) app::toast("已经是最后一句", "", 800);
      } else {
        if (!tts::prevSentence()) app::toast("已经是第一句", "", 800);
      }
    }
    return;
  }
  if (k == Key::Menu) {
    if (p == Press::Short) app::push(newReaderMenu());
    else if (p == Press::Long) app::push(bookChapters().empty() ? newJumpView() : newTocView());
    return;
  }
  int dir = k == Key::Next ? 1 : -1;
  if (p == Press::Short) {
    uint32_t now = millis();
    if (now - lastShort_ < KEY_MIN_GAP_MS) return;  // 开关抖动造成的连按丢掉（文档）
    lastShort_ = now;
    reader::turn(dir);
    return;
  }
  // 长按：有章节就跳章（下一章 / 本章开头），没有章节就连续翻页
  if (!bookChapters().empty()) {
    if (dir > 0) reader::nextChapter();
    else reader::prevChapter();
  } else {
    reader::turn(dir);
  }
}

void ReaderView::draw() {
  reader::ensureLayout(false);  // 正在画这一帧，不能再弹提示框（会留在画面上）
  const ReadGeom g = geom();
  const int W = g.W, H = g.H;
  const SysState &s = app::sys();
  char buf[48];

  // 页眉：章节名（没有章节就是书名）
  int ci = bookChapterAt(bookOffset());
  String head = ci >= 0 ? bookChapterTitle(ci) : bookTitle();
  int hb = g.headH - gfx::px(7);
  gfx::text(g.mx, hb, gfx::fit(head, Font::Small, W - 2 * g.mx), Font::Small);
  gfx::hline(g.mx, g.headH - 2, W - 2 * g.mx);

  // 正文
  static TextLine lines[64];
  int n = bookPageLines(bookPage(), lines, g.lines);
  const int asc = gfx::bodyAscent();
  const int maxW = W - 2 * g.mx;
  const int indent = 2 * gfx::bodySize();
  for (int k = 0; k < n; k++) {
    const TextLine &ln = lines[k];
    if (ln.kind == LINE_BLANK) continue;
    const char *t = bookSpan(ln.start, ln.len);  // 这一行的字（在窗口里）
    if (!t) continue;
    int y = g.top + asc + k * g.lh;
    int x = g.mx + (ln.kind == LINE_PARA ? indent : 0);
    int avail = maxW - (ln.kind == LINE_PARA ? indent : 0);
    bool bold = ln.kind == LINE_HEADING;
    if (bold) x = g.mx + max(0, (maxW - ln.width) / 2);
    // 两端对齐：把行尾剩下的空白分到字间。英文单词里面的字母之间不拉开
    auto stretch = [](uint32_t a, uint32_t b) { return !(a < 0x80 && isalnum(a) && b < 0x80 && isalnum(b)); };
    int gaps = 0;
    if (ln.justify) {
      uint32_t prev = 0;
      for (uint32_t i = 0; i < ln.len;) {
        uint32_t cp = utf8Next(t, ln.len, i);
        if (prev && stretch(prev, cp)) gaps++;
        prev = cp;
      }
    }
    int extra = ln.justify ? avail - ln.width : 0;
    if (extra < 0 || extra > gfx::bodySize() * 3) extra = 0;  // 差太多（英文长词）就不拉了
    int done = 0, acc = 0;
    uint32_t prev = 0;
    for (uint32_t i = 0; i < ln.len;) {
      uint32_t cp = utf8Next(t, ln.len, i);
      if (prev && stretch(prev, cp)) done++;
      int off = gaps > 0 ? extra * done / gaps : 0;
      gfx::bodyGlyph(x + acc + off, y, cp, bold);
      acc += gfx::bodyAdvance(cp);
      prev = cp;
    }
  }

  // 页脚：本章第几页 …… 全书进度 …… 时间 电量。没认出章节的书显示全书页码
  int fb = H - gfx::px(7);
  if (ci >= 0) {
    const auto &ch = bookChapters();
    int p0 = bookPageOf(ch[ci].off);
    int p1 = ci + 1 < (int)ch.size() ? bookPageOf(ch[ci + 1].off) : bookPageCount();
    snprintf(buf, sizeof(buf), "本章 %d / %d", bookPage() - p0 + 1, max(1, p1 - p0));
  } else {
    snprintf(buf, sizeof(buf), "%d / %d", bookPage() + 1, bookPageCount());
  }
  gfx::text(g.mx, fb, buf, Font::Small);
  if (app::sleepBadge()) {
    gfx::textCenter(W / 2, fb, "休眠中，按键继续", Font::Small);
  } else if (tts::active()) {
    // 朗读时中间显示进度 + 状态（暂停了也看得见）
    snprintf(buf, sizeof(buf), "%d.%d%%", bookPermille() / 10, bookPermille() % 10);
    gfx::textCenter(W / 2, fb, String(buf) + "  " + tts::statusText(), Font::Small);
  } else {
    snprintf(buf, sizeof(buf), "%d.%d%%", bookPermille() / 10, bookPermille() % 10);
    gfx::textCenter(W / 2, fb, buf, Font::Small);
  }
  const int bw = gfx::px(20), bh = gfx::px(10);
  gfx::battery(W - g.mx - bw - 2, fb - bh + 1, bw, bh, s.bat.ok ? s.bat.soc : 0, s.usb);
  snprintf(buf, sizeof(buf), "%02d:%02d", s.now.hour, s.now.minute);
  gfx::textRight(W - g.mx - bw - gfx::px(8), fb, buf, Font::Small);
}
