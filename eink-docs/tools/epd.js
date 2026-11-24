// 墨水屏驱动原理图（v3.6 重画）：左边是升压与负压电荷泵，右边是 FPC1 24pin FPC。
// 原图行距 18px，每颗电容各挂一个接地符号，上下行挤在一起；这里行距放大到 25px，接地符号缩小。
// 二极管方向按 silkscreen-pcb：D4 阳极接 VGL、阴极接电荷泵节点；D5 阳极接电荷泵节点、阴极接地；D6 阳极接开关节点、阴极接 VGH。
// hits = true 时带点击热区（墨水屏页用），false 时只带 F1、F2 标签（原理图页用）。
module.exports = function epdSvg(hits = true) {
  const INK = '#202020', SUB = '#5C5C58', NET = '#1D6B45', SW = 1.3;
  let s = '', hit = '';
  const L = (x1, y1, x2, y2, w = SW) => { s += `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${INK}" stroke-width="${w}"/>`; };
  const P = (pts) => { s += `<polyline points="${pts.map(p => p.join(',')).join(' ')}" fill="none" stroke="${INK}" stroke-width="${SW}" stroke-linejoin="round"/>`; };
  const T = (x, y, t, o = {}) => { s += `<text x="${x}" y="${y}" text-anchor="${o.a || 'start'}" font-size="${o.fs || 11}" fill="${o.c || SUB}"${o.w ? ` font-weight="${o.w}"` : ''}>${t}</text>`; };
  const dot = (x, y) => { s += `<circle cx="${x}" cy="${y}" r="2.6" fill="${INK}"/>`; };
  const gnd = (x, y, k = 1) => { L(x, y, x, y + 5 * k); L(x - 7 * k, y + 5 * k, x + 7 * k, y + 5 * k); L(x - 4.5 * k, y + 8 * k, x + 4.5 * k, y + 8 * k); L(x - 2 * k, y + 11 * k, x + 2 * k, y + 11 * k); };
  const H = (ref, x, y, w, h) => { if (hits) hit += `<rect class="hit" data-hit="${ref}" tabindex="0" role="button" aria-label="${ref}" x="${x}" y="${y}" width="${w}" height="${h}" rx="3"/>`; };
  // 竖放电容：上端 y0，下端 y0+40，极板在中间
  const capV = (x, y0, ref, val, side = 'r') => {
    const m = y0 + 20; L(x, y0, x, m - 4); L(x, m + 4, x, y0 + 40); L(x - 12, m - 4, x + 12, m - 4, 2); L(x - 12, m + 4, x + 12, m + 4, 2);
    if (side === 'r') { T(x + 17, m - 2, ref, { c: INK, w: 600 }); T(x + 17, m + 11, val); } else { T(x - 17, m - 2, ref, { a: 'end', c: INK, w: 600 }); T(x - 17, m + 11, val, { a: 'end' }); }
    H(ref, x - 16, y0 + 8, side === 'r' ? 100 : 32, 26);
  };
  // 竖放电阻：上端 y0，下端 y0+50
  const resV = (x, y0, ref, val) => {
    L(x, y0, x, y0 + 10); s += `<rect x="${x - 6}" y="${y0 + 10}" width="12" height="30" fill="none" stroke="${INK}" stroke-width="${SW}"/>`; L(x, y0 + 40, x, y0 + 50);
    T(x + 13, y0 + 23, ref, { c: INK, w: 600 }); T(x + 13, y0 + 36, val); H(ref, x - 10, y0 + 8, 64, 34);
  };
  // 横放二极管：阳极 xa、阴极 xk（任一方向），三角形指向阴极
  const diode = (xa, xk, y, ref) => {
    const d = Math.sign(xk - xa), c = (xa + xk) / 2, a = c - 8 * d, k = c + 8 * d;
    L(xa, y, a, y); L(k, y, xk, y);
    s += `<path d="M${a},${y - 8} L${a},${y + 8} L${k},${y} Z" fill="${INK}"/>`; L(k, y - 8, k, y + 8, 2);
    T(c, y - 13, ref, { a: 'middle', c: INK, w: 600 }); H(ref, c - 16, y - 26, 32, 38);
  };
  const net = (x, y, t, a = 'start') => T(x, y + 4, t, { a, c: NET, w: 700, fs: 12 });

  // ================= 左：升压与电荷泵 =================
  const ySW = 230, yCP = 120, yS = 330, xSW = 300;
  // 3V3 输入、C4
  net(48, ySW - 10, '3V3'); dot(70, ySW); L(48, ySW, 150, ySW);
  capV(70, ySW, 'C4', '22µF', 'l'); gnd(70, ySW + 40);
  // L1
  L(150, ySW, 160, ySW);
  s += `<path d="M160,${ySW} ${[0, 1, 2, 3].map(i => `a10,10 0 0 1 20,0`).join(' ')}" fill="none" stroke="${INK}" stroke-width="${SW}"/>`;
  L(240, ySW, xSW, ySW); T(200, ySW - 20, 'L1', { a: 'middle', c: INK, w: 600 }); T(200, ySW + 22, '10µH', { a: 'middle' }); H('L1', 156, ySW - 32, 88, 60);
  dot(xSW, ySW); T(xSW + 8, ySW + 16, 'SW', { fs: 10 });
  // U13（N 沟道，漏极接 SW，源极接 RESE 节点）
  const qy = 285; L(xSW, ySW, xSW, qy - 22);
  s += `<path d="M${xSW},${qy - 22} H${xSW - 14} M${xSW - 14},${qy - 30} V${qy - 14} M${xSW - 14},${qy - 8} V${qy + 8} M${xSW - 14},${qy + 14} V${qy + 30} M${xSW - 14},${qy + 22} H${xSW} V${yS} M${xSW - 14},${qy} H${xSW} V${qy + 22}" fill="none" stroke="${INK}" stroke-width="${SW}"/>`;
  s += `<path d="M${xSW - 13},${qy} l7,-4 v8 Z" fill="${INK}"/>`;
  L(xSW - 20, qy - 26, xSW - 20, qy + 26, 1.8);
  T(xSW + 10, qy - 2, 'U13', { c: INK, w: 600 }); T(xSW + 10, qy + 12, '2N7002K');
  H('U13', xSW - 30, qy - 34, 96, 68);
  // 栅极：GDR，R12 下拉
  const xg = 205; net(120, qy, 'GDR', 'start'); L(150, qy, xSW - 20, qy); dot(xg, qy);
  resV(xg, qy, 'R12', '10k'); gnd(xg, qy + 50);
  // 源极：RESE，R13 到地
  dot(xSW, yS); L(xSW, yS, 380, yS); net(386, yS, 'RESE');
  resV(xSW, yS, 'R13', '2.2Ω'); gnd(xSW, yS + 50);
  // D6 → VGH，C12
  diode(xSW, 420, ySW, 'D6'); L(420, ySW, 480, ySW); dot(450, ySW); net(486, ySW, 'VGH');
  capV(450, ySW, 'C12', '4.7µF/25V'); gnd(450, ySW + 40);
  // C11：SW 节点 → 电荷泵节点
  L(xSW, ySW, xSW, yCP + 70); capV(xSW, yCP + 30, 'C11', '4.7µF/25V'); L(xSW, yCP + 30, xSW, yCP);
  // 这里 capV 画的是 yCP+30 到 yCP+70，补上与 SW 节点的连线
  dot(xSW, yCP); T(xSW + 8, yCP + 16, 'CP', { fs: 10 });
  // D5：阳极接 CP，阴极接地（三角指向左边的地）
  diode(xSW, 170, yCP, 'D5'); L(170, yCP, 150, yCP); gnd(150, yCP);
  // D4：阳极接 VGL，阴极接 CP（三角指向 CP）
  diode(420, xSW, yCP, 'D4'); L(420, yCP, 480, yCP); dot(450, yCP); net(486, yCP, 'VGL');
  capV(450, yCP, 'C33', '4.7µF/25V'); gnd(450, yCP + 40);
  // 说明
  T(40, 470, 'D4～D6：MBR0530 肖特基（SOD-123）', { fs: 11 });
  T(40, 488, 'D4 阳极接 VGL（官方转接板 PDF 画反了，这里按 silkscreen-pcb）', { fs: 11 });
  T(40, 506, '× 表示悬空；BS1 接地选择 4 线 SPI', { fs: 11 });

  // ================= 右：FPC1 FPC =================
  const bx = 700, bw = 150, y1 = 72, pitch = 25, rowY = k => y1 + (k - 1) * pitch;
  s += `<rect x="${bx}" y="${y1 - 16}" width="${bw}" height="${24 * pitch + 8}" rx="3" fill="#F6F6F1" stroke="${INK}" stroke-width="1.4"/>`;
  T(bx + bw / 2, y1 - 26, 'FPC1 · 24pin FPC 0.5mm', { a: 'middle', c: INK, w: 700, fs: 12.5 });
  const rows = [
    ['NC', 'nc'], ['GDR', 'net', 'GDR'], ['RESE', 'net', 'RESE'], ['NC', 'nc'], ['VSH2', 'cap', 'C13', '4.7µF/25V'], ['GND', 'gnd'], ['GND', 'gnd'], ['BS1', 'gnd'],
    ['BUSY_N', 'net', 'EPD_BUSY'], ['RST_N', 'net', 'EPD_RST'], ['DC', 'net', 'EPD_DC'], ['CSB', 'net', 'EPD_CS'], ['SCL', 'net', 'EPD_SCK'], ['SDA', 'net', 'EPD_MOSI'],
    ['VDDIO', 'net', '3V3', 'C3 22µF + C14 1µF 到地'], ['VCC', 'net', '3V3'], ['VSS', 'gnd'], ['VDD', 'cap', 'C19', '1µF'], ['VPP', 'cap', 'C15', '1µF'],
    ['VSH1', 'cap', 'C20', '4.7µF/25V'], ['VGH', 'cap', 'C16', '1µF/25V', 'VGH'], ['VSL', 'cap', 'C21', '4.7µF/25V'], ['VGL', 'cap', 'C17', '1µF/25V', 'VGL'], ['VCOM', 'cap', 'C22', '1µF/25V']
  ];
  rows.forEach(([name, kind, a, b, tag], i) => {
    const k = i + 1, y = rowY(k);
    if (k > 1) s += `<line x1="${bx}" y1="${y - pitch / 2}" x2="${bx + bw}" y2="${y - pitch / 2}" stroke="#D9DAD2" stroke-width=".8"/>`;
    T(bx + 12, y + 4, String(k), { fs: 10.5 }); T(bx + 38, y + 4, name, { c: INK, fs: 11.5 });
    if (kind === 'nc') T(bx - 12, y + 4, '×', { a: 'middle', fs: 13 });
    if (kind === 'net') { L(bx - 54, y, bx, y); net(bx - 60, y, a, 'end'); if (b) T(bx - 60 - a.length * 8.5 - 10, y + 4, b, { a: 'end', fs: 10.5 }); }
    if (kind === 'gnd') { L(bx - 26, y, bx, y); gnd(bx - 26, y, .75); }
    if (kind === 'cap') {
      const xc = bx - 40; L(xc + 4, y, bx, y); L(xc - 4, y - 8, xc - 4, y + 8, 2); L(xc + 4, y - 8, xc + 4, y + 8, 2); L(xc - 26, y, xc - 4, y); gnd(xc - 26, y, .75);
      T(xc - 36, y + 4, `<tspan fill="${INK}" font-weight="600">${a}</tspan> ${b}`, { a: 'end', fs: 11 });
      if (tag) T(bx - 18, y - 5, tag, { a: 'middle', c: NET, w: 700, fs: 9.5 });
    }
    if (hits) hit += `<rect class="hit" data-pin="${k}" tabindex="0" role="button" aria-label="FPC1 第 ${k} 脚" x="${bx + 2}" y="${y - pitch / 2 + 1}" width="${bw - 4}" height="${pitch - 2}" rx="2"/>`;
  });

  // F1、F2 标签：样板上要看波形的两颗器件
  const tag = (id, x, y) => `<a href="review.html#${id}"><rect x="${x}" y="${y}" width="28" height="17" rx="3" fill="#A3341F"/><text x="${x + 14}" y="${y + 12.5}" text-anchor="middle" fill="#fff" font-family="IBM Plex Mono, Consolas, monospace" font-size="11" font-weight="700">${id}</text></a>`;
  const tags = tag('F2', 218, ySW - 46) + tag('F1', xSW + 72, qy - 12);
  return `<svg viewBox="0 0 880 690" xmlns="http://www.w3.org/2000/svg" font-family="PingFang SC, Microsoft YaHei, Noto Sans SC, sans-serif" role="img" aria-label="墨水屏驱动原理图：左边升压和电荷泵，右边 24pin FPC">${s}${hit}${tags}</svg>`;
};
