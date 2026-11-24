#pragma once

#include <Adafruit_GFX.h>
#include <SPI.h>

enum : uint16_t { EPD_WHITE = 0, EPD_BLACK = 1, EPD_RED = 2 };

// SSD1619A，400 源极 x 300 栅极。显存在 MCU 里，refresh() 时整帧写入并全刷。
class EpdSsd1619a : public Adafruit_GFX {
 public:
  static constexpr int16_t W = 400;
  static constexpr int16_t H = 300;

  EpdSsd1619a(int8_t sck, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy, uint32_t spiHz);

  void begin();
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;

  // 复位、初始化、写两块 RAM、全刷，完成后屏进深睡。返回 false 表示 BUSY 超时。
  bool refresh();
  // 局刷：只刷和上一次刷新相比有变化的像素，不整屏闪。要先全刷过一次。
  bool refreshPartial();
  // 屏上旧画面未知时（刚上电），让下一次局刷只完整重刷这个矩形（逻辑坐标），矩形外不动
  void forcePartialRect(int16_t x, int16_t y, int16_t w, int16_t h);
  // 局刷波形：变黑的像素加 VSH1 共 black 帧；变白的像素先跟着加 VSL black 帧，再多加 whiteExtra 帧
  //（上一页的笔画擦不干净、留灰影时加大 whiteExtra）
  void setPartialFrames(uint8_t black, uint8_t whiteExtra);

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
  uint8_t red_[W / 8 * H];  // 0x26：1 = 红，0 = 不红
  uint8_t prev_[W / 8 * H]; // 上一次刷到屏上的黑白帧，局刷时写进 0x26
};
