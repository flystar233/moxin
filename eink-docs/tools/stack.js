// 叠层示意图（v3.3 重画；v4.44 按外壳 v1 改成亚克力面板）：沿 y≈40mm 横切，穿过电池缺角和电源区；左右方向同背面视图，上方是屏幕一侧。
// 厚度方向按 sy/sx 放大，数字单位 mm，和 layout.html 的厚度表、board-data.js 的 CASE 一致。
module.exports = function stackSvg() {
  const sx = 5.2, sy = 22, ox = 215, oy = 56;          // ox：x = −5mm（外壳左缘）处的像素位置
  const X = mm => +(ox + (mm + 5) * sx).toFixed(1), Y = d => +(oy + d * sy).toFixed(1);
  const font = 'font-family="PingFang SC, Microsoft YaHei, Noto Sans SC, sans-serif"';
  let s = '';
  const rect = (x0, x1, d0, d1, fill, extra = '') => { s += `<rect x="${X(x0)}" y="${Y(d0)}" width="${+(X(x1) - X(x0)).toFixed(1)}" height="${+(Y(d1) - Y(d0)).toFixed(1)}" fill="${fill}"${extra}/>`; };
  const text = (x, y, t, a = 'start', extra = '') => { s += `<text x="${x}" y="${y}" text-anchor="${a}"${extra}>${t}</text>`; };

  // 厚度（从整机正面往后）：亚克力 0～1.5，双面胶 1.5～1.65，屏 1.65～2.5，泡棉/支撑层 2.5～3.1，PCB 3.1～4.3，元件 4.3～8.3（限高 4.0），后盖 8.3～9.9（11-06 后盖 1.6）
  const A = 1.5, F = 1.65, SCR = 2.5, PCB0 = 3.1, PCB1 = 4.3, BK = 8.3, T = 9.9;
  const shell = '#5C5C58', ink = '#202020';

  // 区域标题
  text(X(31.5), 30, '电池区（PCB 缺角）', 'middle', ' font-weight="600"');
  text(X(84), 30, 'PCB 区（电源与屏驱动）', 'middle', ' font-weight="600"');
  s += `<line x1="${X(63)}" y1="38" x2="${X(63)}" y2="${Y(T) + 8}" stroke="#9A9C93" stroke-width="1" stroke-dasharray="4 3"/>`;

  // 外壳：亚克力面板（透明，背面印白框，透光区 x 2.07～95.87 对准显示区）、双面胶、中框侧壁和粘接台、后盖
  rect(-5, 110, 0, A, '#CFE3EE', ` stroke="#7FA6BC" stroke-width="0.8"`);
  rect(-5, 2.07, A - 0.08, A, '#FFFFFF', ` stroke="#9A9C93" stroke-width="0.5"`); rect(95.87, 110, A - 0.08, A, '#FFFFFF', ` stroke="#9A9C93" stroke-width="0.5"`);
  rect(-5, 2.07, A, F, '#E3C66E'); rect(95.87, 110, A, F, '#E3C66E');
  rect(-5, -3.0, F, BK, shell); rect(108.0, 110, F, BK, shell);
  rect(-3.0, -0.47, F, F + 0.6, shell); rect(105.47, 108.0, F, F + 0.6, shell);
  rect(-5, 110, BK, T, shell);
  text(X(48.97), Y(0.75) + 4, '透明亚克力：透光区对准显示区，四周背印白色', 'middle', ' fill="#36586B" font-size="11"');

  // 墨水屏
  rect(0, 105, F, SCR, '#EDEDE8', ` stroke="${ink}" stroke-width="0.8"`);

  // 电池区：支撑片 ST1（两边搭在 PCB 上）、泡棉胶、电池、膨胀余量
  rect(0.5, 64.5, SCR, SCR + 0.2, '#8C9198');
  rect(1, 62, SCR + 0.2, SCR + 0.5, '#D8C27A');
  rect(1, 62, SCR + 0.5, SCR + 5.0, '#B9B9B2', ' rx="3"');
  text(X(31.5), Y(SCR + 2.75) + 5, '锂电池 454261 · 4.5', 'middle', ' font-size="14"');
  s += `<g stroke="#985822" stroke-width="1"><line x1="${X(31.5)}" y1="${Y(SCR + 5.0) + 1}" x2="${X(31.5)}" y2="${Y(BK) - 1}"/>` +
       `<line x1="${X(31.5) - 4}" y1="${Y(SCR + 5.0) + 1}" x2="${X(31.5) + 4}" y2="${Y(SCR + 5.0) + 1}"/><line x1="${X(31.5) - 4}" y1="${Y(BK) - 1}" x2="${X(31.5) + 4}" y2="${Y(BK) - 1}"/></g>`;
  text(X(31.5) + 8, Y(SCR + 5.4) + 4, '0.8 膨胀余量', 'start', ' fill="#985822" font-size="11"');

  // PCB 区：泡棉、四层板、背面元件
  rect(64.5, 105, SCR, PCB0, '#3B3D40');
  rect(63, 105, PCB0, PCB1, '#1D6B45');
  [0.02, 0.4, 0.8, 1.18].forEach((d, i) => {
    s += `<line x1="${X(63)}" y1="${Y(PCB0 + d)}" x2="${X(105)}" y2="${Y(PCB0 + d)}" stroke="#D9A86A" stroke-width="${i % 3 ? 0.9 : 1.4}"/>`;
  });
  // 元件：高度按规格书（U3 3.6，喇叭 3.5，D2 2.1，L1 3.0，U1 1.5，阻容 ≤1.25）
  const comp = [[64.2, 69.4, 3.6, 'U3'], [70.8, 75.7, 1.5, 'U1'], [77, 78.6, 0.8, ''], [80, 81.6, 1.25, ''], [82, 86, 3.0, 'L1'], [87.4, 89, 0.8, ''], [91, 102, 3.5, 'LS1']];
  comp.forEach(([a, b, h, n]) => {
    rect(a, b, PCB1, PCB1 + h, n === 'LS1' ? '#3A3A3A' : ink, ' rx="0.8"');
    if (n === 'U3' || n === 'LS1') text(+((X(a) + X(b)) / 2).toFixed(1), Y(PCB1 + h / 2) + 4, n, 'middle', ' fill="#F2F3ED" font-size="11"');
    else if (n) text(+((X(a) + X(b)) / 2).toFixed(1), Y(PCB1 + h) + 13, n, 'middle', ' font-size="10" fill="#4A4C46"');
  });
  // 元件区限高线
  s += `<line x1="${X(63)}" y1="${Y(BK)}" x2="${X(105)}" y2="${Y(BK)}" stroke="#A52C21" stroke-width="1" stroke-dasharray="5 3"/>`;

  // 整机厚度标注
  const dx = X(112.5);
  s += `<g stroke="${ink}" stroke-width="1"><line x1="${dx}" y1="${Y(0)}" x2="${dx}" y2="${Y(T)}"/><line x1="${dx - 4}" y1="${Y(0)}" x2="${dx + 4}" y2="${Y(0)}"/><line x1="${dx - 4}" y1="${Y(T)}" x2="${dx + 4}" y2="${Y(T)}"/></g>`;
  text(dx, Y(0) - 10, '整机 9.9', 'middle', ' font-weight="600"');

  // 引线标注：左栏写电池区，右栏写 PCB 区；按深度排开，最小间距 16px
  const lead = (items, side) => {
    let last = -1e9;
    items.forEach(([amm, ad, t]) => {
      const ax = X(amm), ay = Y(ad);
      const ty = Math.max(ay, last + 16); last = ty;
      const ex = side === 'L' ? ox - 18 : dx + 22;
      s += `<polyline points="${ax},${ay} ${side === 'L' ? ex + 6 : ex - 6},${ty} ${ex},${ty}" fill="none" stroke="#7A7C75" stroke-width="0.8"/><circle cx="${ax}" cy="${ay}" r="1.8" fill="#7A7C75"/>`;
      text(side === 'L' ? ex - 4 : ex + 4, ty + 4, t, side === 'L' ? 'end' : 'start');
    });
  };
  lead([[-4, 0.75, '亚克力面板 1.5（背印白框）'], [-4, 1.58, '双面胶 0.15'], [2.5, F + 0.42, '墨水屏 0.85（含玻璃）'], [2.5, SCR + 0.1, '支撑片 ST1 0.2'], [2.5, SCR + 0.35, '泡棉胶 0.3'],
        [2.5, SCR + 2.2, '电池 4.5'], [-4.2, 5.5, '中框侧壁 2.0'], [-1, 9.1, '后盖 1.6']], 'L');
  lead([[103, SCR + 0.3, '泡棉 0.6'], [100, PCB0 + 0.6, 'PCB 四层 1.2（铜层 L1～L4）'], [84, PCB1 + 1.2, '元件面（L1）'],
        [67, BK - 0.2, '限高 4.0：U3 高 3.6，离后盖 0.4'], [108, 9.1, '后盖 1.6']], 'R');

  // 四层说明
  text(X(63), Y(T) + 26, 'PCB：L1 元件与信号 · L2 完整地平面 · L3 3V3 / VBUS · L4 朝屏铺地', 'start', ' font-size="11" fill="#4A4C46"');

  const w = 1150, h = Y(T) + 40;
  return `<svg viewBox="0 0 ${w} ${h}" xmlns="http://www.w3.org/2000/svg" ${font} font-size="12" fill="${ink}" role="img" aria-label="整机叠层剖面：左边电池区，右边 PCB 区，整机 9.9mm">${s}</svg>`;
};
