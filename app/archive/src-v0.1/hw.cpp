#include "hw.h"

#include <Wire.h>
#include <time.h>

#include "config.h"
#include "driver/gpio.h"

// =====================================================================
// 电源
// =====================================================================
// 越早拉高 PWR_HOLD 越好：全局构造比 setup() 早几百毫秒
static void earlyPowerHold() __attribute__((constructor(101)));
static void earlyPowerHold() {
  gpio_set_level((gpio_num_t)PIN_PWR_HOLD, 1);
  gpio_set_direction((gpio_num_t)PIN_PWR_HOLD, GPIO_MODE_OUTPUT);
  gpio_hold_en((gpio_num_t)PIN_PWR_HOLD);
}

void powerHoldOn() {
  gpio_hold_dis((gpio_num_t)PIN_PWR_HOLD);
  gpio_set_level((gpio_num_t)PIN_PWR_HOLD, 1);
  gpio_set_direction((gpio_num_t)PIN_PWR_HOLD, GPIO_MODE_OUTPUT);
  gpio_hold_en((gpio_num_t)PIN_PWR_HOLD);  // 程序崩溃重启时也不掉电
}

bool powerKeyDown() { return digitalRead(PIN_PWR_KEY_N) == LOW; }
bool usbPresent() { return digitalRead(PIN_USB_PRES) == HIGH; }

bool powerCut() {
  gpio_hold_dis((gpio_num_t)PIN_PWR_HOLD);  // hold 开着时拉低不生效
  gpio_set_level((gpio_num_t)PIN_PWR_HOLD, 0);
  delay(1500);
  return false;  // 还能执行到这里，说明没断掉
}

void ampOff() {
  pinMode(PIN_AMP_CTRL, OUTPUT);
  digitalWrite(PIN_AMP_CTRL, LOW);
}

void sdPower(bool on) {
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, on ? LOW : HIGH);
  if (on) delay(10);  // Q2 缓启动约 1ms，文档要求 ≥5ms 再初始化卡
}

// =====================================================================
// I2C
// =====================================================================
void i2cBegin() {
  Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);
  Wire.setTimeOut(30);
}

