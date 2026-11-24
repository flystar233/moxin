#pragma once
// 加速度计 U8 SC7A20H：姿态识别 + 软件双击检测，跑在单独的任务里（200Hz）。
#include <Arduino.h>

bool motionBegin();
// 轴向映射（0=X 1=Y 2=Z，符号 ±1）：TOP = 竖拿时朝上的轴，KEY = 指向按键那条边的轴
void motionSetAxes(int axTop, int sgTop, int axKey, int sgKey, bool initialPose = false);
void motionRaw(float g[3]);    // 低通后的传感器原始三轴（g），校准用

// 浅睡：睡前暂停采样任务（不卡在 I2C 中间），醒来恢复
bool motionPause(bool on);      // 返回采样任务是否已暂停；恢复时返回 true
uint32_t motionTapWaitMs(uint32_t now); // 采样暂停后查询：等待第二下的剩余时间，0 表示可睡
void motionLowPower();         // 深睡前调用
// 冲击唤醒：加速度计检测到敲击就锁存并拉高 INT2（GPIO8）
bool motionShockArm();         // 睡前配置中断并清锁存，成功才允许靠敲击唤醒
bool motionSeedTap(uint32_t now); // 确认是冲击中断，把它当作双击的第一下
uint8_t motionPose();          // 当前已确认的姿态（Pose）
bool motionFlat();             // 现在是否平放
uint32_t motionPoseChangedAt();
void motionGravity(float &top, float &keys, float &normal);  // 低通后的三个方向分量（g）

// 双击后背
void motionTapEnable(bool on);
void motionSetReadingUi(bool on);  // 主循环告诉它现在是不是阅读界面（双击只在阅读界面翻页）
bool motionTapEnabled();
void motionTapDebug(bool on);  // 串口打印每次冲击的幅度，用来调门限
void motionSetTapThr(float g);
float motionTapThr();
