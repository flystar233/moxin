// 在电脑上逐日输出 src/core/lunar.cpp 的结果，给 tools/lunartest/check.py 和 lunar_python 对照
// 编译（MSYS2 ucrt64，在 app 目录下）：g++ -O2 -std=c++17 -Isrc tools/lunartest/main.cpp src/core/lunar.cpp -o lunartest
#include <stdio.h>

#include "core/lunar.h"

int main() {
  static const int DM[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for (int y = 2000; y <= 2100; y++)
    for (int m = 1; m <= 12; m++) {
      int dm = DM[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
      for (int d = 1; d <= dm; d++) {
        LunarDate l;
        int since = -1, until = -1;
        int t = solarTermLatest(y, m, d, since);
        int nx = solarTermNext(y, m, d, until);
        if (!lunarFromSolar(y, m, d, l)) { printf("%d-%d-%d -\n", y, m, d); continue; }
        printf("%d-%d-%d %d %d %d %d %s %s %d %s %d\n", y, m, d, l.year, l.month, l.leap, l.day, lunarText(l).c_str(),
               since == 0 ? solarTermName(t) : "-", since, nx >= 0 ? solarTermName(nx) : "-", until);
      }
    }
  return 0;
}
