// 其他界面：跳转进度、设置时间、关于本机、姿态校准、提示框、确认框、关机画面
#include <esp_heap_caps.h>

#include "board/storage.h"
#include "core/settings.h"
#include "gfx.h"
#include "input/events.h"
#include "input/motion.h"
#include "reader/book.h"
#include "reader/library.h"
#include "text/font.h"
#include "art_shutdown.h"
#include "views.h"
#include "widgets.h"

static int P(int v) { return gfx::px(v); }  // 尺寸按屏幕缩放（按 400×300 的小屏定的）
#define M ui::margin()

static void frame(const String &title, const char *hint) { ui::frame(title, hint); }
static void box(const String &title, const String &body, const String &hint) { ui::box(title, body, hint); }

void drawBusyBox(const char *msg) { box(msg, "", ""); }

void bootBanner(const char *msg) {
  gfx::clear();
  ui::BoxRect r = ui::box(msg);
  gfx::d().forcePartialRect(r.x, r.y, r.w, r.h);
  gfx::show(false);
}

// =====================================================================
// 提示框：叠在当前界面上，过一会儿或按任意键消失
// =====================================================================
class MessageView : public View {
 public:
  MessageView(const String &a, const String &b, uint32_t ms) : a_(a), b_(b), ms_(ms) {}
  const char *name() const override { return "message"; }
  bool overlay() const override { return true; }
  void draw() override {
    app::drawBelow(this);
    box(a_, b_, "");
    drawn_ = true;
  }
  void onTick() override {  // 从刷到屏上以后开始计时（刷屏本身要一秒左右）
    if (drawn_ && !at_) at_ = millis();
    if (ms_ && at_ && millis() - at_ > ms_) app::pop(Refresh::Partial);
  }
  void onKey(Key, Press) override { app::pop(Refresh::Partial); }

 private:
  String a_, b_;
  uint32_t ms_, at_ = 0;
  bool drawn_ = false;
};
View *newMessageView(const String &a, const String &b, uint32_t ms) { return new MessageView(a, b, ms); }

class ConfirmView : public View {
 public:
  ConfirmView(const String &t, const String &l, std::function<void()> yes) : t_(t), l_(l), yes_(yes) {}
  const char *name() const override { return "confirm"; }
  bool overlay() const override { return true; }
  void draw() override {
    app::drawBelow(this);
    box(t_, l_, "菜单键确定　长按菜单取消");
  }
  void onKey(Key k, Press p) override {
    auto yes = yes_;
    app::pop(Refresh::Partial);
    if (k == Key::Menu && p == Press::Short && yes) yes();
  }

 private:
  String t_, l_;
  std::function<void()> yes_;
};
View *newConfirmView(const String &t, const String &l, std::function<void()> yes) { return new ConfirmView(t, l, yes); }

// =====================================================================
// 跳转进度
// =====================================================================
class JumpView : public View {
 public:
  JumpView() : pm_(bookPermille()) { pm_ = pm_ / 10 * 10; }
  const char *name() const override { return "jump"; }
  void draw() override {
    const int W = gfx::W(), H = gfx::H();
    frame("跳转进度", "翻页键 ±1%　长按 ±10%　菜单键跳转");
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", pm_ / 10);
    int cy = H / 2;
    gfx::textCenter(W / 2, cy - P(10), buf, Font::Value);
    int barW = W - 4 * M, x = 2 * M;
    gfx::d().drawRect(x, cy + P(6), barW, P(10), EPD_BLACK);
    gfx::d().fillRect(x + 2, cy + P(6) + 2, (barW - 4) * pm_ / 1000, P(10) - 4, EPD_BLACK);
    // 当前位置的小三角
    int cx = x + 2 + (barW - 4) * bookPermille() / 1000;
    gfx::d().fillTriangle(cx - P(4), cy + P(24), cx + P(4), cy + P(24), cx, cy + P(18), EPD_BLACK);
    uint32_t off = target();
    int page = bookPageOf(off);
    int ci = bookChapterAt(off);
    snprintf(buf, sizeof(buf), "第 %d / %d 页", page + 1, bookPageCount());
    gfx::textCenter(W / 2, cy + P(46), buf, Font::Small);
    if (ci >= 0) gfx::textCenter(W / 2, cy + P(66), gfx::fit(bookChapterTitle(ci), Font::Small, W - 4 * M), Font::Small);
  }
  void onKey(Key k, Press p) override {
    if (k == Key::Menu) {
      if (p == Press::Short) {
        reader::gotoOffset(target());
        app::popToRoot(Refresh::Full);
      } else if (p == Press::Long) {
        app::pop();
      }
      return;
    }
    int step = p == Press::Short ? 10 : 100;
    pm_ = constrain(pm_ + (k == Key::Next ? step : -step), 0, 1000);
    app::invalidate(Refresh::Partial);
  }

