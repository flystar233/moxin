#pragma once
// 功放 U9 NS4168（I2S 输入，VSYS 供电）+ 喇叭 LS1。
// 两个用途：测试用的单音、扫频、音阶（阻塞播放）；语音朗读的流式 PCM（边收边放）。
// CTRL 高电平 = 开启并输出左声道，固件左右声道送同一份数据。
#include <stddef.h>
#include <stdint.h>

// ---- 测试音 ----
// 单音：频率 Hz、时长 ms、音量 % （0～100，插 USB 时最大按 70% 算，见文档 F8）
bool audioTone(int hz, int ms, int vol);
// 对数扫频 fromHz → toHz
bool audioSweep(int fromHz, int toHz, int ms, int vol);
// do re mi fa sol la si do
bool audioScale(int vol);

// ---- 流式 PCM（语音朗读）----
// 打开 I2S 和功放。rate = 采样率（16000 / 24000）。已经开着同一采样率就直接复用。
// 注意：换采样率会先关掉再打开，正在放的东西会中断（测试音和朗读不要同时用）。
bool audioBegin(uint32_t rate);
void audioSetVolume(int vol);  // 0～100
// 阻塞写一段：16bit 单声道，左右声道各送一份。frames = 采样数（不是字节数）。
// 开头会自动淡入 8ms，避免"啪"一声。
bool audioWriteMono(const int16_t *pcm, size_t frames);
// 淡出、关功放、卸 I2S
void audioEnd();
