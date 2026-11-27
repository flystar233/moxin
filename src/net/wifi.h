#pragma once
// Wi-Fi：保存的账号、热点配网（手机扫码连热点 → 弹出网页选网络输密码）、联网、网络对时、
// 每天自动对时（后台任务）、无线传输（浏览器上传书 / 字库 / 固件）。
// 平时 Wi-Fi 关着（省电），只在配网、对时、传输时打开，用完就关。设置里的 Wi-Fi 总开关关了就一概不开。
#include <Arduino.h>

#include "board/sensors.h"

namespace net {

bool hasCredentials();
String savedSsid();
void forget();
bool busy();                   // Wi-Fi 正在用（配网、传输、后台对时）：这时不要睡

// ---- 热点配网 ----
enum ProvState : uint8_t {
  PROV_OFF,
  PROV_WAITING,      // 热点开着，等手机连上
  PROV_PHONE,        // 手机连上热点了 / 打开了配网页面
  PROV_CONNECTING,   // 收到账号，正在连家里的 Wi-Fi
  PROV_OK,           // 连上了，账号已保存，时间已校准
  PROV_FAIL,         // 没连上（密码不对、找不到网络……），可以在网页上重试
};
bool startAp();                // 开设备热点 InkReader-xxxx（随机密码），配网和无线传输共用
bool provStart();              // 开热点、DNS（让手机自动弹出配网页）、网页服务
void provStop();               // 关热点，Wi-Fi 关掉
void provLoop();               // 配网期间每轮主循环调一次
ProvState provState();
String provApSsid();
String provApPass();
String provMessage();          // 当前状态的说明（给屏幕显示）

// ---- 用保存的账号 ----
bool connect(uint32_t timeoutMs, String *why = nullptr);
bool syncTime(String *why = nullptr);  // 要先连上；成功会写进时钟芯片
void off();

// ---- 每天自动对时（后台任务，不卡界面）----
// allowed：总开关、自动对时开着、有账号、电量够、现在没在看菜单。每天对一次；失败了同一小时不再试
void autoSyncTick(const DateTime &now, bool timeOk, bool allowed);
bool autoSyncResult(bool &ok);  // 刚对完一次返回 true（只返回一次）

// ---- 无线传输：浏览器打开设备网址，上传书 / 字库 / 固件 ----
bool transferStart(String *why = nullptr);  // 先连家里 Wi-Fi，连不上就开设备热点
void transferStop();
void transferLoop();
bool transferActive();
bool transferViaAp();           // 用的是设备热点（不在家）
String transferUrl();           // http://192.168.x.x/
String transferMessage();       // 最近一件事（收到某本书……）
uint32_t transferVersion();     // 有变化就加一（界面据此刷新）
bool transferRebooting();       // 收到新固件，马上重启

}  // namespace net
