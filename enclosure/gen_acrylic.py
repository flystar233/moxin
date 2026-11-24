"""生成亚克力面板的激光切割 / 丝印文件（只用标准库）。用法：python gen_acrylic.py
输出（正视图，即从屏幕正面看；天线和 Type-C 在左，按键在下；单位 mm，原点在外形左下角）：
  acrylic-printed.dxf   透明亚克力 + 背面丝印白框（白墨 3 遍，不透光）：CUT 层切外形，PRINT_WINDOW 层是透光区（框内不印）
  acrylic-cutwindow.dxf 白色不透明亚克力：外形和开窗都切（CUT 层）
  acrylic-jlc.dxf       嘉立创面板编辑器导入用：OUTLINE / WINDOW / TAPE_CUTOUT 三层，闭合多段线，不带文字
  acrylic-preview.svg   预览
  acrylic-tape.svg      背胶示意图（两个 DXF 里的 TAPE 层：厂家模切后贴在面板背面）
尺寸和 eink-case.scad 同一套：外形 118 × 67 R4（11-06 长边侧壁加到 2.0，往外长 0.5；11-12 屏幕往远离排线那头错开 3mm，
加长 3），透光区 = 显示区 AA 四周各放 0.5。
"""
import math, os

# 背面视图坐标（同设计文档）→ 正视图：x 镜像，y 翻成向上
SCR_DX = -3.0                         # 屏幕相对 PCB 错开（同 eink-case.scad）：正视图里屏幕往右挪，外形往右加长
X0, Y0, X1, Y1, R = -5 + SCR_DX, -2.5, 110, 64.5, 4
WIN = (2.071 + SCR_DX, 95.871 + SCR_DX, 2.661, 59.341)  # 背面视图 x0, x1, y0, y1
WIN_R = 0.5

def front(xb, yb):
    return X1 - xb, Y1 - yb

W, H = X1 - X0, Y1 - Y0
win = (front(WIN[1], 0)[0], front(0, WIN[3])[1], front(WIN[0], 0)[0], front(0, WIN[2])[1])  # x0, y0, x1, y1


def rrect(x0, y0, x1, y1, r):
    """圆角矩形 → [('L', x1, y1, x2, y2) | ('A', cx, cy, r, a0, a1)]"""
    return [
        ('L', x0 + r, y0, x1 - r, y0), ('A', x1 - r, y0 + r, r, 270, 360),
        ('L', x1, y0 + r, x1, y1 - r), ('A', x1 - r, y1 - r, r, 0, 90),
        ('L', x1 - r, y1, x0 + r, y1), ('A', x0 + r, y1 - r, r, 90, 180),
        ('L', x0, y1 - r, x0, y0 + r), ('A', x0 + r, y0 + r, r, 180, 270),
    ]


