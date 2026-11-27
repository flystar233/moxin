"""把 TTF/OTF 字体转成墨水屏用的 1 位点阵字库（.efnt），每个字号一个文件。

用法：
  python tools/makefont.py 字体.ttf --sizes 20 24 28 32 --name wenkai --out 输出目录 [--mode auto|ss|aa|mono] [--preview]
需要：pip install pillow fonttools

收字范围：字体里有的 ASCII、拉丁扩展、常用标点符号、CJK 符号和标点、假名、
CJK 统一汉字（U+4E00～9FFF，约 2 万字）、全角字符。不收扩展 A 区和兼容汉字（太少用，省空间）。

文件格式（小端）：
  文件头 32 字节：'EFNT' | 版本 u8=1 | 字号 u8 | ascent u8 | descent u8 | 保留 u8×2 | 保留 u16
                   | 字数 u32 | 索引偏移 u32 | 点阵偏移 u32 | 字体名 8 字节（ASCII，不足补 0）
  索引 每字 16 字节，按码点升序：码点 u32 | 点阵偏移 u32（相对点阵区）| 宽 u8 | 高 u8
                   | x 偏移 i8（左边离笔位）| y 偏移 i8（顶边在基线以上多少像素）| 前进宽度 u8 | 保留 3 字节
  点阵 每行按字节补齐，高位在左，1 = 黑
"""
import argparse
import os
import struct

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

RANGES = [
    (0x0020, 0x007E),  # ASCII
    (0x00A0, 0x024F),  # 拉丁补充、扩展
    (0x2010, 0x205E),  # 常用标点：— … ‘’“” 等
    (0x2100, 0x215F),  # ℃ № 等
    (0x2190, 0x21FF),  # 箭头
    (0x2460, 0x24FF),  # ①②③
    (0x2500, 0x257F),  # 制表符
    (0x25A0, 0x25FF),  # ■□▲●
    (0x2600, 0x26FF),  # ★☆
    (0x3000, 0x303F),  # CJK 符号和标点：、。〈〉《》「」【】
    (0x3040, 0x30FF),  # 平假名、片假名
    (0x4E00, 0x9FFF),  # CJK 统一汉字
    (0xFE30, 0xFE4F),  # 竖排标点
    (0xFF00, 0xFFEF),  # 全角 ASCII、全角标点
]


def in_gb2312(cp):
    try:
        chr(cp).encode("gb2312")
        return True
    except UnicodeEncodeError:
        return False


def charset(ttf_path, gb2312_only=False):
    cmap = TTFont(ttf_path, fontNumber=0, lazy=True).getBestCmap()
    cps = [cp for cp in sorted(cmap) if any(a <= cp <= b for a, b in RANGES)]
    return cps


