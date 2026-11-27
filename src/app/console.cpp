// 串口调试命令（整行输入；@ 开头的行是电脑上 keybrick 脚本发的，忽略）
#include <esp_heap_caps.h>

#include "app.h"
#include "board/audio.h"
#include "board/i2c.h"
#include "board/power.h"
#include "core/ota.h"
#include "core/settings.h"
#include "core/xfer.h"
#include "input/events.h"
#include "input/keys.h"
#include "input/motion.h"
#include "net/wifi.h"
#include "reader/book.h"
#include "reader/library.h"
#include "text/font.h"
#include "ui/gfx.h"
#include "ui/views.h"
#include "voice/tts.h"
#include "voice/edge_drm.h"

static String s_line;
static bool s_poseLog = false;
static uint32_t s_poseLogAt = 0;

static void printHelp() {
  Serial.println(
      "按键  n / p 下一页 / 上一页 | m 菜单 | k <m|u|d>[l] 模拟物理键（u 上键 d 下键，加 l 长按）| pwr 电源键短按\n"
      "界面  shot 截图（PBM，用 tools/shot.py 收）| full 快速全刷 | clean 标准全刷（清残影）| desk / read 切模式 | off 关机\n"
      "阅读  book 列书 | book <序号> 打开 | toc 目录 | goto <百分比> | font 字库 | font <字体> <字号>\n"
      "设置  set 看设置 | reset 恢复默认 | tap on/off | thr 0.6 | pf 20 10 局刷波形 | fullevery 6\n"
      "      autodesk on/off | deepmin 10（0 = 不深睡）\n"
      "姿态  pose | poselog | tapdbg on/off | keys 按键电平\n"
      "省电  sleep on/off 浅睡 | sleepforce on/off 插着 USB 也睡 | deep [秒] 马上深睡（测试）\n"
      "声音  beep [Hz] [ms] [音量%] 单音（默认 1000 500 30）| sweep [音量%] 扫频 100～8000Hz | scale [音量%] 音阶\n"
      "朗读  tts 状态 | tts model edge|plus | tts ws <业务空间ID>（clear 清除）| tts key sk-...（clear）\n"
      "      tts voice <名字>（不带参数列出来）| tts speed 1.25（0.5～2）| tts vol 70 | tts play 从本页念 | tts say 一段文字 | tts stop | tts next / prev\n"
      "其他  time 2026-11-08 21:30:00 对时 | info 状态 | mem 内存 | cache clear 清阅读缓存（重新导入）\n"
      "TF 卡 fls [目录] | frm <路径> | fdf | 传文件用电脑上的 tools/tfcard.py");
}

static bool onOffArg(const String &c, bool cur) {
  if (c.endsWith(" on")) return true;
  if (c.endsWith(" off")) return false;
  return !cur;
}

static void info() {
  const SysState &s = app::sys();
  const app::SleepStat &st = app::sleepStat();
  View *t = app::top();
  Serial.printf("固件 %s（编译 %s %s），%s\n", FW_VERSION, __DATE__, __TIME__, ota::state().c_str());
  Serial.printf("界面 %s，姿态 %s，%04d-%02d-%02d %02d:%02d:%02d%s\n", t ? t->name() : "-", poseName(app::pose()), s.now.year,
                s.now.month, s.now.day, s.now.hour, s.now.minute, s.now.second, s.timeOk ? "" : "（未对时）");
  Serial.printf("电池 %s%u mV %u%%，USB %s；CO2 传感器 %s\n", s.bat.ok ? "" : "（读不到）", s.bat.mv, s.bat.soc,
                s.usb ? "插着" : "没插", s.co2Present ? "在" : "未安装");
  if (bookIsOpen())
    Serial.printf("书《%s》%s，%u 章，第 %d / %d 页（%.1f%%）\n", bookTitle().c_str(), bookInfo().c_str(),
                  (unsigned)bookChapters().size(), bookPage() + 1, bookPageCount(), bookPermille() / 10.0);
  Serial.printf("字库 %s，字号 %d\n", gfx::bodyIsEfnt() ? gfx::bodyPath().c_str() : "系统点阵", gfx::bodySize());
  uint32_t up = millis();
  Serial.printf("浅睡 %s：睡了 %lu 次，累计 %lu 秒（开机 %lu 秒，占 %.0f%%）；叫醒：按键 %lu、敲击 %lu、定时 %lu、USB %lu；深睡醒来 %lu 次\n",
                settings().lightSleep ? "开" : "关", (unsigned long)st.sleeps, (unsigned long)(st.msAsleep / 1000),
                (unsigned long)(up / 1000), up ? 100.0 * st.msAsleep / up : 0.0, (unsigned long)st.byKey,
                (unsigned long)st.byShock, (unsigned long)st.byTimer, (unsigned long)st.byUsb, (unsigned long)st.deepWakes);
}

