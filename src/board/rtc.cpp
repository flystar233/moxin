// 时钟 U6 PCF8563
#include <Wire.h>
#include <time.h>

#include "config.h"
#include "i2c.h"
#include "sensors.h"

static bool s_timeTrusted = false;
static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

uint8_t weekdayOf(int y, int m, int d) {
  struct tm tm = {};
  tm.tm_year = y - 1900;
  tm.tm_mon = m - 1;
  tm.tm_mday = d;
  tm.tm_hour = 12;
  mktime(&tm);
  return tm.tm_wday;
}

int daysInMonth(int y, int m) {
  static const uint8_t D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return D[(m - 1) % 12];
}

bool rtcWrite(const DateTime &t) {
  Wire.beginTransmission(ADDR_RTC);
  Wire.write(0x02);
  Wire.write(bin2bcd(t.second));  // 写秒同时清 VL
  Wire.write(bin2bcd(t.minute));
  Wire.write(bin2bcd(t.hour));
  Wire.write(bin2bcd(t.day));
  Wire.write(weekdayOf(t.year, t.month, t.day));
  Wire.write(bin2bcd(t.month));   // 世纪位 0 → 20xx
  Wire.write(bin2bcd(t.year - 2000));
  bool ok = Wire.endTransmission() == 0;
  if (ok) s_timeTrusted = true;
  return ok;
}

bool rtcBegin() {
  if (!i2cPing(ADDR_RTC)) return false;
  regWrite(ADDR_RTC, 0x00, 0x00);  // 正常走时
  regWrite(ADDR_RTC, 0x01, 0x00);  // 关中断、清 TF/AF（不清会把 INT 一直拉低 → 唤醒循环）
  regWrite(ADDR_RTC, 0x0D, 0x00);  // 关 CLKOUT
  regWrite(ADDR_RTC, 0x0E, 0x03);  // 关定时器
  uint8_t sec;
  if (!regRead(ADDR_RTC, 0x02, &sec, 1)) return false;
  if (sec & 0x80) {
    // 掉过电：先按编译时间走起来，界面上标"未对时"，等设置里对时或串口 time 命令
    static const char *mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {0};
    int d, y, hh, mm, ss;
    sscanf(__DATE__, "%3s %d %d", m, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
    int mo = (strstr(mon, m) - mon) / 3 + 1;
    DateTime t{(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)hh, (uint8_t)mm, (uint8_t)ss, 0};
    rtcWrite(t);
    s_timeTrusted = false;
  } else {
    s_timeTrusted = true;
  }
  return true;
}

bool rtcRead(DateTime &t) {
  uint8_t r[7];
  if (!regRead(ADDR_RTC, 0x02, r, 7)) return false;
  t.second = bcd2bin(r[0] & 0x7F);
  t.minute = bcd2bin(r[1] & 0x7F);
  t.hour = bcd2bin(r[2] & 0x3F);
  t.day = bcd2bin(r[3] & 0x3F);
  t.weekday = r[4] & 0x07;
  t.month = bcd2bin(r[5] & 0x1F);
  t.year = 2000 + bcd2bin(r[6]);
  return true;
}

bool rtcTimeTrusted() { return s_timeTrusted; }
