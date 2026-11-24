#pragma once
// 电源：PWR_HOLD 自锁、电源键、USB 检测、功放、TF 卡电源、深睡前后的引脚保持。
#include <Arduino.h>

void powerHoldOn();                 // 拉高 GPIO4 并 gpio_hold（开机第一件事）
bool powerKeyDown();
bool usbPresent();
// 断电：放开 PWR_HOLD。按着电源键或者 R33 点了锡时断不掉，函数会返回。
void powerCut();

void ampOff();
void sdPower(bool on);

// 深睡：wakeMs = 0 不定时醒；keys = 按键（含电源键）能叫醒；usb = 插 USB 能叫醒
[[noreturn]] void powerDeepSleep(uint32_t wakeMs, bool keys, bool usb);
// 醒来后放开深睡时保持住的引脚
void powerReleaseHolds();

// 这次启动是不是从深睡醒来，以及叫醒的原因
enum WakeReason : uint8_t { WAKE_NONE, WAKE_TIMER, WAKE_KEY, WAKE_USB, WAKE_OTHER };
struct WakeInfo {
  WakeReason reason;
  int pin;  // WAKE_KEY 时是哪个脚（GPIO 号），否则 -1
};
WakeInfo powerWakeInfo();
