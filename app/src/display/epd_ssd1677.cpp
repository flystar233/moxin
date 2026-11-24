#include "epd_ssd1677.h"

#include "config.h"

// 命令序列参照 SSD1677 规格书和 GxEPD2 的 GxEPD2_426_GDEQ0426T82（同一块屏）

EpdSsd1677::EpdSsd1677(int8_t sck, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy, uint32_t spiHz)
    : EpdPanel(W, H),
      sck_(sck), mosi_(mosi), cs_(cs), dc_(dc), rst_(rst), busy_(busy),
      spi_(spiHz, MSBFIRST, SPI_MODE0) {}

void EpdSsd1677::begin() {
  if (!bw_) {
    bw_ = (uint8_t *)ps_malloc(BYTES);
    prev_ = (uint8_t *)ps_malloc(BYTES);
    if (!bw_ || !prev_) { Serial.println("[屏] 显存分配失败"); return; }
    fillScreen(EPD_WHITE);
    memcpy(prev_, bw_, BYTES);
  }
  pinMode(cs_, OUTPUT);
  digitalWrite(cs_, HIGH);
  pinMode(dc_, OUTPUT);
  pinMode(rst_, OUTPUT);
  digitalWrite(rst_, HIGH);
  pinMode(busy_, INPUT);
  SPI.begin(sck_, -1, mosi_, -1);
}

// 逻辑坐标（随旋转）→ 显存里的字节下标和位掩码
bool EpdSsd1677::phys(int16_t x, int16_t y, uint32_t &i, uint8_t &m) {
  if (x < 0 || y < 0 || x >= width() || y >= height()) return false;
  int16_t t;
  switch (getRotation()) {
    case 1: t = x; x = W - 1 - y; y = t; break;
    case 2: x = W - 1 - x; y = H - 1 - y; break;
    case 3: t = x; x = y; y = H - 1 - t; break;
  }
  if (EPD_MIRROR_X) x = W - 1 - x;
  if (EPD_MIRROR_Y) y = H - 1 - y;
  i = (uint32_t)y * (W / 8) + x / 8;
  m = 0x80 >> (x & 7);
  return true;
}

void EpdSsd1677::drawPixel(int16_t x, int16_t y, uint16_t color) {
  uint32_t i;
  uint8_t m;
  if (!bw_ || !phys(x, y, i, m)) return;
  if (color == EPD_BLACK) bw_[i] &= ~m;
  else bw_[i] |= m;
}

void EpdSsd1677::fillScreen(uint16_t color) {
  if (bw_) memset(bw_, color == EPD_BLACK ? 0x00 : 0xFF, BYTES);
}

bool EpdSsd1677::pixelBlack(int16_t x, int16_t y) {
  uint32_t i;
  uint8_t m;
  return bw_ && phys(x, y, i, m) && !(bw_[i] & m);
}

void EpdSsd1677::assumeShown() {
  if (bw_) memcpy(prev_, bw_, BYTES);
}

// 屏上画面未知：把"旧帧"在这个矩形里设成新帧的反色，局刷时矩形里每个像素都会被驱动；矩形外新旧相同，不动
void EpdSsd1677::forcePartialRect(int16_t x0, int16_t y0, int16_t w, int16_t h) {
  if (!bw_) return;
  memcpy(prev_, bw_, BYTES);
  for (int16_t y = y0; y < y0 + h; y++)
    for (int16_t x = x0; x < x0 + w; x++) {
      uint32_t i;
      uint8_t m;
      if (phys(x, y, i, m)) prev_[i] ^= m;
    }
}

void EpdSsd1677::reset() {
  digitalWrite(rst_, LOW);
  delay(10);
  digitalWrite(rst_, HIGH);
  delay(10);
}

bool EpdSsd1677::waitBusy(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (digitalRead(busy_) == HIGH) {  // SSD1677：BUSY 高 = 忙
    if (millis() - t0 > timeoutMs) return false;
    delay(2);
  }
  return true;
}

