#pragma once
// 墨水屏驱动的统一接口。界面代码只用这里的函数和 Adafruit_GFX 的画图函数，
// 换屏（4.2 寸 SSD1619A → 4.26 寸 SSD1677）只要加一个子类，在 display.cpp 里选它。
#include <Adafruit_GFX.h>

enum : uint16_t { EPD_WHITE = 0, EPD_BLACK = 1 };

class EpdPanel : public Adafruit_GFX {
 public:
  EpdPanel(int16_t w, int16_t h) : Adafruit_GFX(w, h) {}
  virtual ~EpdPanel() = default;

  virtual const char *name() const = 0;
  virtual void begin() = 0;
  // 全刷：整屏闪几下，清掉残影。返回 false 表示 BUSY 超时
  virtual bool refreshFull() = 0;
  // 彻底清残影的全刷（标准波形，更慢、闪得更多）。没有区别的屏就等于 refreshFull
  virtual bool refreshClean() { return refreshFull(); }
  // 局刷：只驱动和上一次刷到屏上的画面相比变了的像素
  virtual bool refreshPartial() = 0;
  // 屏上画面未知（刚上电）时：下一次局刷只完整重刷这个矩形（逻辑坐标），矩形外不动
  virtual void forcePartialRect(int16_t x, int16_t y, int16_t w, int16_t h) = 0;
  // 认定屏上现在显示的就是缓冲区里的画面（深睡醒来后，重画睡前那一帧当作局刷的基准）
  virtual void assumeShown() = 0;
  // 局刷波形参数（变黑帧数、变白额外帧数）
  virtual void setPartialFrames(uint8_t black, uint8_t whiteExtra) { (void)black; (void)whiteExtra; }
  // 缓冲区里逻辑坐标 (x, y) 是不是黑（截图用）
  virtual bool pixelBlack(int16_t x, int16_t y) = 0;
};

EpdPanel &panel();
