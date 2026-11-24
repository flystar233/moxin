#include "i2c.h"

#include <Wire.h>

#include "config.h"

// 总线恢复：芯片在 I2C 读到一半时复位（烧录、崩溃重启），从机会停在"等时钟"的状态把 SDA 拉低，
// 之后整条总线都不通（11-12 加速度计因此开机认不到）。在时钟线上打 9 个脉冲让它把那个字节送完，再发 STOP
static void busRecover() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(5);
  bool stuck = digitalRead(PIN_SDA) == LOW;
  for (int i = 0; i < 9; i++) {
    digitalWrite(PIN_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(5);
  }
  pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);  // STOP：SCL 高时 SDA 由低变高
  digitalWrite(PIN_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_SDA, HIGH);
  delayMicroseconds(5);
  if (stuck) Serial.printf("[I2C] SDA 被拉住了，已恢复总线（现在 SDA = %d）\n", digitalRead(PIN_SDA));
}

void i2cBegin() {
  busRecover();
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
