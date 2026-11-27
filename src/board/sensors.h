#pragma once
// I2C 外设：时钟 U6 PCF8563、电量计 U10 CW2015、CO2 U7 STCC4（温湿度来自它后面的 U11 SHT40）。
#include <Arduino.h>

// ---------- 时钟 ----------
struct DateTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second, weekday;  // weekday 0 = 周日
};
bool rtcBegin();                    // 关 CLKOUT；VL=1（掉过电）时按编译时间预置
bool rtcRead(DateTime &t);
bool rtcWrite(const DateTime &t);   // 会按年月日重算星期
bool rtcTimeTrusted();              // false = 掉过电后还没对过时
uint8_t weekdayOf(int y, int m, int d);
int daysInMonth(int y, int m);

// ---------- 电量计 ----------
struct Battery {
  bool ok;
  uint16_t mv;     // 电池电压
  uint8_t soc;     // 电量百分比（电池模型未写入前不准）
};
bool gaugeBegin();                  // 唤醒
bool gaugeRead(Battery &b);
void gaugeSleep();                  // 关机前让它睡（约 0.5µA）

// ---------- CO2 + 温湿度 ----------
struct Climate {
  bool ok;
  int16_t co2;     // ppm
  float temp;      // °C
  float rh;        // %
};
bool co2Present();
bool co2Begin();
// 非阻塞：start 触发一次单次测量，约 500ms 后 poll 读回；读完让传感器睡
void co2Start();
bool co2Poll(Climate &c);           // 测量好了返回 true
