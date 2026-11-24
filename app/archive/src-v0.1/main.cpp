// 墨水屏监控器与阅读器 · 主程序 v0.1
//   横放（按键朝上）→ 桌面模式：时间、电量、CO2 / 温湿度
//   竖拿（按键在右）→ 阅读模式：TF 卡里的 TXT，按键翻页，后背双击翻下一页
//   倒着拿（按键在左）→ 画面转 180°，上一页 / 下一页自动对调
// 交互、时序按 eink-docs 固件页。省电（深睡）和 Wi-Fi 下一版再加。

#include <Arduino.h>
#include <Preferences.h>

#include "driver/gpio.h"
#include "esp_sleep.h"

#include "book.h"
#include "config.h"
#include "events.h"
#include "hw.h"
#include "keys.h"
#include "motion.h"
#include "font.h"
#include "ui.h"
#include "xfer.h"

#include <algorithm>

QueueHandle_t g_events;

enum Screen : uint8_t { SCR_DESK, SCR_READ, SCR_MENU };

static Screen s_screen = SCR_DESK;
static Screen s_menuReturn = SCR_READ;
static uint8_t s_pose = POSE_DESK;     // 当前用来显示的姿态（不会是 UNKNOWN）
static int s_partials = 0;             // 上次全刷以后局刷了几次
static int s_readFullEvery = READ_FULL_EVERY;  // 阅读时每翻几页全刷一次（串口 fullevery 可改，存 NVS）
static bool s_needFull = true;
static uint32_t s_lastPageTurn = 0;

static DateTime s_now{};
static Battery s_bat{};
static Climate s_clim{};
static uint32_t s_gaugeAt = 0, s_co2At = 0, s_rtcAt = 0;
static int s_lastMinute = -1;
static bool s_hasRtc, s_hasGauge, s_hasAcc;

static std::vector<MenuItem> s_menu;
static int s_menuCursor = 0;
static Preferences s_prefs;
static void startCal();
static void markActivity();

// 浅睡状态（逻辑在下面"省电"一节）
static bool s_sleepOn = true;          // 串口 sleep on/off，存 NVS
static bool s_sleepForce = false;      // 串口 sleepforce on：插着 USB 也睡（测试用，USB 串口会断开）
static uint32_t s_lastAct = 0;         // 最近一次有动作（事件、刷屏、串口输入）的时间
static uint32_t s_awakeUntil = 0;      // 醒来后至少保持清醒到这个时间
struct SleepStat { uint32_t sleeps, msAsleep, byKey, byShock, byTimer, byUsb; };
static SleepStat s_st{};

// ---------------------------------------------------------------------
static uint8_t rotationFor(uint8_t pose) {
  switch (pose) {
    case POSE_DESK_FLIP: return ROT_DESK_FLIP;
    case POSE_READ: return ROT_READ;
    case POSE_READ_FLIP: return ROT_READ_FLIP;
    default: return ROT_DESK;
  }
}

// 物理键 → 逻辑键：倒着拿时上下两个键对调
enum LogicKey : uint8_t { LK_MENU, LK_PREV, LK_NEXT };
static LogicKey logical(uint8_t pk) {
  if (pk == PK_MENU) return LK_MENU;
  bool flip = s_pose == POSE_READ_FLIP || s_pose == POSE_DESK_FLIP;
  bool upper = pk == PK_UPPER;
  return (upper != flip) ? LK_PREV : LK_NEXT;
}

static void readSensors(bool force) {
  uint32_t now = millis();
  if (s_hasRtc && (force || now - s_rtcAt >= 1000)) {
    s_rtcAt = now;
    rtcRead(s_now);
  }
  // 电量计刚唤醒时 VCELL 还是 0，2 秒后补读一次
  bool gaugeStale = s_bat.ok && s_bat.mv == 0 && now - s_gaugeAt >= 2000;
  if (s_hasGauge && (force || gaugeStale || now - s_gaugeAt >= GAUGE_POLL_MS)) {
    s_gaugeAt = now;
    gaugeRead(s_bat);
  }
  // CO2 只在桌面模式测（拿在手里会被呼气和体温影响）
  if (co2Present() && s_screen == SCR_DESK && (force || now - s_co2At >= CO2_POLL_MS)) {
    s_co2At = now;
    co2Start();
  }
  Climate c;
  if (co2Poll(c)) {
    s_clim = c;
    Serial.printf("[环境] CO2 %d ppm  %.1f°C  %.0f%%\n", c.co2, c.temp, c.rh);
  }
}