def dxf(path, layers):
    out = ['0', 'SECTION', '2', 'HEADER', '9', '$ACADVER', '1', 'AC1009', '9', '$INSUNITS', '70', '4', '0', 'ENDSEC',
           '0', 'SECTION', '2', 'TABLES', '0', 'TABLE', '2', 'LAYER', '70', str(len(layers))]
    colors = {'CUT': 1, 'PRINT_WINDOW': 5, 'TAPE': 3, 'NOTE': 8}
    for name in layers:
        out += ['0', 'LAYER', '2', name, '70', '0', '62', str(colors.get(name, 7)), '6', 'CONTINUOUS']
    out += ['0', 'ENDTAB', '0', 'ENDSEC', '0', 'SECTION', '2', 'ENTITIES']
    for name, ents in layers.items():
        for e in ents:
            if e[0] == 'L':
                out += ['0', 'LINE', '8', name, '10', f'{e[1]:.3f}', '20', f'{e[2]:.3f}', '30', '0',
                        '11', f'{e[3]:.3f}', '21', f'{e[4]:.3f}', '31', '0']
            elif e[0] == 'A':
                out += ['0', 'ARC', '8', name, '10', f'{e[1]:.3f}', '20', f'{e[2]:.3f}', '30', '0',
                        '40', f'{e[3]:.3f}', '50', f'{e[4]:.3f}', '51', f'{e[5]:.3f}']
            elif e[0] == 'T':
                out += ['0', 'TEXT', '8', name, '10', f'{e[1]:.3f}', '20', f'{e[2]:.3f}', '30', '0', '40', f'{e[3]:.2f}', '1', e[4]]
    out += ['0', 'ENDSEC', '0', 'EOF']
    with open(path, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')


def preview_svg():
    """正视图预览：白色面板放在灰底上，标尺寸和中框侧壁开孔位置，下面是边缘剖面放大图。"""
    wx0, wx1, wy0, wy1 = win[0], win[2], H - win[3], H - win[1]   # 透光区（SVG 坐标，y 向下）
    sx = lambda xb: X1 - xb          # 背面视图 x → 正视图 x
    sy = lambda yb: yb - Y0          # 背面视图 y → 正视图 y（y 向下）
    INK, SUB, CUT, WINC, DIMC = '#2B2A27', '#77746C', '#D0352B', '#2C6FB7', '#5B6B7A'
    o = []
    t = lambda x, y, s, size=2.2, fill=SUB, anchor='middle', weight=400, extra='': o.append(
        f'<text x="{x:.2f}" y="{y:.2f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}"{extra}>{s}</text>')

    def dim_h(x0, x1, y, label, above=True):
        o.append(f'<path d="M{x0:.2f},{y:.2f}H{x1:.2f}M{x0:.2f},{y - 1:.2f}v2M{x1:.2f},{y - 1:.2f}v2" stroke="{DIMC}" stroke-width="0.18" fill="none"/>')
        t((x0 + x1) / 2, y - 0.8 if above else y + 2.6, label, 2.0, DIMC)

    def dim_v(x, y0, y1, label, right=True):
        o.append(f'<path d="M{x:.2f},{y0:.2f}V{y1:.2f}M{x - 1:.2f},{y0:.2f}h2M{x - 1:.2f},{y1:.2f}h2" stroke="{DIMC}" stroke-width="0.18" fill="none"/>')
        t(x + 1.2 if right else x - 1.2, (y0 + y1) / 2 + 0.7, label, 2.0, DIMC, 'start' if right else 'end')

    # 标题
    t(-24, -14, '亚克力面板 · 正视图（从屏幕正面看）', 3.4, INK, 'start', 600)
    t(-24, -9.6, f'外形 {W} × {H}，四角 R{R}，厚 1.0 · 白色边框 · 透光区 {wx1 - wx0:.1f} × {wy1 - wy0:.2f}', 2.2, SUB, 'start')

    # 面板
    o.append(f'<rect x="0.7" y="0.9" width="{W}" height="{H}" rx="{R}" fill="#000" opacity=".10"/>')
    o.append(f'<rect x="0" y="0" width="{W}" height="{H}" rx="{R}" fill="#FFFFFF" stroke="{CUT}" stroke-width="0.3"/>')
    # 透光区：墨水屏纸色 + 几行“正文”
    o.append(f'<rect x="{wx0:.3f}" y="{wy0:.3f}" width="{wx1 - wx0:.3f}" height="{wy1 - wy0:.3f}" rx="{WIN_R}" fill="#E3E1D9" stroke="{WINC}" stroke-width="0.25" stroke-dasharray="1.2 0.7"/>')
    for i in range(9):
        y = wy0 + 26 + i * 3.2
        w = 70 if i % 4 != 3 else 38
        o.append(f'<rect x="{wx0 + 11:.2f}" y="{y:.2f}" width="{w}" height="0.9" fill="#BDBAB1"/>')
    t((wx0 + wx1) / 2, wy0 + 11, '透光区（不印刷）', 3.0, '#4E5A66', 'middle', 600)
    t((wx0 + wx1) / 2, wy0 + 15.5, '屏幕显示区 92.8 × 55.68，每边放 0.5', 2.2, '#5E6A75')
    t(W / 2, H - 1.5, '白色印刷区（背面印白 3 遍，不透光）', 1.8, '#9A968C')

    # 尺寸
    dim_h(0, W, -4, f'{W}')
    dim_v(W + 5, 0, H, f'{H}')
    dim_h(0, wx0, 33, f'{wx0:.2f}')
    dim_h(wx1, W, 33, f'{W - wx1:.2f}')
    dim_v(30, 0, wy0, f'{wy0:.2f}')
    dim_v(30, wy1, H, f'{H - wy1:.2f}')

    # 左边（天线、Type-C、充电灯）：中框侧壁上的位置，对照用
    o.append(f'<path d="M-1.4,{sy(0):.2f}V{sy(23.5):.2f}" stroke="#C9A37A" stroke-width="0.8"/>')
    t(-3, sy(11.75) + 0.7, '天线', 2.1, '#8A5A1C', 'end')
    t(-3, sy(11.75) + 3.4, '（不放金属）', 1.7, '#8A5A1C', 'end')
    o.append(f'<path d="M-1.4,{sy(26):.2f}V{sy(36):.2f}" stroke="#B8B5AC" stroke-width="0.8" stroke-dasharray="0.8 0.6"/>')
    t(-3, sy(31) + 0.7, '屏 FPC', 2.0, SUB, 'end')
    o.append(f'<rect x="-2" y="{sy(45.339) - 4.85:.2f}" width="1.3" height="9.7" rx="0.5" fill="#3A3B37"/>')
    t(-3, sy(45.339) + 0.7, 'Type-C', 2.1, INK, 'end')
    o.append(f'<circle cx="-1.35" cy="{sy(54.0):.2f}" r="0.7" fill="#E05A47"/>')
    t(-3, sy(54.0) + 0.7, '充电灯', 2.1, INK, 'end')

    # 底边（侧键、TF 卡、出声槽、通风）
    by = H + 1.0
    for i in (-1, 0, 1):   # 出声槽 3 条，中心 x 96.9
        o.append(f'<rect x="{sx(96.9 + i * 1.8) - 0.45:.2f}" y="{by}" width="0.9" height="1.8" rx="0.3" fill="#3A3B37"/>')
    for i in (-1, 0, 1):
        o.append(f'<rect x="{sx(12.95 + i * 1.5) - 0.4:.2f}" y="{by}" width="0.8" height="1.8" rx="0.3" fill="#3A3B37"/>')
    o.append(f'<rect x="{sx(68.571) - 6:.2f}" y="{by}" width="12" height="1.6" rx="0.3" fill="#3A3B37"/>')
    o.append(f'<circle cx="{sx(55.48):.2f}" cy="{by + 0.9}" r="0.9" fill="#C9C4B7" stroke="{INK}" stroke-width="0.15"/>')
    o.append(f'<rect x="{sx(39.91) - 8:.2f}" y="{by}" width="16" height="1.8" rx="0.8" fill="#C9C4B7" stroke="{INK}" stroke-width="0.15"/>')
    o.append(f'<circle cx="{sx(23.87):.2f}" cy="{by + 0.9}" r="0.9" fill="#C9C4B7" stroke="{INK}" stroke-width="0.15"/>')
    ly = by + 5
    for x, s in ((sx(96.9), '出声槽'), (sx(68.571), 'TF 卡'), (sx(55.48), '电源'), (sx(39.91), '翻页翘板'), (sx(23.87), '菜单'), (sx(12.95), '通风')):
        t(x, ly, s, 2.1, INK)
    t(W / 2, ly + 4, '四周标的是中框侧壁上的开孔位置（对照用），亚克力本身不开孔', 1.9, SUB)

    # 图例
    lx, lyy = 0, ly + 9.5
    o.append(f'<path d="M{lx},{lyy}h5" stroke="{CUT}" stroke-width="0.3"/>'); t(lx + 6, lyy + 0.7, '激光切割线（CUT 层）', 2.0, INK, 'start')
    o.append(f'<path d="M{lx + 38},{lyy}h5" stroke="{WINC}" stroke-width="0.25" stroke-dasharray="1.2 0.7"/>'); t(lx + 44, lyy + 0.7, '印刷边界（PRINT_WINDOW 层，框内不印）', 2.0, INK, 'start')

    # 边缘剖面（放大 4 倍）
    cy0 = lyy + 9
    t(-24, cy0, '边缘剖面（厚度方向放大 4 倍，左端是面板外边缘）', 2.6, INK, 'start', 600)

    def section(x0, title, side, solid):
        y0, th, L, wl = cy0 + 7, 4.0, 46, 30     # 板厚 1.0 × 4；印刷 / 实心段长 wl，之后是透光区
        t(x0, cy0 + 4.2, title, 2.3, INK, 'start', 600)
        t(x0 + L + 1, y0 - 0.6, '正面', 1.8, SUB, 'start')
        if solid:
            o.append(f'<rect x="{x0}" y="{y0}" width="{wl}" height="{th}" fill="#FFFFFF" stroke="#A8A49A" stroke-width="0.25"/>')
            o.append(f'<rect x="{x0}" y="{y0 + th + 0.4}" width="{L}" height="2.4" fill="#E3E1D9" stroke="#B8B5AC" stroke-width="0.2"/>')
            t(x0 + wl / 2, y0 + th + 5.6, '屏幕贴在白色亚克力背面，玻璃从开窗露出', 1.8, SUB)
        else:
            o.append(f'<rect x="{x0}" y="{y0}" width="{L}" height="{th}" fill="#D6E9F5" fill-opacity=".75" stroke="#8FB3CC" stroke-width="0.25"/>')
            o.append(f'<rect x="{x0}" y="{y0 + th - 1.0}" width="{wl}" height="1.0" fill="#FFFFFF" stroke="#A8A49A" stroke-width="0.2"/>')
            o.append(f'<rect x="{x0}" y="{y0 + th + 0.4}" width="{L}" height="2.4" fill="#E3E1D9" stroke="#B8B5AC" stroke-width="0.2"/>')
            t(x0 + wl / 2, y0 + th + 5.6, '背面印白 3 遍 · 屏幕贴在下面', 1.8, SUB)
        t(x0 + wl + (L - wl) / 2, y0 - 0.6, '透光区', 1.8, WINC)
        o.append(f'<path d="M{x0 - 0.3},{y0}v{th}" stroke="{CUT}" stroke-width="0.5"/>')
        t(x0, y0 + th + 9.4, side, 2.0, INK, 'start')

    section(0, '方案 A：透明亚克力 + 背印白框', '侧面：透明切割面，透出背面的白色', False)
    section(62, '方案 B：白色亚克力，切开窗', '侧面：实心白', True)

    vb_h = cy0 + 30
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-26 -19 172 {vb_h + 19:.1f}" '
            f'font-family="Microsoft YaHei, PingFang SC, Noto Sans SC, sans-serif">'
            f'<rect x="-26" y="-19" width="172" height="{vb_h + 19:.1f}" fill="#ECEBE7"/>' + ''.join(o) + '</svg>')


