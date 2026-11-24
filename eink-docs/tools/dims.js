// 尺寸图（v4.9）：PCB 外形、外壳正视、外壳开孔边侧视、按键结构图；v4.44 外壳三张按外壳 v1（亚克力面板、中框、后盖，和 enclosure/eink-case.scad 同一套尺寸）重画。尺寸都从 assets/board-data.js 算出来，单位 mm。
// 坐标：板子数据是背面视图（原点在屏幕左上角附近，x 向右、y 向下）；外壳正视图和侧视图换成从屏幕一面看。
const fs = require('fs');
const path = require('path');
const vm = require('vm');

module.exports = function dimsSvgs() {
  const ctx = { window: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'assets', 'board-data.js'), 'utf8'), ctx);
  const B = ctx.window.BOARD, C = B.CASE;
  const posB = ref => B.parts.find(p => p.ref === ref).B;
  const font = 'font-family="PingFang SC, Microsoft YaHei, Noto Sans SC, sans-serif"';
  const INK = '#202020', SUB = '#5D6058', DIM = '#1F5FA8', RAD = '#A5432A';
  const n = v => +(+v).toFixed(1);
  const fmt = v => String(+(+v).toFixed(2));
  const defs = `<defs><marker id="dA" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,1.5 L10,5 L0,8.5 z" fill="${DIM}"/></marker>` +
    `<pattern id="dHatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><line x1="0" y1="0" x2="0" y2="6" stroke="#B9BCB2" stroke-width="1"/></pattern></defs>`;

  // 通用画法：P(x, y) 把毫米坐标换成像素
  const kit = (k, ox, oy, x0, y0) => {
    let s = '';
    const X = x => n(ox + (x - x0) * k), Y = y => n(oy + (y - y0) * k);
    const t = (x, y, txt, o = {}) => { s += `<text x="${n(x)}" y="${n(y)}" text-anchor="${o.a || 'middle'}" font-size="${o.fs || 12}" fill="${o.c || INK}"${o.w ? ` font-weight="${o.w}"` : ''}${o.halo ? ' stroke="#ECEBE6" stroke-width="5" paint-order="stroke" stroke-linejoin="round"' : ''}${o.rot ? ` transform="rotate(${o.rot} ${n(x)} ${n(y)})"` : ''}>${txt}</text>`; };
    // 水平尺寸：在 y 处画尺寸线，从 ya、yb 两个特征点引出界线
    const dimH = (xa, xb, y, txt, ya = null, yb = null) => {
      const py = Y(y);
      [[xa, ya], [xb, yb]].forEach(([x, fy]) => { if (fy !== null) s += `<line x1="${X(x)}" y1="${Y(fy)}" x2="${X(x)}" y2="${n(py + (py > Y(fy) ? 5 : -5))}" stroke="${DIM}" stroke-width="0.7"/>`; });
      s += `<line x1="${X(xa)}" y1="${py}" x2="${X(xb)}" y2="${py}" stroke="${DIM}" stroke-width="1" marker-start="url(#dA)" marker-end="url(#dA)"/>`;
      t((X(xa) + X(xb)) / 2, py - 5, txt, { c: DIM, fs: 12 });
    };
    const dimV = (ya, yb, x, txt, xa = null, xb = null, side = 'l') => {
      const px = X(x);
      [[ya, xa], [yb, xb]].forEach(([y, fx]) => { if (fx !== null) s += `<line x1="${X(fx)}" y1="${Y(y)}" x2="${n(px + (px > X(fx) ? 5 : -5))}" y2="${Y(y)}" stroke="${DIM}" stroke-width="0.7"/>`; });
      s += `<line x1="${px}" y1="${Y(ya)}" x2="${px}" y2="${Y(yb)}" stroke="${DIM}" stroke-width="1" marker-start="url(#dA)" marker-end="url(#dA)"/>`;
      const cy = (Y(ya) + Y(yb)) / 2;
      t(px + (side === 'l' ? -6 : 6), cy + 4, txt, { c: DIM, a: side === 'l' ? 'end' : 'start' });
    };
    // 引线标注（圆角、孔）：从 (x, y) 拉到像素偏移 (dx, dy)
    const lead = (x, y, dx, dy, txt, c = RAD) => {
      const px = X(x), py = Y(y), ex = px + dx, ey = py + dy;
      s += `<polyline points="${px},${py} ${n(ex)},${n(ey)} ${n(ex + (dx < 0 ? -14 : 14))},${n(ey)}" fill="none" stroke="${c}" stroke-width="0.8"/><circle cx="${px}" cy="${py}" r="1.8" fill="${c}"/>`;
      t(ex + (dx < 0 ? -17 : 17), ey + 4, txt, { c, a: dx < 0 ? 'end' : 'start' });
    };
    return { X, Y, t, dimH, dimV, lead, add: v => { s += v; }, get: () => s };
  };

  // 圆角多边形路径（和 board3d.js 的 roundPoly 一样）
  const roundPath = (pts, rs, X, Y) => {
    const m = pts.length; let d = '';
    for (let i = 0; i < m; i++) {
      const p = pts[i], a = pts[(i - 1 + m) % m], c = pts[(i + 1) % m], r = rs[i];
      const u = (q, tt) => { const len = Math.hypot(q[0] - p[0], q[1] - p[1]); return [p[0] + (q[0] - p[0]) * tt / len, p[1] + (q[1] - p[1]) * tt / len]; };
      const i1 = u(a, r), o1 = u(c, r);
      d += `${i ? 'L' : 'M'}${X(i1[0])},${Y(i1[1])} Q${X(p[0])},${Y(p[1])} ${X(o1[0])},${Y(o1[1])} `;
    }
    return d + 'Z';
  };

  /* ---------- 1. PCB 外形尺寸图（背面视图，元件面朝上） ---------- */
  const pcb = (() => {
    const L = 8.5, k = 7, K = kit(k, 120, 110, L, 0), { X, Y, t, dimH, dimV, lead } = K;
    const N = B.NOTCH, O = B.OUTLINE;
    // 板框关键尺寸（v4.23 按 EDA）：右边、下边、舌片上下沿和外沿
    const xR = O.pts[1][0], yB = O.pts[6][1], tY0 = O.pts[2][1], tY1 = O.pts[4][1], xT = O.pts[3][0], cy = +(yB / 2).toFixed(2);
    // 缺角（无板）
    K.add(`<rect x="${X(L)}" y="${Y(0)}" width="${n((N[2] - L) * k)}" height="${n(N[3] * k)}" fill="url(#dHatch)" stroke="#B9BCB2" stroke-width="0.8" stroke-dasharray="5 3"/>`);
    t((X(L) + X(N[2])) / 2, Y(N[3] / 2) - 4, '电池缺角（无板）', { c: SUB, fs: 13 });
    t((X(L) + X(N[2])) / 2, Y(N[3] / 2) + 14, `${fmt(N[2] - L)} × ${fmt(N[3])}，454261 电池贴在屏背面`, { c: SUB, fs: 11 });
    // 板框
    K.add(`<path d="${roundPath(O.pts, O.r, X, Y)}" fill="#DDE8DF" stroke="#1D6B45" stroke-width="1.6"/>`);
    t((X(N[2]) + X(xR)) / 2, Y(9), '右边一块', { c: '#1D6B45', fs: 13, w: 600 });
    t((X(N[2]) + X(xR)) / 2, Y(9) + 16, '模组 · 电源 · 屏驱动 · Type-C · 喇叭 · TF 卡 · 电源键', { c: SUB, fs: 10.5 });
    t((X(L) + X(N[2])) / 2, Y(53.5), '底边窄条：侧键 · RTC · CO2 / 温湿度 · 姿态传感器', { c: '#1D6B45', fs: 12 });
    // 定位孔
    B.HOLES.B.forEach(([hx, hy], i) => {
      K.add(`<circle cx="${X(hx)}" cy="${Y(hy)}" r="${n(1.85 * k)}" fill="none" stroke="#7FA38C" stroke-width="0.7" stroke-dasharray="3 2"/><circle cx="${X(hx)}" cy="${Y(hy)}" r="${n(1.1 * k)}" fill="#fff" stroke="${INK}" stroke-width="1"/>` +
        `<line x1="${X(hx) - 11}" y1="${Y(hy)}" x2="${X(hx) + 11}" y2="${Y(hy)}" stroke="${INK}" stroke-width="0.5"/><line x1="${X(hx)}" y1="${Y(hy) - 11}" x2="${X(hx)}" y2="${Y(hy) + 11}" stroke="${INK}" stroke-width="0.5"/>`);
      t(X(hx), Y(hy) - 16, 'MH' + (i + 1), { fs: 11, w: 600 });
    });
    // 中心线和屏 FPC 座 FPC1：屏 FPC 从短边正中出来，FPC1 中心必须在板子中心线上（v4.10）
    const j1 = posB('FPC1'), j1p = B.parts.find(p => p.ref === 'FPC1');
    K.add(`<line x1="${X(L) - 10}" y1="${Y(cy)}" x2="${X(xT) + 12}" y2="${Y(cy)}" stroke="${RAD}" stroke-width="0.9" stroke-dasharray="14 3 2 3"/>`);
    K.add(`<rect x="${X(j1[0] - j1p.w / 2)}" y="${Y(j1[1] - j1p.d / 2)}" width="${n(j1p.w * k)}" height="${n(j1p.d * k)}" rx="2" fill="#2A2B2D" opacity=".85"/>`);
    t(X(j1[0]), Y(j1[1]) + 4, 'FPC1', { fs: 11, c: '#fff', w: 600 });
    t(X(N[2]) - 8, Y(cy) - 6, `中心线 y ${fmt(cy)}：屏 FPC 从短边正中出来，FPC1 中心应在这条线上（EDA 里在 y ${fmt(j1[1])}）`, { a: 'end', fs: 11, c: RAD });
    dimV(0, cy, xT + 3, fmt(cy), xR, j1[0] + j1p.w / 2, 'r');
    // 尺寸：上方
    dimH(L, N[2], -5, fmt(N[2] - L), 43, 0);
    dimH(N[2], xR, -5, fmt(xR - N[2]), 0, 0);
    // 下方
    dimH(L, xR, yB + 6, fmt(xR - L) + '（板身）', yB, yB);
    dimH(L, xT, yB + 12, fmt(xT - L) + '（含 Type-C 小舌片）', yB, tY1);
    // 左侧
    dimV(0, N[3], L - 6, fmt(N[3]), N[2], L);
    dimV(N[3], yB, L - 6, fmt(yB - N[3]), L, L);
    dimV(0, yB, L - 12, fmt(yB), N[2], L);
    // 右侧：舌片
    dimV(tY0, tY1, xT + 5, `${fmt(tY1 - tY0)} 舌片，伸出 ${fmt(xT - xR)}`, xT, xT, 'r');
    // 圆角
    lead(xR, 0, 20, -18, 'R2'); lead(xR, yB, 20, 18, 'R2'); lead(L, yB, -20, 18, 'R2');
    lead(L, N[3], -22, -16, 'R2'); lead(N[2], 0, -18, 26, 'R2');
    lead(N[2], N[3], 30, -22, 'R2 内角');
    lead(xT, tY1, 26, 34, 'R1（舌片外角）');
    // 说明和孔位表
    const tx = X(107) + 70;
    t(tx, Y(0) + 2, '原点：外框左上角，x 向右、y 向下', { a: 'start', fs: 11.5, c: SUB });
    t(tx, Y(0) + 20, '（背面视图，元件面朝上）', { a: 'start', fs: 11.5, c: SUB });
    t(tx, Y(0) + 46, '板厚 1.2 · 四层 · 元件限高 4.0', { a: 'start', fs: 12 });
    t(tx, Y(0) + 72, '定位孔（非金属化）', { a: 'start', fs: 12.5, w: 600 });
    t(tx, Y(0) + 90, '孔径 Ø2.2，周围 Ø3.7 内不放元件', { a: 'start', fs: 11, c: SUB });
    B.HOLES.B.forEach(([hx, hy], i) => t(tx, Y(0) + 114 + i * 20, `MH${i + 1}　x ${fmt(hx - L).padStart(4, ' ')}　y ${fmt(hy)}`, { a: 'start', fs: 12, c: INK }));
    t(tx, Y(0) + 210, '屏 FPC 座 FPC1', { a: 'start', fs: 12.5, w: 600 });
    t(tx, Y(0) + 228, `24P 0.5mm，中心对准板子中心线 y ${fmt(cy)}`, { a: 'start', fs: 11, c: SUB });
    t(tx, Y(0) + 246, '选总宽 ≤ 15 的，上沿才不进天线净空区', { a: 'start', fs: 11, c: SUB });
    t(tx, Y(0) + 280, `外形 ${fmt(xT - L)} × ${fmt(yB)}（含舌片）`, { a: 'start', fs: 12.5, w: 600 });
    t(tx, Y(0) + 298, '进嘉立创 10 × 10 cm 免费打样', { a: 'start', fs: 11, c: SUB });
    const w = 1180, h = Y(yB + 12) + 40;
    return `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg" ${font} role="img" aria-label="PCB 外形尺寸图：L 形板 ${fmt(xT - L)} × ${fmt(yB)}，左上角缺 ${fmt(N[2] - L)} × ${fmt(N[3])}">${defs}${K.get()}</svg>`;
  })();

  /* ---------- 2. 外壳正视图（v4.44 外壳 v1：亚克力面板；从屏幕一面看，天线和 Type-C 在左） ---------- */
  // 从屏幕一面看：u = 110 − 背面视图 x（外壳左缘为 0），v = 背面视图 y + 2（外壳上缘为 0）
  const U = x => 110 - x, V = y => y + 2;
  const front = (() => {
    const k = 6, K = kit(k, 110, 90, 0, 0), { X, Y, t, dimH, dimV, lead } = K;
    const W = C.x1 - C.x0, H = C.y1 - C.y0;
    const wu0 = U(C.winCx + C.win[0] / 2), wu1 = U(C.winCx - C.win[0] / 2), wv0 = V(31 - C.win[1] / 2), wv1 = V(31 + C.win[1] / 2);
    const g = C.glass;
    K.add(`<rect x="${X(0)}" y="${Y(0)}" width="${n(W * k)}" height="${n(H * k)}" rx="${n(C.r * k)}" fill="#FBFBF8" stroke="${INK}" stroke-width="1.6"/>`);
    K.add(`<rect x="${X(wu0)}" y="${Y(wv0)}" width="${n(C.win[0] * k)}" height="${n(C.win[1] * k)}" fill="#DCE6EA" stroke="${INK}" stroke-width="1.2"/>`);
    K.add(`<rect x="${X(U(g[1]))}" y="${Y(V(g[2]))}" width="${n((g[1] - g[0]) * k)}" height="${n((g[3] - g[2]) * k)}" fill="none" stroke="#9A9C93" stroke-width="0.8" stroke-dasharray="5 3"/>`);
    t((X(wu0) + X(wu1)) / 2, (Y(wv0) + Y(wv1)) / 2 - 6, `透光区 ${fmt(C.win[0])} × ${fmt(C.win[1])}`, { fs: 14, w: 600 });
    t((X(wu0) + X(wu1)) / 2, (Y(wv0) + Y(wv1)) / 2 + 14, '对准显示区（AA 四周各放 0.5）；四周背面印白色。虚线是屏幕玻璃外形 105.33 × 62.37', { fs: 11, c: SUB });
    // 螺丝从后盖拧进中框，正面看不到：画成虚线
    C.bosses.forEach(([bx_, by_]) => K.add(`<circle cx="${X(U(bx_))}" cy="${Y(V(by_))}" r="${n(C.bossD / 2 * k)}" fill="none" stroke="#9A9C93" stroke-width="0.8" stroke-dasharray="3 2"/><circle cx="${X(U(bx_))}" cy="${Y(V(by_))}" r="${n(.6 * k)}" fill="#9A9C93"/>`));
    // 外形
    dimH(0, W, H + 7, fmt(W), H, H);
    dimV(0, H, -7, fmt(H), 0, 0);
    // 透光区位置
    dimH(0, wu0, -5, fmt(wu0), 0, wv0);
    dimH(wu1, W, -5, fmt(W - wu1), wv0, 0);
    dimV(0, wv0, W + 6, fmt(wv0), W, wu1, 'r');
    dimV(wv1, H, W + 6, fmt(H - wv1), wu1, W, 'r');
    // 螺丝
    const bu = U(C.bosses[1][0]), bv = V(C.bosses[1][1]);
    dimH(0, bu, H + 14, fmt(bu), H, V(C.bosses[3][1]));
    dimH(bu, U(C.bosses[0][0]), H + 14, fmt(U(C.bosses[0][0]) - bu) + '（螺丝间距）', V(C.bosses[3][1]), V(C.bosses[2][1]));
    dimV(bv, V(C.bosses[3][1]), W + 16, fmt(V(C.bosses[3][1]) - bv) + '（螺丝间距）', U(C.bosses[1][0]), U(C.bosses[2][0]), 'r');
    lead(W, 0, 18, -22, 'R4');
    // 边上有什么
    t(X(-3), Y(H / 2), '天线 · Type-C · 充电灯', { rot: -90, fs: 12, c: SUB });
    t(X(W / 2), Y(H + 14) + 30, '底边：菜单键 · 翻页翘板 · 电源键 · TF 卡 · 出声槽 · 通风槽（竖拿时在右边）', { fs: 12, c: SUB });
    t(X(W / 2), Y(0) - 46, '顶边：不开孔（电池这一侧，桌面模式插在支架里）', { fs: 12, c: SUB });
    t(X(0), Y(H + 14) + 56, `整机厚 ${fmt(C.T)}：亚克力 1.5 · 双面胶 0.15 · 屏 0.85 · 泡棉 0.6 · PCB 1.2 · 元件区 4.0 · 后盖 1.6；中框侧壁 2.0`, { a: 'start', fs: 11.5, c: INK });
    t(X(0), Y(H + 14) + 76, '面板：透明亚克力 1.5 背面丝印白色（透光区不印），或白色亚克力切穿开窗。螺丝 4 颗从后盖拧入（虚线），左上角天线旁那颗用尼龙螺丝', { a: 'start', fs: 11.5, c: INK });
    const w = 1000, h = Y(H + 14) + 92;
    return `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg" ${font} role="img" aria-label="外壳正视图：115 × 67，亚克力面板，透光区 ${fmt(C.win[0])} × ${fmt(C.win[1])}">${defs}${K.get()}</svg>`;
  })();

  /* ---------- 3. 外壳开孔边侧视图（v4.44 外壳 v1） ---------- */
  // 高度换成离整机正面（亚克力外表面）的距离：d = h − C.acr[0]（h 以 PCB 底面为 0）；整机 9.9，亚克力和胶到 1.65，中框到 8.3，下面是后盖
  const D = h => +(h - C.acr[0]).toFixed(3), T = C.T, YB = D(C.back[0]), YR = D(C.rim);
  const c0 = C.clr.master, CP = C.cap, KH = C.keyH;
  const edges = (() => {
    const k = 8;
    let out = '';
    // 外壳侧面：亚克力 + 胶、中框、后盖三段，接缝画虚线
    const shell = (K, L) => {
      K.add(`<rect x="${K.X(0)}" y="${K.Y(0)}" width="${n(L * k)}" height="${n(T * k)}" rx="4" fill="#EEECE6" stroke="${INK}" stroke-width="1.4"/>`);
      K.add(`<rect x="${K.X(0)}" y="${K.Y(0)}" width="${n(L * k)}" height="${n(1.5 * k)}" rx="3" fill="#DCE9F0" stroke="${INK}" stroke-width="0.8"/>`);
      [YR, YB].forEach(d => K.add(`<line x1="${K.X(0)}" y1="${K.Y(d)}" x2="${K.X(L)}" y2="${K.Y(d)}" stroke="#9A9C93" stroke-width="0.8" stroke-dasharray="5 3"/>`));
      K.t(K.X(L) + 8, K.Y(0.75) + 4, '亚克力面板', { a: 'start', fs: 11, c: SUB });
      K.t(K.X(L) + 8, K.Y((YR + YB) / 2) + 4, '中框', { a: 'start', fs: 11, c: SUB });
      K.t(K.X(L) + 8, K.Y((YB + T) / 2) + 4, '后盖', { a: 'start', fs: 11, c: SUB });
    };
    const box = (K, u, dc, w_, h_, rx, fill = '#fff') => K.add(`<rect x="${K.X(u - w_ / 2)}" y="${K.Y(dc - h_ / 2)}" width="${n(w_ * k)}" height="${n(h_ * k)}" rx="${n(rx * k)}" fill="${fill}" stroke="${INK}" stroke-width="1"/>`);
    const circ = (K, u, dc, r, fill, stroke = true) => K.add(`<circle cx="${K.X(u)}" cy="${K.Y(dc)}" r="${n(r * k)}" fill="${fill}"${stroke ? ` stroke="${INK}" stroke-width="1"` : ''}/>`);
    // 横向定位（从左端到各孔中心的坐标尺寸）：u 最大的排最上面，下面几行的界线够不到上面的文字
    const later = [];
    const ord = (K, u, row, txt, yFrom) => {
      K.dimH(0, u, T + row, '', T, yFrom);
      later.push(() => K.t(K.X(u) + 8, K.Y(T + row) + 4, txt, { a: 'start', fs: 11.5, c: DIM }));
    };
    const flush = () => { later.splice(0).forEach(fn => fn()); };

    // (a) 底边：从外面看，屏幕面朝上，左端是天线 / Type-C 那头
    const A = kit(k, 150, 70, 0, 0);
    const len = C.x1 - C.x0;
    shell(A, len);
    const rc = (C.keyX.SW2 + C.keyX.SW1) / 2, mc = C.keyX.SW6, tf = posB('CARD1')[0], sw = C.keyX.SW3, dk = D(KH);
    const rh = CP.round / 2 + c0, [rw, rhh, rr] = CP.rock;
    // 菜单键、电源键：圆孔、键帽、凹面 / 凸点
    circ(A, U(mc), dk, rh, '#fff'); circ(A, U(mc), dk, CP.round / 2, '#C9C4B7'); circ(A, U(mc), dk, 1.1, '#A9A395', false);
    circ(A, U(sw), dk, rh, '#fff'); circ(A, U(sw), dk, CP.round / 2, '#C9C4B7'); circ(A, U(sw), dk, CP.dot.master[0] / 2, '#A9A395', false);
    // 翻页翘板：开孔、键帽、正中横向凸棱
    box(A, U(rc), dk, rw + 2 * c0, rhh + 2 * c0, rr + c0); box(A, U(rc), dk, rw, rhh, rr, '#C9C4B7');
    A.add(`<rect x="${A.X(U(rc) - CP.ridge.master[0] / 2)}" y="${A.Y(dk - 1.1)}" width="${n(CP.ridge.master[0] * k)}" height="${n(2.2 * k)}" fill="#A9A395"/>`);
    // TF 卡口 + 靠屏幕面一侧的指甲槽（半椭圆）
    const [nw, nh, , ntop] = C.tf.nail;
    A.add(`<path d="M${A.X(U(tf) - nw / 2)},${A.Y(D(ntop))} A${n(nw / 2 * k)},${n(nh * k)} 0 0 1 ${A.X(U(tf) + nw / 2)},${A.Y(D(ntop))} Z" fill="#D9D6CE" stroke="${INK}" stroke-width="0.8"/>`);
    box(A, U(tf), D(C.tf.hc), C.tf.w, C.tf.h, C.tf.r, '#2A2B2D');
    // 出声槽、通风槽
    const slots = (S, fill) => { for (let i = 0; i < S.n; i++) box(A, U(S.xc + (i - (S.n - 1) / 2) * S.pitch), D(S.hc), S.w, S.h, S.w / 2 - .01, fill); };
    slots(C.spk, '#3A3B37'); slots(C.vent, '#3A3B37');
    ord(A, U(C.vent.xc), 5, `${fmt(U(C.vent.xc))}　传感器通风槽：${C.vent.n} 条 ${fmt(C.vent.w)} × ${fmt(C.vent.h)}，间距 ${fmt(C.vent.pitch)}`, D(C.vent.hc));
    ord(A, U(mc), 10.5, `${fmt(U(mc))}　菜单键：圆孔 Ø${fmt(2 * rh)}，键帽 Ø${fmt(CP.round)}，外伸 ${fmt(CP.proud.menu)}，表面微凹`, dk);
    ord(A, U(rc), 16, `${fmt(U(rc))}　翻页翘板：开孔 ${fmt(rw + 2 * c0)} × ${fmt(rhh + 2 * c0)}，翘板 ${fmt(rw)} × ${fmt(rhh)} R${fmt(rr)}，外伸 ${fmt(CP.proud.rock)}，正中横向凸棱`, dk);
    ord(A, U(sw), 21.5, `${fmt(U(sw))}　电源键：圆孔 Ø${fmt(2 * rh)}，键帽 Ø${fmt(CP.round)} 和壳面齐平，中间凸点 Ø${fmt(CP.dot.master[0])} 高 ${fmt(CP.dot.master[1])}`, dk);
    ord(A, U(tf), 27, `${fmt(U(tf))}　TF 卡口 ${fmt(C.tf.w)} × ${fmt(C.tf.h)}，靠屏幕面一侧指甲槽：半椭圆 宽 ${nw} × 高 ${nh}，深 ${fmt(C.tf.nail[2])}`, D(C.tf.hc));
    ord(A, U(C.spk.xc), 32.5, `${fmt(U(C.spk.xc))}　喇叭出声槽：${C.spk.n} 条 ${fmt(C.spk.w)} × ${fmt(C.spk.h)}，间距 ${fmt(C.spk.pitch)}，对准 LS1`, D(C.spk.hc));
    flush();
    // 高度
    A.dimV(0, dk, -6, fmt(dk), U(mc) - rh, 0);
    A.dimV(0, T, -14, fmt(T), 0, 0);
    A.t(A.X(0), A.Y(0) - 44, '(a) 底边（按键、TF 卡、出声槽、通风槽）：从外面看，屏幕面朝上，左端是天线 / Type-C 那头。竖拿时这条边在右边，左端朝下', { a: 'start', fs: 13, w: 600 });
    A.t(A.X(0), A.Y(0) - 26, `开孔按手板版画（键帽每边间隙 ${c0}）；FDM 版每边 ${C.clr.fdm}，圆孔 Ø${fmt(CP.round + 2 * C.clr.fdm)}、翘板孔 ${fmt(rw + 2 * C.clr.fdm)} × ${fmt(rhh + 2 * C.clr.fdm)}。键帽中心离正面 ${fmt(dk)}、离背面 ${fmt(T - dk)}；高度从整机正面量`, { a: 'start', fs: 11.5, c: SUB });
    out += A.get();

    // (b) 右侧短边（Type-C 那头）：从外面看，屏幕面朝上，左边是顶边（电池那条边）
    const oyB = 70 + (T + 38) * k + 100;
    const Bk = kit(k, 150, oyB, 0, 0);
    const hl = C.y1 - C.y0;
    shell(Bk, hl);
    const j2 = posB('USB0')[1], ub = C.usb, ug = ub.gap.master;
    // 天线段（不开孔、不用金属）
    Bk.add(`<rect x="${Bk.X(V(0))}" y="${Bk.Y(YR + .2)}" width="${n(23.5 * k)}" height="${n((YB - YR - .4) * k)}" fill="url(#dHatch)" stroke="#C9A37A" stroke-width="0.8" stroke-dasharray="4 3"/>`);
    Bk.t(Bk.X(V(11.75)), Bk.Y((YR + YB) / 2) + 4, '天线段：不开孔、不用金属', { fs: 11, c: '#8A5A1C' });
    Bk.t(Bk.X(V(31)), Bk.Y((YR + YB) / 2) + 4, '屏 FPC 弯折（内侧）', { fs: 11, c: SUB });
    // Type-C 开孔：下半圈在中框，上半圈在后盖伸下来的舌头上；舌头和中框 U 形口的接缝画虚线
    box(Bk, V(j2), D(ub.hc), ub.w, ub.h, ub.r, '#2A2B2D');
    const ta = V(j2 - ub.tongueW / 2), tb = V(j2 + ub.tongueW / 2), hw = ub.w / 2;
    Bk.add(`<polyline points="${Bk.X(ta)},${Bk.Y(YB)} ${Bk.X(ta)},${Bk.Y(D(ub.hc))} ${Bk.X(V(j2 - hw))},${Bk.Y(D(ub.hc))}" fill="none" stroke="#7A7C75" stroke-width="0.9" stroke-dasharray="3 2"/>`);
    Bk.add(`<polyline points="${Bk.X(V(j2 + hw))},${Bk.Y(D(ub.hc))} ${Bk.X(tb)},${Bk.Y(D(ub.hc))} ${Bk.X(tb)},${Bk.Y(YB)}" fill="none" stroke="#7A7C75" stroke-width="0.9" stroke-dasharray="3 2"/>`);
    circ(Bk, V(C.led.y), D(C.led.hc), C.led.d / 2, '#F4C7C0');
    ord(Bk, V(C.led.y), 5, `${fmt(V(C.led.y))}　充电灯窗 Ø${fmt(C.led.d)}：不正对 LED1，往远离 Type-C 挪 ${fmt(C.led.y - posB('LED1')[1])}，插头胶壳挡不住`, D(C.led.hc));
    ord(Bk, V(j2), 10.5, `${fmt(V(j2))}　Type-C 开孔 ${fmt(ub.w)} × ${fmt(ub.h)} R${fmt(ub.r)}：上半圈在后盖舌头上（虚线是接缝，舌头宽 ${fmt(ub.tongueW)}）`, D(ub.hc));
    flush();
    Bk.dimV(0, D(ub.hc), -6, fmt(D(ub.hc)), V(j2) - hw, 0);
    Bk.t(Bk.X(0), Bk.Y(0) - 44, '(b) Type-C 那头的短边：从外面看，屏幕面朝上，左边是顶边（电池那条边），右边是底边（按键那条边）', { a: 'start', fs: 13, w: 600 });
    Bk.t(Bk.X(0), Bk.Y(0) - 26, `竖拿时这条边在下面。舌头和中框 U 形口之间留 ${ug}（FDM ${ub.gap.fdm}）`, { a: 'start', fs: 11.5, c: SUB });
    out += Bk.get();
    const w = 1320, h = oyB + (T + 16) * k + 10;
    return `<svg viewBox="0 0 ${w} ${n(h)}" xmlns="http://www.w3.org/2000/svg" ${font} role="img" aria-label="外壳开孔边侧视图：底边的按键、TF 卡、出声槽、通风槽，Type-C 那头的开孔和充电灯窗">${defs}${out}</svg>`;
  })();

  /* ---------- 4. 按键结构图（v4.44 外壳 v1）：(a) 从壳内看底边侧壁；(b) 剖面 B-B 过 SW1 中心、垂直于板边 ---------- */
  // 键帽局部坐标：v = 高度 − 键帽中心高度 KH（向后盖为正），y = 背面视图 y（往壳外为正）。和 enclosure/eink-case.scad 的 cap() 同一套尺寸
  // EVQPUC02K：本体 4.7 × 3.5 × 1.65（y 58.2～61.7）；推杆宽 2.6、厚 1.4，伸出本体 1.0，顶端在 y 62.7
  const keys = (() => {
    const k = 26, wIn = C.y1 - C.wall, wOut = C.y1, capIn = CP.in, [fl0, fl1] = CP.fl, band0 = CP.flMin - KH, top = 2.85;
    const PW = 2.6, PT = 1.4, PL = 1.0, sy0 = 58.2, sy1 = 61.7;
    const [rw, rhh, rr] = CP.rock, rc = (C.keyX.SW2 + C.keyX.SW1) / 2, mx = C.keyX.SW6, mr = CP.round / 2;
    const KEY = '#C9C4B7', KEYD = '#A9A395', SHELL = '#E4E1D8', METAL = '#C9CCD1', PCBC = '#1D6B45';
    const poly = (K, pts, fill, extra = '') => K.add(`<polygon points="${pts.map(([a, b]) => K.X(a) + ',' + K.Y(b)).join(' ')}" fill="${fill}" stroke="${INK}" stroke-width="1"${extra}/>`);
    const rect = (K, a0, a1, b0, b1, fill, extra = ' stroke="' + INK + '" stroke-width="1"') => K.add(`<rect x="${K.X(a0)}" y="${K.Y(b0)}" width="${n((a1 - a0) * k)}" height="${n((b1 - b0) * k)}" fill="${fill}"${extra}/>`);
    const cut = (K, a0, a1, b0, b1) => { rect(K, a0, a1, b0, b1, SHELL, ''); rect(K, a0, a1, b0, b1, 'url(#dHatch)'); };
    const tag = (K, x, y, dx, dy, num) => {
      const px = K.X(x), py = K.Y(y), cx = n(px + dx), cy = n(py + dy);
      if (dx || dy) K.add(`<line x1="${px}" y1="${py}" x2="${cx}" y2="${cy}" stroke="${RAD}" stroke-width="0.8"/><circle cx="${px}" cy="${py}" r="1.8" fill="${RAD}"/>`);
      K.add(`<circle cx="${cx}" cy="${cy}" r="8.5" fill="#fff" stroke="${RAD}" stroke-width="1"/>`);
      K.t(cx, cy + 4, num, { c: RAD, fs: 11, w: 600 });
    };
    // 法兰片外形（键帽局部 x、v）：从键身宽度 hw0 起，两端 45° 放宽到比开孔每边多 0.8
    const flange = (W, round) => {
      const hw0 = round ? Math.sqrt(mr * mr - band0 * band0) : W / 2 - .1, hw1 = W / 2 + CP.flW;
      return [[-hw0, band0], [hw0, band0], [hw1, band0 + hw1 - hw0], [hw1, top], [-hw1, top], [-hw1, band0 + hw1 - hw0]];
    };

    /* (a) 从壳内看底边侧壁：横向同背面视图，纵向是离整机正面的距离 */
    const ax0 = mx - 4.5, ax1 = ax0 + 31.5;
    const A = kit(k, 170, 90, ax0, 0.9);
    const Dk = v => D(KH + v);
    rect(A, ax0, ax1, YR, YB, SHELL, '');                                                // 侧壁内表面
    rect(A, ax0, ax1, YB, T, '#D5D2C9', '');                                             // 后盖
    // 开孔（看进去是壳外的亮处）和孔里的键身
    A.add(`<rect x="${A.X(rc - rw / 2 - c0)}" y="${A.Y(Dk(-rhh / 2 - c0))}" width="${n((rw + 2 * c0) * k)}" height="${n((rhh + 2 * c0) * k)}" rx="${n((rr + c0) * k)}" fill="#fff" stroke="${INK}" stroke-width="1" stroke-dasharray="5 3"/>`);
    A.add(`<circle cx="${A.X(mx)}" cy="${A.Y(Dk(0))}" r="${n((mr + c0) * k)}" fill="#fff" stroke="${INK}" stroke-width="1" stroke-dasharray="5 3"/>`);
    A.add(`<rect x="${A.X(rc - rw / 2)}" y="${A.Y(Dk(-rhh / 2))}" width="${n(rw * k)}" height="${n(rhh * k)}" rx="${n(rr * k)}" fill="${KEY}" stroke="${INK}" stroke-width="1"/>`);
    A.add(`<circle cx="${A.X(mx)}" cy="${A.Y(Dk(0))}" r="${n(mr * k)}" fill="${KEY}" stroke="${INK}" stroke-width="1"/>`);
    // 法兰片（在侧壁内表面前面，盖住开孔靠后盖的一截）
    poly(A, flange(rw, false).map(([x, v]) => [rc + x, Dk(v)]), KEYD, ' fill-opacity=".9"');
    poly(A, flange(CP.round, true).map(([x, v]) => [mx + x, Dk(v)]), KEYD, ' fill-opacity=".9"');
    // 屏幕、泡棉、PCB 和开关在键帽前面（离看的人更近）
    rect(A, ax0, ax1, YR, D(-0.6), '#EDEDE8');
    rect(A, ax0, ax1, D(-0.6), D(0), '#3B3D40', '');
    rect(A, ax0, ax1, D(0), D(1.2), PCBC, ' opacity=".88"');
    [['SW6', '菜单'], ['SW1', '上一页'], ['SW2', '下一页']].forEach(([ref, fn]) => {
      const x = posB(ref)[0];
      rect(A, x - 2.35, x + 2.35, D(1.2), D(2.85), METAL);
      rect(A, x - PW / 2, x + PW / 2, Dk(-PT / 2), Dk(PT / 2), '#2A2B2D');
      A.t(A.X(x), A.Y((YB + T) / 2) + 4, ref + ' ' + fn, { fs: 11, w: 600 });
    });
    A.t(A.X(ax1) + 6, A.Y((YR + D(-0.6)) / 2) + 4, '屏幕', { a: 'start', fs: 10, c: SUB });
    A.t(A.X(ax1) + 6, A.Y(D(0.6)) + 4, 'PCB', { a: 'start', fs: 10, c: PCBC });
    A.t(A.X(ax1) + 6, A.Y(D(4.2)) + 4, '侧壁内表面', { a: 'start', fs: 10, c: SUB });
    A.t(A.X(ax1) + 6, A.Y((YB + T) / 2) + 4, '后盖', { a: 'start', fs: 10, c: SUB });
    // 尺寸
    const fw = rw + 2 * CP.flW, mfw = CP.round + 2 * CP.flW;
    A.dimH(mx, C.keyX.SW1, 1.2, fmt(C.keyX.SW1 - mx), D(1.2), D(1.2));
    A.dimH(C.keyX.SW1, C.keyX.SW2, 1.2, fmt(C.keyX.SW2 - C.keyX.SW1), D(1.2), D(1.2));
    A.dimH(rc - fw / 2, rc + fw / 2, T + 0.9, `${fmt(fw)} 法兰（开孔两端各多 ${fmt(CP.flW - c0)}）`, Dk(top), Dk(top));
    A.dimH(rc - rw / 2 - c0, rc + rw / 2 + c0, T + 1.9, `${fmt(rw + 2 * c0)} 开孔`, Dk(rhh / 2 + c0), Dk(rhh / 2 + c0));
    A.dimH(mx - mfw / 2, mx + mfw / 2, T + 0.9, fmt(mfw), Dk(top), Dk(top));
    A.dimV(Dk(band0), Dk(top), ax0 - .5, `离 PCB 底面 ${fmt(CP.flMin)}～${fmt(KH + top)}`, mx - mfw / 2, mx - mfw / 2, 'l');
    tag(A, rc, Dk(0.2), 0, 0, '1');
    tag(A, mx, Dk(2.1), 0, 0, '2');
    tag(A, rc + fw / 2 - .3, Dk(top - .4), 30, 0, '4');
    tag(A, rc - fw / 2 + .45, Dk(band0 + .45), -30, 30, '5');
    A.t(A.X(ax0), A.Y(0.9) - 46, '(a) 从壳内看底边侧壁（拿掉后盖，看向侧壁内表面）', { a: 'start', fs: 13, w: 600 });
    A.t(A.X(ax0), A.Y(0.9) - 28, '横向同背面视图，纵向从整机正面往后。虚线是侧壁上的开孔；法兰片贴在侧壁内表面，盖住开孔靠后盖的一截。电源键 SW3 做法同菜单键', { a: 'start', fs: 11.5, c: SUB });

    /* (b) 剖面 B-B：过 SW1 中心；横向是板子 y（右边是壳外），纵向是离整机正面的距离 */
    const oyB = A.Y(T + 1.9) + 120, b0 = 56.5, Bk = kit(k, 110, oyB, b0, 0);
    cut(Bk, b0, wOut, 0, 1.5); rect(Bk, b0, wOut, 0, 1.5, '#DCE9F0', ' fill-opacity=".75"');
    rect(Bk, b0, wOut, 1.5, YR, '#E3C66E', '');
    cut(Bk, wIn, wOut, YR, Dk(-rhh / 2 - c0));
    cut(Bk, wIn, wOut, Dk(rhh / 2 + c0), YB);
    cut(Bk, b0, wOut, YB, T);
    rect(Bk, b0, C.glass[3], YR, D(-0.6), '#EDEDE8');
    rect(Bk, b0, 62, D(-0.6), D(0), '#3B3D40', '');
    rect(Bk, b0, 62.1, D(0), D(1.2), PCBC, '');
    rect(Bk, sy0, sy1, D(1.2), D(2.85), METAL);
    rect(Bk, sy1, sy1 + PL, Dk(-PT / 2), Dk(PT / 2), '#2A2B2D');
    // 键帽外形：键身 + 法兰片 + 连接层，45° 斜面切底（y + v ≥ capIn + band0）
    const cy = capIn + band0, out = wOut + CP.proud.rock;
    poly(Bk, [[capIn, -rhh / 2], [out, -rhh / 2], [out, rhh / 2], [capIn, rhh / 2], [capIn, rhh / 2 - .05], [fl1, rhh / 2 - .05], [fl1, top], [fl0, top],
      [fl0, cy - fl0], [cy - (band0 + .05), band0 + .05], [capIn, band0 + .05]].map(([y, v]) => [y, Dk(v)]), KEY);
    Bk.add(`<line x1="${Bk.X(55.9)}" y1="${Bk.Y(Dk(0))}" x2="${Bk.X(66.2)}" y2="${Bk.Y(Dk(0))}" stroke="${RAD}" stroke-width="0.9" stroke-dasharray="12 3 2 3"/>`);
    Bk.t(Bk.X(56.8), Bk.Y(0.75) + 4, '亚克力 1.5', { a: 'start', fs: 10, halo: true });
    Bk.t(Bk.X(56.8), Bk.Y((YR + D(-0.6)) / 2) + 4, '墨水屏 0.85', { a: 'start', fs: 10 });
    Bk.t(Bk.X(56.8), Bk.Y(D(-0.3)) + 3.5, '泡棉 0.6', { a: 'start', fs: 9.5, c: '#F2F3ED' });
    Bk.t(Bk.X(56.8), Bk.Y(D(0.6)) + 4, 'PCB 1.2', { a: 'start', fs: 10, c: '#F2F3ED' });
    Bk.t(Bk.X(56.8), Bk.Y((YB + T) / 2) + 4, '后盖 1.6', { a: 'start', fs: 10, halo: true });
    Bk.t(Bk.X((sy0 + sy1) / 2), Bk.Y(D(2.0)) + 4, 'SW1', { fs: 11, w: 600 });
    Bk.t(Bk.X(55.9), Bk.Y(Dk(0)) - 5, '键帽中心线', { a: 'start', fs: 10, c: RAD, halo: true });
    Bk.dimV(0, T, 55.0, fmt(T), b0, b0, 'l');
    Bk.dimV(0, Dk(0), 55.9, fmt(Dk(0)), b0, b0, 'l');
    Bk.dimV(Dk(-rhh / 2), Dk(rhh / 2), 66.2, `${fmt(rhh)} 键帽高`, out, out, 'r');
    tag(Bk, fl0 + .35, Dk(top - .5), -50, 0, '4');
    tag(Bk, (fl0 + capIn) / 2 - .1, Dk((cy - (fl0 + capIn) / 2 + .1)), -80, 10, '5');
    tag(Bk, sy1 + PL + .05, Dk(0), 40, -40, '6');
    tag(Bk, out - .2, Dk(-1.0), 26, -20, '1');
    Bk.t(Bk.X(b0), oyB - 40, '(b) 剖面 B-B：过 SW1 中心、垂直于板边剖开', { a: 'start', fs: 13, w: 600 });
    Bk.t(Bk.X(b0), oyB - 22, '右边是壳外；高度从整机正面量', { a: 'start', fs: 11.5, c: SUB });

    /* 编号说明和开关参数 */
    const N = kit(1, 0, 0, 0, 0), nx = 470;
    const notes = [
      ['1', `翻页翘板 KC1：${rw} × ${rhh} R${rr}，外伸 ${CP.proud.rock}，正中横向凸棱 ${CP.ridge.master.join(' × ')}（FDM ${CP.ridge.fdm.join(' × ')}）`],
      ['2', `菜单键 KC2：Ø${CP.round}，外伸 ${CP.proud.menu}，表面凹 ${CP.dish.master}；电源键 KC3 和壳面齐平，中间凸点 Ø${CP.dot.master[0]} 高 ${CP.dot.master[1]}`],
      ['3', `翘板不做支点筋，以另一端的法兰片为支点：压上半只触发 SW1，压下半只触发 SW2；`, '正中按下两个都触发，固件把两键同时按下当作无效'],
      ['4', `法兰片：在键帽靠后盖一侧（离 PCB 底面 ${fmt(CP.flMin)}～${fmt(KH + top)}，开关本体顶面 2.85 之上），`, `比开孔两端各宽 ${fmt(CP.flW - c0)}，贴住侧壁内表面；键帽从壳内装，掉不出去`],
      ['5', '45° 斜面：法兰片下面和两端都是 45°，按下 0.4 也碰不到开关本体；', 'FDM 侧躺打印不用支撑'],
      ['6', `开关推杆（宽 ${PW}、厚 ${PT}，伸出本体 ${PL}）顶端离键帽内表面 ${fmt(capIn - sy1 - PL)}，不预压，不用加凸点`],
      ['7', `键帽和开孔每边间隙：手板 / 复模 ${C.clr.master}，FDM 打印 ${C.clr.fdm}（以后注塑可到 0.15）`]
    ];
    let ny = oyB - 34;
    notes.forEach(([num, ...lines]) => {
      N.add(`<circle cx="${nx}" cy="${ny - 4}" r="8.5" fill="#fff" stroke="${RAD}" stroke-width="1"/>`);
      N.t(nx, ny, num, { c: RAD, fs: 11, w: 600 });
      lines.forEach((l, i) => N.t(nx + 16, ny + i * 18, l, { a: 'start', fs: 11.5 }));
      ny += lines.length * 18 + 6;
    });
    ny += 10;
    N.t(nx - 8, ny, '开关：EVQPUC02K 侧按轻触，2.2N、行程 0.3、10 万次（松下 EVQPU 规格书）', { a: 'start', fs: 11.5, w: 600 });
    N.t(nx - 8, ny + 20, '材料：手板 ABS 类 PU（键帽用邵氏 D 80 以上的硬 PU）；自己打印 PETG / PLA；量产注塑 PC/ABS', { a: 'start', fs: 11.5, c: SUB });
    N.t(nx - 8, ny + 40, '装配：先从壳内装键帽，再放屏幕和 PCB；MH2 定位压柱在翘板后方，按键时板子不弯', { a: 'start', fs: 11.5, c: SUB });
    const w = 1080, h = Math.max(Bk.Y(T) + 30, ny + 56);
    return `<svg viewBox="0 0 ${w} ${n(h)}" xmlns="http://www.w3.org/2000/svg" ${font} role="img" aria-label="按键结构图：从壳内看底边侧壁，和过 SW1 的剖面">${defs}${A.get()}${Bk.get()}${N.get()}</svg>`;
  })();

  return { pcb, front, edges, keys };
};
