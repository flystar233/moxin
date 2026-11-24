// L3 电源层分区图（v4.24 按实际 PCB）：整层 3V3，Type-C 旁边挖一块 VBUS（v4.27 充电芯片挪过来后收成一块）；VSYS 不铺平面，靠 L1 走线。
// 天线净空区和隔热岛（CO2 U7、温湿度 U11）不铺。板框、器件位置都从 assets/board-data.js 读（v4.23 起就是 EDA 的实际摆放），
// 坐标是元件面俯视（x 向右、y 向下，单位 mm），和平面图、3D 视图一致。
const fs = require('fs');
const path = require('path');
const vm = require('vm');

// VBUS 块（mm）：按器件位置画的示意，实际边界以 EDA 铺铜为准。v4.27 起 USB0、D1、D3、LED1、R19，FPC1 右边的 C34、C35、R25，
// 充电芯片 U1 和 C1、C8、R20，Q1 的栅极和 R21 都在 Type-C 左边这一块；v4.38 按坐标文件 D2 在 x 81，左边伸出一条接 D2 的阳极，避开 LDO 的 C10、U4
const VBUS = [[82.4, 39.2], [89.9, 39.2], [89.9, 33.5], [105.02, 33.5], [105.02, 39.12], [107.02, 39.12], [107.02, 50.54], [105.02, 50.54],
  [105.02, 53.4], [87.6, 53.4], [87.6, 47.6], [90.3, 47.6], [90.3, 40.6], [82.4, 40.6]];
// L1 上的电源宽线：[网络, 颜色, 点列]（示意走向）
const TRACES = [ // v4.38 按坐标文件的器件位置重画，走向是示意
  ['VSYS', '#A3341F', [[74.1, 42.6], [74.9, 42.6], [74.9, 43.9], [87.4, 43.9], [87.4, 45.0]]],
  ['VSYS', '#A3341F', [[78.2, 43.9], [78.2, 49.5]]],
  ['VSYS', '#A3341F', [[85.21, 43.9], [85.21, 48.8]]],
  ['VSYS_IN', '#7A4E9C', [[79.6, 39.75], [73.27, 39.75], [73.27, 40.4]]],
  ['VSYS_IN', '#7A4E9C', [[91.6, 42.04], [90.6, 42.04], [90.6, 41.0], [80.0, 41.0], [80.0, 40.2]]],
  ['VBAT', '#985822', [[69.6, 33.0], [77.5, 33.0], [77.5, 38.4], [93.6, 38.4], [93.6, 41.0]]],
  ['VBAT', '#985822', [[93.6, 38.4], [95.6, 38.4], [95.6, 49.0]]]
];
// 隔热岛：L 形槽以内
const ISLAND = [[8.5, 46.1], [18.3, 46.1], [18.3, 62.1], [8.5, 62.1]];

