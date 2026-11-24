#pragma once
// 板级硬件：电源保持、I2C 器件（时钟、电量计、CO2）、TF 卡电源。
#include <Arduino.h>

// ---------- 电源 ----------
void powerHoldOn();                 // 拉高 GPIO4 并 gpio_hold（开机第一件事）
bool powerKeyDown();
bool usbPresent();
// 断电：放开 PWR_HOLD。按着电源键或者 R33 点了锡时断不掉，函数会返回 false。
bool powerCut();

// ---------- I2C ----------
void i2cBegin();
bool i2cPing(uint8_t addr);
bool regWrite(uint8_t addr, uint8_t reg, uint8_t v);
bool regRead(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);

// ---------- 时钟 U6 PCF8563 ----------
struct DateTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second, weekday;  // weekday 0 = 周日
};
bool rtcBegin();                    // 关 CLKOUT；VL=1（掉过电）时按编译时间预置
bool rtcRead(DateTime &t);
bool rtcWrite(const DateTime &t);
bool rtcTimeTrusted();              // false = 掉过电后还没对过时

// ---------- 电量计 U10 CW2015 ----------
struct Battery {
  bool ok;
  uint16_t mv;     // 电池电压
  uint8_t soc;     // 电量百分比（电池模型未写入前不准）
};
bool gaugeBegin();                  // 唤醒
bool gaugeRead(Battery &b);
void gaugeSleep();                  // 关机前让它睡（约 0.5µA）

// ---------- CO2 U7 STCC4 + 温湿度 U11 SHT40 ----------
struct Climate {
  bool ok;
  int16_t co2;     // ppm
  float temp;      // °C
  float rh;        // %
  uint32_t at;     // 读到的时间（millis）
};
bool co2Present();
bool co2Begin();
// 非阻塞：start 触发一次单次测量，约 500ms 后 poll 读回；读完让传感器睡
void co2Start();
bool co2Poll(Climate &c);           // 测量好了返回 true

// ---------- 其他 ----------
void ampOff();
void sdPower(bool on);
