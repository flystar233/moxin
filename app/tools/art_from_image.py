"""把一幅水墨画（图片）转成关机画面：横屏 800×480 整幅缩放，竖屏 480×800 从里面裁一条。
输出 src/ui/art_shutdown.h（1 位，高位在左，1 = 黑）和预览 PNG。

转换：纸的底色和纸纹映射成纯白（不然满屏杂点），墨色按浓淡用 Floyd–Steinberg 抖动成黑白点；
固件写字的地方渐渐淡出成白。

用法：python tools/art_from_image.py 画.jpg [预览目录] [--portrait-x 竖屏裁剪起点（原图像素）]
需要 pillow、numpy。
"""
import argparse
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "..", "src", "ui", "art_shutdown.h")

# 固件写字的区域（和 view_misc.cpp 对应）
LAND_TEXT = (0, 396, 400, 480)   # 横屏左下，小舟左边
PORT_TEXT = (0, 705, 480, 800)   # 竖屏底部


def ink_of(img, white=226, black=40, gamma=0.85):
    """灰度图 → 墨色浓度 0～1。比 white 亮的（纸）都当白"""
    v = np.asarray(img, dtype=np.float32)
    d = (white - v) / (white - black)
    return np.clip(d, 0, 1) ** gamma


def fade_box(ink, box, feather):
    """box 里清成白，往外 feather 像素内渐变（像墨在宣纸上化开）"""
    h, w = ink.shape
    x0, y0, x1, y1 = box
    ys = np.arange(h)[:, None]
    xs = np.arange(w)[None, :]
    dx = np.maximum(np.maximum(x0 - xs, xs - (x1 - 1)), 0)
    dy = np.maximum(np.maximum(y0 - ys, ys - (y1 - 1)), 0)
    dist = np.sqrt(dx * dx + dy * dy)
    k = np.clip(dist / feather, 0, 1)
    k = k * k * (3 - 2 * k)
    return ink * k


def to_1bit(ink):
    g = Image.fromarray((255 * (1 - ink)).astype(np.uint8), "L")
    d = g.convert("1")  # Floyd–Steinberg；0 = 黑
    return Image.eval(d.convert("L"), lambda v: 255 - v).convert("1")  # 1 = 黑


def to_c(name, im):
    w, h = im.size
    px = im.load()
    stride = (w + 7) // 8
    data = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                data[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("preview", nargs="?")
    ap.add_argument("--portrait-x", type=float, default=None, help="竖屏从原图哪个 x 开始裁（按缩放到高 800 后的像素）")
    a = ap.parse_args()
    src = Image.open(a.image).convert("L")

    # 横屏：按宽 800 缩放，上下居中裁到 480
    s = 800 / src.width
    land = src.resize((800, round(src.height * s)), Image.LANCZOS)
    top = (land.height - 480) // 2
    land = land.crop((0, top, 800, top + 480))
    ink_l = fade_box(ink_of(land), LAND_TEXT, 14)

    # 竖屏：按高 800 缩放，裁一条 480 宽的（默认取右半边的山、亭子、松树）
    s = 800 / src.height
    big = src.resize((round(src.width * s), 800), Image.LANCZOS)
    px0 = int(a.portrait_x if a.portrait_x is not None else big.width * 0.53)
    px0 = max(0, min(px0, big.width - 480))
    port = big.crop((px0, 0, px0 + 480, 800))
    ink_p = fade_box(ink_of(port), PORT_TEXT, 70)

    L, P = to_1bit(ink_l), to_1bit(ink_p)
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n// 关机画面的水墨山水（tools/art_from_image.py 从图片生成，不要手改）。1 位，高位在左，1 = 黑\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define ART_LAND_W 800\n#define ART_LAND_H 480\n#define ART_PORT_W 480\n#define ART_PORT_H 800\n")
        f.write(f"// 固件写字的留白区：横屏 ({LAND_TEXT[0]},{LAND_TEXT[1]})～({LAND_TEXT[2]},{LAND_TEXT[3]})，"
                f"竖屏 ({PORT_TEXT[0]},{PORT_TEXT[1]})～({PORT_TEXT[2]},{PORT_TEXT[3]})\n")
        f.write(f"#define ART_LAND_TEXT_X1 {LAND_TEXT[2]}\n#define ART_LAND_TEXT_Y {LAND_TEXT[1]}\n")
        f.write(f"#define ART_PORT_TEXT_X1 {PORT_TEXT[2]}\n#define ART_PORT_TEXT_Y {PORT_TEXT[1]}\n\n")
        f.write(to_c("ART_LAND", L) + "\n\n" + to_c("ART_PORT", P) + "\n")
    if a.preview:
        os.makedirs(a.preview, exist_ok=True)
        for n, im in (("关机_横屏.png", L), ("关机_竖屏.png", P)):
            Image.eval(im.convert("L"), lambda v: 255 - v).save(os.path.join(a.preview, n))
    print("ok", OUT_H)


if __name__ == "__main__":
    main()