#   ---- 背胶（TAPE 层）：亚克力厂家模切后贴在面板背面（印白那面），离型纸不撕 ----
# 一整片"回"字形，同时粘：中框前沿、4 个螺丝柱端面、两头粘接台、屏幕玻璃的边框（都在同一平面）。
# 背面视图坐标（和 eink-case.scad 一样），最后转成正视图画进 DXF。
AA = (WIN[0] + 0.5, WIN[1] - 0.5, WIN[2] + 0.5, WIN[3] - 0.5)   # 显示区 -0.429～92.371 × 3.161～58.841
GLASS = (-0.165 + SCR_DX, 105.165 + SCR_DX, -0.185, 62.185)
FPC_Y0, FPC_Y1 = 14.8, 42.2          # 屏 FPC 弯折空当（中框右头这一段没有粘接台）；11-12 往天线那边再开 2（原 16.8）
WALL = 2.0
TAPE_IN = 0.8                         # 胶内边离显示区 0.8（印刷透光区是 0.5，胶边藏在白墨后面）
TAPE_OUT = 0.3                        # 胶外边比面板外形小 0.3（透明侧边看不到胶边）
FPC_STRIP = 2.5                       # 排线那头只在离显示区 2.5 以内贴（再往外是驱动芯片和排线压接的台阶）
VENT = (28.5, 30.5)                   # 通气口：排线那头的胶条断开 2mm，空气层不会完全密封


