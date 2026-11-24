"""生成关机画面的水墨山水（横屏 800×480、竖屏 480×800），输出 src/ui/art_shutdown.h 和预览 PNG。

画法：远山淡、近山浓，山脚化进雾里（大气透视）；墨色浓淡用 Floyd–Steinberg 抖动成 1 位黑白，
在 219 PPI 的墨水屏上像宣纸上的墨晕。勾线、小舟、飞鸟、题字、印章是纯黑，不抖动。
题字：王维《终南别业》"行到水穷处，坐看云起时"（霞鹜文楷，竖排）。
留白处（横屏左下 520×92、竖屏底部 480×95）由固件写关机时间、电量、读到哪本书。

用法：python tools/gen_shutdown_art.py 霞鹜文楷.ttf [预览输出目录]
需要 pillow、numpy。随机种子固定，每次生成的画一样。
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

TTF = sys.argv[1]
PREVIEW = sys.argv[2] if len(sys.argv) > 2 else None
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "..", "src", "ui", "art_shutdown.h")


# ---------------------------------------------------------------------------
# 噪声
def smooth_noise_1d(n, cells, rng):
    """余弦插值的一维值噪声，长度 n，cells 个控制点"""
    pts = rng.random(cells + 2)
    x = np.linspace(0, cells, n)
    i = np.floor(x).astype(int)
    f = x - i
    f = (1 - np.cos(f * np.pi)) / 2
    return pts[i] * (1 - f) + pts[i + 1] * f


def fbm_1d(n, rng, base=4, octaves=5):
    y = np.zeros(n)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        y += amp * smooth_noise_1d(n, base * 2 ** o, rng)
        tot += amp
        amp *= 0.5
    return y / tot


def noise_2d(w, h, cx, cy, rng):
    """二维值噪声（双线性 + 平滑），cx×cy 个格子"""
    g = rng.random((cy + 2, cx + 2))
    ys = np.linspace(0, cy, h)
    xs = np.linspace(0, cx, w)
    yi, xi = np.floor(ys).astype(int), np.floor(xs).astype(int)
    fy, fx = ys - yi, xs - xi
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = g[yi][:, xi]
    b = g[yi][:, xi + 1]
    c = g[yi + 1][:, xi]
    d = g[yi + 1][:, xi + 1]
    top = a * (1 - fx) + b * fx
    bot = c * (1 - fx) + d * fx
    return top * (1 - fy[:, None]) + bot * fy[:, None]


# ---------------------------------------------------------------------------
class Painting:
    def __init__(self, w, h, seed):
        self.w, self.h = w, h
        self.rng = np.random.default_rng(seed)
        self.ink = np.zeros((h, w))          # 墨色浓度 0～1（会被抖动）
        self.line = Image.new("1", (w, h), 0)  # 纯黑的线和字（1 = 黑）
        self.ld = ImageDraw.Draw(self.line)
        self.occ = np.zeros((h, w), bool)    # 有山的地方（水纹、飞鸟避开）

    def ridge(self, peaks, base, rough, rough_cells=6):
        """山脊线：几座山取最大 + 分形起伏。peaks: [(中心 x, 高度, 半宽, 偏斜 -1～1)]
        每座山是圆顶、陡坡的钟形（中国山水里的峰），偏斜让一边陡一边缓"""
        x = np.arange(self.w)
        hgt = np.zeros(self.w)
        for cx, ph, hw, skew in peaks:
            dx = x - cx
            hwe = hw * (1 + skew * np.sign(dx))
            t = np.exp(-np.abs(dx / hwe) ** 2.1 * 2.3)
            hgt = np.maximum(hgt, ph * t)
        hgt += rough * (fbm_1d(self.w, self.rng, rough_cells) - 0.5) * (0.4 + hgt / max(1, hgt.max()))
        return base - hgt

    def mountain(self, ridge, tone, mist_y, mist_h, edge=18, texture=0.5, streak=(60, 6)):
        """一层山：山脊下面上浓下淡，到 mist_y 附近化进雾里；新画的一层把后面的遮住（雾是白的，也遮）"""
        h, w = self.h, self.w
        ys = np.arange(h)[:, None]
        depth = ys - ridge[None, :]
        inside = depth >= 0
        d = tone * (0.28 + 0.72 * np.exp(-np.clip(depth, 0, None) / edge))
        # 皴擦：竖向拉长的噪声，像笔触
        n = noise_2d(w, h, streak[0], streak[1], self.rng)
        n2 = noise_2d(w, h, streak[0] * 3, streak[1] * 3, self.rng)
        t = 0.6 * n + 0.4 * n2
        d *= (1 - texture) + texture * (t * t) * 2.6
        # 雾：从 mist_y - mist_h 到 mist_y 渐渐变白
        fade = np.clip((mist_y - ys) / mist_h, 0, 1)
        fade = fade * fade * (3 - 2 * fade)
        # 雾线本身也起伏一点
        d *= fade
        self.ink = np.where(inside, np.clip(d, 0, 1), self.ink)
        self.occ |= inside & (fade > 0.15)
        return ridge

    def cun(self, ridge, x0, x1, n, depth=40, length=(6, 18), stop_y=None):
        """皴笔：沿坡往下的短弧线，靠近山脊多、往下少（披麻皴的意思）"""
        for _ in range(n):
            x = int(self.rng.uniform(x0 + 2, x1 - 3))
            dy = self.rng.exponential(depth)
            y = ridge[x] + 3 + dy
            if stop_y is not None and y > stop_y:
                continue
            slope = (ridge[min(x + 3, self.w - 1)] - ridge[max(x - 3, 0)]) / 6
            # 往低的一边斜着下去
            dirx = np.sign(slope) if abs(slope) > 0.15 else self.rng.choice([-1, 1])
            ln = self.rng.uniform(*length)
            pts = []
            for k in range(6):
                t = k / 5
                pts.append((x + dirx * ln * 0.45 * t + self.rng.uniform(-0.6, 0.6), y + ln * t))
            self.ld.line(pts, fill=1, width=1)

    def ridge_line(self, ridge, x0=0, x1=None, wmin=1.0, wmax=3.0, gap=0.25):
        """沿山脊勾一道深色的线，粗细随机变化，偶尔断开（飞白）"""
        x1 = x1 or self.w
        widths = wmin + (wmax - wmin) * fbm_1d(self.w, self.rng, 8, 3)
        brk = fbm_1d(self.w, self.rng, 20, 2)
        for x in range(x0, x1 - 1):
            if brk[x] < gap:
                continue
            y = ridge[x]
            wd = widths[x]
            self.ld.line([(x, y), (x + 1, ridge[x + 1])], fill=1, width=max(1, int(round(wd))))

    def moss(self, ridge, x0, x1, n, size=(1, 3)):
        """点苔：山脊上一簇簇小黑点（远看像树）"""
        for _ in range(n):
            x = int(self.rng.uniform(x0, x1))
            y = ridge[x] + self.rng.uniform(-1, 4)
            r = self.rng.uniform(*size)
            self.ld.ellipse([x - r, y - r * 1.3, x + r, y + r * 0.7], fill=1)

    def trees(self, ridge, x0, x1, n, hgt=(10, 22)):
        """山脊上的小树：一根竖线加几层横点"""
        for _ in range(n):
            x = int(self.rng.uniform(x0, x1))
            y = ridge[x] + 2
            th = self.rng.uniform(*hgt)
            self.ld.line([(x, y), (x, y - th)], fill=1, width=1)
            for k in range(3):
                yy = y - th * (0.45 + 0.2 * k)
                hw = th * (0.35 - 0.08 * k)
                self.ld.line([(x - hw, yy + 1), (x + hw, yy - 1)], fill=1, width=2)

    def pine(self, x, y, size, lean=0.15):
        """近景松树：下粗上细、有点弯的干，几团平展的松针（每团是一片密集的短线）"""
        pts = []
        for i in range(14):
            t = i / 13
            pts.append((x + lean * size * (np.sin(t * 2.4) * 0.6 + t), y - size * t))
        for i in range(13):  # 干：下粗上细
            wd = max(1, int(round((size / 22) * (1 - i / 16))))
            self.ld.line([pts[i], pts[i + 1]], fill=1, width=wd)
        for k in range(4):
            t = 0.42 + 0.15 * k
            px, py = pts[int(t * 13)]
            side = -1 if k % 2 else 1
            ln = size * (0.5 - 0.08 * k)
            ex, ey = px + side * ln * 0.75, py - 4
            self.ld.line([(px, py), (ex, ey)], fill=1, width=1)  # 枝
            # 松针团：一片扁的椭圆区域里密集的短斜线
            cw, ch = ln * 0.55, ln * 0.16
            for j in range(int(ln * 2.2)):
                ox = ex + self.rng.uniform(-cw, cw * 0.6) * (1 if side > 0 else -1)
                oy = ey + self.rng.uniform(-ch, ch * 0.4)
                if ((ox - ex) / cw) ** 2 + ((oy - ey) / ch) ** 2 > 1.2:
                    continue
                l = self.rng.uniform(3, 7)
                a = self.rng.uniform(0.3, 1.2) * self.rng.choice([-1, 1])
                self.ld.line([(ox, oy), (ox + l * np.cos(a) * 0.6, oy - l * abs(np.sin(a)))], fill=1, width=1)

    def water(self, y0, y1, n, keep_out=None):
        """水面：几道细横线"""
        for _ in range(n):
            y = self.rng.uniform(y0, y1)
            x = self.rng.uniform(0, self.w)
            if keep_out and keep_out[0] <= x <= keep_out[2] and keep_out[1] <= y <= keep_out[3]:
                continue
            ln = self.rng.uniform(8, 36)
            xi, xe, yi = int(x), int(min(x + ln, self.w - 1)), int(min(y, self.h - 1))
            if self.occ[yi, xi:xe + 1].any():
                continue
            self.ld.line([(x, y), (x + ln, y)], fill=1, width=1)

    def boat(self, x, y, s=1.0):
        """一叶扁舟和披蓑戴笠的渔人"""
        d = self.ld
        hull = [(x - 26 * s, y - 3 * s), (x - 18 * s, y + 3 * s), (x + 18 * s, y + 3 * s), (x + 28 * s, y - 5 * s),
                (x + 16 * s, y), (x - 16 * s, y)]
        d.polygon(hull, fill=1)
        d.polygon([(x - 2 * s, y - 2 * s), (x + 4 * s, y - 2 * s), (x + 3 * s, y - 11 * s), (x - 1 * s, y - 11 * s)], fill=1)
        d.polygon([(x - 7 * s, y - 11 * s), (x + 9 * s, y - 11 * s), (x + 1 * s, y - 16 * s)], fill=1)  # 斗笠
        d.line([(x + 6 * s, y - 6 * s), (x + 34 * s, y - 20 * s)], fill=1, width=1)  # 钓竿
        d.line([(x + 34 * s, y - 20 * s), (x + 36 * s, y + 4 * s)], fill=1, width=1)
        for k in range(3):  # 倒影
            yy = y + 6 * s + k * 3 * s
            d.line([(x - 14 * s + k * 4 * s, yy), (x + 12 * s - k * 4 * s, yy)], fill=1, width=1)

    def birds(self, pts, s=1.0):
        for x, y in pts:
            k = s * self.rng.uniform(0.8, 1.2)
            self.ld.line([(x - 6 * k, y - 2 * k), (x - 2 * k, y), (x, y - 1 * k), (x + 2 * k, y), (x + 7 * k, y - 3 * k)],
                         fill=1, width=1)

    def moon(self, x, y, r):
        self.ld.ellipse([x - r, y - r, x + r, y + r], outline=1, width=1)

    def vtext(self, x, y, text, size, gap=4):
        """竖排题字，一列"""
        f = ImageFont.truetype(TTF, size)
        for ch in text:
            self.ld.text((x, y), ch, font=f, fill=1, anchor="mt")
            y += size + gap
        return y

    def seal(self, x, y, size, ch):
        """印章：黑底白字（阴文），四角略圆，边上有点斑驳"""
        d = self.ld
        d.rounded_rectangle([x, y, x + size, y + size], radius=size // 8, fill=1)
        f = ImageFont.truetype(TTF, int(size * 0.78))
        tmp = Image.new("1", (size, size), 0)
        ImageDraw.Draw(tmp).text((size / 2, size / 2), ch, font=f, fill=1, anchor="mm")
        # 字笔画加粗一点（印章字是刻出来的，比书写粗）
        tmp2 = tmp.copy()
        for dx, dy in ((1, 0), (0, 1), (1, 1)):
            tmp2.paste(1, (dx, dy), tmp)
        self.line.paste(0, (x, y), tmp2)
        for _ in range(int(size * 0.6)):  # 斑驳
            px = x + self.rng.integers(0, size)
            py = y + self.rng.choice([self.rng.integers(0, 3), size - self.rng.integers(1, 4)])
            d.point((px, py), fill=0)

    def clear(self, box):
        """留白：这里要写字"""
        x0, y0, x1, y1 = box
        self.ink[y0:y1, x0:x1] = 0
        self.ld.rectangle([x0, y0, x1 - 1, y1 - 1], fill=0)

    def render(self):
        ink = np.clip(self.ink, 0, 1) ** 1.25
        ink = np.where(ink > 0.02, np.clip(ink + self.rng.normal(0, 0.05, ink.shape), 0, 1), ink)
        g = Image.fromarray((255 * (1 - ink)).astype(np.uint8), "L")
        dith = g.convert("1")  # Floyd–Steinberg，0 = 黑
        ink = Image.eval(dith.convert("L"), lambda v: 255 - v).convert("1")  # 1 = 黑
        out = Image.new("1", (self.w, self.h), 0)
        out.paste(1, (0, 0), ink)
        out.paste(1, (0, 0), self.line)
        return out  # 1 = 黑


# ---------------------------------------------------------------------------
def landscape():
    W, H = 800, 480
    p = Painting(W, H, seed=7)
    # 远山（最淡）：横贯，峰在中右
    r = p.ridge([(470, 110, 150, 0.3), (620, 150, 110, -0.2), (300, 60, 160, 0.2), (760, 120, 90, 0)], 300, 30, 9)
    p.mountain(r, 0.2, 335, 95, edge=26, texture=0.45)
    # 中景：右边主峰
    r = p.ridge([(640, 260, 95, -0.25), (745, 200, 70, 0.2), (540, 150, 80, 0.3), (460, 70, 60, 0)], 365, 40, 8)
    p.mountain(r, 0.5, 375, 120, edge=16, texture=0.6)
    p.ridge_line(r, 420, 800, 1.0, 2.4, 0.3)
    p.cun(r, 430, 800, 260, depth=45, stop_y=330)
    p.trees(r, 580, 780, 9, (6, 11))
    p.moss(r, 450, 800, 30, (0.8, 1.6))
    # 近景：右下崖石和松
    r = p.ridge([(770, 175, 80, -0.3), (660, 80, 55, 0.2)], 470, 25, 10)
    p.mountain(r, 0.8, 490, 70, edge=10, texture=0.65, streak=(90, 10))
    p.ridge_line(r, 590, 800, 1.5, 3.5, 0.15)
    p.cun(r, 600, 800, 160, depth=30, length=(8, 22))
    p.moss(r, 600, 800, 25, (1, 2.2))
    p.pine(718, int(r[718]) + 4, 96, lean=-0.3)
    # 水、舟、鸟、月
    p.water(372, 470, 34, keep_out=(0, 385, 520, 480))
    p.boat(330, 372, 1.0)
    p.birds([(296, 166), (318, 156), (309, 182)], 1.2)
    p.moon(206, 92, 22)
    # 题字（右起竖排两列）和印章，放在左上的天空里
    p.vtext(96, 34, "行到水穷处", 30)
    p.vtext(56, 34, "坐看云起时", 30)
    p.seal(40, 222, 38, "息")
    p.clear((0, 388, 520, 480))  # 固件写字的地方
    return p.render()


def portrait():
    W, H = 480, 800
    p = Painting(W, H, seed=11)
    # 远山：峰在右，低一点，不挡题字
    r = p.ridge([(300, 150, 120, 0.2), (420, 120, 80, -0.2), (120, 120, 100, 0)], 500, 30, 7)
    p.mountain(r, 0.2, 530, 110, edge=26, texture=0.45)
    # 中景主峰：高耸，偏左
    r = p.ridge([(170, 400, 105, -0.15), (290, 270, 85, 0.25), (45, 290, 75, 0.1), (370, 150, 70, 0)], 575, 35, 7)
    p.mountain(r, 0.5, 590, 150, edge=16, texture=0.6)
    p.ridge_line(r, 0, 420, 1.0, 2.4, 0.3)
    p.cun(r, 0, 420, 320, depth=50, stop_y=540)
    p.trees(r, 40, 330, 9, (6, 12))
    p.moss(r, 0, 420, 30, (0.8, 1.6))
    # 近景：左下崖石和松（在留白区上面）
    r = p.ridge([(40, 150, 90, 0.2), (170, 60, 55, 0)], 700, 20, 8)
    p.mountain(r, 0.8, 712, 60, edge=10, texture=0.65, streak=(70, 12))
    p.ridge_line(r, 0, 260, 1.5, 3.5, 0.15)
    p.cun(r, 0, 250, 140, depth=30, length=(8, 22), stop_y=700)
    p.moss(r, 0, 240, 22, (1, 2.2))
    p.pine(70, int(r[70]) + 4, 118, lean=0.32)
    # 水、舟、鸟、月
    p.water(600, 700, 26, keep_out=(0, 700, 480, 800))
    p.boat(340, 646, 1.0)
    p.birds([(320, 330), (342, 320), (334, 346)], 1.2)
    p.moon(110, 96, 24)
    # 题字：右上竖排
    p.vtext(440, 70, "行到水穷处", 30)
    p.vtext(400, 70, "坐看云起时", 30)
    p.seal(384, 258, 38, "息")
    p.clear((0, 705, 480, 800))
    return p.render()


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
    land, port = landscape(), portrait()
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n// 关机画面的水墨山水（tools/gen_shutdown_art.py 生成，不要手改）。1 位，高位在左，1 = 黑\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define ART_LAND_W 800\n#define ART_LAND_H 480\n#define ART_PORT_W 480\n#define ART_PORT_H 800\n")
        f.write("// 固件写字的留白区：横屏左下 (0,388)～(520,480)，竖屏底部 (0,705)～(480,800)\n")
        f.write("#define ART_LAND_TEXT_Y 388\n#define ART_PORT_TEXT_Y 705\n\n")
        f.write(to_c("ART_LAND", land) + "\n\n" + to_c("ART_PORT", port) + "\n")
    if PREVIEW:
        os.makedirs(PREVIEW, exist_ok=True)
        for n, im in (("关机_横屏.png", land), ("关机_竖屏.png", port)):
            Image.eval(im.convert("L"), lambda v: 255 - v).save(os.path.join(PREVIEW, n))
    print("ok", OUT_H)


if __name__ == "__main__":
    main()
