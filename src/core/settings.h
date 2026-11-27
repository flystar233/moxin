#pragma once
// 用户设置：菜单里能改的都在这里，改完调 settingsSave() 存 NVS。默认值见 config.h。
#include <Arduino.h>

struct Settings {
  // 阅读
  String fontFamily;      // 卡上字库的字体名（wenkai），"" = 固件里的文泉驿 16
  uint8_t fontSize;       // 像素
  uint8_t lineSpacing;    // 0 紧凑 1 标准 2 宽松
  uint8_t margin;         // 0 窄 1 标准 2 宽
  uint8_t fullEvery;      // 每翻几页全刷一次
  bool tapTurn;           // 双击后背翻页
  float tapThr;           // 敲击门限（g）
  // 屏
  uint8_t pfBlack, pfWhite;  // 局刷波形
  // 朗读（Edge 朗读 / qwen-audio-3.1-tts-plus）
  String ttsModel;        // 模型 ID
  String ttsWs;           // 业务空间 ID（plus 模型用；百炼控制台右上角复制）
  String ttsKey;          // 百炼 API Key（只有 plus 用；无线传输网页或串口 tts key 设置）
  String ttsVoice;        // 音色：zh-CN-YunyangNeural / longanhuan_v3.1 / ...
  uint8_t ttsVolume;      // 音量 %
  float ttsSpeed;         // 语速倍率（0.5～2.0）
  bool ttsAutoPage;       // 朗读时自动翻页跟着
  // 系统
  bool autoDesk;          // 横放时自动切到桌面模式（躺着看书时可以关掉）
  bool wifiOn;            // Wi-Fi 总开关：关了就不自动对时，配网、无线传输也不能用
  bool autoTime;          // 每天自动联网对时一次
  bool lightSleep;        // 空闲浅睡（调试时可关）
  uint8_t deepSleepMin;   // 多少分钟没操作就深睡，0 = 不深睡
  // 姿态校准
  bool calibrated;
  int8_t axTop, sgTop, axKey, sgKey;
};

Settings &settings();
void settingsLoad();
void settingsSave();
void settingsReset();   // 恢复默认（不清阅读进度）

// 行距倍数（×10）
int lineSpacingX10(uint8_t idx);
const char *lineSpacingName(uint8_t idx);
const char *marginName(uint8_t idx);