// ---------------------------------------------------------------------
// 画当前界面并刷新
// ---------------------------------------------------------------------
static void render(bool forceFull = false) {
  uiSetRotation(rotationFor(s_pose));
  int fullEvery = s_screen == SCR_READ ? s_readFullEvery : DESK_FULL_EVERY;
  bool full = forceFull || s_needFull || s_partials >= fullEvery;
  switch (s_screen) {
    case SCR_DESK: {
      DeskData d{s_now, rtcTimeTrusted(), s_bat, usbPresent(), co2Present(), s_clim};
      uiDrawDesk(d);
      break;
    }
    case SCR_READ: uiDrawReading(s_now, s_bat); break;
    case SCR_MENU: uiDrawMenu("菜单", s_menu, s_menuCursor); break;
  }
  uiShow(full);
  if (full) { s_partials = 0; s_needFull = false; }
  else s_partials++;
  markActivity();  // 刷完屏从这时开始算空闲
}

static void openReading() {
  bookPaginate(uiReadingLayout());
  s_screen = SCR_READ;
}

// 进入某个姿态对应的界面
static void applyPose(uint8_t pose) {
  if (pose == POSE_UNKNOWN) return;
  bool rotChanged = rotationFor(pose) != rotationFor(s_pose);
  bool wasReadingDims = poseIsReading(s_pose);
  s_pose = pose;
  uiSetRotation(rotationFor(pose));
  if (poseIsReading(pose)) {
    if (!wasReadingDims || s_screen != SCR_READ) openReading();  // 版面尺寸变了要重新分页
    s_screen = SCR_READ;
  } else {
    s_screen = SCR_DESK;
    readSensors(true);
  }
  if (rotChanged) s_needFull = true;
  s_needFull = true;  // 切模式全刷（文档）
  Serial.printf("[姿态] %s → %s模式\n", poseName(pose), s_screen == SCR_READ ? "阅读" : "桌面");
  render(true);
}

// ---------------------------------------------------------------------
// 菜单
// ---------------------------------------------------------------------
// 字库：NVS 里记着选的是哪个文件。卡上有字库时只在卡上的字号之间轮换（默认 18 号）；
// 记住的那个文件不在了（比如换了一套字库）就退回默认；卡上一个都没有才用固件里的文泉驿 16
static String defaultFont() {
  const auto &files = fontFiles();
  for (auto &f : files)
    if (f.endsWith("_18.efnt")) return f;
  return files.empty() ? String() : files[files.size() / 2];
}

static void applyFontPref() {
  const auto &files = fontFiles();
  String want = s_prefs.getString("font", "");
  if (std::find(files.begin(), files.end(), want) == files.end()) want = defaultFont();
  if (!fontLoad(want)) fontLoad("");
}

static void cycleFont() {
  const auto &files = fontFiles();
  std::vector<String> opts(files.begin(), files.end());
  if (opts.empty()) opts.push_back("");
  auto it = std::find(opts.begin(), opts.end(), fontPath());
  size_t i = it == opts.end() ? 0 : (it - opts.begin() + 1) % opts.size();
  // 不在这里存进度：NVS 里是上次真正翻页时的字节位置，换几次字号都按它找页，不会越换越往前漂
  if (!fontLoad(opts[i])) fontLoad("");
  s_prefs.putString("font", fontPath());
  bookPaginate(uiReadingLayout());
  s_needFull = true;
  Serial.printf("[字库] 切换到 %s\n", fontLoaded() ? fontPath().c_str() : "系统 16");
}

static int s_menuBookBase = 0;  // 书在菜单里的起始下标
static void buildMenu() {
  s_menu.clear();
  for (int i = 0; i < bookCount(); i++) {
    String t = (i == bookCurrentIndex() ? "● " : "　") + bookTitleAt(i);
    s_menu.push_back({t});
  }
  s_menu.push_back({String(bookCurrentIndex() == -1 ? "● " : "　") + "内置：" + bookTitleAt(-1)});
  s_menu.push_back({String("敲击后背翻页：") + (motionTapEnabled() ? "开" : "关")});
  s_menu.push_back({String("字号：") + (fontLoaded() ? String(fontSize()) + "（霞鹜文楷）" : String("16（系统）")) +
                    (fontFiles().empty() ? "" : "，按确定切换")});
  s_menu.push_back({"切到桌面模式"});
  s_menu.push_back({"姿态校准"});
  s_menu.push_back({"返回"});
  s_menuBookBase = 0;
}

