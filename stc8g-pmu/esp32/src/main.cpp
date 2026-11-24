// 示例：打印电源状态；短按、长按关机、定时唤醒的用法
#include <Arduino.h>

#include "PmuLink.h"

static constexpr int PMU_RX = 18;  // 经 R11 接 STC P3.1/TXD
static constexpr int PMU_TX = 17;  // 经 R12 接 STC P3.0/RXD

PmuLink pmu;

// 关机：先收尾，再通知 STC 断电
static void shutdownNow(uint16_t wakeAfterMinutes) {
  // TODO: 保存设置 / 数据、让屏幕进入休眠
  Serial.printf("关机，%u 分钟后唤醒（0 = 只能按键开机）\n", wakeAfterMinutes);
  Serial.flush();
  pmu.powerOff(wakeAfterMinutes);
  for (;;) delay(100);  // 等 STC 断开 3.3V
}

void setup() {
  Serial.begin(115200);

  pmu.onStatus = [](const PmuStatus &s) {
    Serial.printf("USB=%d 充电中=%d 已充满=%d 电量低=%d VCC=%umV 开机原因=%u\n",
                  s.usb, s.charging, s.full, s.lowBattery, s.vccMv, s.bootReason);
  };
  pmu.onShortPress = [] { Serial.println("短按电源键"); };
  pmu.onPowerOffRequest = [](uint8_t reason) {
    Serial.println(reason == PmuLink::OFF_BY_LOW_BATTERY ? "电量低，关机" : "长按电源键，关机");
    shutdownNow(0);
  };

  pmu.begin(Serial1, PMU_RX, PMU_TX);
  pmu.requestStatus();
}

void loop() {
  pmu.poll();

  // 定时唤醒开机的：干完活马上关机，30 分钟后再叫醒（比如墨水屏定时刷新）
  static bool handled = false;
  if (!handled && pmu.hasStatus() && pmu.status().bootReason == PmuLink::BOOT_TIMER) {
    handled = true;
    // TODO: 联网取数据、刷新屏幕
    shutdownNow(30);
  }
}
