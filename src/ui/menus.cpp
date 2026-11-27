// 菜单、书库、目录
#include <algorithm>

#include "core/ota.h"
#include "core/settings.h"
#include "gfx.h"
#include "input/motion.h"
#include "list_view.h"
#include "net/update.h"
#include "net/wifi.h"
#include "reader/book.h"
#include "reader/library.h"
#include "text/font.h"
#include "views.h"
#include "voice/tts.h"

static String onOff(bool b) { return b ? "开" : "关"; }
static String pct(int permille) {
  char b[12];
  snprintf(b, sizeof(b), "%d%%", permille / 10);
  return b;
}

// =====================================================================
// 阅读时按菜单键
// =====================================================================
static void startVoice() {
  String why;
  if (!tts::ready(&why)) {
    app::toast("还开始不了朗读", why, 3000);
    return;
  }
  if (!tts::start(bookOffset())) {
    app::toast("朗读开不了", tts::lastError(), 3500);
    return;
  }
  app::popToRoot(Refresh::Full);  // 回到正文，跟着朗读自动翻页
}

class ReaderMenu : public MenuView {
 public:
  ReaderMenu() : MenuView(bookTitle()) {}
  const char *name() const override { return "reader-menu"; }

 protected:
  String titleRight() const override { return pct(bookPermille()); }
  void build(std::vector<MenuItem> &m) override {
    int nch = bookChapters().size();
    m.push_back({"目录", [nch] { return nch ? String(nch) + " 章" : String("无"); }, [nch] {
                   if (nch) app::push(newTocView());
                   else app::toast("这本书没有认出章节", "可以用\"跳转进度\"");
                 }});
    // 语音朗读
    if (tts::active()) {
      m.push_back({tts::paused() ? "继续朗读" : "暂停朗读", [] { return tts::statusText(); },
                   [] {
                     tts::togglePause();
                     app::popToRoot(Refresh::Full);
                   }});
      m.push_back({"停止朗读", nullptr, [] {
                     tts::stop();
                     app::popToRoot(Refresh::Full);
                   }});
    } else {
      m.push_back({"语音朗读", [] {
                     String why;
                     return tts::ready(&why) ? String("从本页开始") : why;
                   },
                   [] { startVoice(); }});
    }
    m.push_back({"朗读设置", [] { return tts::modelLabel(settings().ttsModel); },
                 [] { app::push(newVoiceSettings()); }});
    m.push_back({"跳转进度", [] { return String(bookPage() + 1) + "/" + String(bookPageCount()) + " 页"; },
                 [] { app::push(newJumpView()); }});
    m.push_back({"书库", nullptr, [] { app::push(newLibraryView()); }});
    m.push_back({"阅读设置", nullptr, [] { app::push(newReadingSettings()); }});
    m.push_back({"系统设置", nullptr, [] { app::push(newSystemSettings()); }});
    m.push_back({"切到桌面模式", nullptr, [] { app::enterDesk(); }});
  }
};
View *newReaderMenu() { return new ReaderMenu(); }

// =====================================================================
// 桌面模式按菜单键
// =====================================================================
class DeskMenu : public MenuView {
 public:
  DeskMenu() : MenuView("菜单") {}
  const char *name() const override { return "desk-menu"; }

 protected:
  void build(std::vector<MenuItem> &m) override {
    String last = libraryLastPath();
    m.push_back({"继续阅读", [last] { return titleFromPath(last); }, [] { app::enterReading(); }});
    m.push_back({"书库", nullptr, [] { app::push(newLibraryView()); }});
    m.push_back({"系统设置", nullptr, [] { app::push(newSystemSettings()); }});
    m.push_back({"返回", nullptr, [] { app::pop(); }});
  }
};
View *newDeskMenu() { return new DeskMenu(); }

// =====================================================================
// 阅读设置
// =====================================================================
template <typename T>
static T nextOf(const std::vector<T> &opts, const T &cur) {
  auto it = std::find(opts.begin(), opts.end(), cur);
  return it == opts.end() || it + 1 == opts.end() ? opts.front() : *(it + 1);
}

class ReadingSettings : public MenuView {
 public:
  ReadingSettings() : MenuView("阅读设置") { fontScan(); }  // 每次进来看一眼卡上新放的字库
  const char *name() const override { return "reading-settings"; }