def tape_holes_back():
    """胶上要挖掉的区域（背面视图）：显示区开窗 + 通气口 + 排线头台阶和弯折空当，连成一个多边形"""
    wx0, wx1, wy0, wy1 = AA[0] - TAPE_IN, AA[1] + TAPE_IN, AA[2] - TAPE_IN, AA[3] + TAPE_IN
    sx = AA[1] + FPC_STRIP                 # 排线那头胶条的外边
    gx = GLASS[1] + 0.3                    # 玻璃端 + 0.3
    gy0, gy1 = GLASS[2] - 0.3, GLASS[3] + 0.3
    wall_x = X1 - WALL                     # 中框侧壁内面
    return [(wx0, wy0), (wx1, wy0), (wx1, VENT[0]), (sx, VENT[0]), (sx, gy0), (gx, gy0), (gx, FPC_Y0),
            (wall_x, FPC_Y0), (wall_x, FPC_Y1), (gx, FPC_Y1), (gx, gy1), (sx, gy1), (sx, VENT[1]),
            (wx1, VENT[1]), (wx1, wy1), (wx0, wy1)]


def tape_entities():
    o = TAPE_OUT
    ents = rrect(o, o, W - o, H - o, R - o)          # 外边（正视图，外形对称，不用镜像）
    pts = [front(x, y) for x, y in tape_holes_back()]
    for i in range(len(pts)):
        (x1, y1), (x2, y2) = pts[i], pts[(i + 1) % len(pts)]
        ents.append(('L', x1, y1, x2, y2))
    return ents