static void openMenu() {
  s_menuReturn = s_screen == SCR_MENU ? s_menuReturn : s_screen;
  bookRescan();  // 没有插卡检测：每次进菜单看一眼卡，换卡、加书后打开菜单就能看到
  fontScan();
  buildMenu();
  s_menuCursor = bookCurrentIndex() >= 0 ? bookCurrentIndex() : bookCount();
  s_screen = SCR_MENU;
  render();
}

static void closeMenu() {
  s_screen = s_menuReturn;
  render();
}

static void menuSelect() {
  int n = bookCount();
  int c = s_menuCursor;
  if (c <= n) {  // 选书（含内置）
    int idx = c < n ? c : -1;
    if (idx != bookCurrentIndex()) {
      if (!bookOpen(idx)) {
        uiDrawMessage("打不开这本书", bookStatus().c_str());
        uiShow(false);
        delay(1500);
        render();
        return;
      }
    }
    s_menuReturn = SCR_READ;
    if (!poseIsReading(s_pose)) s_pose = POSE_READ;
    openReading();
    s_needFull = true;
    render();
    return;
  }
  if (c == n + 1) {  // 敲击开关
    bool on = !motionTapEnabled();
    motionTapEnable(on);
    s_prefs.putBool("tap", on);
    buildMenu();
    render();
    return;
  }
  if (c == n + 2) {  // 字号：在"文泉驿 16"和卡上的各个字库之间轮换
    cycleFont();
    buildMenu();
    render();
    return;
  }
  if (c == n + 3) {  // 手动切到桌面
    s_screen = SCR_DESK;
    readSensors(true);
    s_needFull = true;
    render();
    return;
  }
  if (c == n + 4) {  // 姿态校准
    s_screen = s_menuReturn;
    startCal();
    return;
  }
  closeMenu();
}

// ---------------------------------------------------------------------
// 姿态校准：两步，按菜单键确认，结果存 NVS
// ---------------------------------------------------------------------
static int s_cal = 0;  // 0 = 没在校准，1 / 2 = 第几步
static int s_calTop = -1, s_calTopSign = 1;

static void calPrompt(const char *l1, const char *l2, bool full) {
  uiDrawMessage(l1, l2);
  uiShow(full);
}

static void startCal() {
  s_cal = 1;
  Serial.println("[校准] 第 1 步：竖拿，按键在右手边，Type-C 朝下，拿稳后按菜单键（长按菜单取消）");
  calPrompt("校准 1/2：竖着拿，按键在右手边、Type-C 朝下", "拿稳后按一下菜单键", true);
}

// 当前重力最大的轴和方向；|g| 不够大说明没拿正
static bool dominantAxis(int &ax, int &sign, float &mag) {
  float g[3];
  motionRaw(g);
  ax = 0;
  for (int i = 1; i < 3; i++)
    if (fabsf(g[i]) > fabsf(g[ax])) ax = i;
  sign = g[ax] >= 0 ? 1 : -1;
  mag = fabsf(g[ax]);
  Serial.printf("[校准] X %+.2f  Y %+.2f  Z %+.2f → 轴 %c%c\n", g[0], g[1], g[2], sign > 0 ? '+' : '-', "XYZ"[ax]);
  return mag > 0.75f;
}

static void calStep() {
  int ax, sign;
  float mag;
  if (!dominantAxis(ax, sign, mag)) {
    calPrompt("没有拿正（要竖直立着，别斜）", s_cal == 1 ? "竖着拿，按键在右，Type-C 朝下，再按菜单键" : "横放，按键那条边朝上，再按菜单键", false);
    return;
  }
  if (s_cal == 1) {
    s_calTop = ax;
    s_calTopSign = sign;
    s_cal = 2;
    Serial.println("[校准] 第 2 步：横放，按键那条边朝上，拿稳后按菜单键");
    calPrompt("校准 2/2：横过来立着，按键那条边朝上", "拿稳后按一下菜单键", false);
    return;
  }
  if (ax == s_calTop) {
    calPrompt("两次方向一样，没有转过去", "横放，按键那条边朝上，再按菜单键", false);
    return;
  }
  motionSetAxes(s_calTop, s_calTopSign, ax, sign);
  s_prefs.putInt("axT", s_calTop);
  s_prefs.putInt("sgT", s_calTopSign);
  s_prefs.putInt("axK", ax);
  s_prefs.putInt("sgK", sign);
  s_cal = 0;
  Serial.printf("[校准] 完成：上方 = %c%c，按键方向 = %c%c，已保存\n", s_calTopSign > 0 ? '+' : '-', "XYZ"[s_calTop],
                sign > 0 ? '+' : '-', "XYZ"[ax]);
  calPrompt("姿态校准完成", "现在是横放，进入桌面模式", false);
  delay(1200);
  applyPose(POSE_DESK);
}

