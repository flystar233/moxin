"""生成 src/text/gbk_table.h：GBK 双字节 → Unicode（BMP）。
用法：python tools/gen_gbk.py
首字节 0x81～0xFE，尾字节 0x40～0xFE（0x7F 不用）。表按 (首-0x81)*191 + (尾-0x40) 排，0 = 没有这个字。"""
import os

rows = []
n_ok = 0
for lead in range(0x81, 0xFF):
    for trail in range(0x40, 0xFF):
        cp = 0
        if trail != 0x7F:
            try:
                ch = bytes([lead, trail]).decode("gbk")
                if len(ch) == 1 and ord(ch) <= 0xFFFF:
                    cp = ord(ch)
                    n_ok += 1
            except UnicodeDecodeError:
                pass
        rows.append(cp)

out = os.path.join(os.path.dirname(__file__), "..", "src", "text", "gbk_table.h")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("#pragma once\n// 由 tools/gen_gbk.py 生成，不要手改。GBK 双字节 → Unicode，%d 个字\n" % n_ok)
    f.write("#include <stdint.h>\n\nstatic const uint16_t GBK_TABLE[%d] = {\n" % len(rows))
    for i in range(0, len(rows), 16):
        f.write("  " + ", ".join("0x%04X" % v for v in rows[i:i + 16]) + ",\n")
    f.write("};\n")
print(out, len(rows), "项，有效", n_ok)
