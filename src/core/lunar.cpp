#include "lunar.h"

#include "lunar_table.h"

// 离 2000-01-01 多少天（公历）
static int32_t dayNumber(int y, int m, int d) {
  // 先算到 0000-03-01 的天数（把 1、2 月当上一年的 13、14 月）
  auto f = [](int y, int m, int d) -> int32_t {
    if (m < 3) { y--; m += 12; }
    return 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + d - 1;
  };
  return f(y, m, d) - f(2000, 1, 1);
}

bool lunarFromSolar(int y, int m, int d, LunarDate &out) {
  const int32_t n = dayNumber(y, m, d);
  const int years = LUNAR_Y1 - LUNAR_Y0 + 1;
  int k = -1;
  for (int i = 0; i < years; i++) {
    if (LUNAR_NEWYEAR[i] <= n) k = i;
    else break;
  }
  if (k < 0 || k == years - 1) return false;  // 表的最后一年只用来定界
  int32_t off = n - LUNAR_NEWYEAR[k];
  const int leap = LUNAR_LEAP[k], count = leap ? 13 : 12;
  for (int i = 0; i < count; i++) {
    int len = (LUNAR_MONTHS[k] >> i) & 1 ? 30 : 29;
    if (off < len) {
      out.year = LUNAR_Y0 + k;
      // 第 i 个月是几月：闰月排在它那个月后面
      if (leap && i == leap) { out.month = leap; out.leap = true; }
      else { out.month = (leap && i > leap) ? i : i + 1; out.leap = false; }
      out.day = off + 1;
      return true;
    }
    off -= len;
  }
  return false;
}

std::string lunarMonthName(const LunarDate &l) {
  static const char *const M[] = {"正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"};
  std::string s = l.leap ? "闰" : "";
  s += M[(l.month - 1) % 12];
  return s + "月";
}

std::string lunarDayName(int day) {
  static const char *const N[] = {"一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};
  if (day == 10) return "初十";
  if (day == 20) return "二十";
  if (day == 30) return "三十";
  static const char *const T[] = {"初", "十", "廿", "三"};
  return std::string(T[(day - 1) / 10]) + N[(day - 1) % 10];
}

std::string lunarText(const LunarDate &l) { return "农历" + lunarMonthName(l) + lunarDayName(l.day); }

const char *solarTermName(int i) {
  static const char *const T[] = {"小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
                                  "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
                                  "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"};
  return (i >= 0 && i < 24) ? T[i] : "";
}

// 某年第 i 个节气（i 可以是 -1 = 上一年冬至，24 = 下一年小寒）的天数编号
static bool termDay(int y, int i, int32_t &n) {
  if (i < 0) { y--; i += 24; }
  if (i > 23) { y++; i -= 24; }
  if (y < TERM_Y0 || y > TERM_Y1) return false;
  n = dayNumber(y, i / 2 + 1, TERM_DAY[y - TERM_Y0][i]);
  return true;
}

int solarTermLatest(int y, int m, int d, int &daysSince) {
  const int32_t n = dayNumber(y, m, d);
  for (int i = 23; i >= -1; i--) {
    int32_t t;
    if (!termDay(y, i, t)) continue;
    if (t <= n) {
      daysSince = n - t;
      return (i + 24) % 24;
    }
  }
  return -1;
}

int solarTermNext(int y, int m, int d, int &daysUntil) {
  const int32_t n = dayNumber(y, m, d);
  for (int i = 0; i <= 24; i++) {
    int32_t t;
    if (!termDay(y, i, t)) continue;
    if (t > n) {
      daysUntil = t - n;
      return i % 24;
    }
  }
  return -1;
}
