#pragma once
// 按键任务：消抖、短按 / 长按 / 长按连发，电源键长按 3 秒。
void keysBegin(bool powerKeyHeldAtBoot);
// 这个键是不是已经被当作"卡住"（按住超过 KEY_STUCK_MS）：卡住的键不挡休眠、不当唤醒源
bool keyStuck(int pin);
