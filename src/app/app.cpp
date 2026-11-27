#include "app.h"

#include <vector>

#include "board/storage.h"
#include "core/ota.h"
#include "core/settings.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "input/events.h"
#include "input/keys.h"
#include "input/motion.h"
#include "net/wifi.h"
#include "reader/library.h"
#include "ui/gfx.h"
#include "ui/views.h"
#include "voice/tts.h"

void consolePoll();  // console.cpp

namespace app {

// ---------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------
static std::vector<View *> s_stack;
static std::vector<View *> s_graveyard;  // 关掉的界面，本轮主循环结束再 delete（可能正在它自己的 onKey 里）
static Refresh s_dirty = Refresh::None;
static int s_partials = 0;               // 上次全刷以后局刷了几次
static uint8_t s_pose = POSE_DESK;       // 当前用来显示的姿态（不会是 UNKNOWN）
static BootInfo s_boot{};

static SysState s_sys{};
static uint32_t s_gaugeAt = 0, s_co2At = 0, s_rtcAt = 0;
static bool s_co2Pending = false;
static bool s_co2Now = false;            // 下一轮就测一次 CO2（不等 CO2_POLL_MS）
static int s_lowBat = 0;

static bool s_sleepBadge = false, s_sleepForce = false;
static uint32_t s_lastAct = 0, s_lastRefresh = 0, s_awakeUntil = 0;
static SleepStat s_st{};

// 深睡时 RTC 内存里留着的东西：醒来按它重画睡前那一帧当作局刷基准
struct Resume {
  uint32_t magic;
  uint8_t root;      // 0 桌面 1 阅读
  uint8_t pose;
  uint8_t partials;
  bool regime;       // 处在"深睡状态"：定时醒来更新完就接着睡，直到有人操作
  uint8_t co2Wakes;  // 深睡定时醒了几次没测 CO2
  uint32_t deepWakes;
  SysState drawn;    // 最后一帧画的是哪个状态
};
static const uint32_t RESUME_MAGIC = 0x45494E4B;
RTC_DATA_ATTR static Resume r_res;

// ---------------------------------------------------------------------
// 姿态、方向、按键映射
// ---------------------------------------------------------------------
bool rootIsReader() { return !s_stack.empty() && strcmp(s_stack.front()->name(), "reader") == 0; }

static uint8_t rotation() {
  if (rootIsReader()) return s_pose == POSE_READ_FLIP ? ROT_READ_FLIP : ROT_READ;
  return s_pose == POSE_DESK_FLIP ? ROT_DESK_FLIP : ROT_DESK;
}

// 物理键 → 逻辑键：倒着拿时上下两个键对调
static Key mapKey(uint8_t pk) {
  if (pk == PK_MENU) return Key::Menu;
  uint8_t r = rotation();
  bool flip = r == ROT_READ_FLIP || r == ROT_DESK_FLIP;
  return ((pk == PK_UPPER) != flip) ? Key::Prev : Key::Next;
}

uint8_t pose() { return s_pose; }
bool sleepBadge() { return s_sleepBadge; }
const SysState &sys() { return s_sys; }
void markActivity() { s_lastAct = millis(); }
const SleepStat &sleepStat() { return s_st; }
void setSleepForce(bool on) { s_sleepForce = on; }
bool sleepForce() { return s_sleepForce; }

// ---------------------------------------------------------------------
// 界面栈
// ---------------------------------------------------------------------
View *top() { return s_stack.empty() ? nullptr : s_stack.back(); }

void invalidate(Refresh r) {
  if (r > s_dirty) s_dirty = r;  // None < Partial < Full < Clean
}

void push(View *v) {
  s_stack.push_back(v);
  v->onShow();
  invalidate(Refresh::Partial);
}

void pop(Refresh r) {
  if (s_stack.size() <= 1) return;
  s_graveyard.push_back(s_stack.back());
  s_stack.pop_back();
  View *t = top();
  t->onShow();
  invalidate(r != Refresh::None ? r : t->isRoot() ? Refresh::Full : Refresh::Partial);
}

void popToRoot(Refresh r) {
  while (s_stack.size() > 1) {
    s_graveyard.push_back(s_stack.back());
    s_stack.pop_back();
  }
  if (top()) top()->onShow();
  invalidate(r);
}

void replace(View *v) {
  if (!s_stack.empty()) {
    s_graveyard.push_back(s_stack.back());
    s_stack.pop_back();
  }
  push(v);
}

static void setRoot(View *v) {
  for (View *x : s_stack) s_graveyard.push_back(x);
  s_stack.clear();
  s_stack.push_back(v);
  gfx::setRotation(rotation());
  v->onShow();
  invalidate(Refresh::Full);  // 切模式全刷（文档）
  if (!rootIsReader()) s_co2Now = true;
  Serial.printf("[界面] %s，%s\n", v->name(), poseName(s_pose));
}

void enterReading() {
  if (!poseIsReading(s_pose)) s_pose = POSE_READ;
  if (!rootIsReader()) setRoot(newReaderView());
  else popToRoot(Refresh::Full);
}

void enterDesk() {
  if (tts::active()) tts::stop();  // 切到桌面就不念了（省电）
  if (!poseIsDesk(s_pose)) s_pose = POSE_DESK;
  if (rootIsReader()) setRoot(newDeskView());
  else popToRoot(Refresh::Full);
}

void drawBelow(const View *self) {
  for (size_t i = 1; i < s_stack.size(); i++)
    if (s_stack[i] == self) { s_stack[i - 1]->draw(); return; }
}

static void render() {
  View *t = top();
  if (!t) return;
  gfx::setRotation(rotation());
  bool clean = s_dirty == Refresh::Clean;
  bool full = clean || s_dirty == Refresh::Full || s_partials >= t->fullEvery();
  // 快速全刷（假装高温的短波形）冷天驱动不到位、残影也清得不彻底：
  // 室温低于 FAST_FULL_MIN_C 时不用；每 FAST_FULL_MAX 次快刷换一次标准全刷
  static int fastFulls = 0;
  if (full && !clean) {
    bool cold = s_sys.clim.ok && s_sys.clim.temp < FAST_FULL_MIN_C;
    if (cold || ++fastFulls >= FAST_FULL_MAX) clean = true;
  }
  if (clean) fastFulls = 0;
  s_dirty = Refresh::None;
  gfx::clear();
  t->draw();
  if (clean) gfx::showClean();
  else gfx::show(full);
  if (full) s_partials = 0;
  else s_partials++;
  r_res.drawn = s_sys;
  s_lastRefresh = millis();  // 时钟和传感器也会刷屏，只延后浅睡。
}

void renderNow() {
  if (s_dirty == Refresh::None) s_dirty = Refresh::Partial;
  render();
}

void toast(const String &a, const String &b, uint32_t ms) { push(newMessageView(a, b, ms)); }

void busy(const char *msg) {
  gfx::setRotation(rotation());
  drawBusyBox(msg);
  gfx::show(false);
  s_partials++;
}

// ---------------------------------------------------------------------
// 传感器
// ---------------------------------------------------------------------
void refreshSensors(bool force) {
  uint32_t now = millis();
  s_sys.usb = usbPresent();
  if (s_boot.hasRtc && (force || now - s_rtcAt >= 1000)) {
    s_rtcAt = now;
    rtcRead(s_sys.now);
    s_sys.timeOk = rtcTimeTrusted();
  }
  // 电量计刚唤醒时 VCELL 还是 0，2 秒后补读一次
  bool gaugeStale = s_sys.bat.ok && s_sys.bat.mv == 0 && now - s_gaugeAt >= 2000;
  if (s_boot.hasGauge && (force || gaugeStale || now - s_gaugeAt >= GAUGE_POLL_MS)) {
    s_gaugeAt = now;
    gaugeRead(s_sys.bat);
    // 低电量：用电池、连续两次读数都低才关机（一次可能是翻页时的压降）
    if (!s_sys.usb && s_sys.bat.ok && s_sys.bat.mv > 2500 && s_sys.bat.mv < BAT_CUTOFF_MV) {
      if (++s_lowBat >= 2) shutdown("电量低，请充电");
    } else {
      s_lowBat = 0;
    }
  }
  s_sys.co2Present = co2Present();
  // CO2 只在桌面模式测（拿在手里会被呼气和体温影响）
  if (s_sys.co2Present && !rootIsReader() && !s_co2Pending && (s_co2Now || now - s_co2At >= CO2_POLL_MS)) {
    s_co2At = now;
    s_co2Now = false;
    co2Start();
    s_co2Pending = true;
  }
  Climate c;
  if (co2Poll(c)) {
    s_co2Pending = false;
    bool changed = !s_sys.clim.ok || c.co2 != s_sys.clim.co2 || (int)(c.temp * 10) != (int)(s_sys.clim.temp * 10) ||
                   (int)c.rh != (int)s_sys.clim.rh;
    s_sys.clim = c;
    Serial.printf("[环境] CO2 %d ppm  %.1f°C  %.0f%%\n", c.co2, c.temp, c.rh);
    if (changed && !rootIsReader()) invalidate(Refresh::Partial);
  } else if (s_co2Pending && now - s_co2At > 3000) {
    s_co2Pending = false;  // 没读回来，算了
  }
}

// ---------------------------------------------------------------------
// 姿态
// ---------------------------------------------------------------------
static void onPose(uint8_t p) {
  if (p == POSE_UNKNOWN) return;
  View *t = top();
  if (t && !t->acceptsPose()) return;
  uint8_t oldRot = rotation();
  if (poseIsReading(p)) {
    s_pose = p;
    if (!rootIsReader()) { setRoot(newReaderView()); return; }
  } else {
    if (rootIsReader() && !settings().autoDesk) return;  // 躺着看书：设置里关了自动切桌面
    if (tts::active()) tts::stop();  // 切到桌面就不念了
    s_pose = p;
    if (rootIsReader()) { setRoot(newDeskView()); return; }
  }
  if (rotation() != oldRot) invalidate(Refresh::Full);
}

// ---------------------------------------------------------------------
// 事件
// ---------------------------------------------------------------------
static void handle(const Event &e) {
  markActivity();
  r_res.regime = false;
  View *t = top();
  if (!t) return;
  switch (e.type) {
    case EV_KEY_SHORT:
    case EV_KEY_LONG:
    case EV_KEY_REPEAT: {
      static const char *kn[] = {"菜单", "上一页", "下一页"};
      static const char *tn[] = {"短按", "长按", "连发"};
      Key k = mapKey(e.a);
      Serial.printf("[键] %s %s（%s）\n", tn[e.type], kn[(int)k], t->name());
      t->onKey(k, e.type == EV_KEY_SHORT ? Press::Short : e.type == EV_KEY_LONG ? Press::Long : Press::Repeat);
      break;
    }
    case EV_POWER_SHORT:
      Serial.println("[键] 电源键短按 → 全刷清残影");
      invalidate(Refresh::Clean);
      break;
    case EV_POWER_LONG: shutdown(); break;
    case EV_TAP2: Serial.println("[敲击] 双击"); t->onTap(); break;
    case EV_POSE: onPose(e.a); break;
  }
}

// ---------------------------------------------------------------------
// 关机
// ---------------------------------------------------------------------
void shutdown(const char *reason) {
  if (tts::active()) tts::stop();  // 先停下来，别念着关机
  Serial.printf("[电源] 关机%s%s\n", reason ? "：" : "", reason ? reason : "");
  if (rootIsReader()) reader::saveProgress();
  gfx::setRotation(rotation());
  if (!reason) busy("正在关机…");  // 先马上给个反应，再全刷关机画面
  refreshSensors(true);
  gfx::clear();
  drawShutdownScreen(reason);
  gfx::show(true);
  r_res.magic = 0;
  gaugeSleep();
  ampOff();
  sdPower(false);
  while (powerKeyDown()) delay(10);  // 按着键时 ON 被按键拉着，要等松手才断得掉
  delay(50);
  ota::confirm("关机");
  powerCut();
  // 还活着：R33 点了锡，或开发时一直供电
  Serial.println("[电源] 断不了电（R33 跳线或一直有电），待机：长按电源键 3 秒重新开机");
  for (;;) {
    uint32_t t0 = millis();
    while (powerKeyDown()) {
      if (millis() - t0 > PWR_LONG_MS) {
        powerHoldOn();
        ota::confirm("重启");
        ESP.restart();
      }
      delay(10);
    }
    delay(50);
  }
}

// ---------------------------------------------------------------------
// 省电
// ---------------------------------------------------------------------
static bool onBattery() { return !usbPresent() || s_sleepForce; }

static void wakePin(int pin, gpio_int_type_t level) {
  gpio_sleep_sel_dis((gpio_num_t)pin);  // 睡着时保持正常的输入配置（不被隔离），才能当唤醒源
  gpio_wakeup_enable((gpio_num_t)pin, level);
}

// 浅睡（light sleep）：内存、PSRAM、屏的帧缓存都保留，醒来几毫秒，翻页照样局刷
static void lightSleep() {
  bool usb = usbPresent();
  View *t = top();
  bool shock = rootIsReader() && t->isRoot() && settings().tapTurn;
  if (s_boot.hasAcc && !motionPause(true)) {
    motionPause(false);
    s_awakeUntil = millis() + TAP_WAIT_AFTER_SHOCK_MS;
    return;
  }
  // 采样任务在暂停前可能刚刚识别了敲击。先处理已识别的双击，或继续等第二下。
  if (uxQueueMessagesWaiting(g_events)) {
    motionPause(false);
    return;
  }
  if (shock) {
    uint32_t now = millis();
    uint32_t wait = s_boot.hasAcc ? motionTapWaitMs(now) : 0;
    if (wait || !s_boot.hasAcc || !motionShockArm()) {
      // 第一击还在配对窗口内，或唤醒源没准备好时，保持软件采样。
      motionPause(false);
      s_awakeUntil = wait ? now + wait : millis() + TAP_WAIT_AFTER_SHOCK_MS;
      return;
    }
  }

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
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  s_st.sleeps++;
  s_st.msAsleep += t1 - t0;

  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  if (why == ESP_SLEEP_WAKEUP_TIMER) {
    s_st.byTimer++;
    s_awakeUntil = t1 + TICK_AWAKE_MS;           // 只醒一下：让姿态任务采几次样、主循环看看分钟
  } else if (why == ESP_SLEEP_WAKEUP_GPIO && shock && motionSeedTap(t1)) {
    s_st.byShock++;
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

static void enterDeepSleep(uint32_t wakeSec) {
  bool reader = rootIsReader();
  Serial.printf("[睡] 深睡（%s）%s\n", reader ? "阅读" : "桌面", wakeSec ? "，定时醒来更新" : "，按键叫醒");
  if (reader) {  // 页脚画上"休眠中"，让人知道按键要等一下
    reader::saveProgress();
    s_sleepBadge = true;
    s_dirty = Refresh::Partial;
    render();
  }
  r_res.magic = RESUME_MAGIC;
  r_res.root = reader ? 1 : 0;
  r_res.pose = s_pose;
  r_res.partials = s_partials;
  r_res.regime = true;
  if (s_boot.hasAcc) motionLowPower();
  ampOff();
  ota::confirm("深睡");  // 没确认就深睡，醒来时 bootloader 会当成没跑稳而退回旧版
  powerDeepSleep(wakeSec * 1000, true, true);
}

void deepSleepNow(uint32_t wakeSec) {
  if (tts::active()) tts::stop();
  if (!rootIsReader() && wakeSec == 0) wakeSec = 60 - s_sys.now.second;
  popToRoot(Refresh::None);
  enterDeepSleep(wakeSec);
}

static void maybeSleep() {
  uint32_t now = millis();
  View *t = top();
  if (!t || !onBattery()) return;
  if (net::busy()) return;  // Wi-Fi 在用（配网、传输、后台对时）：睡了就断了
  if (s_dirty != Refresh::None || uxQueueMessagesWaiting(g_events) || !keysAllReleased() || s_co2Pending) return;

  // 定时醒来更新完就继续深睡；正常运行时从最后一次操作开始算，不把刷屏算进去。
  uint32_t deepMs = (uint32_t)settings().deepSleepMin * 60000UL;
  bool deep = r_res.regime || (deepMs && now - s_lastAct >= deepMs);
  if (deep && (int32_t)(now - s_awakeUntil) >= 0) {
    if (!t->isRoot()) { popToRoot(Refresh::Full); return; }  // 菜单开着就先关掉，画完下一轮再睡
    uint32_t wake = rootIsReader() ? 0 : (uint32_t)(60 - s_sys.now.second % 60) + 1;
    enterDeepSleep(wake);
  }

  if (!settings().lightSleep) return;
  uint32_t idle = t->idleMs();
  if (now - s_lastAct < idle || now - s_lastRefresh < idle || (int32_t)(now - s_awakeUntil) < 0) return;
  lightSleep();
}

// ---------------------------------------------------------------------
// 启动、主循环
// ---------------------------------------------------------------------
void applySettings(bool initialPose) {
  Settings &s = settings();
  if (s.calibrated) motionSetAxes(s.axTop, s.sgTop, s.axKey, s.sgKey, initialPose);
  motionTapEnable(s.tapTurn);
  motionSetTapThr(s.tapThr);
  gfx::d().setPartialFrames(s.pfBlack, s.pfWhite);
}

void begin(const BootInfo &b) {
  s_boot = b;
  settingsLoad();
  libraryBegin();
  applySettings(true);
  refreshSensors(true);

  bool resumed = b.wake.reason != WAKE_NONE && r_res.magic == RESUME_MAGIC;
  xQueueReset(g_events);  // 开机过程中攒下的事件不要了
  uint8_t p = b.hasAcc ? motionPose() : POSE_UNKNOWN;
  if (resumed) {
    s_st.deepWakes = ++r_res.deepWakes;
    s_pose = r_res.pose;
    s_partials = r_res.partials;
    View *root = r_res.root ? newReaderView() : newDeskView();
    s_stack.push_back(root);
    // 重画睡前那一帧，当作局刷的基准（屏上现在就是它）
    SysState real = s_sys;
    s_sys = r_res.drawn;
    s_sleepBadge = r_res.root == 1;
    gfx::setRotation(rotation());
    gfx::clear();
    root->draw();
    gfx::d().assumeShown();
    s_sys = real;
    s_sleepBadge = false;
    root->onShow();
    Serial.printf("[睡] 从深睡醒来（第 %lu 次）：%s\n", (unsigned long)r_res.deepWakes,
                  b.wake.reason == WAKE_TIMER ? "定时" : b.wake.reason == WAKE_KEY ? "按键" : b.wake.reason == WAKE_USB ? "插 USB" : "其他");
    if (b.wake.reason != WAKE_TIMER) r_res.regime = false;
    if (p != POSE_UNKNOWN && p != s_pose) { r_res.regime = false; onPose(p); }
    invalidate(Refresh::Partial);
    // 叫醒它的那个键当作按了一下（按键事件在醒来的过程中已经错过了）
    if (b.wake.reason == WAKE_KEY) {
      uint8_t pk = keyFromPin(b.wake.pin);
      if (pk <= PK_LOWER) postEvent(EV_KEY_SHORT, pk);
    }
    // 定时醒来的桌面：每 5 分钟测一次 CO2；有人操作就马上测
    s_co2At = millis();
    if (!r_res.regime) s_co2Now = true;
    else if (++r_res.co2Wakes >= 5) { r_res.co2Wakes = 0; s_co2Now = true; }
  } else {
    r_res = Resume{};
    s_pose = p == POSE_UNKNOWN ? POSE_DESK : p;  // 平放或读不到姿态：先进桌面
    setRoot(poseIsReading(s_pose) ? newReaderView() : newDeskView());
  }
  if (!s_co2At) s_co2At = millis();
  markActivity();
}

// 开机没认到加速度计（总线被卡住等）：运行中每 10 秒再找一次，找到就把姿态和敲击检测接上
static void retryMotion() {
  static uint32_t last = 0;
  if (s_boot.hasAcc || millis() - last < 10000) return;
  last = millis();
  if (!motionBegin()) return;
  s_boot.hasAcc = true;
  applySettings(true);
  uint8_t p = motionPose();
  if (p != POSE_UNKNOWN && p != s_pose) onPose(p);
  Serial.println("[姿态] 加速度计找到了，姿态和敲击检测已启用");
}

// 每天自动联网对时（后台任务）：Wi-Fi 开着、有账号、电量够（插着电或电池 ≥ 3.5V，文档：低于它停止联网）、
// 没在看菜单时才做
static void autoTime() {
  const Settings &st = settings();
  bool power = s_sys.usb || (s_sys.bat.ok && s_sys.bat.mv >= 3500);
  View *t = top();
  bool allowed = st.wifiOn && st.autoTime && net::hasCredentials() && power && t && t->isRoot();
  net::autoSyncTick(s_sys.now, s_sys.timeOk, allowed);
  bool ok;
  if (net::autoSyncResult(ok)) {
    Serial.printf("[网] 自动对时%s\n", ok ? "成功" : "失败（这个小时内不再试）");
    if (ok) {
      refreshSensors(true);
      if (!rootIsReader()) invalidate(Refresh::Partial);
    }
  }
}

void loop() {
  consolePoll();
  retryMotion();
  motionSetReadingUi(rootIsReader());
  autoTime();
  Event e;
  while (xQueueReceive(g_events, &e, 0) == pdTRUE) handle(e);
  tts::tick();  // 按句取文、自动翻页、收尾（在渲染之前，翻页这一轮就画出来）
  String ttsMsg;
  switch (tts::pollEvent(ttsMsg)) {
    case tts::EV_ERROR: toast("朗读出错", ttsMsg, 4000); break;
    case tts::EV_DONE: toast("朗读结束", ttsMsg, 1500); break;
    default: break;
  }
  if (View *t = top()) t->onTick();
  refreshSensors(false);
  if (s_dirty != Refresh::None) render();
  for (View *v : s_graveyard) delete v;
  s_graveyard.clear();
  maybeSleep();
  delay(5);
}

}  // namespace app
