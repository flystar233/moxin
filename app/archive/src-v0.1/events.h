#pragma once
// 按键任务、姿态任务往主循环发的事件。主循环在刷屏时会阻塞几百毫秒到几秒，
// 事件先进队列，不会丢。
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum EvType : uint8_t {
  EV_KEY_SHORT,   // a = 物理键
  EV_KEY_LONG,    // a = 物理键，按住满 KEY_LONG_MS 时发一次
  EV_KEY_REPEAT,  // a = 物理键，长按后每 KEY_REPEAT_MS 发一次（只有上一页、下一页）
  EV_POWER_LONG,  // 电源键长按 3 秒
  EV_TAP2,        // 后背双击
  EV_POSE,        // a = 新姿态
};

// 物理按键（和姿态无关；倒着拿时由主循环把上一页、下一页对调）
enum PhysKey : uint8_t { PK_MENU = 0, PK_UPPER = 1, PK_LOWER = 2 };

enum Pose : uint8_t {
  POSE_UNKNOWN = 0,
  POSE_DESK,       // 横放，按键朝上
  POSE_DESK_FLIP,  // 横放，按键朝下
  POSE_READ,       // 竖拿，按键在右
  POSE_READ_FLIP,  // 竖拿，按键在左
};

struct Event {
  EvType type;
  uint8_t a;
};

extern QueueHandle_t g_events;
// 最近一次有按键按下或松开的时间：按键的震动会被敲击检测误当成"双击后背"，这段时间里不认敲击
extern volatile uint32_t g_lastKeyActivity;
#define TAP_IGNORE_AFTER_KEY_MS 800
inline void postEvent(EvType t, uint8_t a = 0) {
  Event e{t, a};
  xQueueSend(g_events, &e, 0);
}

inline const char *poseName(uint8_t p) {
  switch (p) {
    case POSE_DESK: return "横放（按键朝上）";
    case POSE_DESK_FLIP: return "横放（按键朝下）";
    case POSE_READ: return "竖拿（按键在右）";
    case POSE_READ_FLIP: return "竖拿（按键在左）";
    default: return "未知";
  }
}
inline bool poseIsReading(uint8_t p) { return p == POSE_READ || p == POSE_READ_FLIP; }
inline bool poseIsDesk(uint8_t p) { return p == POSE_DESK || p == POSE_DESK_FLIP; }
