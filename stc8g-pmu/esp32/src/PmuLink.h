// ESP32-S3 和 STC8G 电源管理 MCU 的串口通讯（Arduino）
// 协议见 ../../stc/pmu.c 文件头。9600 8N1，用 UART1，引脚任选（不要用 GPIO0/3/45/46/19/20）。
#pragma once
#include <Arduino.h>
#include <functional>

struct PmuStatus {
  bool usb = false;         // 插着 USB
  bool charging = false;    // 正在充电
  bool full = false;        // 插着 USB 且没在充电（已充满）
  bool lowBattery = false;  // 不插 USB 且电压低于 3.3V
  uint16_t vccMv = 0;       // 不插 USB 时约等于电池电压；插着 USB 时是 VSYS，不代表电池
  uint8_t bootReason = 0;   // PmuLink::BOOT_KEY / BOOT_TIMER
};

class PmuLink {
 public:
  static constexpr uint8_t BOOT_KEY = 1;
  static constexpr uint8_t BOOT_TIMER = 2;
  static constexpr uint8_t OFF_BY_KEY = 1;
  static constexpr uint8_t OFF_BY_LOW_BATTERY = 2;

  void begin(HardwareSerial &port, int rxPin, int txPin);
  void poll();  // 在 loop() 里一直调用
  void requestStatus();

  // 通知 STC 断电。调用前先保存数据、让屏幕休眠；STC 收到后约 10ms 断开 3.3V。
  // wakeAfterMinutes = 0 表示只能按键开机，否则到时间 STC 会自动重新上电（误差约 ±10%）。
  void powerOff(uint16_t wakeAfterMinutes = 0);

  bool hasStatus() const { return hasStatus_; }
  const PmuStatus &status() const { return status_; }

  std::function<void(const PmuStatus &)> onStatus;
  std::function<void()> onShortPress;
  // 收到关机请求后要在 5 秒内调用 powerOff()，否则 STC 强制断电。
  // 没设置这个回调时，收到请求会直接 powerOff(0)。
  std::function<void(uint8_t reason)> onPowerOffRequest;

 private:
  void feed(uint8_t b);
  void handle(uint8_t cmd, const uint8_t *data, uint8_t len);
  void send(uint8_t cmd, const uint8_t *data, uint8_t len);

  HardwareSerial *port_ = nullptr;
  PmuStatus status_;
  bool hasStatus_ = false;

  uint8_t ps_ = 0, cmd_ = 0, len_ = 0, idx_ = 0, sum_ = 0;
  uint8_t buf_[8];
};
