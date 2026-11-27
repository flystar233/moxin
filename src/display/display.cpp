// 按 config.h 的 EPD_MODEL 选屏驱动
#include "config.h"
#include "epd_panel.h"

#if EPD_MODEL == EPD_MODEL_SSD1619A_420
#include "epd_ssd1619a.h"
static EpdSsd1619a s_panel(EPD_PIN_SCK, EPD_PIN_MOSI, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY, EPD_SPI_HZ);
#elif EPD_MODEL == EPD_MODEL_SSD1677_426
#include "epd_ssd1677.h"
static EpdSsd1677 s_panel(EPD_PIN_SCK, EPD_PIN_MOSI, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY, EPD_SPI_HZ);
#else
#error "未知的 EPD_MODEL"
#endif

EpdPanel &panel() { return s_panel; }
