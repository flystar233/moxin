#pragma once
// 升级回滚：串口 / 网页升级后，新固件第一次启动处于"试运行"。跑稳了才确认；
// 确认之前只要重启过（崩溃、看门狗、按 RESET、断电、深睡醒来），bootloader 就自动退回上一版固件。
// 用 esptool 直接烧的固件没有试运行这一说，这里什么也不做。
#include <Arduino.h>

namespace ota {
void begin();                    // setup 里调用：打印程序区，上次升级被退回的话提示一下
void loop();                     // 主循环里调用：试运行满 OTA_TRIAL_MS 自动确认
void confirm(const char *why);   // 深睡、关机、软件重启、开始下一次升级之前调用
String state();                  // 给 info 命令用，例如 "程序区 app1，试运行中"
}  // namespace ota
