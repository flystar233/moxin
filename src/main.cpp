// 墨水屏阅读器 · 主程序
//   横放（按键朝上）→ 桌面模式：时间、电量、CO2 / 温湿度、在读的书
//   竖拿（按键在右）→ 阅读模式：TF 卡里的 TXT（UTF-8 / GBK），按键翻页，后背双击翻下一页
//   倒着拿（按键在左）→ 画面转 180°，上一页 / 下一页自动对调
//
// 代码结构：
//   board/    板级硬件：电源、I2C 外设、TF 卡
//   display/  墨水屏驱动（换屏只动这里）
//   input/    按键、加速度计（姿态 + 双击）
//   text/     UTF-8、编码转换、点阵字库
//   reader/   书：打开、章节、分页、书库和进度
//   ui/       画图和各个界面
//   app/      界面栈、刷新策略、省电、串口命令
//   core/     设置、串口传文件
#include <Arduino.h>

#include "app/app.h"
#include "board/i2c.h"
#include "board/power.h"
#include "board/sensors.h"
#include "config.h"
#include "core/ota.h"
#include "input/events.h"
#include "input/keys.h"
#include "input/motion.h"
#include "ui/gfx.h"
#include "ui/views.h"

QueueHandle_t g_events;
void consoleHelp();

// 开机：电源键从上电起要按满 3 秒才算开机；提前松手就断电（文档 v4.26）
static bool bootPowerKeyCheck() {
  if (!powerKeyDown()) return false;  // 不是按键开机（R33 / 软件重启 / 插 USB 调试）
  while (powerKeyDown() && millis() < PWR_LONG_MS) delay(10);
  if (millis() < PWR_LONG_MS) {
    Serial.println("[电源] 电源键没按满 3 秒，关机");
    powerCut();
    Serial.println("[电源] 没断掉（R33 或一直有电），继续开机");
    return false;
  }
  return true;
}

void setup() {
  powerHoldOn();
  WakeInfo wake = powerWakeInfo();
  if (wake.reason != WAKE_NONE) powerReleaseHolds();
  ampOff();
  pinMode(PIN_PWR_KEY_N, INPUT);
  pinMode(PIN_USB_PRES, INPUT);
  Serial.setRxBufferSize(24576);  // 默认 256 字节，满了会丢数据；传文件时一次在途 16KB（xfer.cpp CHUNK）
  Serial.begin(115200);
  ota::begin();
  g_events = xQueueCreate(32, sizeof(Event));

  // 等电源键按满 3 秒的同时把姿态和屏准备好（只读不写，这段时间随时可能断电）
  i2cBegin();
  bool hasAcc = motionBegin();
  for (int i = 0; i < 2 && !hasAcc; i++) {  // 认不到就再试两次（之后运行中还会每 10 秒找一次）
    delay(20);
    hasAcc = motionBegin();
  }
  gfx::begin();
  bool keyBoot = false;
  if (wake.reason == WAKE_NONE) {
    keyBoot = bootPowerKeyCheck();
    if (keyBoot) {  // 按满 3 秒：马上局刷"正在开机"，让人知道可以松手了
      uint8_t p = hasAcc ? motionPose() : POSE_UNKNOWN;
      gfx::setRotation(p == POSE_READ ? ROT_READ : p == POSE_READ_FLIP ? ROT_READ_FLIP : p == POSE_DESK_FLIP ? ROT_DESK_FLIP : ROT_DESK);
      bootBanner("正在开机…");
    }
  }

  bool hasRtc = rtcBegin();
  bool hasGauge = gaugeBegin();
  bool co2 = co2Begin();
  Serial.printf("\n[开机] %s %s（编译 %s %s）；时钟 %s，电量计 %s，CO2 %s，加速度计 %s\n", FW_NAME, FW_VERSION, __DATE__,
                __TIME__, hasRtc ? "OK" : "无", hasGauge ? "OK" : "无", co2 ? "OK" : "未安装", hasAcc ? "OK" : "无");

  keysBegin(keyBoot || (wake.reason == WAKE_KEY && wake.pin == PIN_PWR_KEY_N));
  app::begin({keyBoot, wake, hasAcc, hasRtc, hasGauge});
  if (wake.reason == WAKE_NONE) consoleHelp();
  setCpuFrequencyMhz(CPU_MHZ);
}

void loop() {
  ota::loop();
  app::loop();
}
