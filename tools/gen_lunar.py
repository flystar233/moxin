"""生成农历和二十四节气数据表 src/core/lunar_table.h（公历 2000～2100 年都能查：农历表 1999～2101 年，节气 2000～2100 年）。
数据来自 lunar_python（6tail/lunar-python，MIT），只在电脑上用一次：pip install lunar_python
用法：python tools/gen_lunar.py
"""
import datetime
import os

from lunar_python import LunarYear, Lunar, Solar

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "core", "lunar_table.h")
EPOCH = datetime.date(2000, 1, 1)
LY0, LY1 = 1999, 2101   # 农历年（多一年：最后一年只用来定 2100 年的边界）
SY0, SY1 = 2000, 2100   # 节气（公历年）

TERMS = ["小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨", "立夏", "小满", "芒种", "夏至",
         "小暑", "大暑", "立秋", "处暑", "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"]


def days(y, m, d):
    return (datetime.date(y, m, d) - EPOCH).days


new_year, bits, leap = [], [], []
for y in range(LY0, LY1 + 1):
    s = Lunar.fromYmd(y, 1, 1).getSolar()
    new_year.append(days(s.getYear(), s.getMonth(), s.getDay()))
    ms = LunarYear.fromYear(y).getMonthsInYear()
    b = 0
    for i, m in enumerate(ms):  # 按顺序（闰月排在它那个月后面），30 天的记 1
        if m.getDayCount() == 30:
            b |= 1 << i
    bits.append(b)
    leap.append(LunarYear.fromYear(y).getLeapMonth())
    assert len(ms) == (13 if leap[-1] else 12), (y, len(ms))

# 节气：每年 24 个，按 小寒（1 月）…… 冬至（12 月）的顺序，存几号
term_day = []
for y in range(SY0, SY1 + 1):
    row = [0] * 24
    d = datetime.date(y, 1, 1)
    while d.year == y:
        name = Solar.fromYmd(d.year, d.month, d.day).getLunar().getJieQi()
        if name in TERMS:
            k = TERMS.index(name)
            assert (k // 2 + 1) == d.month, (y, name, d)
            row[k] = d.day
        d += datetime.timedelta(days=1)
    assert all(row), (y, row)
    term_day.append(row)

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("// 自动生成：tools/gen_lunar.py（数据来自 lunar_python）。不要手改\n#pragma once\n#include <stdint.h>\n\n")
    f.write(f"#define LUNAR_Y0 {LY0}\n#define LUNAR_Y1 {LY1}\n#define TERM_Y0 {SY0}\n#define TERM_Y1 {SY1}\n\n")
    f.write("// 每个农历年正月初一：离 2000-01-01 多少天\n")
    f.write(f"static const int32_t LUNAR_NEWYEAR[{len(new_year)}] = {{" + ",".join(map(str, new_year)) + "};\n")
    f.write("// 每个农历年各月大小：第 i 位 = 这一年第 i 个月（闰月排在它那个月后面）是 30 天\n")
    f.write(f"static const uint16_t LUNAR_MONTHS[{len(bits)}] = {{" + ",".join(map(str, bits)) + "};\n")
    f.write("// 闰几月（0 = 不闰）\n")
    f.write(f"static const uint8_t LUNAR_LEAP[{len(leap)}] = {{" + ",".join(map(str, leap)) + "};\n")
    f.write("// 节气在几号：每年 24 个，小寒（1 月）…… 冬至（12 月）\n")
    f.write(f"static const uint8_t TERM_DAY[{len(term_day)}][24] = {{\n")
    for row in term_day:
        f.write("  {" + ",".join(map(str, row)) + "},\n")
    f.write("};\n")
print("ok", OUT)
