// 桌面界面（横放在支架上）。版式见 docs/mockups-426/桌面A_细化.png：
//   顶行：日期 星期 …… 农历 · 节气  电量
//   中间：左边大时钟；右边细竖线隔出读数栏（二氧化碳、温度、湿度，超标时标签旁反白"该开窗了"）
//         没有 CO2 传感器（温湿度也读不到）就不要右栏，时钟在整行居中
//   底部：细线；在读《书名》章节 …… 百分比；最下面一条细线当进度条，读过的部分加粗
#include "core/lunar.h"
#include "core/settings.h"
#include "gfx.h"
#include "reader/library.h"
#include "views.h"

static const char *WEEK[] = {"日", "一", "二", "三", "四", "五", "六"};

class DeskView : public View {
 public:
  const char *name() const override { return "desk"; }
  bool isRoot() const override { return true; }
  uint32_t idleMs() const override { return IDLE_DESK_MS; }
  int fullEvery() const override { return DESK_FULL_EVERY; }
  void onShow() override { minute_ = app::sys().now.minute; }
  void onTick() override {
    int m = app::sys().now.minute;
    if (m != minute_) {  // 每分钟局刷一次
      minute_ = m;
      app::invalidate(Refresh::Partial);
    }
  }
  void onKey(Key k, Press p) override {
    if (k == Key::Menu && p == Press::Short) app::push(newDeskMenu());
    else if (k == Key::Menu && p == Press::Long) app::enterReading();  // 长按菜单：切到阅读（文档）
    else if (p == Press::Short) { app::refreshSensors(true); app::invalidate(Refresh::Partial); }
  }
  void draw() override;

 private:
  int minute_ = -1;
};

View *newDeskView() { return new DeskView(); }

static int P(int v) { return gfx::px(v); }  // 按 400×300 定的尺寸，800×480 上 ×1.6

// 反白的小标签（"该开窗了""电量低"），x 是字的左边，返回占的宽度
static int tag(int x, int base, const String &s) {
  const int w = gfx::textW(s, Font::Small), pad = P(4);
  gfx::d().fillRect(x - pad, base - gfx::ascent(Font::Small) - P(1), w + 2 * pad, gfx::lineH(Font::Small) + P(1), EPD_BLACK);
  gfx::text(x, base, s, Font::Small, EPD_WHITE);
  return w + 2 * pad;
}