// ---------------------------------------------------------------------
// 翻页
// ---------------------------------------------------------------------
static int s_pendingTurn = 0;
static void queueTurn(int d, bool fromShortPress) {
  uint32_t now = millis();
  if (fromShortPress && now - s_lastPageTurn < KEY_MIN_GAP_MS) return;  // 抖动造成的连按丢掉
  s_lastPageTurn = now;
  s_pendingTurn += d;
}

static void flushTurns() {
  if (!s_pendingTurn || s_screen != SCR_READ) { s_pendingTurn = 0; return; }
  int target = bookPage() + s_pendingTurn;
  s_pendingTurn = 0;
  int before = bookPage();
  bookGoto(target);
  if (bookPage() == before) return;  // 已经是第一页 / 最后一页
  readSensors(false);
  render();
}

// ---------------------------------------------------------------------
// 关机
// ---------------------------------------------------------------------
static void shutdown() {
  Serial.println("[电源] 关机");
  uiSetRotation(rotationFor(s_pose));
  uiBanner("正在关机…", false);  // 先马上给个反应，再全刷关机画面
  readSensors(true);
  uiDrawShutdown(s_bat, s_now, rtcTimeTrusted(), bookTitle(), bookPage(), bookPageCount());
  uiShow(true);
  gaugeSleep();
  ampOff();
  sdPower(false);
  while (powerKeyDown()) delay(10);  // 按着键时 ON 被按键拉着，要等松手才断得掉
  delay(50);
  powerCut();
  // 还活着：R33 点了锡，或开发时一直供电
  Serial.println("[电源] 断不了电（R33 跳线或一直有电），进入待机：长按电源键 3 秒重新开机");
  for (;;) {
    uint32_t t0 = millis();
    while (powerKeyDown()) {
      if (millis() - t0 > PWR_LONG_MS) {
        powerHoldOn();
        ESP.restart();
      }
      delay(10);
    }
    delay(50);
  }
}

