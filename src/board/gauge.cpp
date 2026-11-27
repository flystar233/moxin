// 电量计 U10 CW2015
#include "config.h"
#include "i2c.h"
#include "sensors.h"

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
