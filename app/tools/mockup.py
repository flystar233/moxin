"""离线画 4.26 寸屏（800×480）的界面示意图：阅读页（各个字号）和桌面首页。
排版规则照抄固件（ui/view_reader.cpp、reader/book.cpp 的 layoutFrom、ui/view_desk.cpp），
文字用真实的 .efnt 点阵字库。大字时钟和读数在固件里是 U8g2 的 Logisoso，这里用 Windows 的
Bahnschrift 窄体近似（只影响数字字形，位置和大小一样）。

用法：python tools/mockup.py --fonts 字库目录 --ui data --book 书.txt --out 输出目录
需要 pillow。
"""
import argparse
import os
import re
import struct

from PIL import Image, ImageDraw, ImageFont

SHORT = 480  # 屏幕短边


def px(v):  # gfx::px()
    return (v * SHORT + 150) // 300


# ---------------------------------------------------------------------------
class EFont:
    def __init__(self, path):
        b = open(path, "rb").read()
        assert b[:4] == b"EFNT"
        self.size, self.asc, self.desc = b[5], b[6], b[7]
        n, io, bo = struct.unpack_from("<III", b, 12)
        self.g = {}
        for k in range(n):
            cp, off, w, h, xo, yo, adv = struct.unpack_from("<IIBBbbB", b, io + 16 * k)
            self.g[cp] = (bo + off, w, h, xo, yo, adv)
        self.b = b

    def adv(self, cp):
        e = self.g.get(cp)
        return e[5] if e else 0

    def draw(self, im, x, y, cp, color=0, bold=False):
        e = self.g.get(cp)
        if not e:
            return False
        off, w, h, xo, yo, adv = e
        stride = (w + 7) // 8
        px_ = im.load()
        W, H = im.size
        for r in range(h):
            row = self.b[off + r * stride: off + (r + 1) * stride]
            for c in range(w):
                if row[c >> 3] & (0x80 >> (c & 7)):
                    for dx in ((0, 1) if bold else (0,)):
                        X, Y = x + xo + c + dx, y - yo + r
                        if 0 <= X < W and 0 <= Y < H:
                            px_[X, Y] = color
        return True


class UiText:
    """界面文字（gfx::text 的大屏路径）：界面字库没有的字用正文字库补，都没有画方框"""

    def __init__(self, ui, body):
        self.ui, self.body = ui, body

    def width(self, s, f):
        s = s.replace("\u3000", "  ")
        w = 0
        for ch in s:
            cp = ord(ch)
            a = f.adv(cp) or self.body.adv(cp)
            w += a if a else f.asc
        return w

    def text(self, im, x, y, s, f, color=0):
        s = s.replace("\u3000", "  ")
        x0 = x
        for ch in s:
            cp = ord(ch)
            if f.draw(im, x, y, cp, color):
                x += f.adv(cp)
            elif self.body.draw(im, x, y, cp, color):
                x += self.body.adv(cp)
            else:
                ImageDraw.Draw(im).rectangle([x + 1, y - f.asc + 1, x + f.asc - 2, y - 1], outline=color)
                x += f.asc
        return x - x0

    def fit(self, s, f, maxw):
        if self.width(s, f) <= maxw:
            return s
        ell = self.width("…", f)
        out = ""
        for ch in s:
            if self.width(out + ch, f) + ell > maxw:
                break
            out += ch
        return out + "…"


def hline(d, x, y, w):
    d.line([x, y, x + w - 1, y], fill=0)