// 开机：电源键从上电起要按满 3 秒才算开机；提前松手就断电（文档 v4.26）
static bool bootPowerKeyCheck() {
  bool held = powerKeyDown();
  if (!held) return false;  // 不是按键开机（R33 / 软件重启）
  while (powerKeyDown() && millis() < PWR_LONG_MS) delay(10);
  if (millis() < PWR_LONG_MS) {
    Serial.println("[电源] 电源键没按满 3 秒，关机");
    powerCut();
    Serial.println("[电源] 没断掉（R33 或一直有电），继续开机");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------
// 串口调试命令（整行输入；@ 开头的行是电脑上 keybrick 脚本发的，忽略）
// ---------------------------------------------------------------------
static String s_line;
static bool s_poseLog = false;
static uint32_t s_poseLogAt = 0;

static void printHelp() {
  Serial.println(
    "命令：n 下一页 | p 上一页 | m 菜单 | full 全刷 | desk / read 切模式\n"
    "      pose 姿态读数 | poselog 连续打印姿态 | cal 姿态校准 | keys 按键电平\n"
    "      tap on/off | tapdbg on/off | thr 0.35 敲击门限\n"
    "      sleep on/off 自动浅睡 | sleepforce on/off 插着 USB 也睡（测试）\n"
    "      fls [目录] 列 TF 卡 | frm <路径> 删除 | fdf 剩余空间 | 传文件用电脑上的 tools/tfcard.py\n"
    "      font 当前字库 | font next 换字号 | font scan 重新读卡上的字库\n"
    "      pf 20 10 局刷波形（变黑帧数 变白额外帧数）| fullevery 6 每几页全刷\n"
    "      time 2026-11-08 21:30:00 对时 | info 状态 | off 关机");
}

static void handleCommand(String c) {
  c.trim();
  if (c.length() == 0 || c[0] == '@') return;
  if (xferCommand(c)) return;  // fput / fls / frm / fdf：通过 USB 串口管理 TF 卡上的文件
  if (c == "help" || c == "h") printHelp();
  else if (c == "n") { queueTurn(+1, false); }
  else if (c == "p") { queueTurn(-1, false); }
  else if (c == "m") { s_screen == SCR_MENU ? closeMenu() : openMenu(); }
  else if (c == "full") render(true);
  else if (c.startsWith("pf")) {  // pf 20 10：局刷变黑帧数、变白额外帧数
    int b = 0, w = 0;
    if (sscanf(c.c_str(), "pf %d %d", &b, &w) == 2 && b >= 4 && b <= 60 && w >= 0 && w <= 60) {
      s_prefs.putUChar("pfB", b);
      s_prefs.putUChar("pfW", w);
      uiSetPartialFrames(b, w);
    }
    Serial.printf("局刷波形：变黑 %u 帧，变白额外 %u 帧（格式 pf 20 10）\n", s_prefs.getUChar("pfB", EPD_PARTIAL_FRAMES),
                  s_prefs.getUChar("pfW", EPD_PARTIAL_WHITE_EXTRA));
  } else if (c.startsWith("fullevery")) {
    int n = c.substring(9).toInt();
    if (n >= 1 && n <= 50) { s_readFullEvery = n; s_prefs.putInt("fullN", n); }
    Serial.printf("阅读时每 %d 页全刷一次\n", s_readFullEvery);
  }
  else if (c == "font" || c == "font next" || c == "font scan") {
    if (c == "font scan") { fontScan(); applyFontPref(); bookPaginate(uiReadingLayout()); s_needFull = true; render(); }
    if (c == "font next") { cycleFont(); if (s_screen == SCR_MENU) buildMenu(); render(); }
    Serial.printf("字库：%s（卡上 %u 个）\n", fontLoaded() ? fontPath().c_str() : "系统 16", (unsigned)fontFiles().size());
  }
  else if (c == "desk") applyPose(POSE_DESK);
  else if (c == "read") applyPose(POSE_READ);
  else if (c == "off") shutdown();
  else if (c == "pose") {
    float t, k, n;
    motionGravity(t, k, n);
    Serial.printf("上方轴 %+.2f  按键轴 %+.2f  法线 %+.2f  → %s%s\n", t, k, n, poseName(motionPose()),
                  motionFlat() ? "（平放）" : "");
  } else if (c == "poselog") { s_poseLog = !s_poseLog; }
  else if (c == "cal") startCal();
  else if (c.startsWith("sleepforce")) {
    s_sleepForce = c.endsWith("on");
    Serial.printf("插着 USB 也睡：%s%s\n", s_sleepForce ? "开" : "关", s_sleepForce ? "（睡着时 USB 串口会断开，醒来再连）" : "");
  } else if (c.startsWith("sleep")) {
    s_sleepOn = !c.endsWith("off");
    s_prefs.putBool("sleep", s_sleepOn);
    Serial.printf("自动浅睡：%s\n", s_sleepOn ? "开" : "关");
  }
  else if (c == "keys") {
    // 各键当前电平：1 = 松开（上拉），0 = 按下或被拉低
    const int pins[4] = {PIN_KEY_MENU, PIN_KEY_PREV, PIN_KEY_NEXT, PIN_PWR_KEY_N};
    const char *names[4] = {"SW6 菜单 GPIO7", "SW1 上一页 GPIO6", "SW2 下一页 GPIO5", "SW3 电源 GPIO1"};
    for (int i = 0; i < 4; i++) Serial.printf("  %-18s %s\n", names[i], digitalRead(pins[i]) ? "1 松开" : "0 按下/被拉低");
  }
  else if (c.startsWith("tapdbg")) { motionTapDebug(c.endsWith("on")); Serial.println("ok"); }
  else if (c.startsWith("tap")) {
    bool on = c.endsWith("on");
    motionTapEnable(on);
    s_prefs.putBool("tap", on);
    Serial.printf("敲击翻页：%s\n", on ? "开" : "关");
  } else if (c.startsWith("thr ")) {
    float g = c.substring(4).toFloat();
    if (g > 0.05f && g < 4) { motionSetTapThr(g); s_prefs.putFloat("thr", g); }
    Serial.printf("敲击门限 %.2fg\n", motionTapThr());
  } else if (c.startsWith("time ")) {
    int y, mo, d, hh, mm, ss = 0;
    if (sscanf(c.c_str() + 5, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss) >= 5) {
      struct tm tm = {};
      tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = 12;
      mktime(&tm);
      DateTime t{(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)hh, (uint8_t)mm, (uint8_t)ss, (uint8_t)tm.tm_wday};
      rtcWrite(t);
      readSensors(true);
      Serial.println("已对时");
      if (s_screen == SCR_DESK) render();
    } else Serial.println("格式：time 2026-11-08 21:30:00");
  } else if (c == "info") {
    Serial.printf("模式 %s，姿态 %s，%04d-%02d-%02d %02d:%02d:%02d%s\n", s_screen == SCR_READ ? "阅读" : s_screen == SCR_DESK ? "桌面" : "菜单",
                  poseName(s_pose), s_now.year, s_now.month, s_now.day, s_now.hour, s_now.minute, s_now.second,
                  rtcTimeTrusted() ? "" : "（未对时）");
    Serial.printf("电池 %s %u mV %u%%，USB %s\n", s_bat.ok ? "" : "（读不到）", s_bat.mv, s_bat.soc, usbPresent() ? "插着" : "没插");
    Serial.printf("CO2 传感器 %s；书《%s》第 %d / %d 页；敲击翻页 %s，门限 %.2fg\n", co2Present() ? "在" : "未安装",
                  bookTitle().c_str(), bookPage() + 1, bookPageCount(), motionTapEnabled() ? "开" : "关", motionTapThr());
    uint32_t up = millis();
    Serial.printf("浅睡 %s：睡了 %lu 次，累计 %lu 秒（开机 %lu 秒，睡着占 %.0f%%）；叫醒：按键 %lu、敲击 %lu、定时 %lu、USB %lu\n",
                  s_sleepOn ? "开" : "关", (unsigned long)s_st.sleeps, (unsigned long)(s_st.msAsleep / 1000), (unsigned long)(up / 1000),
                  up ? 100.0 * s_st.msAsleep / up : 0.0, (unsigned long)s_st.byKey, (unsigned long)s_st.byShock,
                  (unsigned long)s_st.byTimer, (unsigned long)s_st.byUsb);
  } else Serial.println("不认识的命令，输入 help 看列表");
}

static void pollSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    markActivity();
    if (ch == '\r' || ch == '\n') {
      if (s_line.length()) handleCommand(s_line);
      s_line = "";
    } else if (s_line.length() < 400) s_line += ch;  // 文件路径可能比较长
  }
}