def tape_svg():
    """背胶示意图（正视图，从面板正面看；胶贴在背面）"""
    INK, SUB, TAPEC, CUT = '#2B2A27', '#77746C', '#3E8E5A', '#D0352B'
    sy = lambda y: H - y   # 正视图 y 向上 → SVG y 向下
    o = []
    t = lambda x, y, s, size=2.2, fill=SUB, anchor='middle', weight=400: o.append(
        f'<text x="{x:.2f}" y="{y:.2f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}">{s}</text>')
    t(-24, -9, '面板背胶 · 正视图（从面板正面看，胶贴在背面的印白区上）', 3.2, INK, 'start', 600)
    t(-24, -4.8, '3M 9495LE 或同类 0.15～0.17mm 透明 / 白色薄型双面胶，模切后贴好，背面离型纸保留', 2.1, SUB, 'start')
    o.append(f'<rect x="0" y="0" width="{W}" height="{H}" rx="{R}" fill="#FFFFFF" stroke="{CUT}" stroke-width="0.3"/>')
    oo = TAPE_OUT
    hole = ' '.join(f'{x:.3f},{sy(y):.3f}' for x, y in (front(*p) for p in tape_holes_back()))
    o.append(f'<path d="M{oo + R - oo},{oo} H{W - R} A{R - oo},{R - oo} 0 0 1 {W - oo},{R} V{H - R} '
             f'A{R - oo},{R - oo} 0 0 1 {W - R},{H - oo} H{R} A{R - oo},{R - oo} 0 0 1 {oo},{H - R} V{R} '
             f'A{R - oo},{R - oo} 0 0 1 {R},{oo} Z M{hole} Z" fill="{TAPEC}" fill-opacity=".35" fill-rule="evenodd" '
             f'stroke="{TAPEC}" stroke-width="0.25"/>')
    ax0, ay0 = front(AA[1], AA[3])
    ax1, ay1 = front(AA[0], AA[2])
    o.append(f'<rect x="{ax0:.3f}" y="{sy(ay1):.3f}" width="{ax1 - ax0:.3f}" height="{ay1 - ay0:.3f}" fill="#E3E1D9" stroke="#8C8A82" stroke-width="0.2" stroke-dasharray="1 0.6"/>')
    t((ax0 + ax1) / 2, sy((ay0 + ay1) / 2) - 2, '屏幕显示区（不贴胶）', 3, '#4E5A66', 'middle', 600)
    t((ax0 + ax1) / 2, sy((ay0 + ay1) / 2) + 2.5, '胶内边离显示区 0.8，藏在白色印刷后面', 2.1, '#5E6A75')
    # 排线头的说明写在面板左边外面（正视图里排线头在左）
    for dy, s1 in ((-17, '排线头只贴'), (-14.5, f'{FPC_STRIP - TAPE_IN:.1f}mm 一条'), (0.8, '通气口 2mm'),
                   (12, '芯片台阶、'), (14.5, '排线弯折处'), (17, '不贴胶')):
        t(-1.5, sy(H / 2) + dy, s1, 1.9, INK, 'end')
    t(W / 2, H + 5, '绿色 = 背胶（TAPE 层）　红线 = 面板外形（CUT 层）　胶外边比外形小 0.3', 2.1, SUB)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-26 -13 {W + 30} {H + 22}" '
            f'font-family="Microsoft YaHei, PingFang SC, Noto Sans SC, sans-serif">'
            f'<rect x="-26" y="-13" width="{W + 30}" height="{H + 22}" fill="#ECEBE7"/>' + ''.join(o) + '</svg>')


