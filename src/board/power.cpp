#include "power.h"

#include "config.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"

// 越早拉高 PWR_HOLD 越好：全局构造比 setup() 早几百毫秒
static void earlyPowerHold() __attribute__((constructor(101)));
static void earlyPowerHold() {
  gpio_set_level((gpio_num_t)PIN_PWR_HOLD, 1);
  gpio_set_direction((gpio_num_t)PIN_PWR_HOLD, GPIO_MODE_OUTPUT);
  gpio_hold_en((gpio_num_t)PIN_PWR_HOLD);
}

void powerHoldOn() {
  gpio_hold_dis((gpio_num_t)PIN_PWR_HOLD);
  gpio_set_level((gpio_num_t)PIN_PWR_HOLD, 1);
  gpio_set_direction((gpio_num_t)PIN_PWR_HOLD, GPIO_MODE_OUTPUT);
  gpio_hold_en((gpio_num_t)PIN_PWR_HOLD);  // 程序崩溃重启、深睡时也不掉电
}

bool powerKeyDown() { return digitalRead(PIN_PWR_KEY_N) == LOW; }
bool usbPresent() { return digitalRead(PIN_USB_PRES) == HIGH; }

void powerCut() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_PWR_HOLD);  // hold 开着时拉低不生效
  gpio_set_level((gpio_num_t)PIN_PWR_HOLD, 0);
  delay(1500);  // 还能执行到这里，说明没断掉
}

void ampOff() {
  pinMode(PIN_AMP_CTRL, OUTPUT);
  digitalWrite(PIN_AMP_CTRL, LOW);
}

void sdPower(bool on) {
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, on ? LOW : HIGH);
  if (on) delay(10);  // Q2 缓启动约 1ms，文档要求 ≥5ms 再初始化卡
}

static void holdLevel(int pin, int level) {
  gpio_hold_dis((gpio_num_t)pin);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)pin, level);
  gpio_hold_en((gpio_num_t)pin);
}

static const int KEY_PINS[] = {PIN_KEY_MENU, PIN_KEY_PREV, PIN_KEY_NEXT, PIN_PWR_KEY_N};

void powerDeepSleep(uint32_t wakeMs, bool keys, bool usb) {
  // 深睡时数字引脚默认会浮空：把要保持电平的都锁住（文档 F6：PWR_HOLD 漏了一睡就断电）
  holdLevel(PIN_PWR_HOLD, 1);
  holdLevel(PIN_SD_PWR, 1);   // 卡断电
  for (int p : {PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0}) holdLevel(p, 0);  // 不从信号线反灌给卡
  holdLevel(PIN_AMP_CTRL, 0);
  holdLevel(EPD_PIN_CS, 1);
  holdLevel(EPD_PIN_RST, 1);
  gpio_deep_sleep_hold_en();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (wakeMs) esp_sleep_enable_timer_wakeup((uint64_t)wakeMs * 1000);
  if (keys) {
    // 防唤醒循环（文档 F6）：一直为低的脚不加进掩码，免得刚睡就醒
    uint64_t mask = 0;
    for (int p : KEY_PINS) {
      if (digitalRead(p) == LOW) { Serial.printf("[睡] GPIO%d 一直是低，不当唤醒源\n", p); continue; }
      mask |= 1ULL << p;
      rtc_gpio_pullup_dis((gpio_num_t)p);  // 外部有 10k 上拉
      rtc_gpio_pulldown_dis((gpio_num_t)p);
    }
    if (mask) esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  }
  if (usb && !usbPresent()) esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USB_PRES, 1);
  Serial.flush();
  esp_deep_sleep_start();
}

void powerReleaseHolds() {
  gpio_deep_sleep_hold_dis();
  for (int p : {PIN_SD_PWR, PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_AMP_CTRL, EPD_PIN_CS, EPD_PIN_RST})
    gpio_hold_dis((gpio_num_t)p);
  sdPower(false);
  ampOff();
  for (int p : KEY_PINS) {
    if (rtc_gpio_is_valid_gpio((gpio_num_t)p)) rtc_gpio_deinit((gpio_num_t)p);
  }
  if (rtc_gpio_is_valid_gpio((gpio_num_t)PIN_USB_PRES)) rtc_gpio_deinit((gpio_num_t)PIN_USB_PRES);
}

WakeInfo powerWakeInfo() {
  WakeInfo w{WAKE_NONE, -1};
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: w.reason = WAKE_TIMER; break;
    case ESP_SLEEP_WAKEUP_EXT0: w.reason = WAKE_USB; break;
    case ESP_SLEEP_WAKEUP_EXT1: {
      w.reason = WAKE_KEY;
      uint64_t st = esp_sleep_get_ext1_wakeup_status();
      for (int p : KEY_PINS)
        if (st & (1ULL << p)) w.pin = p;
      break;
    }
    case ESP_SLEEP_WAKEUP_UNDEFINED: break;
    default: w.reason = WAKE_OTHER; break;
  }
  return w;
}