 protected:
  void build(std::vector<MenuItem> &m) override {
    Settings &s = settings();
    m.push_back({"字体",
                 [] {
                   const Settings &s = settings();
                   return gfx::bodyIsEfnt() ? fontFamilyLabel(s.fontFamily) : String("系统点阵");
                 },
                 [this] {
                   std::vector<String> fams = fontFamilies();
                   fams.push_back("");  // 固件里的文泉驿
                   Settings &s = settings();
                   s.fontFamily = nextOf(fams, gfx::bodyIsEfnt() ? s.fontFamily : String(""));
                   changed();
                 }});
    m.push_back({"字号", [] { return String(gfx::bodySize()); },
                 [this] {
                   Settings &s = settings();
                   std::vector<int> sizes = gfx::bodyIsEfnt() ? fontSizes(s.fontFamily) : std::vector<int>();
                   if (sizes.size() <= 1) { app::toast("这个字体只有一个字号", gfx::bodyIsEfnt() ? "" : "卡上 /fonts 里放字库可以换字号"); return; }
                   s.fontSize = nextOf(sizes, (int)s.fontSize);
                   changed();
                 }});
    m.push_back({"行距", [] { return String(lineSpacingName(settings().lineSpacing)); },
                 [this] { settings().lineSpacing = (settings().lineSpacing + 1) % 3; changed(); }});
    m.push_back({"页边距", [] { return String(marginName(settings().margin)); },
                 [this] { settings().margin = (settings().margin + 1) % 3; changed(); }});
    m.push_back({"全刷间隔", [] { return "每 " + String(settings().fullEvery) + " 页"; },
                 [this] {
                   settings().fullEvery = nextOf(std::vector<int>{1, 3, 6, 10, 20}, (int)settings().fullEvery);
                   saved();
                 }});
    m.push_back({"双击后背翻页", [] { return onOff(settings().tapTurn); },
                 [this] {
                   settings().tapTurn = !settings().tapTurn;
                   motionTapEnable(settings().tapTurn);
                   saved();
                 }});
    (void)s;
  }

 private:
  void saved() {
    settingsSave();
    refresh();
  }
  void changed() {  // 字体、版面变了：载入字库，回到正文时重新分页
    if (tts::active()) tts::stop();  // 重新排版时先别念（要重新分页，内存也紧）
    bool ok = reader::applyFont();
    saved();
    if (!ok) app::toast("内存不够，换不了这个字号", "这本书太大，换小一点的字号或者小一点的书");
  }
};
View *newReadingSettings() { return new ReadingSettings(); }

