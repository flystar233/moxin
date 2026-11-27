#pragma once
#include <Arduino.h>

void i2cBegin();
bool i2cPing(uint8_t addr);
bool regWrite(uint8_t addr, uint8_t reg, uint8_t v);
bool regRead(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