 private:
  uint32_t target() const { return (uint64_t)bookSize() * pm_ / 1000; }
  int pm_;
};
View *newJumpView() { return new JumpView(); }

// =====================================================================
// 设置时间：年 月 日 时 分，一项一项改
// =====================================================================
class TimeSetView : public View {
 public:
  TimeSetView() {
    const DateTime &n = app::sys().now;
    v_[0] = n.year < 2025 ? 2026 : n.year;
    v_[1] = n.month;
    v_[2] = n.day;
    v_[3] = n.hour;
    v_[4] = n.minute;
  }
  const char *name() const override { return "time-set"; }
  void draw() override {
    const int W = gfx::W(), H = gfx::H();
    frame("设置时间", "翻页键调整　菜单键下一项　长按菜单取消");
    static const char *names[] = {"年", "月", "日", "时", "分"};
    char buf[16];
    // 两行：日期、时间；正在改的那一项反白
    int cw = P(64), gap = P(14);
    int rowY[2] = {H / 2 - P(28), H / 2 + P(30)};
    for (int i = 0; i < 5; i++) {
      int r = i < 3 ? 0 : 1;
      int cols = r == 0 ? 3 : 2;
      int c = r == 0 ? i : i - 3;
      int w = (i == 0 ? cw + P(24) : cw);
      int total = r == 0 ? (cw + P(24)) + 2 * cw + 2 * gap : 2 * cw + gap;
      int x0 = (W - total) / 2;
      int x = x0 + (c == 0 ? 0 : (r == 0 ? cw + P(24) + gap + (c - 1) * (cw + gap) : cw + gap));
      (void)cols;
      int y = rowY[r];
      bool sel = i == field_;
      if (sel) gfx::d().fillRect(x, y - P(30), w, P(38), EPD_BLACK);
      else gfx::d().drawRect(x, y - P(30), w, P(38), EPD_BLACK);
      snprintf(buf, sizeof(buf), i == 0 ? "%d" : "%02d", v_[i]);
      gfx::textCenter(x + w / 2, y, buf, Font::Value, sel ? EPD_WHITE : EPD_BLACK);
      gfx::textCenter(x + w / 2, y + P(22), names[i], Font::Small);
    }
  }
  void onKey(Key k, Press p) override {
    if (k == Key::Menu) {
      if (p == Press::Long) { app::pop(); return; }
      if (p != Press::Short) return;
      if (++field_ < 5) { app::invalidate(Refresh::Partial); return; }
      DateTime t{(uint16_t)v_[0], (uint8_t)v_[1], (uint8_t)v_[2], (uint8_t)v_[3], (uint8_t)v_[4], 0, 0};
      rtcWrite(t);
      app::refreshSensors(true);
      app::pop();
      app::toast("时间已设好");
      return;
    }
    int d = k == Key::Next ? 1 : -1;
    if (p != Press::Short) d *= (field_ == 4 ? 10 : field_ == 0 ? 1 : 5);
    static const int LO[] = {2024, 1, 1, 0, 0};
    int hi[] = {2099, 12, daysInMonth(v_[0], v_[1]), 23, 59};
    int span = hi[field_] - LO[field_] + 1;
    v_[field_] = LO[field_] + ((v_[field_] - LO[field_] + d) % span + span) % span;
    v_[2] = min(v_[2], daysInMonth(v_[0], v_[1]));
    app::invalidate(Refresh::Partial);
  }

 private:
  int v_[5];
  int field_ = 0;
};
View *newTimeSetView() { return new TimeSetView(); }

