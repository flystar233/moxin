#pragma once

#include <SPI.h>

#include "epd_panel.h"

// 4.26 寸黑白屏 GDEY0426T82，SSD1677，800 源极 × 480 栅极（控制器坐标是横屏）。
// 显存两帧（当前、上一次刷到屏上的）各 48KB，放 PSRAM。
// 全刷用 OTP 的模式 1（约 1.6 秒）；局刷用 OTP 的模式 2：0x24 写新帧、0x26 写旧帧，控制器自己比较，
// 只驱动变了的像素（约 0.6 秒），不用自己写波形。
class EpdSsd1677 : public EpdPanel {
 public:
  static constexpr int16_t W = 800;
  static constexpr int16_t H = 480;

  EpdSsd1677(int8_t sck, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy, uint32_t spiHz);

  const char *name() const override { return "4.26 寸 SSD1677 800×480"; }
  void begin() override;
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
  bool refreshFull() override;   // 快速全刷：温度寄存器写成 90°C，用高温短波形，约 1.5 秒、只闪一两下
  bool refreshClean() override;  // 标准全刷：按真实温度的波形，约 4 秒（19°C），清残影最彻底
  bool refreshPartial() override;
  void forcePartialRect(int16_t x, int16_t y, int16_t w, int16_t h) override;
  void assumeShown() override;
  bool pixelBlack(int16_t x, int16_t y) override;

 private:
  bool phys(int16_t x, int16_t y, uint32_t &i, uint8_t &m);
  void reset();
  bool waitBusy(uint32_t timeoutMs);
  void cmd(uint8_t c);
  void data(uint8_t d);
  void writeRam(uint8_t ramCmd, const uint8_t *buf);
  bool init();
  bool update(uint8_t mode, uint32_t timeoutMs);
  bool full(bool fast);
  void sleep();

  int8_t sck_, mosi_, cs_, dc_, rst_, busy_;
  SPISettings spi_;
  static constexpr size_t BYTES = W / 8 * H;
  uint8_t *bw_ = nullptr;    // 1 = 白，0 = 黑
  uint8_t *prev_ = nullptr;  // 上一次刷到屏上的帧
};