// ---------------------------------------------------------------------
// 省电：浅睡（light sleep）。内存、PSRAM、屏的帧缓存都保留，醒来几毫秒，翻页照样局刷。
// 插着 USB 不睡（串口、烧录），用电池时空闲就睡；按键、插 USB、后背敲击、定时器（每 2 秒）叫醒。
// ---------------------------------------------------------------------
static void markActivity() { s_lastAct = millis(); }

static uint32_t idleLimit() {
  return s_screen == SCR_READ ? IDLE_READ_MS : s_screen == SCR_MENU ? IDLE_MENU_MS : IDLE_DESK_MS;
}

// 所有键都松开才睡；卡住的键（硬件粘连、被东西顶着）不算，否则整板永远睡不了
static bool keysReleased() {
  for (int p : {PIN_KEY_MENU, PIN_KEY_PREV, PIN_KEY_NEXT, PIN_PWR_KEY_N})
    if (!keyStuck(p) && digitalRead(p) == LOW) return false;
  return true;
}

static void wakePin(int pin, gpio_int_type_t level) {
  gpio_sleep_sel_dis((gpio_num_t)pin);  // 睡着时保持正常的输入配置（不被隔离），才能当唤醒源
  gpio_wakeup_enable((gpio_num_t)pin, level);
}

