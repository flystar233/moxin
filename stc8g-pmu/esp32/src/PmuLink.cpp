#include "PmuLink.h"

namespace {
constexpr uint8_t HEAD = 0xA5;
constexpr uint8_t CMD_STATUS = 0x01;
constexpr uint8_t CMD_KEY = 0x02;
constexpr uint8_t CMD_OFF_REQ = 0x03;
constexpr uint8_t CMD_GET_STATUS = 0x81;
constexpr uint8_t CMD_POWER_OFF = 0x82;
}  // namespace

void PmuLink::begin(HardwareSerial &port, int rxPin, int txPin) {
  port_ = &port;
  port_->begin(9600, SERIAL_8N1, rxPin, txPin);
}

void PmuLink::poll() {
  if (!port_) return;
  while (port_->available() > 0) feed(static_cast<uint8_t>(port_->read()));
}

void PmuLink::requestStatus() { send(CMD_GET_STATUS, nullptr, 0); }

void PmuLink::powerOff(uint16_t wakeAfterMinutes) {
  const uint8_t d[2] = {static_cast<uint8_t>(wakeAfterMinutes >> 8),
                        static_cast<uint8_t>(wakeAfterMinutes)};
  send(CMD_POWER_OFF, d, 2);
  if (port_) port_->flush();  // 等最后一个字节发完
}

void PmuLink::send(uint8_t cmd, const uint8_t *data, uint8_t len) {
  if (!port_) return;
  uint8_t frame[3 + sizeof(buf_) + 1];
  uint8_t sum = cmd + len;
  frame[0] = HEAD;
  frame[1] = cmd;
  frame[2] = len;
  for (uint8_t i = 0; i < len; i++) {
    frame[3 + i] = data[i];
    sum += data[i];
  }
  frame[3 + len] = sum;
  port_->write(frame, 4 + len);
}

void PmuLink::feed(uint8_t b) {
  switch (ps_) {
    case 0:
      if (b == HEAD) ps_ = 1;
      break;
    case 1:
      cmd_ = b;
      sum_ = b;
      ps_ = 2;
      break;
    case 2:
      len_ = b;
      sum_ += b;
      idx_ = 0;
      if (len_ > sizeof(buf_)) ps_ = 0;
      else ps_ = len_ ? 3 : 4;
      break;
    case 3:
      buf_[idx_++] = b;
      sum_ += b;
      if (idx_ >= len_) ps_ = 4;
      break;
    case 4:
      if (b == sum_) handle(cmd_, buf_, len_);
      ps_ = 0;
      break;
  }
}

void PmuLink::handle(uint8_t cmd, const uint8_t *d, uint8_t len) {
  switch (cmd) {
    case CMD_STATUS:
      if (len < 4) return;
      status_.usb = d[0] & 0x01;
      status_.charging = d[0] & 0x02;
      status_.full = d[0] & 0x04;
      status_.lowBattery = d[0] & 0x08;
      status_.vccMv = (uint16_t(d[1]) << 8) | d[2];
      status_.bootReason = d[3];
      hasStatus_ = true;
      if (onStatus) onStatus(status_);
      break;
    case CMD_KEY:
      if (len >= 1 && d[0] == 1 && onShortPress) onShortPress();
      break;
    case CMD_OFF_REQ:
      if (len < 1) return;
      if (onPowerOffRequest) onPowerOffRequest(d[0]);
      else powerOff(0);
      break;
  }
}
