#include "list_view.h"

#include "gfx.h"
#include "widgets.h"

static int P(int v) { return gfx::px(v); }  // 尺寸按屏幕缩放
#define M ui::margin()

// 行高：竖屏地方多，行距放宽一点，手指（眼睛）好对准；横屏紧一点，一屏多放几项
static int rowH() { return gfx::lineH(Font::Ui) + (gfx::H() > gfx::W() ? P(20) : P(14)); }

int ListView::visibleRows() const {
  int v = (ui::bodyBottom() - ui::bodyTop() - 2) / rowH();
  return v < 1 ? 1 : v;
}

void ListView::setCursor(int c) {
  int n = count();
  if (n <= 0) { cursor_ = 0; return; }
  cursor_ = constrain(c, 0, n - 1);
}

void ListView::draw() {
  const int W = gfx::W();
  const int rh = rowH(), pd = ui::pad();
  const int n = count(), vis = visibleRows();
  const int pages = n ? (n + vis - 1) / vis : 1;
  const int page = cursor_ / vis;

  // 标题栏（右边是附加信息，多页时是页码）+ 底部提示
  String right = titleRight();
  if (pages > 1) right = String(page + 1) + "/" + String(pages);
  ui::frame(title_, hint(), right);

  // 各行：选中条和分隔线左右对齐，文字离边 pad
  const int asc = gfx::ascent(Font::Ui), desc = gfx::lineH(Font::Ui) - asc;
  const int top = ui::bodyTop();
  for (int i = page * vis; i < n && i < (page + 1) * vis; i++) {
    ListRow r;
    row(i, r);
    int y = top + (i - page * vis) * rh;
    bool sel = i == cursor_;
    uint16_t fg = sel ? EPD_WHITE : EPD_BLACK;
    if (sel) gfx::d().fillRect(M, y + 1, W - 2 * M, rh - 1, EPD_BLACK);
    else if (i != page * vis && i - 1 != cursor_) gfx::hline(M, y, W - 2 * M);
    int tb = y + (rh + asc - desc) / 2;  // 字的上下居中（按 ascent / descent）
    int vw = r.value.length() ? gfx::textW(r.value, Font::Ui) : 0;
    if (vw) gfx::text(W - M - pd - vw, tb, r.value, Font::Ui, fg);
    int x = M + pd;
    if (r.marked) {
      gfx::d().fillCircle(x + P(3), y + rh / 2, P(3), fg);
      x += P(12);
    }
    int avail = W - M - pd - (vw ? vw + P(12) : 0) - x;
    gfx::text(x, tb, gfx::fit(r.label, Font::Ui, avail), Font::Ui, fg);
  }
  if (n == 0) gfx::textCenter(W / 2, top + rh, "（空）");
}

void ListView::onKey(Key k, Press p) {
  const int n = count(), vis = visibleRows();
  if (k == Key::Menu) {
    if (p == Press::Short && n > 0) select(cursor_);
    else if (p == Press::Long) app::pop();
    return;
  }
  if (n == 0) return;
  int dir = k == Key::Next ? 1 : -1;
  int old = cursor_;
  if (p == Press::Short) cursor_ = (cursor_ + dir + n) % n;  // 短按：一行一行，到头绕回去
  else cursor_ = constrain((cursor_ / vis + dir) * vis, 0, n - 1);  // 长按：翻一屏
  if (cursor_ != old) app::invalidate(Refresh::Partial);
}

// ---------------------------------------------------------------------
void MenuView::row(int i, ListRow &r) const {
  r.label = items_[i].label;
  r.value = items_[i].value ? items_[i].value() : String();
}

void MenuView::select(int i) {
  auto act = items_[i].action;  // 拷一份：动作里可能会重建菜单或关掉这个界面
  if (act) act();
}

void MenuView::refresh() {
  items_.clear();
  build(items_);
  setCursor(cursor_);
  app::invalidate(Refresh::Partial);
}
