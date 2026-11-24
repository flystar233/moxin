#include "keys.h"

#include "config.h"
#include "events.h"

volatile uint32_t g_lastKeyActivity = 0;

struct KeySt {
  uint8_t pin;
  uint8_t id;       // PhysKey；电源键用 0xFF
  bool repeat;      // 长按后连发
  uint32_t longMs;
  bool stable = false, raw = false;  // true = 按下
  uint32_t changedAt = 0, downAt = 0, lastRepeat = 0;
  bool longSent = false;
  bool armed = true;  // 电源键：开机那次按压松开前不算
  bool stuckLogged = false;
  KeySt(uint8_t p, uint8_t i, bool r, uint32_t l) : pin(p), id(i), repeat(r), longMs(l) {}
};

static KeySt keys[] = {
  {PIN_KEY_MENU, PK_MENU, false, KEY_LONG_MS},
  {PIN_KEY_PREV, PK_UPPER, true, KEY_LONG_MS},
  {PIN_KEY_NEXT, PK_LOWER, true, KEY_LONG_MS},
  {PIN_PWR_KEY_N, 0xFF, false, PWR_LONG_MS},
};

static void keyTask(void *) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
    uint32_t now = millis();
    for (KeySt &k : keys) {
      bool down = digitalRead(k.pin) == LOW;
      if (down != k.raw) { k.raw = down; k.changedAt = now; g_lastKeyActivity = now; }
      if (down) g_lastKeyActivity = now;  // 按住期间一直算
      if (down != k.stable && now - k.changedAt >= KEY_DEBOUNCE_MS) {
        k.stable = down;
        if (down) {
          k.downAt = now;
          k.longSent = false;
        } else {
          if (k.id == 0xFF) { k.armed = true; continue; }
          if (!k.longSent) postEvent(EV_KEY_SHORT, k.id);
        }
      }
      if (k.stable && !k.longSent && now - k.downAt >= k.longMs) {
        k.longSent = true;
        k.lastRepeat = now;
        if (k.id == 0xFF) {
          if (k.armed) postEvent(EV_POWER_LONG);
        } else {
          postEvent(EV_KEY_LONG, k.id);
        }
      }
      // 一直按着超过 KEY_STUCK_MS：当作卡住（开关粘连、被东西顶着、漏电拉低），停止连发，等松开
      bool stuck = k.stable && now - k.downAt >= KEY_STUCK_MS;
      if (stuck && !k.stuckLogged) {
        k.stuckLogged = true;
        Serial.printf("[键] GPIO%d 已经按住 %d 秒，当作卡住，不再连续翻页（松开后恢复）\n", k.pin, KEY_STUCK_MS / 1000);
      }
      if (!k.stable) k.stuckLogged = false;
      if (k.stable && k.longSent && k.repeat && !stuck && now - k.lastRepeat >= KEY_REPEAT_MS) {
        k.lastRepeat = now;
        postEvent(EV_KEY_REPEAT, k.id);
      }
    }
  }
}

bool keyStuck(int pin) {
  for (KeySt &k : keys)
    if (k.pin == pin) return k.stuckLogged;
  return false;
}

void keysBegin(bool powerKeyHeldAtBoot) {
  for (KeySt &k : keys) {
    pinMode(k.pin, INPUT);
    k.raw = k.stable = digitalRead(k.pin) == LOW;
    if (k.stable) { k.downAt = millis(); k.longSent = true; }  // 开机时就按着的键不触发
  }
  keys[3].armed = !powerKeyHeldAtBoot;  // 开机那次按压要先松手，之后再长按 3 秒才算关机
  xTaskCreatePinnedToCore(keyTask, "keys", 3072, nullptr, 4, nullptr, 0);
}