here = os.path.dirname(os.path.abspath(__file__))
outline = rrect(0, 0, W, H, R)
window = rrect(*win, WIN_R)
tape = tape_entities()
tape_note = ('T', 2, -10, 2.5, 'TAPE: 0.15-0.17mm double-sided adhesive (3M 9495LE or equal), die-cut to TAPE layer, '
             'laminated on the BACK side (printed side), keep release liner')
note = [('T', 2, -6, 2.5, 'CLEAR ACRYLIC 1.0mm (SINGLE-SIDE HARD COAT ON FRONT), BACK-PRINT WHITE (3 LAYERS, OPAQUE) OUTSIDE PRINT_WINDOW (artwork shown from FRONT)'),
        tape_note]
dxf(os.path.join(here, 'acrylic-printed.dxf'), {'CUT': outline, 'PRINT_WINDOW': window, 'TAPE': tape, 'NOTE': note})
dxf(os.path.join(here, 'acrylic-cutwindow.dxf'), {'CUT': outline + window, 'TAPE': tape,
    'NOTE': [('T', 2, -6, 2.5, 'OPAQUE WHITE ACRYLIC 1.0mm, CUT OUTLINE AND WINDOW (view from FRONT)'), tape_note]})

# ---- 给嘉立创面板编辑器导入用：每个形状一条闭合多段线（R12 POLYLINE，圆角用 bulge），不带文字 ----
# 导入时按图层名对应：OUTLINE → 板框层；PRINT_FRAME → 打印层（填白）；WHITE_FRAME → 白色遮盖层；WINDOW → 透明控制层（方案 A，编辑器有这层时）
# 或挖孔层（方案 B）；TAPE_CUTOUT → 背胶挖空层
BULGE90 = math.tan(math.radians(90) / 4)   # 逆时针 90° 圆弧