bool i2cPing(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

bool regWrite(uint8_t a, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool regRead(uint8_t a, uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)a, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// =====================================================================
// 时钟 PCF8563
// =====================================================================
static bool s_timeTrusted = false;
static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool rtcWrite(const DateTime &t) {
  Wire.beginTransmission(ADDR_RTC);
  Wire.write(0x02);
  Wire.write(bin2bcd(t.second));  // 写秒同时清 VL
  Wire.write(bin2bcd(t.minute));
  Wire.write(bin2bcd(t.hour));
  Wire.write(bin2bcd(t.day));
  Wire.write(t.weekday);
  Wire.write(bin2bcd(t.month));   // 世纪位 0 → 20xx
  Wire.write(bin2bcd(t.year - 2000));
  bool ok = Wire.endTransmission() == 0;
  if (ok) s_timeTrusted = true;
  return ok;
}

static uint8_t weekdayOf(int y, int m, int d) {
  struct tm tm = {};
  tm.tm_year = y - 1900;
  tm.tm_mon = m - 1;
  tm.tm_mday = d;
  tm.tm_hour = 12;
  mktime(&tm);
  return tm.tm_wday;
}

bool rtcBegin() {
  if (!i2cPing(ADDR_RTC)) return false;
  regWrite(ADDR_RTC, 0x00, 0x00);  // 正常走时
  regWrite(ADDR_RTC, 0x01, 0x00);  // 关中断、清 TF/AF
  regWrite(ADDR_RTC, 0x0D, 0x00);  // 关 CLKOUT
  regWrite(ADDR_RTC, 0x0E, 0x03);  // 关定时器
  uint8_t sec;
  if (!regRead(ADDR_RTC, 0x02, &sec, 1)) return false;
  if (sec & 0x80) {
    // 掉过电：先按编译时间走起来，界面上标"未对时"，等串口 time 命令或电脑端对时
    static const char *mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {0};
    int d, y, hh, mm, ss;
    sscanf(__DATE__, "%3s %d %d", m, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
    int mo = (strstr(mon, m) - mon) / 3 + 1;
    DateTime t{(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)hh, (uint8_t)mm, (uint8_t)ss, weekdayOf(y, mo, d)};
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

// =====================================================================
// 电量计 CW2015
// =====================================================================
bool gaugeBegin() {
  if (!i2cPing(ADDR_GAUGE)) return false;
  regWrite(ADDR_GAUGE, 0x0A, 0x00);  // MODE：唤醒
  uint8_t hi;
  if (regRead(ADDR_GAUGE, 0x06, &hi, 1) && (hi & 0x80)) regWrite(ADDR_GAUGE, 0x06, hi & 0x7F);  // 清 ALRT
  return true;
}

bool gaugeRead(Battery &b) {
  uint8_t v[2], s[2];
  b.ok = regRead(ADDR_GAUGE, 0x02, v, 2) && regRead(ADDR_GAUGE, 0x04, s, 2);
  if (!b.ok) return false;
  b.mv = (uint32_t)(((v[0] << 8) | v[1]) & 0x3FFF) * 305 / 1000;
  b.soc = s[0] > 100 ? 100 : s[0];
  return true;
}

void gaugeSleep() { regWrite(ADDR_GAUGE, 0x0A, 0xC0); }

// =====================================================================
// CO2 STCC4（单次测量，地址 0x64）
// =====================================================================
static bool s_co2 = false;
static uint32_t s_co2StartAt = 0;
static bool s_co2Busy = false;

static uint8_t crc8(const uint8_t *d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (c << 1) ^ 0x31 : (c << 1);
  }
  return c;
}

static void co2Wake() {  // 地址写 + 0x00，没有应答是正常的
  Wire.beginTransmission(ADDR_CO2);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(6);
}

static bool co2Cmd(uint16_t cmd) {
  Wire.beginTransmission(ADDR_CO2);
  Wire.write(cmd >> 8);
  Wire.write(cmd & 0xFF);
  return Wire.endTransmission() == 0;
}

static bool co2ReadWords(uint16_t *w, int n) {
  int bytes = n * 3;
  if (Wire.requestFrom((int)ADDR_CO2, bytes) != bytes) return false;
  bool ok = true;
  for (int i = 0; i < n; i++) {
    uint8_t b[3] = {(uint8_t)Wire.read(), (uint8_t)Wire.read(), (uint8_t)Wire.read()};
    if (crc8(b, 2) != b[2]) ok = false;
    w[i] = (b[0] << 8) | b[1];
  }
  return ok;
}

bool co2Present() { return s_co2; }

bool co2Begin() {
  co2Wake();
  uint16_t w[6];
  s_co2 = co2Cmd(0x365B) && (delay(2), co2ReadWords(w, 6)) && (((uint32_t)w[0] << 16 | w[1]) == 0x0901018A);
  if (s_co2) co2Cmd(0x3650);  // 睡
  return s_co2;
}

void co2Start() {
  if (!s_co2 || s_co2Busy) return;
  co2Wake();
  if (co2Cmd(0x219D)) {  // 单次测量，约 500ms
    s_co2Busy = true;
    s_co2StartAt = millis();
  }
}

bool co2Poll(Climate &c) {
  if (!s_co2Busy || millis() - s_co2StartAt < 550) return false;
  s_co2Busy = false;
  uint16_t w[4];
  bool ok = co2Cmd(0xEC05) && (delay(2), co2ReadWords(w, 4));
  co2Cmd(0x3650);
  if (!ok) return false;
  c.ok = true;
  c.co2 = (int16_t)w[0];
  c.temp = -45.0f + 175.0f * w[1] / 65535.0f;
  c.rh = -6.0f + 125.0f * w[2] / 65535.0f;
  c.at = millis();
  return true;
}
