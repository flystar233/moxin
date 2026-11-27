#pragma once
// 按键任务：消抖、短按 / 长按 / 长按连发，电源键短按和长按 3 秒。
#include <Arduino.h>

// powerKeyHeldAtBoot：开机那次按压松手前不算关机
void keysBegin(bool powerKeyHeldAtBoot);
// 这个键是不是已经被当作"卡住"（按住超过 KEY_STUCK_MS）：卡住的键不挡休眠、不当唤醒源
bool keyStuck(int pin);
// 所有键（不算卡住的）都松开了
bool keysAllReleased();
// GPIO 号 → 物理键（PK_*），电源键返回 0xFF，不是按键返回 0xFE
uint8_t keyFromPin(int pin);
