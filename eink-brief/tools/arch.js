// 客户版系统架构图：上半部分是主控和外设（按总线分组），下半部分是供电链路。
// 器件型号从器件清单读，位号和 eink-docs 一致；不画联网服务（天气、NTP 等），只保留和电脑端之间的 Wi-Fi。
module.exports = function archSvg(D) {
  const part = ref => {
    const b = D.bom.find(x => x.refs.includes(ref));
    if (!b) throw new Error('器件清单里找不到 ' + ref);
    return String(b.part).replace(/（[^）]*）/g, '').trim();
  };
  const W = 1000, H = 640;
  const INK = '#2D312C', SUB = '#5D6058', PWR = '#985822', PAPER = '#FBFAF5', PANEL = '#F1F0EA', RULE = '#C9C8C1';
  const font = 'font-family="PingFang SC, Microsoft YaHei, Noto Sans SC, sans-serif"', mono = 'IBM Plex Mono, Consolas, monospace';
  let s = `<defs><marker id="aD" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,1 L10,5 L0,9 z" fill="${INK}"/></marker>` +
    `<marker id="aP" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,1 L10,5 L0,9 z" fill="${PWR}"/></marker></defs>`;
  const t = (x, y, txt, o = {}) => { s += `<text x="${x}" y="${y}" text-anchor="${o.a || 'middle'}" font-size="${o.fs || 12}" fill="${o.c || INK}"${o.w ? ` font-weight="${o.w}"` : ''}${o.mono ? ` font-family="${mono}"` : ''}>${txt}</text>`; };
  // 方框：标题 + 一行说明；dark 是主控，power 是供电链路
  const box = (x, y, w, h, title, sub, o = {}) => {
    const fill = o.dark ? '#1F1F1D' : o.power ? '#F6EBDD' : PAPER, stroke = o.power ? PWR : INK, tc = o.dark ? '#F4F3EE' : INK;
    s += `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="4" fill="${fill}" stroke="${stroke}" stroke-width="1.2"/>`;
    const cy = y + h / 2;
    if (sub) { t(x + w / 2, cy - 3, title, { fs: o.fs || 13, w: 600, c: tc }); t(x + w / 2, cy + 14, sub, { fs: 11, c: o.dark ? '#C9C8C1' : SUB }); }
    else t(x + w / 2, cy + 4, title, { fs: o.fs || 13, w: 600, c: tc });
  };
  const line = (d, o = {}) => { s += `<path d="${d}" fill="none" stroke="${o.p ? PWR : INK}" stroke-width="${o.sw || 1.4}"${o.dash ? ` stroke-dasharray="${o.dash}"` : ''}${o.end ? ` marker-end="url(#${o.p ? 'aP' : 'aD'})"` : ''}${o.both ? ` marker-start="url(#${o.p ? 'aP' : 'aD'})"` : ''}/>`; };

  /* ---------- 主控与外设 ---------- */
  s += `<rect x="16" y="40" width="${W - 32}" height="356" rx="6" fill="${PANEL}" stroke="${RULE}"/>`;
  t(30, 30, '主控与外设', { a: 'start', fs: 13, w: 600, c: SUB });
  // 电脑端：只经 Wi-Fi 交换数据
  box(400, 54, 200, 44, '电脑端程序', '排版 · 语音合成 · 状态数据');
  line('M500,98 V128', { dash: '5 4', end: true, both: true });
  t(512, 118, 'Wi-Fi', { a: 'start', fs: 11, c: SUB, mono: true });
  // 主控
  box(300, 130, 400, 190, '', '', { dark: true });
  t(500, 196, 'ESP32-S3', { fs: 24, w: 700, c: '#F4F3EE' });
  t(500, 222, 'U2 · ' + part('U2'), { fs: 11.5, c: '#C9C8C1', mono: true });
  t(500, 242, '16MB Flash · 8MB PSRAM', { fs: 11.5, c: '#C9C8C1', mono: true });
  t(500, 262, 'Wi-Fi + BLE', { fs: 11.5, c: '#C9C8C1', mono: true });
  // 左：高速外设
  [[120, '墨水屏 4.26″ · FPC1', '800×480 · SSD1677 屏内驱动', 'SPI'],
   [196, 'TF 卡 · CARD1', '卡电源开关 Q2，可关断', 'SDMMC'],
   [272, '功放 ' + part('U9') + ' · U9', '→ 喇叭 LS1 8Ω 1W', 'I2S']
  ].forEach(([y, a, b, bus]) => {
    box(40, y, 200, 56, a, b);
    line(`M300,${y + 28} H242`, { end: true });
    t(271, y + 22, bus, { fs: 11, c: SUB, mono: true });
  });
  // 右：I2C 总线上的四个器件
  line('M700,225 H740', {});
  t(720, 218, 'I2C', { fs: 11, c: SUB, mono: true });
  t(720, 242, '4.7k', { fs: 10, c: SUB, mono: true });
  line('M740,140 V278', { sw: 1.6 });
  [[120, '时钟 ' + part('U6') + ' · U6'], [166, 'CO2 ' + part('U7') + ' · U7 + U11'], [212, '姿态 ' + part('U8') + ' · U8'], [258, '电量计 ' + part('U10') + ' · U10']]
    .forEach(([y, a]) => { box(762, y, 206, 40, a, '', { fs: 12 }); line(`M740,${y + 20} H760`, { end: true }); s += `<circle cx="740" cy="${y + 20}" r="2.6" fill="${INK}"/>`; });
  t(865, 320, 'U6、U8、U10 的中断线接主控，可唤醒', { fs: 10.5, c: SUB });
  // 下：按键、USB、调试
  [[300, '侧键 ×3', 'SW1 · SW2 · SW6 · 可唤醒'], [438, 'USB 数据', 'D+ / D− · 烧录'], [576, '调试', 'H3 UART · RST / BOOT']].forEach(([x, a, b]) => {
    box(x, 340, 124, 44, a, b, { fs: 12 });
    line(`M${x + 62},340 V322`, { end: true });
  });

  /* ---------- 供电 ---------- */
  s += `<rect x="16" y="430" width="${W - 32}" height="194" rx="6" fill="${PANEL}" stroke="${RULE}"/>`;
  t(30, 420, '供电', { a: 'start', fs: 13, w: 600, c: SUB });
  // 第一行：输入 → 保护 → 充电 → 电池
  box(40, 452, 140, 52, 'Type-C · USB0', '5V 输入', { power: true });
  box(220, 452, 180, 52, '输入保护', 'D3 TVS · D1 ESD · C35 / R25', { power: true });
  box(440, 452, 170, 52, '充电 ' + part('U1').split('-')[0], 'U1 · 约 600mA', { power: true });
  box(650, 452, 190, 52, '电池 454261 · BT1', '约 1500mAh · 带保护板', { power: true });
  line('M180,478 H218', { p: true, end: true, sw: 2 });
  line('M400,478 H438', { p: true, end: true, sw: 2 });
  line('M610,478 H648', { p: true, end: true, sw: 2 });
  t(420, 470, 'VBUS', { fs: 10, c: PWR, mono: true });
  t(629, 470, 'VBAT', { fs: 10, c: PWR, mono: true });
  // 第二行：负载共享 → 总开关 → LDO
  box(220, 560, 180, 52, '负载共享', 'D2（USB）· Q1（电池）', { power: true });
  box(440, 560, 170, 52, '电源开关 U14 · ' + part('U14'), '电源键 SW3 / PWR_HOLD 控制 ON', { power: true });
  box(680, 560, 280, 52, 'LDO ' + part('U4') + ' · U4', '3V3 → 主控、屏、传感器、TF 卡（U6、U10 接电池）', { power: true });
  line('M300,504 V558', { p: true, end: true, sw: 2 });
  line('M745,504 V532 H360 V558', { p: true, end: true, sw: 2 });
  t(306, 540, 'VBUS', { a: 'start', fs: 10, c: PWR, mono: true });
  t(752, 524, 'VBAT', { a: 'start', fs: 10, c: PWR, mono: true });
  line('M400,586 H438', { p: true, end: true, sw: 2 });
  line('M610,586 H678', { p: true, end: true, sw: 2 });
  t(419, 578, 'VSYS_IN', { fs: 9.5, c: PWR, mono: true });
  t(644, 578, 'VSYS', { fs: 10, c: PWR, mono: true });
  t(644, 604, '同时直供', { fs: 9.5, c: PWR });
  t(644, 616, '功放 U9', { fs: 9.5, c: PWR });
  return `<svg viewBox="0 0 ${W} ${H}" xmlns="http://www.w3.org/2000/svg" ${font} role="img" aria-label="系统架构：主控与外设、供电链路">${s}</svg>`;
};
