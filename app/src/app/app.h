#pragma once
// 应用框架：界面栈、按键分发、姿态切换、刷新策略、省电。
//
// 界面（View）叠成一个栈：最底下是"根界面"（桌面或阅读），上面是菜单、目录、提示……
// 每个界面只管两件事：draw() 画自己，onKey() 处理按键；要重画就调 app::invalidate()。
// 什么时候全刷、什么时候局刷、什么时候睡，都由这里统一决定。
#include <Arduino.h>

#include "board/power.h"
#include "board/sensors.h"
#include "config.h"

enum class Key : uint8_t { Menu, Prev, Next };
enum class Press : uint8_t { Short, Long, Repeat };
// Full = 快速全刷（翻几页一次、切模式）；Clean = 标准全刷，清残影最彻底（电源键短按、菜单"全屏刷新"）
enum class Refresh : uint8_t { None, Partial, Full, Clean };

class View {
 public:
  virtual ~View() = default;
  virtual const char *name() const = 0;
  virtual void draw() = 0;
  virtual void onKey(Key k, Press p) { (void)k; (void)p; }
  virtual void onTap() {}
  virtual void onTick() {}            // 在栈顶时每轮主循环调一次
  virtual void onShow() {}            // 成为栈顶（刚压进来，或上面的界面关掉了）
  virtual bool isRoot() const { return false; }
  virtual bool overlay() const { return false; }  // 画在下面那个界面之上（提示框）
  virtual bool acceptsPose() const { return true; }  // false：姿态变化不切模式（校准时）
  virtual uint32_t idleMs() const { return IDLE_MENU_MS; }  // 空闲多久浅睡
  virtual int fullEvery() const { return MENU_FULL_EVERY; }  // 局刷几次后全刷一次
};

// 系统状态：传感器读数等，界面从这里取
struct SysState {
  DateTime now;
  bool timeOk;
  Battery bat;
  bool usb;
  bool co2Present;
  Climate clim;
};

namespace app {

struct BootInfo {
  bool keyBoot;      // 按电源键开机（这次按压松手前不算关机）
  WakeInfo wake;     // 从深睡醒来的原因
  bool hasAcc, hasRtc, hasGauge;
};
void begin(const BootInfo &b);
void loop();

// ---- 界面栈 ----
void push(View *v);                           // 接管 v 的所有权
void pop(Refresh r = Refresh::None);          // None = 自动：回到根界面全刷，否则局刷
void popToRoot(Refresh r = Refresh::Full);
void replace(View *v);                        // 换掉栈顶
View *top();
void drawBelow(const View *self);             // 提示框：先画下面那一层
void invalidate(Refresh r = Refresh::Partial);
void renderNow();                             // 马上画、马上刷（不等主循环）

void enterReading();                          // 手动切到阅读（不管姿态）
void enterDesk();
bool rootIsReader();

// ---- 提示 ----
void toast(const String &line1, const String &line2 = "", uint32_t ms = 1500);
void busy(const char *msg);                   // 在当前画面中间局刷一个提示框（长操作前用）

// ---- 状态 ----
const SysState &sys();
void refreshSensors(bool force);
uint8_t pose();
bool sleepBadge();                            // 阅读界面画"休眠中"标记
void markActivity();

void applySettings(bool initialPose = false); // 姿态轴向、敲击、局刷波形

// ---- 电源 ----
[[noreturn]] void shutdown(const char *reason = nullptr);
void deepSleepNow(uint32_t wakeSec = 0);      // 串口测试用
void setSleepForce(bool on);                  // 插着 USB 也睡（测试）
bool sleepForce();
struct SleepStat { uint32_t sleeps, msAsleep, byKey, byShock, byTimer, byUsb, deepWakes; };
const SleepStat &sleepStat();

}  // namespace app