// =====================================================================
// 关于本机
// =====================================================================
class InfoView : public View {
 public:
  InfoView() {
    SdSession sd;
    if (sd.ok) {
      char b[48];
      snprintf(b, sizeof(b), "%.1f GB，剩 %.1f GB", SD_MMC.totalBytes() / 1073741824.0,
               (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / 1073741824.0);
      card_ = b;
    } else {
      card_ = "没有卡";
    }
  }
  const char *name() const override { return "info"; }
  void draw() override {
    const SysState &s = app::sys();
    const app::SleepStat &st = app::sleepStat();
    char b[96];
    frame("关于本机", "按任意键返回");
    int y = ui::bodyTop() + P(8) + gfx::ascent(Font::Small);
    const int kx = M + ui::pad(), vx = kx + gfx::textW("TF 卡", Font::Small) + P(14);
    auto line = [&](const char *k, const String &v) {
      gfx::text(kx, y, k, Font::Small);
      gfx::text(vx, y, gfx::fit(v, Font::Small, gfx::W() - M - ui::pad() - vx), Font::Small);
      y += gfx::lineH(Font::Small) + P(6);
    };
    line("固件", String(FW_NAME " " FW_VERSION "（" __DATE__ "）"));
    line("屏幕", gfx::d().name());
    snprintf(b, sizeof(b), "%.2f V，%d%%，%s", s.bat.mv / 1000.0, s.bat.soc, s.usb ? "插着 USB" : "用电池");
    line("电池", s.bat.ok ? String(b) : String("读不到电量计"));
    snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d%s", s.now.year, s.now.month, s.now.day, s.now.hour, s.now.minute,
             s.timeOk ? "" : "（未对时）");
    line("时间", b);
    line("TF 卡", card_);
    line("字库", gfx::bodyIsEfnt() ? fontFamilyLabel(settings().fontFamily) + " " + String(gfx::bodySize()) + " 像素"
                                   : String("系统点阵 16"));
    if (bookIsOpen()) line("书", bookTitle() + "（" + bookInfo() + "，" + String(bookChapters().size()) + " 章）");
    snprintf(b, sizeof(b), "PSRAM 空 %.1f MB，内部 %u KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576.0,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    line("内存", b);
    uint32_t up = millis() / 1000;
    snprintf(b, sizeof(b), "开机 %luh%02lum，浅睡 %.0f%%，深睡醒 %lu 次", (unsigned long)(up / 3600),
             (unsigned long)(up / 60 % 60), up ? st.msAsleep / 10.0 / up : 0.0, (unsigned long)st.deepWakes);
    line("运行", b);
    line("姿态", String(poseName(app::pose())) + (settings().calibrated ? "，已校准" : "，默认轴向"));
  }
  void onKey(Key, Press p) override {
    if (p == Press::Short || p == Press::Long) app::pop();
  }

 private:
  String card_;
};
View *newInfoView() { return new InfoView(); }

// =====================================================================
// 姿态校准：两步，按菜单键确认，结果存 NVS
// =====================================================================
class CalView : public View {
 public:
  const char *name() const override { return "calibrate"; }
  bool acceptsPose() const override { return false; }
  uint32_t idleMs() const override { return 60000; }
  void draw() override {
    const int W = gfx::W(), H = gfx::H();
    frame(step_ == 1 ? "姿态校准 1/2" : "姿态校准 2/2", "拿稳后按菜单键　长按菜单取消");
    int y = ui::paragraph(W / 2, H / 2 - P(12),
                          step_ == 1 ? "竖着拿：按键在右手边，Type-C 朝下" : "横过来立着：按键那条边朝上", Font::Ui,
                          W - 2 * M - 2 * ui::pad(), 3);
    if (msg_.length()) ui::paragraph(W / 2, y + P(10), msg_, Font::Small, W - 2 * M - 2 * ui::pad(), 2);
  }
  void onKey(Key k, Press p) override {
    if (k != Key::Menu) return;
    if (p == Press::Long) { app::pop(); return; }
    if (p != Press::Short) return;
    float g[3];
    motionRaw(g);
    int ax = 0;
    for (int i = 1; i < 3; i++)
      if (fabsf(g[i]) > fabsf(g[ax])) ax = i;
    int sign = g[ax] >= 0 ? 1 : -1;
    Serial.printf("[校准] X %+.2f  Y %+.2f  Z %+.2f → 轴 %c%c\n", g[0], g[1], g[2], sign > 0 ? '+' : '-', "XYZ"[ax]);
    if (fabsf(g[ax]) < 0.75f) { msg_ = "没有拿正（要竖直立着，别斜），再按一次"; app::invalidate(); return; }
    if (step_ == 1) {
      top_ = ax;
      topSign_ = sign;
      step_ = 2;
      msg_ = "";
      app::invalidate();
      return;
    }
    if (ax == top_) { msg_ = "两次方向一样，没有转过去"; app::invalidate(); return; }
    Settings &s = settings();
    s.calibrated = true;
    s.axTop = top_; s.sgTop = topSign_; s.axKey = ax; s.sgKey = sign;
    settingsSave();
    motionSetAxes(top_, topSign_, ax, sign);
    Serial.printf("[校准] 完成：上方 = %c%c，按键方向 = %c%c\n", topSign_ > 0 ? '+' : '-', "XYZ"[top_],
                  sign > 0 ? '+' : '-', "XYZ"[ax]);
    app::pop();
    app::toast("姿态校准完成");
  }