static void maybeSleep() {
  uint32_t now = millis();
  bool usb = usbPresent();
  if (!s_sleepOn || s_cal || (usb && !s_sleepForce)) return;
  if (now - s_lastAct < idleLimit() || (int32_t)(now - s_awakeUntil) < 0) return;
  if (!keysReleased() || uxQueueMessagesWaiting(g_events) || s_pendingTurn) return;

  // 敲击唤醒只在阅读界面、敲击翻页打开时用
  bool shock = s_screen == SCR_READ && motionTapEnabled() && s_hasAcc;
  motionPause(true);
  if (shock) shock = motionShockArm();  // INT2 还是高（刚被敲过）就先不用它

  for (int p : {PIN_KEY_MENU, PIN_KEY_PREV, PIN_KEY_NEXT, PIN_PWR_KEY_N}) {
    if (keyStuck(p)) gpio_wakeup_disable((gpio_num_t)p);  // 卡住的键一直是低，当唤醒源会刚睡就醒
    else wakePin(p, GPIO_INTR_LOW_LEVEL);
  }
  if (!usb) wakePin(PIN_USB_PRES, GPIO_INTR_HIGH_LEVEL); else gpio_wakeup_disable((gpio_num_t)PIN_USB_PRES);
  if (shock) wakePin(PIN_ACC_INT2, GPIO_INTR_HIGH_LEVEL); else gpio_wakeup_disable((gpio_num_t)PIN_ACC_INT2);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TICK_MS * 1000);

  Serial.flush();
  uint32_t t0 = millis();
  esp_light_sleep_start();
  uint32_t t1 = millis();
  s_st.sleeps++;
  s_st.msAsleep += t1 - t0;

  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  if (why == ESP_SLEEP_WAKEUP_TIMER) {
    s_st.byTimer++;
    s_awakeUntil = t1 + TICK_AWAKE_MS;           // 只醒一下：让姿态任务采几次样、主循环看看分钟
  } else if (shock && digitalRead(PIN_ACC_INT2)) {
    s_st.byShock++;
    motionSeedTap();                              // 这一下当作双击的第一下，接着等第二下
    s_awakeUntil = t1 + TAP_WAIT_AFTER_SHOCK_MS;
  } else if (usbPresent() && !usb) {
    s_st.byUsb++;
    markActivity();
  } else {
    s_st.byKey++;
    s_awakeUntil = t1 + 300;                      // 按键事件随后到，事件里会再续上空闲时间
  }
  motionPause(false);
}

// ---------------------------------------------------------------------
static void handleEvent(const Event &e) {
  markActivity();
  if (e.type == EV_KEY_SHORT || e.type == EV_KEY_LONG || e.type == EV_KEY_REPEAT) {
    static const char *kn[] = {"菜单", "上一页", "下一页"};
    static const char *tn[] = {"短按", "长按", "连发"};
    Serial.printf("[键] %s %s（物理键 %s）\n", tn[e.type], kn[logical(e.a)], e.a == PK_MENU ? "SW6" : e.a == PK_UPPER ? "SW1" : "SW2");
  }
  if (s_cal) {  // 校准中：只认菜单键（短按确认、长按取消）和电源长按
    if (e.type == EV_KEY_SHORT && e.a == PK_MENU) calStep();
    else if (e.type == EV_KEY_LONG && e.a == PK_MENU) {
      s_cal = 0;
      Serial.println("[校准] 取消");
      s_needFull = true;
      render();
    } else if (e.type == EV_POWER_LONG) shutdown();
    return;
  }
  switch (e.type) {
    case EV_POWER_LONG: shutdown(); break;
    case EV_POSE: {
      // 姿态没变、界面也已经对应：不重刷（比如校准后姿态任务再报一次）
      bool same = e.a == s_pose && ((poseIsReading(e.a) && s_screen == SCR_READ) || (poseIsDesk(e.a) && s_screen == SCR_DESK));
      if (same) break;
      if (s_screen == SCR_MENU) s_screen = SCR_READ;
      applyPose(e.a);
      break;
    }
    case EV_TAP2:
      if (s_screen == SCR_READ) { Serial.println("[敲击] 双击 → 下一页"); queueTurn(+1, false); }
      break;
    case EV_KEY_SHORT: {
      LogicKey k = logical(e.a);
      if (s_screen == SCR_MENU) {
        if (k == LK_MENU) menuSelect();
        else {
          int n = s_menu.size();
          s_menuCursor = (s_menuCursor + (k == LK_NEXT ? 1 : -1) + n) % n;
          render();
        }
      } else if (s_screen == SCR_READ) {
        if (k == LK_MENU) openMenu();
        else queueTurn(k == LK_NEXT ? +1 : -1, true);
      } else {  // 桌面：短按菜单 = 立即刷新一次读数
        if (k == LK_MENU) { readSensors(true); render(); }
      }
      break;
    }
    case EV_KEY_LONG: {
      LogicKey k = logical(e.a);
      if (k == LK_MENU) {
        if (s_screen == SCR_MENU) closeMenu();
        else if (s_screen == SCR_DESK) {  // 桌面模式长按菜单：切到阅读（文档）
          if (!poseIsReading(s_pose)) s_pose = POSE_READ;
          openReading();
          s_needFull = true;
          render();
        } else {  // 阅读模式长按菜单：文档里是朗读，还没做，先切到桌面
          s_screen = SCR_DESK;
          readSensors(true);
          s_needFull = true;
          render();
        }
      } else if (s_screen == SCR_READ) {
        queueTurn(k == LK_NEXT ? +1 : -1, false);  // 长按：连续翻页
      }
      break;
    }
    case EV_KEY_REPEAT:
      if (s_screen == SCR_READ) queueTurn(logical(e.a) == LK_NEXT ? +1 : -1, false);
      break;
  }
}