// =====================================================================
// 朗读设置：模型、音色、语速、音量、自动翻页、业务空间 ID、API Key、试听
// =====================================================================
static float nextSpeed(float cur) {
  static const float STEPS[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
  for (float v : STEPS)
    if (v > cur + 0.001f) return v;
  return STEPS[0];
}

class VoiceSettings : public MenuView {
 public:
  VoiceSettings() : MenuView("朗读设置") {}
  const char *name() const override { return "voice-settings"; }

 protected:
  void build(std::vector<MenuItem> &m) override {
    m.push_back({"模型", [] { return tts::modelLabel(settings().ttsModel); },
                 [this] {
                   Settings &s = settings();
                   int n = tts::modelCount();
                   int i = 0;
                   for (; i < n; i++)
                     if (s.ttsModel == tts::modelId(i)) break;
                   s.ttsModel = tts::modelId((i + 1) % n);
                   s.ttsVoice = tts::defaultVoice(s.ttsModel);  // 换模型音色名不通用，用默认的
                   tts::setModel(s.ttsModel, s.ttsWs);
                   tts::setVoice(s.ttsVoice);
                   settingsSave();
                   refresh();
                 }});
    m.push_back({"音色", [] { return tts::voiceLabel(settings().ttsModel, settings().ttsVoice); },
                 [this] {
                   Settings &s = settings();
                   int n = tts::voiceCount(s.ttsModel);
                   int i = 0;
                   for (; i < n; i++)
                     if (s.ttsVoice == tts::voiceName(s.ttsModel, i)) break;
                   s.ttsVoice = tts::voiceName(s.ttsModel, (i + 1) % n);
                   tts::setVoice(s.ttsVoice);  // 正在念的话，下一句就用新音色
                   settingsSave();
                   refresh();
                 }});
    m.push_back({"语速", [] { return String(settings().ttsSpeed, 2) + " 倍"; },
                 [this] {
                   Settings &s = settings();
                   s.ttsSpeed = nextSpeed(s.ttsSpeed);
                   tts::setSpeed(s.ttsSpeed);  // 下一句生效
                   settingsSave();
                   refresh();
                 }});
    m.push_back({"音量", [] { return String(settings().ttsVolume) + "%"; },
                 [this] {
                   int v = (int)settings().ttsVolume + 10;
                   if (v > 100) v = 30;
                   settings().ttsVolume = (uint8_t)v;
                   tts::setVolume(v);
                   settingsSave();
                   refresh();
                 }});
    m.push_back({"朗读时自动翻页", [] { return onOff(settings().ttsAutoPage); },
                 [this] {
                   settings().ttsAutoPage = !settings().ttsAutoPage;
                   settingsSave();
                   refresh();
                 }});
    m.push_back({"业务空间 ID",
                 [] {
                   const String &w = settings().ttsWs;
                   if (!w.length()) return String("未设置");
                   String out = "已设置 …";
                   out += w.substring(w.length() > 6 ? w.length() - 6 : 0);
                   return out;
                 },
                 [] { app::toast("百炼控制台右上角图标里复制", "或在 无线传输 网页里填", 4500); }});
    m.push_back({"API Key",
                 [] {
                   const String &k = settings().ttsKey;
                   if (!k.length()) return String("未设置");
                   String out = "已设置 …";
                   out += k.substring(k.length() > 4 ? k.length() - 4 : 0);
                   return out;
                 },
                 [] { app::toast("在 系统设置 → Wi-Fi → 无线传输 的网页里填", "也可以串口：tts key sk-...", 4500); }});
    m.push_back({"试听一段", nullptr, [] {
                   String why;
                   if (!tts::ready(&why)) {
                     app::toast("还开始不了朗读", why, 3000);
                     return;
                   }
                   app::busy("正在连接 Wi-Fi…");
                   if (!tts::say("你好，这是语音朗读的试听。以后书里的每一句都这样边下载边播放。"))
                     app::toast("试听失败", tts::lastError(), 3000);
                   else
                     app::invalidate(Refresh::Partial);  // 把"正在连接"的提示框盖掉
                 }});
  }
};
View *newVoiceSettings() { return new VoiceSettings(); }

// =====================================================================
// Wi-Fi：总开关、配网、无线传输、对时
// =====================================================================
static bool wifiAllowed() {
  if (settings().wifiOn) return true;
  app::toast("Wi-Fi 已关闭", "先把第一项\"Wi-Fi\"打开");
  return false;
}

class WifiMenu : public MenuView {
 public:
  WifiMenu() : MenuView("Wi-Fi") {}
  const char *name() const override { return "wifi-menu"; }

 protected:
  void build(std::vector<MenuItem> &m) override {
    m.push_back({"Wi-Fi", [] { return onOff(settings().wifiOn); },
                 [this] {
                   settings().wifiOn = !settings().wifiOn;
                   if (!settings().wifiOn) {
                     if (tts::active()) tts::stop();  // 关 Wi-Fi 就别念了
                     net::off();
                   }
                   saved();
                 }});
    m.push_back({"配网", [] { return net::hasCredentials() ? net::savedSsid() : String("未设置"); },
                 [] { if (wifiAllowed()) app::push(newWifiSetupView()); }});
    m.push_back({"无线传输（传书、升级）", nullptr, [] { if (wifiAllowed()) app::push(newTransferView()); }});
    m.push_back({"每天自动对时", [] { return onOff(settings().autoTime); },
                 [this] { settings().autoTime = !settings().autoTime; saved(); }});
    m.push_back({"现在联网对时", nullptr, [] {
                   if (!wifiAllowed()) return;
                   if (!net::hasCredentials()) { app::toast("还没配网", "先点\"配网\""); return; }
                   app::busy("正在联网对时…");
                   String why;
                   bool ok = net::connect(15000, &why) && net::syncTime(&why);
                   net::off();
                   const SysState &s = app::sys();
                   char b[32];
                   snprintf(b, sizeof(b), "%d-%02d-%02d %02d:%02d", s.now.year, s.now.month, s.now.day, s.now.hour, s.now.minute);
                   if (ok) app::toast("时间已校准", b);
                   else app::toast("对时失败", why);
                 }});
    if (net::hasCredentials())
      m.push_back({"忘记这个 Wi-Fi", nullptr, [this] {
                     app::push(newConfirmView("忘记 " + net::savedSsid(), "以后要重新配网", [] {
                       net::forget();
                       app::toast("已忘记");
                     }));
                   }});
  }

 private:
  void saved() {
    settingsSave();
    refresh();
  }
};
View *newWifiMenu() { return new WifiMenu(); }

// =====================================================================
// 检查新版本：只有点了才联网查；查到新版本先问，按确定才下载安装
// =====================================================================
static void installUpdate(const net::FwInfo &fw) {
  const SysState &s = app::sys();
  if (!s.usb && s.bat.ok && s.bat.soc < 30) {  // 下载、写 flash 要一分钟左右，中途没电会白下
    app::toast("电量不足", "充到 30% 以上或插上 USB 再升级", 3000);
    return;
  }
  ota::confirm("开始联网升级");  // 试运行中不能开始下一次升级（esp_ota_begin 会拒绝）
  app::busy("正在连接…");
  String why;
  if (!net::connect(15000, &why)) {
    net::off();
    app::toast("连不上 Wi-Fi", why, 3000);
    return;
  }
  setCpuFrequencyMhz(240);
  int shown = -1;
  bool ok = net::fwInstall(fw, [&shown](int p) {
    if (p / 10 == shown) return;  // 每 10% 局刷一次，刷屏太勤会拖慢下载
    shown = p / 10;
    char b[40];
    snprintf(b, sizeof(b), "正在下载新固件 %d%%", p);
    app::busy(b);
  }, &why);
  net::off();
  setCpuFrequencyMhz(CPU_MHZ);
  if (!ok) {
    app::toast("升级失败", why + "，还是原来的固件", 3000);
    return;
  }
  app::busy("升级完成，正在重启…");
  delay(500);
  ESP.restart();  // 软件重启：PWR_HOLD 的保持不会丢；新固件先试运行（core/ota.h）
}

static void checkUpdate() {
  if (!wifiAllowed()) return;
  if (!net::hasCredentials()) { app::toast("还没配网", "先在 Wi-Fi 菜单里配网"); return; }
  if (net::busy()) { app::toast("Wi-Fi 正忙", "过一会儿再试"); return; }
  app::busy("正在检查新版本…");
  String why;
  net::FwInfo fw;
  int r = net::connect(15000, &why) ? net::fwCheck(fw, &why) : -1;
  net::off();  // 先断开：用户考虑要不要装的时候不耗电，装的时候再连
  if (r < 0) { app::toast("检查失败", why, 3000); return; }
  if (r == 0) { app::toast("已是最新版本", "当前 " FW_VERSION); return; }
  String body = "当前 " FW_VERSION "。" + (fw.notes.length() ? fw.notes : String("（没有更新说明）"));
  app::push(newConfirmView("发现新版本 " + fw.version, body, [fw] { installUpdate(fw); }));
}

// =====================================================================
// 系统设置
// =====================================================================
class SystemSettings : public MenuView {
 public:
  SystemSettings() : MenuView("系统设置") {}
  const char *name() const override { return "system-settings"; }

 protected:
  void build(std::vector<MenuItem> &m) override {
    m.push_back({"Wi-Fi",
                 [] {
                   if (!settings().wifiOn) return String("关");
                   return net::hasCredentials() ? net::savedSsid() : String("未配网");
                 },
                 [] { app::push(newWifiMenu()); }});
    m.push_back({"横放自动切到桌面", [] { return onOff(settings().autoDesk); },
                 [this] { settings().autoDesk = !settings().autoDesk; saved(); }});
    m.push_back({"自动休眠",
                 [] {
                   int v = settings().deepSleepMin;
                   return v ? String(v) + " 分钟" : String("从不");
                 },
                 [this] {
                   settings().deepSleepMin = nextOf(std::vector<int>{5, 10, 30, 60, 0}, (int)settings().deepSleepMin);
                   saved();
                 }});
    m.push_back({"设置时间",
                 [] {
                   const SysState &s = app::sys();
                   if (!s.timeOk) return String("未对时");
                   char b[24];
                   snprintf(b, sizeof(b), "%d-%d %02d:%02d", s.now.month, s.now.day, s.now.hour, s.now.minute);
                   return String(b);
                 },
                 [] { app::push(newTimeSetView()); }});
    m.push_back({"姿态校准", [] { return String(settings().calibrated ? "已校准" : "默认"); }, [] { app::push(newCalView()); }});
    m.push_back({"全屏刷新（清残影）", nullptr, [] { app::invalidate(Refresh::Clean); }});
    m.push_back({"清除阅读缓存", nullptr, [] {
                   app::push(newConfirmView("清除阅读缓存", "每本书下次打开时重新导入、排版；阅读进度不受影响", [] {
                     int n = bookCacheClear();
                     if (bookIsOpen() && bookPath() != BUILTIN_PATH) {
                       app::busy("正在重新导入…");
                       reader::openBook(bookPath());  // 正在读的这本马上重建，不然翻页读不到正文
                     }
                     app::toast("阅读缓存已清除", String(n) + " 个文件");
                   }));
                 }});
    m.push_back({"关于本机", nullptr, [] { app::push(newInfoView()); }});
    m.push_back({"检查新版本", [] { return String(FW_VERSION); }, [] { checkUpdate(); }});
    m.push_back({"恢复默认设置", nullptr, [] {
                   app::push(newConfirmView("恢复默认设置", "字体、行距、休眠等恢复默认，阅读进度保留", [] {
                     if (tts::active()) tts::stop();
                     settingsReset();
                     app::applySettings();
                     reader::applyFont();
                     app::toast("已恢复默认设置");
                   }));
                 }});
  }

 private:
  void saved() {
    settingsSave();
    refresh();
  }
};
View *newSystemSettings() { return new SystemSettings(); }

// =====================================================================
// 书库
// =====================================================================
class LibraryView : public ListView {
 public:
  LibraryView() : ListView("书库") {
    cardOk_ = libraryScan();
    if (!cardOk_) title_ = "书库（没有 TF 卡）";
    const auto &b = libraryBooks();
    for (size_t i = 0; i < b.size(); i++)
      if (bookIsOpen() && b[i].path == bookPath()) cursor_ = i;
  }
  const char *name() const override { return "library"; }

 protected:
  String titleRight() const override { return String(libraryBooks().size()) + " 本"; }
  int count() const override { return libraryBooks().size(); }
  void row(int i, ListRow &r) const override {
    const BookEntry &b = libraryBooks()[i];
    r.label = b.title;
    r.marked = bookIsOpen() && b.path == bookPath();
    r.value = b.permille < 0 ? String("新") : b.permille >= 1000 ? String("读完") : pct(b.permille);
  }
  void select(int i) override {
    BookEntry b = libraryBooks()[i];
    if (!(bookIsOpen() && b.path == bookPath())) {
      app::busy("正在打开…");
      if (!reader::openBook(b.path)) {
        app::toast("打不开这本书", bookError(), 2500);
        return;
      }
    }
    app::enterReading();
  }

 private:
  bool cardOk_;
};
View *newLibraryView() { return new LibraryView(); }

// =====================================================================
// 目录
// =====================================================================
class TocView : public ListView {
 public:
  TocView() : ListView("目录") {
    int c = bookChapterAt(bookOffset());
    cursor_ = c < 0 ? 0 : c;
  }
  const char *name() const override { return "toc"; }

 protected:
  int count() const override { return bookChapters().size(); }
  void row(int i, ListRow &r) const override {
    r.label = bookChapterTitle(i);
    r.value = String(bookPageOf(bookChapters()[i].off) + 1);
    r.marked = i == bookChapterAt(bookOffset());
  }
  void select(int i) override {
    reader::gotoOffset(bookChapters()[i].off);
    app::popToRoot(Refresh::Full);
  }
};
View *newTocView() { return new TocView(); }
