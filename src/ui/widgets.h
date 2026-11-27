#pragma once
// 各界面共用的版面零件：统一的边距、标题栏、底部提示、折行、提示框。
// 所有尺寸都从这里取，菜单、提示页、配网页的边距和字号就对得齐。
#include <Arduino.h>

#include <vector>

#include "gfx.h"

namespace ui {

int margin();     // 屏幕左右边距（标题线、分隔线、选中条都从这里开始）
int pad();        // 文字离选中条 / 框边的距离
int titleH();     // 标题栏高（含下面的粗线）
int footH();      // 底部提示栏高（含上面的细线）
int bodyTop();    // 标题栏下面内容区的顶
int bodyBottom(); // 底部提示栏上面内容区的底

// 标题栏 + 底部提示；right 是标题右边的小字（页码、进度）
void frame(const String &title, const String &hint, const String &right = "");

// 按宽度折行：中文逐字折，英文单词和数字不拆开，标点不放到行首；
// balance = true 时各行尽量一样长（居中的说明文字不会剩一两个字孤零零在最后一行）。
// 超过 maxLines 行的部分截掉加"…"
std::vector<String> wrap(const String &s, Font f, int maxW, int maxLines = 4, bool balance = false);

// 居中画一段（自动折行），y 是第一行基线；返回下一行的基线
int paragraph(int cx, int y, const String &s, Font f, int maxW, int maxLines = 4);

// 提示框（叠在当前画面中间，双线框）：title 用大字，body 用小字可以折几行，hint 是最下面一行小字
struct BoxRect { int x, y, w, h; };
BoxRect box(const String &title, const String &body = "", const String &hint = "");

}  // namespace ui