def battery(im, x, y, w, h, pct, charging):
    d = ImageDraw.Draw(im)
    d.rectangle([x, y, x + w - 1, y + h - 1], outline=0)
    d.rectangle([x + w, y + h // 4, x + w + 1, y + h - h // 4 - 1], fill=0)
    fill = (w - 4) * max(0, min(100, pct)) // 100
    if fill > 0:
        d.rectangle([x + 2, y + 2, x + 2 + fill - 1, y + h - 3], fill=0)


# ---------------------------------------------------------------------------
# 排版（reader/book.cpp layoutFrom 的 Python 版）
CLOSING = set(map(ord, "，。、；：？！）”’》」』】…,.;:!?)"))
OPENING = set(map(ord, "“‘《「『【（〈〔(["))
SPACES = {0x20, 0x09, 0x3000, 0xA0}


def is_word(c):
    return c < 0x80 and (chr(c).isalnum() or c in (0x27, 0x2D, 0x5F))


def layout_from(t, start, max_lines, max_w, indent, adv, chapters, heads):
    n = len(t)
    pos = start
    para = start == 0 or t[start - 1] == "\n"
    in_head = False
    lines = []
    while pos < n and len(lines) < max_lines:
        if t[pos] == "\n":
            pos += 1
            para = True
            in_head = False
            continue
        if para:
            while pos < n and ord(t[pos]) in SPACES:
                pos += 1
            if pos >= n or t[pos] == "\n":
                continue
            if pos in chapters and lines:
                break
            if pos in heads:
                in_head = True
        kind = "head" if in_head else ("para" if para else "text")
        avail = max_w - (indent if kind == "para" else 0)
        para = False
        st = pos
        w = 0
        hung = False
        while pos < n and t[pos] != "\n":
            cp = ord(t[pos])
            cw = adv(cp)
            if w + cw > avail and pos > st:
                if not hung and cp in CLOSING:
                    hung = True
                else:
                    if is_word(cp):
                        k = pos
                        back = 0
                        while k > st and is_word(ord(t[k - 1])):
                            k -= 1
                            back += adv(ord(t[k]))
                        if k > st:
                            pos, w = k, w - back
                    if pos - 1 > st and ord(t[pos - 1]) in OPENING:
                        w -= adv(ord(t[pos - 1]))
                        pos -= 1
                    break
            w += cw
            pos += 1
        ends = pos >= n or t[pos] == "\n"
        lines.append(dict(start=st, end=pos, kind=kind, width=w, justify=(not ends and kind != "head" and w <= avail)))
        if in_head and ends and len(lines) < max_lines:
            lines.append(dict(start=pos, end=pos, kind="blank", width=0, justify=False))
    return lines, pos


# ---------------------------------------------------------------------------
def reading_page(body, ui_small, txt, size_label, out):
    W, H = 480, 800
    ut = UiText(ui_small, body)
    mx = 12 * W // 300
    head_h = ui_small.asc + ui_small.desc + px(9)
    foot_h = ui_small.asc + ui_small.desc + px(8)
    top = head_h + px(8)
    size = body.size
    lh = (size * 15 + 5) // 10
    bottom = H - foot_h - px(6)
    nlines = max(1, (bottom - top - size - size // 4) // lh + 1)
    max_w = W - 2 * mx
    indent = 2 * size

    def adv(cp):
        a = body.adv(cp)
        if a:
            return a
        return size if cp >= 0x2E80 or cp == 9 else (size + 1) // 2

    # 章节：行首"第X章"
    chapters, titles = [], {}
    for m in re.finditer(r"(?m)^[ \u3000]*(第.{1,12}?[章回节卷].*)$", txt):
        s = m.start(1)
        if len(m.group(1)) <= 30:
            chapters.append(s)
            titles[s] = m.group(1).strip()
    chap_set = set(chapters)
    # 分页（为了总页数）
    pages = [0]
    pos = 0
    while True:
        _, nxt = layout_from(txt, pos, nlines, max_w, indent, adv, chap_set, chap_set)
        if nxt <= pos or not txt[nxt:].strip():
            break
        pages.append(nxt)
        pos = nxt
    # 画第一章开始那一页
    page = max(i for i, p in enumerate(pages) if p <= chapters[0])
    lines, _ = layout_from(txt, pages[page], nlines, max_w, indent, adv, chap_set, chap_set)

    im = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(im)
    cur = [c for c in chapters if c <= pages[page]]
    head = titles[cur[-1]] if cur else "书名"
    ut.text(im, mx, head_h - px(7), ut.fit(head, ui_small, W - 2 * mx), ui_small)
    hline(d, mx, head_h - 2, W - 2 * mx)
    for k, ln in enumerate(lines):
        if ln["kind"] == "blank":
            continue
        y = top + body.asc + k * lh
        x = mx + (indent if ln["kind"] == "para" else 0)
        avail = max_w - (indent if ln["kind"] == "para" else 0)
        bold = ln["kind"] == "head"
        if bold:
            x = mx + max(0, (max_w - ln["width"]) // 2)
        seg = txt[ln["start"]:ln["end"]]
        stretch = lambda a, b: not (a < 0x80 and chr(a).isalnum() and b < 0x80 and chr(b).isalnum())
        gaps = sum(1 for a, b in zip(seg, seg[1:]) if stretch(ord(a), ord(b))) if ln["justify"] else 0
        extra = avail - ln["width"] if ln["justify"] else 0
        if extra < 0 or extra > size * 3:
            extra = 0
        acc, done, prev = 0, 0, None
        for ch in seg:
            cp = ord(ch)
            if prev is not None and stretch(prev, cp):
                done += 1
            off = extra * done // gaps if gaps else 0
            body.draw(im, x + acc + off, y, cp, 0, bold)
            acc += adv(cp)
            prev = cp
    fb = H - px(7)
    ut.text(im, mx, fb, f"{page + 1} / {len(pages)}", ui_small)
    pct = f"{pages[page] * 1000 // len(txt) / 10:.1f}%"
    ut.text(im, W // 2 - ut.width(pct, ui_small) // 2, fb, pct, ui_small)
    bw, bh = px(20), px(10)
    battery(im, W - mx - bw - 2, fb - bh + 1, bw, bh, 85, False)
    tm = "21:30"
    ut.text(im, W - mx - bw - px(8) - ut.width(tm, ui_small), fb, tm, ui_small)
    im.save(out)
    return nlines, len(pages), (W - 2 * mx) // size


def num_font(target_h, text="0123456789"):
    f = ImageFont.truetype("C:/Windows/Fonts/bahnschrift.ttf", 100)
    f.set_variation_by_name("SemiBold Condensed")
    l, t, r, b = f.getbbox("0", anchor="ls")
    size = int(100 * target_h / (b - t))
    f = ImageFont.truetype("C:/Windows/Fonts/bahnschrift.ttf", size)
    f.set_variation_by_name("SemiBold Condensed")
    return f


def desk_page(ui, small, body, co2, out):
    W, H = 800, 480
    im = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(im)
    ut = UiText(ui, body)
    P = px
    M = P(12)
    hb = P(8) + ui.asc
    hl = hb + P(9)
    ut.text(im, M, hb, "11月12日\u3000星期四", ui)
    bw, bh = P(26), P(13)
    battery(im, W - M - bw - 2, hb - bh + 1, bw, bh, 85, False)
    b = "85%"
    ut.text(im, W - M - bw - P(10) - ut.width(b, ui), hb, b, ui)
    hline(d, M, hl, W - 2 * M)
    y0 = H - P(96)
    clock = num_font(92)
    s = "21:30"
    cw = d.textlength(s, font=clock)
    cy = hl + (y0 - hl + 92) // 2
    d.text((W / 2 - cw / 2, cy), s, font=clock, fill=0, anchor="ls")
    hline(d, M, y0, W - 2 * M)
    reading = "在读《西游记》 23%"
    if co2:
        val = num_font(46)
        labels, vals, units = ["二氧化碳", "温度", "湿度"], ["650", "23.5", "48"], ["ppm", "°C", "%"]
        for i in range(3):
            cx = M + (W - 2 * M) * i // 3 + P(4)
            lb = y0 + P(6) + small.asc
            ut.text(im, cx, lb, labels[i], small)
            vb = lb + P(6) + 46
            d.text((cx, vb), vals[i], font=val, fill=0, anchor="ls")
            vw = int(d.textlength(vals[i], font=val))
            ut.text(im, cx + vw + P(4), vb, units[i], small)
        fh = small.asc + small.desc + P(8)
        hline(d, M, H - fh, W - 2 * M)
        ut.text(im, M, H - P(6), reading, small)
    else:
        lb = y0 + P(6) + small.asc
        ut.text(im, M, lb, "在读", small)
        tb = lb + P(6) + ui.asc
        ut.text(im, M, tb, "《西游记》", ui)
        ut.text(im, W - M - ut.width("23%", ui), tb, "23%", ui)
        barw, by = W - 2 * M, tb + P(8)
        d.rectangle([M, by, M + barw - 1, by + P(6) - 1], outline=0)
        d.rectangle([M + 2, by + 2, M + 2 + (barw - 4) * 230 // 1000, by + P(6) - 3], fill=0)
        ut.text(im, M, H - P(6), "竖着拿起来接着读，菜单键打开设置", small)
    im.save(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts", required=True)
    ap.add_argument("--ui", required=True)
    ap.add_argument("--book", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sizes", type=int, nargs="+", default=[22, 24, 26, 28])
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    ui = EFont(os.path.join(a.ui, "ui_big.efnt"))
    small = EFont(os.path.join(a.ui, "ui_small.efnt"))
    raw = open(a.book, "rb").read()
    try:
        txt = raw.decode("utf-8")
    except UnicodeDecodeError:
        txt = raw.decode("gbk")
    txt = txt.replace("\r\n", "\n").replace("\r", "\n")
    imgs = []
    for z in a.sizes:
        body = EFont(os.path.join(a.fonts, f"wenkai_{z}.efnt"))
        out = os.path.join(a.out, f"阅读_{z}号.png")
        nl, np_, cpl = reading_page(body, small, txt, z, out)
        print(f"{z} 号：每页 {nl} 行，每行约 {cpl} 字，全书 {np_} 页 → {out}")
        imgs.append(Image.open(out))
    body = EFont(os.path.join(a.fonts, f"wenkai_{a.sizes[len(a.sizes) // 2]}.efnt"))
    desk_page(ui, small, body, False, os.path.join(a.out, "首页.png"))
    desk_page(ui, small, body, True, os.path.join(a.out, "首页_有CO2传感器.png"))
    # 四个字号拼一张
    gap = 24
    sheet = Image.new("L", (len(imgs) * (480 + gap) + gap, 800 + 2 * gap + 30), 200)
    dd = ImageDraw.Draw(sheet)
    lab = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 22)
    for i, im in enumerate(imgs):
        x = gap + i * (480 + gap)
        sheet.paste(im.convert("L"), (x, gap + 30))
        dd.text((x, gap), f"{a.sizes[i]} 号", font=lab, fill=0)
    sheet.save(os.path.join(a.out, "四个字号对比.png"))


if __name__ == "__main__":
    main()
