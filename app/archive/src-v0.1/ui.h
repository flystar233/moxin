#pragma once
// 界面：桌面（横放）、阅读（竖拿）、菜单、提示、关机画面。只按屏幕宽高排版，换屏不用改这里。
#include <Arduino.h>
#include <vector>

#include "book.h"
#include "hw.h"

struct DeskData {
  DateTime now;
  bool timeOk;
  Battery bat;
  bool usb;
  bool co2Present;
  Climate clim;
};

struct MenuItem {
  String text;
};

void uiBegin();
void uiSetRotation(uint8_t rot);
void uiSetPartialFrames(uint8_t black, uint8_t whiteExtra);
bool uiShow(bool full);               // 把画好的帧刷到屏上
void uiResetPartialBaseline();        // 换方向后第一帧必须全刷

void uiDrawDesk(const DeskData &d);
void uiDrawReading(const DateTime &now, const Battery &bat);
void uiDrawMenu(const char *title, const std::vector<MenuItem> &items, int cursor);
void uiDrawMessage(const char *line1, const char *line2 = nullptr);
// 关机画面：关机时间、正在读的书和页码、电量
void uiDrawShutdown(const Battery &bat, const DateTime &now, bool timeOk, const String &book, int page, int pages);
// 屏幕正中一个带框的提示（"正在开机…"/"正在关机…"），马上局刷出来。
// screenUnknown = true：刚上电、不知道屏上原来是什么，只重刷提示框那一块
void uiBanner(const char *msg, bool screenUnknown);

// 阅读版面（在当前方向下）
Layout uiReadingLayout();