 private:
  int step_ = 1, top_ = 0, topSign_ = 1;
  String msg_;
};
View *newCalView() { return new CalView(); }

// =====================================================================
// 关机画面
// =====================================================================
// 关机时间、电量
static String shutdownWhen() {
  const SysState &s = app::sys();
  char buf[64];
  String t;
  if (s.timeOk) {
    snprintf(buf, sizeof(buf), "%d月%d日 %02d:%02d 关机", s.now.month, s.now.day, s.now.hour, s.now.minute);
    t = buf;
  } else {
    t = "关机";
  }
  if (s.bat.ok) t += "　电量 " + String(s.bat.soc) + "%";
  return t;
}

// 读到哪本书；太长时只截书名，百分比留着。没读过书返回空
static String shutdownBook(int maxW) {
  String last = libraryLastPath();
  int pm = progressPermille(last);
  if (pm < 0) return "";
  String tail = "》读到 " + String(pm / 10) + "%";
  int room = maxW - gfx::textW("《" + tail, Font::Small);
  return "《" + gfx::fit(titleFromPath(last), Font::Small, room) + tail;
}

void drawShutdownScreen(const char *reason) {
  const int W = gfx::W(), H = gfx::H();
  // 4.26 寸屏：水墨山水（app/art/shutdown.jpg，tools/art_from_image.py 转的），留白处写三行字
  const bool land = W == ART_LAND_W && H == ART_LAND_H, port = W == ART_PORT_W && H == ART_PORT_H;
  if (land || port) {
    gfx::d().drawBitmap(0, 0, land ? ART_LAND : ART_PORT, W, H, EPD_BLACK);
    const int x = P(14), maxW = (land ? ART_LAND_TEXT_X1 : ART_PORT_TEXT_X1) - 2 * x;
    const int step = gfx::lineH(Font::Small) + P(2);
    int y = (land ? ART_LAND_TEXT_Y : ART_PORT_TEXT_Y) + P(2) + gfx::ascent(Font::Ui);
    String l1 = reason ? String(reason) : String("已关机");
    int w1 = gfx::text(x, y, l1, Font::Ui);
    gfx::text(x + 1, y, l1, Font::Ui);  // 描一遍，粗一点
    String hint = reason ? "充上电后长按电源键 3 秒开机" : "长按电源键 3 秒开机";
    gfx::text(x + w1 + P(10), y, gfx::fit(hint, Font::Small, maxW - w1 - P(10)), Font::Small);
    y += step + P(1);
    gfx::text(x, y, gfx::fit(shutdownWhen(), Font::Small, maxW), Font::Small);
    y += step;
    gfx::text(x, y, shutdownBook(maxW), Font::Small);
    return;
  }
  // 其他屏（4.2 寸测试屏）：简单版
  const SysState &s = app::sys();
  char buf[96];
  gfx::d().drawRect(P(8), P(8), W - P(16), H - P(16), EPD_BLACK);
  gfx::d().drawRect(P(11), P(11), W - P(22), H - P(22), EPD_BLACK);

  int cy = H / 2 - P(30);
  int r = P(14), ix = W / 2, iy = cy - P(30);  // 电源键图标
  for (int k = 0; k < P(2); k++) gfx::d().drawCircle(ix, iy, r - k, EPD_BLACK);
  gfx::d().fillRect(ix - P(4), iy - r - 2, P(8), r, EPD_WHITE);
  gfx::d().fillRect(ix - P(1), iy - r - 2, P(3), r + 2, EPD_BLACK);
  gfx::textCenter(W / 2, cy + P(10) + gfx::ascent(Font::Ui) / 2, reason ? reason : "已关机");
  gfx::textCenter(W / 2, cy + P(36) + gfx::ascent(Font::Ui) / 2, reason ? "充上电后长按电源键 3 秒开机" : "长按电源键 3 秒开机", Font::Small);

  String last = libraryLastPath();
  int pm = progressPermille(last);
  if (pm >= 0) {
    String t = "《" + titleFromPath(last) + "》读到 " + String(pm / 10) + "%";
    gfx::textCenter(W / 2, H - P(62), gfx::fit(t, Font::Ui, W - P(40)));
  }
  if (s.timeOk) snprintf(buf, sizeof(buf), "%d月%d日 %02d:%02d 关机", s.now.month, s.now.day, s.now.hour, s.now.minute);
  else snprintf(buf, sizeof(buf), "关机");
  if (s.bat.ok) {
    size_t l = strlen(buf);
    snprintf(buf + l, sizeof(buf) - l, "　电量 %d%%", s.bat.soc);
  }
  gfx::textCenter(W / 2, H - P(36), buf, Font::Small);
}
