#pragma once
// 农历和二十四节气（查表，表由 tools/gen_lunar.py 生成：公历 2000～2100 年）。
// 纯 C++，电脑上能逐日对照测（tools/lunartest）。
#include <stdint.h>

#include <string>

struct LunarDate {
  int year;     // 农历年（正月初一开始算）
  int month;    // 1～12
  bool leap;    // 闰月
  int day;      // 1～30
};

// 公历 → 农历；超出表的范围返回 false
bool lunarFromSolar(int y, int m, int d, LunarDate &out);
// "十月""闰四月""冬月""腊月""正月"
std::string lunarMonthName(const LunarDate &l);
// "初五""十五""廿三""三十"
std::string lunarDayName(int day);
// "农历十月初五"
std::string lunarText(const LunarDate &l);

// 节气：0 = 小寒 …… 23 = 冬至
const char *solarTermName(int i);
// 这一天或之前最近的一个节气，daysSince = 过了几天（0 = 就是今天）；超出表的范围返回 -1
int solarTermLatest(int y, int m, int d, int &daysSince);
// 今天之后的下一个节气，daysUntil = 还有几天；超出范围返回 -1
int solarTermNext(int y, int m, int d, int &daysUntil);
