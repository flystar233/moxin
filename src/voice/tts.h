#pragma once
// 小说语音朗读：按句向 Edge 朗读（免费）/ qwen-audio-3.1-tts-flash 要音频，边下边播。
//
// 一条主线程 + 两个后台任务：
//   主线程（tick）：从正文里按句切分，排队等合成；跟着正在播的那一句自动翻页；
//   合成任务：一句一句请求，边收边把 PCM 写进环形缓冲（PSRAM）；
//   播放任务：从环形缓冲取 PCM，送 I2S（24kHz 16bit 单声道，左右声道各一份）。
// 环形缓冲有几百 KB（约 3～7 秒），所以一句还没放完，下一句已经在下来的路上了。
//
// 界面用法：
//   tts::start(bookOffset()) 开始；tts::togglePause() 暂停/继续；tts::stop() 停止；
//   tts::tick() 由 app::loop 每轮调用；tts::pollEvent() 取"读完了 / 出错了"。
#include <Arduino.h>

namespace tts {

// 能不能开始朗读（Wi-Fi 开着、有账号、有 API Key）。why 里是缺哪一样
bool ready(String *why = nullptr);

// 从正文 off 开始朗读（会连 Wi-Fi，失败返回 false，原因见 lastError）
bool start(uint32_t off);
// 念一段临时文本（试听、串口 tts say），不跟正文、不翻页
bool say(const String &text);
void stop();
void togglePause();
bool active();
bool paused();
uint32_t offset();     // 正在播的那一句在正文里的起点（试听时是 0）
String lastError();
String statusText();   // "朗读中" / "已暂停"

// 跳句 / 跳章：从正文某个位置重新开始（Wi-Fi 不断，但会重新合成）
bool jumpTo(uint32_t off);
bool nextSentence();
bool prevSentence();

void tick();  // app::loop 每轮调用
enum Event : uint8_t { EV_NONE, EV_DONE, EV_ERROR };
Event pollEvent(String &msg);

// 设置界面用：改完设置马上生效
void setVolume(int vol);
void setSpeed(float speed);  // 语速倍率（0.5～2.0），下一句生效
void setVoice(const String &voice);
void setKey(const String &key);
void setModel(const String &model, const String &workspace);

// 本次会话已经合成多少字（按输入字符算，和计费口径一致）
uint32_t charsUsed();

// 模型表（菜单里轮换用）
int modelCount();
const char *modelId(int i);
String modelLabel(const String &model);
String defaultVoice(const String &model);
float modelPrice(const String &model);  // 元/万字符（估算用）

// 音色表（每个模型的音色不一样）
int voiceCount(const String &model);
const char *voiceName(const String &model, int i);           // 请求用的名字
String voiceLabel(const String &model, const String &name);  // 菜单上显示的中文名

}  // namespace tts