module.exports = function l2Svg() {
  const ctx = { window: {} };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '..', 'assets', 'board-data.js'), 'utf8'), ctx);
  const B = ctx.window.BOARD;
  const font = 'font-family="PingFang SC, Microsoft YaHei, Noto Sans SC, sans-serif"';
  const INK = '#202020', SUB = '#5D6058';
  const k = 7.6, ox = 20, oy = 46, x0 = 8.5;
  const n = v => +(+v).toFixed(1);
  const X = x => n(ox + (x - x0) * k), Y = y => n(oy + y * k);
  const pts = a => a.map(([x, y]) => X(x) + ',' + Y(y)).join(' ');
  let s = '';
  const t = (x, y, txt, o = {}) => { s += `<text x="${n(x)}" y="${n(y)}" text-anchor="${o.a || 'middle'}" font-size="${o.fs || 12}" fill="${o.c || INK}"${o.w ? ` font-weight="${o.w}"` : ''}>${txt}</text>`; };

  s += `<defs><pattern id="l2Hatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><line x1="0" y1="0" x2="0" y2="6" stroke="#B9BCB2" stroke-width="1.2"/></pattern></defs>`;
  // 3V3 底色 = 整个板框
  s += `<polygon points="${pts(B.OUTLINE.pts)}" fill="#D7E7EC" stroke="${INK}" stroke-width="1.4"/>`;
  // 不铺铜：天线净空区、隔热岛
  const [kx, ky, kw, kh] = B.keepout.B;
  s += `<rect x="${X(kx)}" y="${Y(ky)}" width="${n(kw * k)}" height="${n(kh * k)}" fill="url(#l2Hatch)" stroke="${SUB}" stroke-width="0.8"/>`;
  s += `<polygon points="${pts(ISLAND)}" fill="url(#l2Hatch)" stroke="${SUB}" stroke-width="0.8"/>`;
  s += `<polyline points="${pts(B.SLOTS.B)}" fill="none" stroke="${INK}" stroke-width="2"/>`;
  // VBUS 块
  s += `<polygon points="${pts(VBUS)}" fill="#F3DFB0" stroke="#985822" stroke-width="1.2" stroke-dasharray="5 3"/>`;
  // 参考器件：浅色外框 + 位号
  const ref = ['U2', 'FPC1', 'USB0', 'D1', 'U1', 'D2', 'Q1', 'U3', 'U4', 'U9', 'U14', 'SW3', 'LS1', 'CARD1', 'L1', 'U6', 'U7', 'U11', 'U10', 'H4'];
  B.parts.filter(p => ref.includes(p.ref)).forEach(p => {
    const [x, y, r] = p.B, rot = Math.abs(r) % 180 === 90, w = rot ? p.d : p.w, d = rot ? p.w : p.d;
    s += `<rect x="${X(x - w / 2)}" y="${Y(y - d / 2)}" width="${n(w * k)}" height="${n(d * k)}" fill="none" stroke="#6B6D66" stroke-width="0.8" stroke-dasharray="3 2"/>`;
    if (w * k > 14) t(X(x), Y(y) + 4, p.ref, { fs: w * k > 40 ? 11 : 9.5, c: SUB });
  });
  // 定位孔（建议位置）
  B.HOLES.B.forEach(([hx, hy]) => { s += `<circle cx="${X(hx)}" cy="${Y(hy)}" r="${n(1.1 * k)}" fill="#FFFFFF" stroke="${SUB}" stroke-width="0.8"/>`; });
  // L1 宽线
  TRACES.forEach(([, c, a]) => { s += `<polyline points="${pts(a)}" fill="none" stroke="${c}" stroke-width="3" stroke-dasharray="7 4" stroke-linecap="round"/>`; });
  // 区域标注
  t(X(40), Y(52) + 5, '3V3', { fs: 18, w: 700, c: '#2F6F86' });
  t(X(88), Y(13), '3V3', { fs: 16, w: 700, c: '#2F6F86' });
  t(X(101.5), Y(46) + 4, 'VBUS', { fs: 12, w: 700, c: '#985822' });
  t(X(101.2), Y(11.5), '天线净空', { fs: 10.5, c: SUB });
  t(X(101.2), Y(11.5) + 14, '不铺', { fs: 10.5, c: SUB });
  t(X(13), Y(55), '隔热岛', { fs: 10.5, c: SUB });
  t(X(13), Y(55) + 14, '不铺', { fs: 10.5, c: SUB });
  // 图例
  const lg = (x, fill, stroke, label, dash) => {
    s += dash ? `<line x1="${x}" y1="20" x2="${x + 22}" y2="20" stroke="${stroke}" stroke-width="3" stroke-dasharray="${dash}"/>`
      : `<rect x="${x}" y="13" width="22" height="14" fill="${fill}" stroke="${stroke}" stroke-width="1"/>`;
    t(x + 28, 24.5, label, { a: 'start', fs: 12 });
  };
  lg(20, '#D7E7EC', INK, 'L3 · 3V3');
  lg(120, '#F3DFB0', '#985822', 'L3 · VBUS（示意）');
  lg(270, 'url(#l2Hatch)', SUB, '不铺铜');
  lg(360, null, '#A3341F', 'L1 · VSYS', '7 4');
  lg(460, null, '#7A4E9C', 'L1 · VSYS_IN', '7 4');
  lg(580, null, '#985822', 'L1 · VBAT', '7 4');
  const W = n(X(107) + 20), H = n(Y(62.1) + 20);
  return `<svg viewBox="0 0 ${W} ${H}" xmlns="http://www.w3.org/2000/svg" ${font} role="img" aria-label="L3 电源层：整层 3V3，Type-C 旁边挖一块 VBUS（含充电芯片）；VSYS 靠 L1 走线">${s}</svg>`;
};