void EpdSsd1677::cmd(uint8_t c) {
  SPI.beginTransaction(spi_);
  digitalWrite(dc_, LOW);
  digitalWrite(cs_, LOW);
  SPI.transfer(c);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

void EpdSsd1677::data(uint8_t d) {
  SPI.beginTransaction(spi_);
  digitalWrite(dc_, HIGH);
  digitalWrite(cs_, LOW);
  SPI.transfer(d);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

// 整屏写一块 RAM。数据输入方向是 X 递增、Y 递减（这块屏的栅极是反的），所以 Y 计数器从 479 开始，
// 缓冲区第 0 行写到 Y = 479
void EpdSsd1677::writeRam(uint8_t ramCmd, const uint8_t *buf) {
  cmd(0x11);  // 数据输入模式：X 递增，Y 递减
  data(0x01);
  cmd(0x44);  // X 范围（像素）：0～799
  data(0x00);
  data(0x00);
  data((W - 1) & 0xFF);
  data((W - 1) >> 8);
  cmd(0x45);  // Y 范围：479～0
  data((H - 1) & 0xFF);
  data((H - 1) >> 8);
  data(0x00);
  data(0x00);
  cmd(0x4E);  // X 计数器
  data(0x00);
  data(0x00);
  cmd(0x4F);  // Y 计数器
  data((H - 1) & 0xFF);
  data((H - 1) >> 8);

  cmd(ramCmd);
  SPI.beginTransaction(spi_);
  digitalWrite(dc_, HIGH);
  digitalWrite(cs_, LOW);
  // 从 PSRAM 分块拷到内部 RAM 再发（SPI 驱动一次发一块比逐字节快得多）
  static uint8_t chunk[1024];
  for (size_t off = 0; off < BYTES; off += sizeof(chunk)) {
    size_t n = min(sizeof(chunk), BYTES - off);
    memcpy(chunk, buf + off, n);
    SPI.writeBytes(chunk, n);
  }
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

bool EpdSsd1677::init() {
  reset();
  if (!waitBusy(1000)) return false;
  cmd(0x12);  // 软复位
  delay(10);
  if (!waitBusy(1000)) return false;
  cmd(0x18);  // 用片内温度传感器
  data(0x80);
  cmd(0x0C);  // 升压软启动（屏厂参数）
  data(0xAE);
  data(0xC7);
  data(0xC3);
  data(0xC0);
  data(0x80);
  cmd(0x01);  // 栅极数 480
  data((H - 1) & 0xFF);
  data((H - 1) >> 8);
  data(0x02);
  cmd(0x3C);  // 边框波形
  data(0x01);
  return true;
}

// 发刷新命令，等 BUSY 先变高（开始）再变低（结束）。
// BUSY 一直不变高：多半是 BUSY 线没接好或电平不对，按典型时间死等，免得刷到一半就被深睡命令打断
bool EpdSsd1677::update(uint8_t mode, uint32_t timeoutMs) {
  cmd(0x22);
  data(mode);
  cmd(0x20);
  uint32_t t0 = millis();
  bool started = false;
  while (millis() - t0 < 50) {
    if (digitalRead(busy_) == HIGH) { started = true; break; }
    delayMicroseconds(200);
  }
  if (!started) {
    if (mode == 0x83) return true;  // 关电：刷新时已经关过了，没事可做，BUSY 不会变高
    uint32_t typ = mode == 0xF7 ? 4000 : mode == 0xD7 ? 2000 : 800;
    Serial.printf("[屏] 命令 0x%02X 后 BUSY 没有变高（屏没响应或 BUSY 线接触不良），按 %lu ms 等\n", mode,
                  (unsigned long)typ);
    delay(typ);
    return false;
  }
  return waitBusy(timeoutMs);
}

// 先关高压（0x83），再进深睡模式 1（RAM 保留；之后要硬件复位才能再用）
void EpdSsd1677::sleep() {
  update(0x83, 1000);
  cmd(0x10);
  data(0x01);
}

bool EpdSsd1677::refreshFull() { return full(true); }
bool EpdSsd1677::refreshClean() { return full(false); }

// fast：和 GxEPD2 的 useFastFullUpdate 一样，把温度寄存器写成 0x5A（90°C），屏就选高温用的短波形，
// 比按真实温度（冷天要 4 秒、反复闪）快一倍多、闪得少；清残影没有标准波形彻底
bool EpdSsd1677::full(bool fast) {
  if (!bw_ || !init()) return false;
  writeRam(0x24, bw_);
  writeRam(0x26, bw_);
  cmd(0x21);  // 全刷不看红色 RAM（当作 0）
  data(0x40);
  data(0x00);
  bool ok;
  if (fast) {
    cmd(0x1A);  // 写温度寄存器
    data(0x5A);
    ok = update(0xD7, 10000);  // 开时钟和模拟电路、（不读温度）载入模式 1 波形、刷新、关掉
  } else {
    ok = update(0xF7, 10000);  // 同上，但先读片内温度
  }
  sleep();
  memcpy(prev_, bw_, BYTES);
  return ok;
}

bool EpdSsd1677::refreshPartial() {
  if (!bw_ || !init()) return false;
  writeRam(0x26, prev_);  // 旧帧
  writeRam(0x24, bw_);    // 新帧
  cmd(0x21);              // 正常比较新旧两帧
  data(0x00);
  data(0x00);
  bool ok = update(0xFC, 5000);  // 模式 2（局刷）
  sleep();
  memcpy(prev_, bw_, BYTES);
  return ok;
}
