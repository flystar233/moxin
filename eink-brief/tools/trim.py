"""把 shoot.js 出的透明背景 PNG 裁到内容外框，四周留一点边。用法：python tools/trim.py assets/img/studio-*.png（需要 pymupdf）"""
import sys, fitz

for p in sys.argv[1:]:
    pix = fitz.Pixmap(p)
    if not pix.alpha:
        continue
    w, h, n, s = pix.width, pix.height, pix.n, pix.samples
    alpha = s[n - 1::n]
    xs, ys = [], []
    for y in range(0, h, 2):
        row = alpha[y * w:(y + 1) * w]
        hit = [x for x in range(0, w, 2) if row[x] > 8]
        if hit:
            ys.append(y); xs += [hit[0], hit[-1]]
    if not ys:
        continue
    pad = int(0.035 * max(max(xs) - min(xs), max(ys) - min(ys)))
    x0, x1 = max(min(xs) - pad, 0), min(max(xs) + pad, w)
    y0, y1 = max(min(ys) - pad, 0), min(max(ys) + pad, h)
    rows = b''.join(s[(y * w + x0) * n:(y * w + x1) * n] for y in range(y0, y1))
    fitz.Pixmap(fitz.csRGB, x1 - x0, y1 - y0, rows, True).save(p)
    print(p, f'{w}x{h} -> {x1 - x0}x{y1 - y0}')
