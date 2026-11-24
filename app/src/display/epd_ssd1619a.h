#pragma once

#include <SPI.h>

#include "epd_panel.h"

// 4.2 寸黑白测试屏，SSD1619A，400 源极 × 300 栅极。显存在 MCU 里。
class EpdSsd1619a : public EpdPanel {
 public:
  static constexpr int16_t W = 400;
  static constexpr int16_t H = 300;

  EpdSsd1619a(int8_t sck, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy, uint32_t spiHz);

  const char *name() const override { return "4.2 寸 SSD1619A 400×300"; }
  void begin() override;
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;
  bool refreshFull() override;
  bool refreshPartial() override;
  void forcePartialRect(int16_t x, int16_t y, int16_t w, int16_t h) override;
  void assumeShown() override;
  // 局刷波形：变黑的像素加 VSH1 共 black 帧；变白的像素先跟着加 VSL black 帧，再多加 whiteExtra 帧
  void setPartialFrames(uint8_t black, uint8_t whiteExtra) override;
  bool pixelBlack(int16_t x, int16_t y) override;

 private:
  bool phys(int16_t x, int16_t y, uint32_t &i, uint8_t &m);
  void reset();
  bool waitBusy(uint32_t timeoutMs);
  void cmd(uint8_t c);
  void data(uint8_t d);
  void writeRam(uint8_t ramCmd, const uint8_t *buf, const uint8_t *diff = nullptr);
  bool init(bool partial);
  void sleep();

  int8_t sck_, mosi_, cs_, dc_, rst_, busy_;
  SPISettings spi_;
  uint8_t bw_[W / 8 * H];   // 0x24：1 = 白，0 = 黑
  uint8_t prev_[W / 8 * H]; // 上一次刷到屏上的黑白帧，局刷时用来算"没变"掩码
};