static void printSettings() {
  const Settings &s = settings();
  Serial.printf("字体 %s %d | 行距 %s | 边距 %s | 每 %d 页全刷 | 敲击 %s %.2fg | 局刷 %u/%u\n",
                s.fontFamily.length() ? s.fontFamily.c_str() : "（系统）", s.fontSize, lineSpacingName(s.lineSpacing),
                marginName(s.margin), s.fullEvery, s.tapTurn ? "开" : "关", s.tapThr, s.pfBlack, s.pfWhite);
  Serial.printf("横放切桌面 %s | 浅睡 %s | 深睡 %d 分钟 | 姿态 %s\n", s.autoDesk ? "开" : "关", s.lightSleep ? "开" : "关",
                s.deepSleepMin, s.calibrated ? "已校准" : "默认");
}

static void handle(String c) {
  c.trim();
  if (c.length() == 0 || c[0] == '@') return;
  if (xferCommand(c)) return;
  Settings &st = settings();
  if (c == "help" || c == "h") printHelp();
  else if (c == "tts") {
    Serial.printf("语音朗读：%s\n", tts::active() ? tts::statusText().c_str() : "没在朗读");
    Serial.printf("  模型 %s\n", tts::modelLabel(settings().ttsModel).c_str());
    Serial.printf("  音色 %s，语速 %.2f 倍，音量 %u%%，自动翻页 %s\n",
                  tts::voiceLabel(settings().ttsModel, settings().ttsVoice).c_str(), settings().ttsSpeed,
                  settings().ttsVolume, settings().ttsAutoPage ? "开" : "关");
    Serial.printf("  API Key %s；业务空间 ID %s；Wi-Fi %s\n", settings().ttsKey.length() ? "已设置" : "未设置",
                  settings().ttsWs.length() ? "已设置" : "未设置",
                  net::hasCredentials() ? net::savedSsid().c_str() : "未配网");
    if (tts::modelPrice(settings().ttsModel) <= 0) {
      Serial.printf("  本次已合成 %lu 字（免费）\n", (unsigned long)tts::charsUsed());
    } else {
      Serial.printf("  本次已合成 %lu 字，约 %.3f 元（按 %.2f 元/万字符算）\n", (unsigned long)tts::charsUsed(),
                    tts::charsUsed() * tts::modelPrice(settings().ttsModel) / 10000.0,
                    tts::modelPrice(settings().ttsModel));
    }
    if (tts::active())
      Serial.printf("  正在读正文 %lu 字节处（%.1f%%）\n", (unsigned long)tts::offset(),
                    bookSize() ? 100.0 * tts::offset() / bookSize() : 0.0);
  } else if (c.startsWith("tts model")) {
    String a = c.substring(9);
    a.trim();
    if (!a.length()) {
      for (int i = 0; i < tts::modelCount(); i++)
        Serial.printf("  %-28s %s\n", tts::modelId(i), tts::modelLabel(tts::modelId(i)).c_str());
      Serial.printf("现在：%s（别名 edge / plus 也能用）\n", tts::modelLabel(settings().ttsModel).c_str());
    } else {
      String m = (a.equalsIgnoreCase("edge") || a.equalsIgnoreCase("flash")) ? String(TTS_MODEL_EDGE)
                 : a.equalsIgnoreCase("plus")                            ? String(TTS_MODEL_PLUS)
                                                                         : a;
      settings().ttsModel = m;
      settings().ttsVoice = tts::defaultVoice(m);
      tts::setModel(m, settings().ttsWs);
      tts::setVoice(settings().ttsVoice);
      settingsSave();
      Serial.printf("模型：%s，音色 %s\n", tts::modelLabel(m).c_str(),
                    tts::voiceLabel(m, settings().ttsVoice).c_str());
      if (m == TTS_MODEL_PLUS && !settings().ttsWs.length())
        Serial.println("（plus 还需要业务空间 ID：tts ws <ID>）");
    }
  } else if (c.startsWith("tts ws")) {
    String a = c.substring(6);
    a.trim();
    if (a.equalsIgnoreCase("clear")) a = "";
    settings().ttsWs = a;
    settingsSave();
    tts::setModel(settings().ttsModel, settings().ttsWs);
    Serial.printf("业务空间 ID %s%s\n", a.length() ? "已保存：" : "已清除", a.length() ? a.c_str() : "");
  } else if (c.startsWith("tts key")) {
    String k = c.substring(7);
    k.trim();
    if (k.equalsIgnoreCase("clear")) k = "";
    settings().ttsKey = k;
    settingsSave();
    tts::setKey(k);
    Serial.printf("API Key %s（%u 个字符）\n", k.length() ? "已保存" : "已清除", (unsigned)k.length());
  } else if (c.startsWith("tts voice")) {
    String v = c.substring(9);
    v.trim();
    if (!v.length()) {
      for (int i = 0; i < tts::voiceCount(settings().ttsModel); i++) {
        const char *id = tts::voiceName(settings().ttsModel, i);
        Serial.printf("  %-22s %s\n", id, tts::voiceLabel(settings().ttsModel, id).c_str());
      }
    } else {
      settings().ttsVoice = v;
      settingsSave();
      tts::setVoice(v);
    }
    Serial.printf("音色：%s\n", tts::voiceLabel(settings().ttsModel, settings().ttsVoice).c_str());
  } else if (c.startsWith("tts vol")) {
    String a = c.substring(7);
    a.trim();
    if (a.length()) {
      int v = constrain(a.toInt(), 0, 100);
      settings().ttsVolume = (uint8_t)v;
      settingsSave();
      tts::setVolume(v);
    }
    Serial.printf("朗读音量：%u%%\n", settings().ttsVolume);
  } else if (c.startsWith("tts speed")) {
    String a = c.substring(9);
    a.trim();
    if (a.length()) {
      float v = constrain(a.toFloat(), TTS_SPEED_MIN, TTS_SPEED_MAX);
      settings().ttsSpeed = v;
      settingsSave();
      tts::setSpeed(v);
    }
    Serial.printf("朗读语速：%.2f 倍（下一句生效）\n", settings().ttsSpeed);
  } else if (c.startsWith("tts token")) {
    // 调试用：按某个时间戳算 Sec-MS-GEC 令牌，和电脑上 edge-tts 的输出对一下
    String a = c.substring(9);
    a.trim();
    uint32_t ts = a.length() ? (uint32_t)a.toInt() : (uint32_t)time(nullptr);
    Serial.printf("令牌 %s\n", tts::edgeSecMsGec(ts).c_str());
  } else if (c == "tts play") {
    String why;
    if (!tts::ready(&why)) {
      Serial.printf("还开始不了：%s\n", why.c_str());
    } else if (!bookIsOpen()) {
      Serial.println("先 book <序号> 打开一本书");
    } else {
      Serial.println(tts::start(bookOffset()) ? "开始朗读" : ("开不了：" + tts::lastError()).c_str());
    }
  } else if (c.startsWith("tts say ")) {
    String text = c.substring(8);
    Serial.printf("试听：%s\n", tts::say(text) ? "开始" : ("失败：" + tts::lastError()).c_str());
  } else if (c == "tts stop") {
    tts::stop();
    Serial.println("已停止");
  } else if (c == "tts next") {
    Serial.println(tts::nextSentence() ? "下一句" : "没有下一句");
  } else if (c == "tts prev") {
    Serial.println(tts::prevSentence() ? "上一句" : "没有上一句");
  } else if (c.startsWith("beep")) {
    if (tts::active()) tts::stop();  // I2S 是同一个外设，测试音前先把朗读停掉
    int hz = 1000, ms = 500, vol = 30;
    sscanf(c.c_str() + 4, "%d %d %d", &hz, &ms, &vol);
    hz = constrain(hz, 50, 8000); ms = constrain(ms, 10, 10000);
    Serial.printf("[声] 单音 %d Hz %d ms 音量 %d%%%s\n", hz, ms, vol, usbPresent() && vol > 70 ? "（插着 USB，按 70% 放）" : "");
    Serial.println(audioTone(hz, ms, vol) ? "ok" : "失败");
  } else if (c.startsWith("sweep")) {
    if (tts::active()) tts::stop();
    int vol = 30;
    sscanf(c.c_str() + 5, "%d", &vol);
    Serial.printf("[声] 扫频 100→8000 Hz 4 秒，音量 %d%%\n", vol);
    Serial.println(audioSweep(100, 8000, 4000, vol) ? "ok" : "失败");
  } else if (c.startsWith("scale")) {
    if (tts::active()) tts::stop();
    int vol = 30;
    sscanf(c.c_str() + 5, "%d", &vol);
    Serial.printf("[声] 音阶 C5～C6，音量 %d%%\n", vol);
    Serial.println(audioScale(vol) ? "ok" : "失败");
  }
  else if (c == "n") postEvent(EV_KEY_SHORT, PK_LOWER);
  else if (c == "p") postEvent(EV_KEY_SHORT, PK_UPPER);
  else if (c == "m") postEvent(EV_KEY_SHORT, PK_MENU);
  else if (c.startsWith("k ") && c.length() >= 3) {
    char ch = c[2];
    uint8_t pk = ch == 'm' ? PK_MENU : ch == 'u' ? PK_UPPER : PK_LOWER;
    postEvent(c.endsWith("l") ? EV_KEY_LONG : EV_KEY_SHORT, pk);
  } else if (c == "pwr") postEvent(EV_POWER_SHORT);
  else if (c == "shot") { gfx::screenshot(Serial); Serial.println(); }
  else if (c == "full") app::invalidate(Refresh::Full);
  else if (c == "clean") app::invalidate(Refresh::Clean);
  else if (c == "desk") app::enterDesk();
  else if (c == "read") app::enterReading();
  else if (c == "off") app::shutdown();
  else if (c == "offshot") {  // 只画关机画面（不关机），截图看效果；再发 full 回到原来的界面
    gfx::clear();
    drawShutdownScreen(nullptr);
    gfx::show(true);
  }
  else if (c == "book") {
    libraryScan();
    const auto &b = libraryBooks();
    for (size_t i = 0; i < b.size(); i++)
      Serial.printf("%2u %s%s  %s\n", (unsigned)i, b[i].path == bookPath() ? "● " : "  ", b[i].title.c_str(),
                    b[i].permille < 0 ? "新" : (String(b[i].permille / 10) + "%").c_str());
  } else if (c.startsWith("book ")) {
    libraryScan();
    int i = c.substring(5).toInt();
    const auto &b = libraryBooks();
    if (i >= 0 && i < (int)b.size()) {
      if (reader::openBook(b[i].path)) app::enterReading();
      else Serial.printf("打不开：%s\n", bookError().c_str());
    }
  } else if (c == "toc") {
    const auto &ch = bookChapters();
    for (size_t i = 0; i < ch.size() && i < 300; i++)
      Serial.printf("%4u  p%-5d %s\n", (unsigned)i, bookPageOf(ch[i].off) + 1, bookChapterTitle(i).c_str());
    Serial.printf("共 %u 章\n", (unsigned)ch.size());
  } else if (c.startsWith("goto ")) {
    float p = c.substring(5).toFloat();
    reader::gotoOffset((uint32_t)(bookSize() * constrain(p, 0.0f, 100.0f) / 100));
  } else if (c == "font") {
    fontScan();
    for (auto &f : fontFiles()) Serial.printf("  %s  %s %d\n", f.path.c_str(), fontFamilyLabel(f.family).c_str(), f.size);
    Serial.printf("当前：%s\n", gfx::bodyIsEfnt() ? gfx::bodyPath().c_str() : "系统点阵 16");
  } else if (c.startsWith("font ")) {
    char fam[32] = {0};
    int size = 0;
    if (sscanf(c.c_str() + 5, "%31s %d", fam, &size) >= 1) {
      st.fontFamily = strcmp(fam, "system") == 0 ? "" : fam;
      if (size) st.fontSize = size;
      if (!reader::applyFont()) Serial.println("换不了（内存不够？），还用原来的字库");
      if (app::rootIsReader()) reader::ensureLayout();  // 先排版（会弹提示），再刷新
      settingsSave();
      app::invalidate(Refresh::Full);
      Serial.printf("字库：%s\n", gfx::bodyIsEfnt() ? gfx::bodyPath().c_str() : "系统点阵 16");
    }
  } else if (c == "set") printSettings();
  else if (c == "reset") { settingsReset(); app::applySettings(); reader::applyFont(); app::invalidate(Refresh::Full); printSettings(); }
  else if (c.startsWith("pf")) {  // pf 20 10：局刷变黑帧数、变白额外帧数
    int b = 0, w = 0;
    if (sscanf(c.c_str(), "pf %d %d", &b, &w) == 2 && b >= 4 && b <= 60 && w >= 0 && w <= 60) {
      st.pfBlack = b;
      st.pfWhite = w;
      settingsSave();
      app::applySettings();
    }
    Serial.printf("局刷波形：变黑 %u 帧，变白额外 %u 帧（格式 pf 20 10）\n", st.pfBlack, st.pfWhite);
  } else if (c.startsWith("fullevery")) {
    int n = c.substring(9).toInt();
    if (n >= 1 && n <= 50) { st.fullEvery = n; settingsSave(); }
    Serial.printf("阅读时每 %d 页全刷一次\n", st.fullEvery);
  } else if (c.startsWith("autodesk")) {
    st.autoDesk = onOffArg(c, st.autoDesk);
    settingsSave();
    Serial.printf("横放自动切桌面：%s\n", st.autoDesk ? "开" : "关");
  } else if (c.startsWith("deepmin")) {
    int n = c.substring(7).toInt();
    if (n >= 0 && n <= 240) { st.deepSleepMin = n; settingsSave(); }
    Serial.printf("空闲 %d 分钟深睡%s\n", st.deepSleepMin, st.deepSleepMin ? "" : "（关）");
  } else if (c == "pose") {
    float t, k, n;
    motionGravity(t, k, n);
    Serial.printf("上方轴 %+.2f  按键轴 %+.2f  法线 %+.2f  → %s%s\n", t, k, n, poseName(motionPose()), motionFlat() ? "（平放）" : "");
  } else if (c == "poselog") s_poseLog = !s_poseLog;
  else if (c == "posedesk") postEvent(EV_POSE, POSE_DESK);  // 调试：模拟翻到横放
  else if (c == "poseread") postEvent(EV_POSE, POSE_READ);  // 调试：模拟翻回竖拿
  else if (c == "cal") app::push(newCalView());
  else if (c == "wifi") app::push(newWifiSetupView());
  else if (c == "xfer") app::push(newTransferView());
  else if (c.startsWith("sleepforce")) {
    app::setSleepForce(onOffArg(c, app::sleepForce()));
    Serial.printf("插着 USB 也睡：%s\n", app::sleepForce() ? "开（睡着时 USB 串口会断开，醒来再连）" : "关");
  } else if (c.startsWith("sleep")) {
    st.lightSleep = onOffArg(c, st.lightSleep);
    settingsSave();
    Serial.printf("自动浅睡：%s\n", st.lightSleep ? "开" : "关");
  } else if (c.startsWith("deep")) {
    int sec = c.length() > 5 ? c.substring(5).toInt() : 0;
    Serial.printf("马上深睡%s\n", sec ? "，定时醒来" : "（阅读：按键叫醒；桌面：下一分钟醒来）");
    app::deepSleepNow(sec);
  } else if (c == "keys") {
    const int pins[4] = {PIN_KEY_MENU, PIN_KEY_PREV, PIN_KEY_NEXT, PIN_PWR_KEY_N};
    const char *names[4] = {"SW6 菜单 GPIO7", "SW1 上一页 GPIO6", "SW2 下一页 GPIO5", "SW3 电源 GPIO1"};
    for (int i = 0; i < 4; i++) Serial.printf("  %-18s %s\n", names[i], digitalRead(pins[i]) ? "1 松开" : "0 按下/被拉低");
  } else if (c.startsWith("tapdbg")) { motionTapDebug(c.endsWith("on")); Serial.println("ok"); }
  else if (c.startsWith("tap")) {
    st.tapTurn = onOffArg(c, st.tapTurn);
    settingsSave();
    app::applySettings();
    Serial.printf("敲击翻页：%s\n", st.tapTurn ? "开" : "关");
  } else if (c.startsWith("thr ")) {
    float g = c.substring(4).toFloat();
    if (g > 0.05f && g < 4) { st.tapThr = g; settingsSave(); app::applySettings(); }
    Serial.printf("敲击门限 %.2fg\n", motionTapThr());
  } else if (c.startsWith("time ")) {
    int y, mo, d, hh, mm, ss = 0;
    if (sscanf(c.c_str() + 5, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss) >= 5) {
      DateTime t{(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)hh, (uint8_t)mm, (uint8_t)ss, 0};
      rtcWrite(t);
      app::refreshSensors(true);
      app::invalidate(Refresh::Partial);
      Serial.println("已对时");
    } else Serial.println("格式：time 2026-11-08 21:30:00");
  } else if (c == "i2c") {  // 扫 I2C 总线，看各器件在不在（U8 加速度计 0x19 / 0x18，U6 时钟 0x51，U10 电量计 0x62，U7 CO2 0x64）
    for (uint8_t a = 1; a < 0x78; a++)
      if (i2cPing(a)) Serial.printf("  0x%02X 有应答\n", a);
    for (uint8_t a : {0x18, 0x19}) {
      uint8_t who = 0;
      if (regRead(a, 0x0F, &who, 1)) Serial.printf("  0x%02X 的 WHO_AM_I（0x0F）= 0x%02X（SC7A20H 应为 0x11）\n", a, who);
    }
    Serial.println("扫完");
  } else if (c == "info") info();
  else if (c == "cache clear") {  // 删掉卡上的阅读缓存，正在读的书马上重新导入
    Serial.printf("删了 %d 个缓存文件\n", bookCacheClear());
    if (bookIsOpen() && bookPath() != BUILTIN_PATH) reader::openBook(bookPath());
    app::invalidate(Refresh::Full);
  } else if (c == "mem") {
    Serial.printf("PSRAM 空 %u / %u，内部空 %u（最大块 %u）\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  } else Serial.println("不认识的命令，输入 help 看列表");
}

void consolePoll() {
  while (Serial.available()) {
    char ch = Serial.read();
    app::markActivity();
    if (ch == '\r' || ch == '\n') {
      if (s_line.length()) handle(s_line);
      s_line = "";
    } else if (s_line.length() < 400) {
      s_line += ch;  // 文件路径可能比较长
    }
  }
  if (s_poseLog && millis() - s_poseLogAt > 500) {
    s_poseLogAt = millis();
    float t, k, n;
    motionGravity(t, k, n);
    Serial.printf("上方轴 %+.2f  按键轴 %+.2f  法线 %+.2f  → %s\n", t, k, n, poseName(motionPose()));
  }
}

void consoleHelp() { printHelp(); }
