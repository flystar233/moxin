#include "epd_ssd1619a.h"

#include "config.h"

// 初始化顺序参照 SSD1619A 规格书和屏厂 4.2 寸例程

EpdSsd1619a::EpdSsd1619a(int8_t sck, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy,
                         uint32_t spiHz)
    : Adafruit_GFX(W, H),
      sck_(sck), mosi_(mosi), cs_(cs), dc_(dc), rst_(rst), busy_(busy),
      spi_(spiHz, MSBFIRST, SPI_MODE0) {
  fillScreen(EPD_WHITE);
}

void EpdSsd1619a::begin() {
  pinMode(cs_, OUTPUT);
  digitalWrite(cs_, HIGH);
  pinMode(dc_, OUTPUT);
  pinMode(rst_, OUTPUT);
  digitalWrite(rst_, HIGH);
  pinMode(busy_, INPUT);
  SPI.begin(sck_, -1, mosi_, -1);
}

// 逻辑坐标（随旋转）→ 显存里的字节下标和位掩码
bool EpdSsd1619a::phys(int16_t x, int16_t y, uint32_t &i, uint8_t &m) {
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

// 屏上原来是什么不知道（刚上电）时用：下一次局刷只驱动这个矩形，矩形里每个像素都重刷，
// 矩形外当作"没变"不驱动，旧画面原样保留。
void EpdSsd1619a::forcePartialRect(int16_t x0, int16_t y0, int16_t w, int16_t h) {
  memcpy(prev_, bw_, sizeof(prev_));
  for (int16_t y = y0; y < y0 + h; y++)
    for (int16_t x = x0; x < x0 + w; x++) {
      uint32_t i;
      uint8_t m;
      if (phys(x, y, i, m)) prev_[i] ^= m;
    }
}

void EpdSsd1619a::drawPixel(int16_t x, int16_t y, uint16_t color) {
  uint32_t i;
  uint8_t m;
  if (!phys(x, y, i, m)) return;
  switch (color) {
    case EPD_BLACK: bw_[i] &= ~m; red_[i] &= ~m; break;
    case EPD_RED:   bw_[i] |= m;  red_[i] |= m;  break;
    default:        bw_[i] |= m;  red_[i] &= ~m; break;
  }
}

void EpdSsd1619a::fillScreen(uint16_t color) {
  memset(bw_, color == EPD_BLACK ? 0x00 : 0xFF, sizeof(bw_));
  memset(red_, color == EPD_RED ? 0xFF : 0x00, sizeof(red_));
}

void EpdSsd1619a::reset() {
  digitalWrite(rst_, LOW);
  delay(10);
  digitalWrite(rst_, HIGH);
  delay(10);
}

bool EpdSsd1619a::waitBusy(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (digitalRead(busy_) == HIGH) {
    if (millis() - t0 > timeoutMs) return false;
    delay(5);
  }
  return true;
}

void EpdSsd1619a::cmd(uint8_t c) {
  SPI.beginTransaction(spi_);
  digitalWrite(dc_, LOW);
  digitalWrite(cs_, LOW);
  SPI.transfer(c);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

void EpdSsd1619a::data(uint8_t d) {
  SPI.beginTransaction(spi_);
  digitalWrite(dc_, HIGH);
  digitalWrite(cs_, LOW);
  SPI.transfer(d);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

// 数据输入模式是 X 递增、Y 递减，所以地址计数器从 Y = 299 开始
// 给了 diff 时写的是 ~(buf ^ diff)：两帧相同的像素为 1
void EpdSsd1619a::writeRam(uint8_t ramCmd, const uint8_t *buf, const uint8_t *diff) {
  cmd(0x4E);  // RAM X 计数器
  data(0x00);
  cmd(0x4F);  // RAM Y 计数器
  data((H - 1) & 0xFF);
  data((H - 1) >> 8);

  cmd(ramCmd);
  SPI.beginTransaction(spi_);
  digitalWrite(dc_, HIGH);
  digitalWrite(cs_, LOW);
  for (size_t i = 0; i < sizeof(bw_); i++) SPI.transfer(diff ? (uint8_t)~(buf[i] ^ diff[i]) : buf[i]);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

bool EpdSsd1619a::init(bool partial) {
  reset();
  if (!waitBusy(1000)) return false;
  cmd(0x12);  // 软复位
  if (!waitBusy(1000)) return false;

  cmd(0x74);  // 模拟模块控制，规格书固定值
  data(0x54);
  cmd(0x7E);  // 数字模块控制，规格书固定值
  data(0x3B);

  cmd(0x01);  // 栅极数 300
  data((H - 1) & 0xFF);
  data((H - 1) >> 8);
  data(0x00);

  cmd(0x11);  // X 递增，Y 递减
  data(0x01);
  cmd(0x44);  // X 范围，单位是字节：0～49
  data(0x00);
  data(W / 8 - 1);
  cmd(0x45);  // Y 范围：299～0
  data((H - 1) & 0xFF);
  data((H - 1) >> 8);
  data(0x00);
  data(0x00);

  cmd(0x3C);  // 全刷时边框跟随白色；局刷时边框不动，免得四周闪
  data(partial ? 0x80 : 0x05);
  cmd(0x18);  // 用片内温度传感器
  data(0x80);

  cmd(0x22);  // 读温度、从 OTP 载入波形和电压；局刷随后用 0x32 覆盖波形
  data(0xB1);
  cmd(0x20);
  return waitBusy(2000);
}

void EpdSsd1619a::sleep() {
  cmd(0x10);  // 深睡模式 1，高压关掉；之后要硬件复位才能再用
  data(0x01);
}

bool EpdSsd1619a::refresh() {
  if (!init(false)) return false;
  writeRam(0x24, bw_);
  writeRam(0x26, red_);
  cmd(0x22);  // 开时钟和模拟电路、显示模式 1、再关掉
  data(0xC7);
  cmd(0x20);
  // 黑白全刷约 3～4 秒，三色屏要 15 秒左右
  bool ok = waitBusy(40000);
  sleep();
  memcpy(prev_, bw_, sizeof(prev_));
  return ok;
}

// SSD1619A 按 (R, B/W) 两个位选 LUT0～3，自己不比较新旧帧（OTP 里的"模式 2"是快速全刷）。
// 局刷的做法：B/W RAM 写新帧，R RAM 写"没变"掩码（旧帧同或新帧），再换上自己的波形：
//   R=0 B=0 → LUT0：变黑，加 VSH1
//   R=0 B=1 → LUT1：变白，加 VSL
//   R=1     → LUT2/LUT3（规格书说 LUT3 跟 LUT2 一样）：没变，接 VSS 不驱动
// 只适用于黑白屏。
static uint8_t LUT_PARTIAL[70] = {
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT0 第 0 组 A 相 VSH1
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT1 第 0 组 A 相 VSL（B 相按需也加 VSL）
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT2
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT3
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // LUT4 VCOM 保持 DCVCOM
    // 每组 TP[A] TP[B] TP[C] TP[D] RP，单位是帧
    EPD_PARTIAL_FRAMES, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

void EpdSsd1619a::setPartialFrames(uint8_t black, uint8_t whiteExtra) {
  // 第 0 组：A 相两种都驱动 black 帧；B 相只有变白的（LUT1）接 VSL，变黑的接 VSS 不动
  LUT_PARTIAL[7] = whiteExtra ? 0xA0 : 0x80;  // LUT1：A=VSL，B=VSL
  LUT_PARTIAL[35] = black;                    // TP[0A]
  LUT_PARTIAL[36] = whiteExtra;               // TP[0B]
}

bool EpdSsd1619a::refreshPartial() {
  if (!init(true)) return false;
  cmd(0x32);
  for (uint8_t b : LUT_PARTIAL) data(b);

  writeRam(0x24, bw_);
  writeRam(0x26, bw_, prev_);
  cmd(0x22);  // 用寄存器里的波形刷，不再从 OTP 载入
  data(0xC7);
  cmd(0x20);
  bool ok = waitBusy(10000);
  sleep();
  memcpy(prev_, bw_, sizeof(prev_));
  return ok;
}
