// CO2 U7 STCC4（单次测量，地址 0x64），温湿度来自它后面的 U11 SHT40
#include <Wire.h>

#include "config.h"
#include "sensors.h"

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
  return true;
}