def rrect_poly(x0, y0, x1, y1, r):
    return [(x0 + r, y0, 0), (x1 - r, y0, BULGE90), (x1, y0 + r, 0), (x1, y1 - r, BULGE90),
            (x1 - r, y1, 0), (x0 + r, y1, BULGE90), (x0, y1 - r, 0), (x0, y0 + r, BULGE90)]


def dxf_poly(path, layers):
    out = ['0', 'SECTION', '2', 'HEADER', '9', '$ACADVER', '1', 'AC1009', '9', '$INSUNITS', '70', '4', '0', 'ENDSEC',
           '0', 'SECTION', '2', 'TABLES', '0', 'TABLE', '2', 'LAYER', '70', str(len(layers))]
    for i, name in enumerate(layers):
        out += ['0', 'LAYER', '2', name, '70', '0', '62', str(i + 1), '6', 'CONTINUOUS']
    out += ['0', 'ENDTAB', '0', 'ENDSEC', '0', 'SECTION', '2', 'ENTITIES']
    for name, polys in layers.items():
        for pts in polys:
            out += ['0', 'POLYLINE', '8', name, '66', '1', '10', '0', '20', '0', '30', '0', '70', '1']
            for x, y, b in pts:
                out += ['0', 'VERTEX', '8', name, '10', f'{x:.3f}', '20', f'{y:.3f}', '30', '0']
                if b: out += ['42', f'{b:.6f}']
            out += ['0', 'SEQEND', '8', name]
    out += ['0', 'ENDSEC', '0', 'EOF']
    with open(path, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')


def frame_pieces():
    """白框拆成 4 块不带洞的实心形状（嘉立创编辑器不认洞：外形套窗口导进去整块都填满了）"""
    x0, y0, x1, y1 = win
    return [
        [(R, 0, 0), (x0, 0, 0), (x0, H, 0), (R, H, BULGE90), (0, H - R, 0), (0, R, BULGE90)],   # 左条（排线那头，外侧两个圆角）
        [(x1, 0, 0), (W - R, 0, BULGE90), (W, R, 0), (W, H - R, BULGE90), (W - R, H, 0), (x1, H, 0)],  # 右条
        [(x0, y1, 0), (x1, y1, 0), (x1, H, 0), (x0, H, 0)],   # 上条
        [(x0, 0, 0), (x1, 0, 0), (x1, y0, 0), (x0, y0, 0)],   # 下条
    ]


tape_cut = [front(x, y) + (0,) for x, y in tape_holes_back()]
dxf_poly(os.path.join(here, 'acrylic-jlc.dxf'), {
    'OUTLINE': [rrect_poly(0, 0, W, H, R)],
    'WINDOW': [rrect_poly(*win, WIN_R)],
    'PRINT_FRAME': frame_pieces(),  # 白框 4 块，导入打印层后填白、去描边（编辑器要求必须有打印层）
    'WHITE_FRAME': frame_pieces(),  # 同上，编辑器有白色遮盖层导入选项时用
    'TAPE_CUTOUT': [tape_cut],
})

with open(os.path.join(here, 'acrylic-tape.svg'), 'w', encoding='utf-8') as f:
    f.write(tape_svg())

with open(os.path.join(here, 'acrylic-preview.svg'), 'w', encoding='utf-8') as f:
    f.write(preview_svg())
print('window front-view x %.3f..%.3f  y %.3f..%.3f  (%.3f x %.3f)' % (win[0], win[2], win[1], win[3], win[2] - win[0], win[3] - win[1]))
print('margins L %.3f R %.3f T %.3f B %.3f' % (win[0], W - win[2], H - win[3], win[1]))