void DeskView::draw() {
  const SysState &s = app::sys();
  const int W = gfx::W(), H = gfx::H(), M = P(25);
  char buf[64];

  // ---- 顶行 ----
  const int hb = P(36);
  // 右边：电量（图标在最右，百分比在左），用电池且电量低时前面加反白"电量低"
  const int bw = P(16), bh = P(8);
  int xr = W - M - bw - 3;
  gfx::battery(xr, hb - bh, bw, bh, s.bat.ok ? s.bat.soc : 0, s.usb);
  xr -= P(5);
  if (s.bat.ok) {
    snprintf(buf, sizeof(buf), "%d%%", s.bat.soc);
    xr -= gfx::textW(buf, Font::NumSmall);
    gfx::text(xr, hb, buf, Font::NumSmall);
  }
  if (s.bat.ok && !s.usb && s.bat.mv && s.bat.mv < BAT_LOW_MV) {
    xr -= P(8) + gfx::textW("电量低", Font::Small) + P(4);
    tag(xr, hb, "电量低");
  }
  if (s.timeOk) {
    snprintf(buf, sizeof(buf), "%d月%d日  星期%s", s.now.month, s.now.day, WEEK[s.now.weekday % 7]);
    gfx::text(M, hb, buf);
    // 农历 · 节气（最近的一个节气；当天是节气就是它）
    LunarDate l;
    String right;
    if (lunarFromSolar(s.now.year, s.now.month, s.now.day, l)) right = lunarText(l).c_str();
    int since;
    int t = solarTermLatest(s.now.year, s.now.month, s.now.day, since);
    if (t >= 0) right += String(right.length() ? " · " : "") + solarTermName(t);
    if (right.length()) gfx::textRight(xr - P(14), hb, right);
  }

  // ---- 中间：上下边界，右栏 ----
  const int top = P(57), bot = H - P(80);
  const bool col = s.co2Present;  // 温湿度也是 CO2 传感器那块板上的，没有它就没有右栏
  const int colX = col ? W * 27 / 40 : W - M;

  // 大时钟：在中间区域上下居中；有右栏时左对齐，没有就整行居中
  if (s.timeOk) {
    snprintf(buf, sizeof(buf), "%02d:%02d", s.now.hour, s.now.minute);
    const int cap = gfx::glyphTop(Font::Clock, '0');
    const int base = (top + bot) / 2 + cap / 2;
    if (col) gfx::text(M - P(4), base, buf, Font::Clock);
    else gfx::textCenter(W / 2, base, buf, Font::Clock);
  } else {  // 没对时：不画假的钟，写清楚怎么办
    const int mid = (top + bot) / 2;
    gfx::text(M, mid + P(5), "还没对时", Font::Note);
    gfx::text(M + 2, mid + P(35), "菜单 → 系统设置 → Wi-Fi → 现在联网对时", Font::Small);
  }

  if (col) {
    gfx::d().drawFastVLine(colX, top, bot - top, EPD_BLACK);
    struct Row { const char *label, *unit; String value; bool alarm; };
    Row rows[3] = {
        {"二氧化碳", "ppm", s.clim.ok ? String(s.clim.co2) : String("--"), s.clim.ok && s.clim.co2 >= 1000},
        {"温度", "\xC2\xB0" "C", s.clim.ok ? String(s.clim.temp, 1) : String("--"), false},
        {"湿度", "%", s.clim.ok ? String((int)(s.clim.rh + 0.5f)) : String("--"), false},
    };
    const int x0 = colX + P(20), pitch = (bot - top) / 3, item = P(44);
    for (int i = 0; i < 3; i++) {
      const int y = top + pitch * i + (pitch - item) / 2;
      const int lb = y + P(9);
      gfx::text(x0, lb, rows[i].label, Font::Small);
      if (rows[i].alarm) tag(x0 + gfx::textW(rows[i].label, Font::Small) + P(10), lb, "该开窗了");
      const int vb = y + item;
      int vw = gfx::text(x0, vb, rows[i].value, Font::Value);
      gfx::text(x0 + vw + P(4), vb, rows[i].unit, Font::Small);
    }
  }

  // ---- 底部：在读 ----
  const int ry = H - P(65);
  gfx::hline(M, ry, W - 2 * M);
  const String last = libraryLastPath();
  const int pm = progressPermille(last);
  const int b2 = ry + P(29);
  String pct = pm >= 0 ? String(pm / 10) + "%" : String("");
  int pw = pct.length() ? gfx::textW(pct, Font::Num) : 0;
  if (pw) gfx::textRight(W - M, b2, pct, Font::Num);
  int x = M;
  x += gfx::text(x, b2, "在读", Font::Small) + P(4);
  const int room = W - M - (pw ? pw + P(16) : 0);  // 书名和章节能用到哪
  String title = "《" + titleFromPath(last) + "》";
  x += gfx::text(x, b2, gfx::fit(title, Font::Ui, room - x), Font::Ui) + P(6);
  String ch = libraryLastChapter();
  if (ch.length() && room - x > P(30)) gfx::text(x, b2, gfx::fit(ch, Font::Small, room - x), Font::Small);
  // 进度条：一条细线，读过的部分加粗
  const int py = b2 + P(14);
  gfx::hline(M, py, W - 2 * M);
  if (pm > 0) gfx::d().fillRect(M, py - 2, (W - 2 * M) * pm / 1000, 5, EPD_BLACK);
}
