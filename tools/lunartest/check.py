"""对照 lunartest 的输出和 lunar_python：python tools/lunartest/check.py lunartest的输出.txt"""
import sys

from lunar_python import Solar

bad = n = 0
for line in open(sys.argv[1], encoding="utf-8"):
    f = line.split()
    y, m, d = map(int, f[0].split("-"))
    l = Solar.fromYmd(y, m, d).getLunar()
    if f[1] == "-":
        print("超出范围", f[0]); bad += 1; continue
    ly, lm, leap, ld, text, term = int(f[1]), int(f[2]), f[3] == "1", int(f[4]), f[5], f[6]
    exp_text = "农历" + l.getMonthInChinese() + "月" + l.getDayInChinese()
    ok = (ly == l.getYear() and lm == abs(l.getMonth()) and leap == (l.getMonth() < 0) and ld == l.getDay()
          and text == exp_text and term == (l.getJieQi() or "-"))
    n += 1
    if not ok:
        bad += 1
        if bad < 10: print("不一致", line.strip(), "| 应为", l.getYear(), l.getMonth(), l.getDay(), exp_text, l.getJieQi())
print(f"{n} 天，{bad} 处不一致")