def efnt_characters(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 32 or data[:5] != b"EFNT\x01":
        raise ValueError(f"{path}: not an EFNT v1 font")
    count, index, bitmap = struct.unpack_from("<III", data, 12)
    if index < 32 or index + 16 * count > bitmap or bitmap > len(data):
        raise ValueError(f"{path}: invalid EFNT index")
    return data[5], {struct.unpack_from("<I", data, index + 16 * i)[0] for i in range(count)}


SS = 4  # ss 模式的放大倍数


def render_glyph(font, ch, mode, thr, font_ss=None):
    """返回 (宽, 高, x偏移, y偏移, 前进宽度, 打包好的点阵)"""
    pad = 2
    if mode == "ss":
        # 放大 4 倍渲染，按 4×4 平均缩回来再取阈值：笔画粗细由真实覆盖率决定，不受 hinting 取整影响，
        # 1.5 像素左右的笔画不会一会儿 1 像素一会儿 2 像素
        adv = int(round(font_ss.getlength(ch) / SS))
        l4, t4, r4, b4 = font_ss.getbbox(ch, anchor="ls")
        l, t = l4 // SS, t4 // SS
        r, b = -(-r4 // SS), -(-b4 // SS)
        w, h = r - l, b - t
        if w <= 0 or h <= 0:
            return 0, 0, 0, 0, adv, b""
        g = Image.new("L", ((w + 2 * pad) * SS, (h + 2 * pad) * SS), 0)
        d = ImageDraw.Draw(g)
        d.text(((pad - l) * SS, (pad - t) * SS), ch, font=font_ss, fill=255, anchor="ls")
        g = g.resize((w + 2 * pad, h + 2 * pad), Image.BOX)
        im = g.point(lambda v: 1 if v >= thr else 0, mode="1")
        return pack(im, l, t, pad, adv)
    adv = int(round(font.getlength(ch)))
    l, t, r, b = font.getbbox(ch, anchor="ls")  # 以基线左端为原点，t 为负
    w, h = r - l, b - t
    if w <= 0 or h <= 0:
        return 0, 0, 0, 0, adv, b""
    if mode == "mono":
        im = Image.new("1", (w + 2 * pad, h + 2 * pad), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"  # FreeType 单色渲染（带 hinting）
        d.text((pad - l, pad - t), ch, font=font, fill=1, anchor="ls")
    else:
        g = Image.new("L", (w + 2 * pad, h + 2 * pad), 0)
        d = ImageDraw.Draw(g)
        d.text((pad - l, pad - t), ch, font=font, fill=255, anchor="ls")
        im = g.point(lambda v: 1 if v >= thr else 0, mode="1")
    return pack(im, l, t, pad, adv)


def pack(im, l, t, pad, adv):
    box = im.getbbox()
    if not box:
        return 0, 0, 0, 0, adv, b""
    x0, y0, x1, y1 = box
    im = im.crop(box)
    w, h = x1 - x0, y1 - y0
    xoff = l + x0 - pad          # 字形左边离笔位
    yoff = -(t + y0 - pad)       # 字形顶边在基线以上多少
    stride = (w + 7) // 8
    px = im.load()
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return w, h, xoff, yoff, adv, bytes(out)


def build(ttf, size, name, mode, thr, cps):
    font = ImageFont.truetype(ttf, size)
    font_ss = ImageFont.truetype(ttf, size * SS) if mode == "ss" else None
    asc, desc = font.getmetrics()
    index, data = [], bytearray()
    for cp in cps:
        w, h, xo, yo, adv, bits = render_glyph(font, chr(cp), mode, thr, font_ss)
        if w > 255 or h > 255 or adv > 255:
            continue
        xo = max(-128, min(127, xo))
        yo = max(-128, min(127, yo))
        index.append(struct.pack("<IIBBbbB3x", cp, len(data), w, h, xo, yo, adv))
        data += bits
    n = len(index)
    idx_off = 32
    bmp_off = idx_off + 16 * n
    head = struct.pack("<4sBBBBBBHIII8s", b"EFNT", 1, size, min(asc, 255), min(desc, 255), 0, 0, 0,
                       n, idx_off, bmp_off, name.encode("ascii")[:8])
    assert len(head) == 32
    return head + b"".join(index) + bytes(data), n, asc, desc


def preview(efnt, text, out_png, width=300):
    """用生成的字库排一段文字，看效果（模拟设备上的画法）"""
    import bisect
    hdr = struct.unpack_from("<4sBBBBBBHIII8s", efnt, 0)
    size, asc, desc, n, io, bo = hdr[2], hdr[3], hdr[4], hdr[8], hdr[9], hdr[10]
    cps = [struct.unpack_from("<I", efnt, io + 16 * i)[0] for i in range(n)]
    line_h = int(size * 1.5)
    im = Image.new("1", (width, 400), 1)
    px = im.load()
    x, y = 8, 8 + asc
    for ch in text:
        if ch == "\n":
            x, y = 8, y + line_h
            continue
        i = bisect.bisect_left(cps, ord(ch))
        if i >= n or cps[i] != ord(ch):
            x += size // 2
            continue
        _, off, w, h, xo, yo, adv = struct.unpack_from("<IIBBbbB3x", efnt, io + 16 * i)
        if x + adv > width - 8:
            x, y = 8, y + line_h
        stride = (w + 7) // 8
        for r in range(h):
            for c in range(w):
                if efnt[bo + off + r * stride + (c >> 3)] & (0x80 >> (c & 7)):
                    xx, yy = x + xo + c, y - yo + r
                    if 0 <= xx < width and 0 <= yy < 400:
                        px[xx, yy] = 0
        x += adv
    im.save(out_png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ttf")
    ap.add_argument("--sizes", type=int, nargs="+", default=[20, 24, 28, 32])
    ap.add_argument("--name", default="font")
    ap.add_argument("--out", default=".")
    ap.add_argument("--mode", choices=["auto", "ss", "aa", "mono"], default="auto",
                    help="auto（推荐）：16 号及以下用 mono，18 号及以上用 ss；"
                         "ss：放大 4 倍渲染再缩回取阈值，大字号笔画最均匀，但 16 号以下细笔画会断；"
                         "mono：FreeType 单色 hinting，小字号最完整，18 号以上有的字笔画会变 2 像素；"
                         "aa：抗锯齿渲染再取阈值")
    ap.add_argument("--thr", type=int, default=None, help="ss / aa 模式的阈值 0～255，越小笔画越粗（默认 ss 135、aa 110；11-13 由 150 改 135：22px 文楷断笔 66 字 → 7 字）")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--gb2312", action="store_true",
                    help="汉字只收 GB2312 里的 6763 个简体字（界面字体用，文件小 3 倍；ASCII 和标点照收）")
    ap.add_argument("--chars", default=None,
                    help="只收这些字（桌面大时钟这类只用到几个字的字库，比如 \"0123456789:-\"）")
    ap.add_argument("--missing-from", metavar="EFNT", help="只收指定 EFNT 字库里缺少的字，字号须一致")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    cps = charset(a.ttf)
    if a.gb2312:
        cps = [cp for cp in cps if cp < 0x2E80 or in_gb2312(cp)]
    if a.chars:
        want = set(ord(ch) for ch in a.chars)
        cmap = TTFont(a.ttf, fontNumber=0, lazy=True).getBestCmap()
        cps = sorted(cp for cp in want if cp in cmap)
    if a.missing_from:
        size, existing = efnt_characters(a.missing_from)
        if a.sizes != [size]:
            ap.error(f"--missing-from requires --sizes {size} to match the base font")
        cps = [cp for cp in cps if cp not in existing]
    print(f"字体里可收的字：{len(cps)} 个")
    sample = "秦孝公据崤函之固，拥雍州之地，君臣固守以窥周室，有席卷天下，包举宇内，囊括四海之意，并吞八荒之心。\n“你好，墨水屏！”——2026 年 11 月 10 日 ABC xyz 123"
    for s in a.sizes:
        mode = a.mode if a.mode != "auto" else ("mono" if s <= 16 else "ss")
        thr = a.thr if a.thr is not None else (135 if mode == "ss" else 110)
        blob, n, asc, desc = build(a.ttf, s, a.name, mode, thr, cps)
        path = os.path.join(a.out, f"{a.name}_{s}.efnt")
        with open(path, "wb") as f:
            f.write(blob)
        print(f"{path}（{mode}）：{n} 字，{len(blob) / 1048576:.2f} MB，ascent {asc} descent {desc}")
        if a.preview:
            preview(blob, sample, os.path.join(a.out, f"{a.name}_{s}_{mode}.png"))


if __name__ == "__main__":
    main()