// ---------------------------------------------------------------------
void setup() {
  powerHoldOn();
  ampOff();
  pinMode(PIN_PWR_KEY_N, INPUT);
  pinMode(PIN_USB_PRES, INPUT);
  Serial.setRxBufferSize(24576);  // 默认 256 字节，满了会丢数据；传文件时一次在途 16KB（xfer.cpp CHUNK）
  Serial.begin(115200);
  g_events = xQueueCreate(32, sizeof(Event));

  // 等电源键按满 3 秒的同时把姿态和屏准备好（只读不写，这段时间随时可能断电）
  i2cBegin();
  s_hasAcc = motionBegin();
  uiBegin();
  bool keyBoot = bootPowerKeyCheck();
  if (keyBoot) {  // 按满 3 秒：马上局刷"正在开机"，让人知道可以松手了
    uint8_t p = s_hasAcc ? motionPose() : POSE_UNKNOWN;
    uiSetRotation(rotationFor(p == POSE_UNKNOWN ? POSE_DESK : p));
    uiBanner("正在开机…", true);
  }

  s_hasRtc = rtcBegin();
  s_hasGauge = gaugeBegin();
  bool co2 = co2Begin();
  s_prefs.begin("app", false);
  if (s_prefs.isKey("axT")) {  // 用校准过的轴向
    motionSetAxes(s_prefs.getInt("axT"), s_prefs.getInt("sgT"), s_prefs.getInt("axK"), s_prefs.getInt("sgK"));
    delay(60);  // 等姿态任务按新轴向算一轮
  }
  motionTapEnable(s_prefs.getBool("tap", true));
  motionSetTapThr(s_prefs.getFloat("thr", TAP_THR_DEFAULT));
  if (s_hasAcc) motionShockConfig(SHOCK_WAKE_G);
  s_sleepOn = s_prefs.getBool("sleep", true);
  s_readFullEvery = s_prefs.getInt("fullN", READ_FULL_EVERY);
  uiSetPartialFrames(s_prefs.getUChar("pfB", EPD_PARTIAL_FRAMES), s_prefs.getUChar("pfW", EPD_PARTIAL_WHITE_EXTRA));
  Serial.printf("\n[开机] 时钟 %s，电量计 %s，CO2 %s，加速度计 %s\n", s_hasRtc ? "OK" : "无", s_hasGauge ? "OK" : "无",
                co2 ? "OK" : "未安装", s_hasAcc ? "OK" : "无");

  readSensors(true);
  bookStorageBegin();
  fontScan();
  applyFontPref();   // 要在分页之前：字号决定版面
  bookOpenLast();

  xQueueReset(g_events);  // 开机过程中攒下的姿态事件不要了，下面直接按当前姿态进界面
  keysBegin(keyBoot);
  uint8_t p = s_hasAcc ? motionPose() : POSE_UNKNOWN;
  if (p == POSE_UNKNOWN) p = POSE_DESK;  // 平放或读不到姿态：先进桌面
  applyPose(p);
  printHelp();
  setCpuFrequencyMhz(CPU_MHZ);
  markActivity();
}

void loop() {
  pollSerial();
  Event e;
  while (xQueueReceive(g_events, &e, 0) == pdTRUE) handleEvent(e);
  flushTurns();

  readSensors(false);
  if (s_screen == SCR_DESK && s_hasRtc && s_now.minute != s_lastMinute) {
    bool first = s_lastMinute < 0;
    s_lastMinute = s_now.minute;
    if (!first) render();  // 每分钟局刷一次
  }
  if (s_poseLog && millis() - s_poseLogAt > 500) {
    s_poseLogAt = millis();
    float t, k, n;
    motionGravity(t, k, n);
    Serial.printf("上方轴 %+.2f  按键轴 %+.2f  法线 %+.2f  → %s\n", t, k, n, poseName(motionPose()));
  }
  maybeSleep();
  delay(5);
}
