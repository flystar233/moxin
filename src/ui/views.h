#pragma once
// 所有界面的创建函数。界面对象由 app 的界面栈管理（push 之后不用自己 delete）。
#include <Arduino.h>

#include <functional>

#include "app/app.h"
#include "reader/book.h"

// 根界面
View *newDeskView();
View *newReaderView();

// 菜单
View *newReaderMenu();
View *newDeskMenu();
View *newReadingSettings();
View *newVoiceSettings();
View *newSystemSettings();
View *newLibraryView();
View *newTocView();

// 其他
View *newJumpView();
View *newTimeSetView();
View *newInfoView();
View *newCalView();
View *newWifiSetupView();
View *newTransferView();
View *newWifiMenu();
View *newMessageView(const String &line1, const String &line2, uint32_t autoCloseMs);
View *newConfirmView(const String &title, const String &line, std::function<void()> onYes);

// 直接画（不是界面）
void drawShutdownScreen(const char *reason);
void drawBusyBox(const char *msg);
void bootBanner(const char *msg);  // 刚上电、不知道屏上是什么：只重刷提示框那一块

// 阅读器的数据操作（界面之间共用）
namespace reader {
bool ensureOpen();                 // 第一次进阅读：载入字库、打开上次的书、分页
bool applyFont();                  // 按设置载入字库（字体或字号改了以后调）；失败时字库和设置都保持原样
bool openBook(const String &path); // 打开另一本书（失败时原来的书不动）
Layout layout();                   // 当前设置和屏幕方向下的版面
// 版面变了（字号、行距、方向）就重新分页，保持读到的位置。showBusy：大书先在屏上弹"正在重新排版"
// （画页面的过程中调用时要传 false，不然提示框会留在画面上）
void ensureLayout(bool showBusy = true);
void gotoOffset(uint32_t off);
void turn(int pages);              // 翻页并存进度
void saveProgress();
}  // namespace reader
