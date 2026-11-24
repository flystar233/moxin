// 系统架构图（v3.6 重画）：上半部分是数据与外设，下半部分是供电链路。每个方框都可点击，跳到对应章节。
// 内容按当前设计：含电量计、TF 卡、侧键、电源键、USB 检测；唤醒线集中走一条通道，避免和数据线交叉。
module.exports = function archSvg() {
  const INK = '#23251f', SUB = '#5d6058', BOX = '#FBFBF6', EDGE = '#3d4039', BAND = '#E8E9E1';
  const WAKE = '#1E6B39', PWR = '#985822', DARK = '#1F211D';
  let s = '';
  const t = (x, y, txt, o = {}) => {
    s += `<text x="${x}" y="${y}" text-anchor="${o.a || 'middle'}" font-size="${o.fs || 12}"${o.w ? ` font-weight="${o.w}"` : ''} fill="${o.c || INK}"${o.mono ? ' font-family="IBM Plex Mono, Consolas, monospace"' : ''}>${txt}</text>`;
  };
  const box = (x, y, w, h, title, sub, o = {}) => {
    const dark = o.dark, cx = x + w / 2;
    let g = `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="6" fill="${dark ? DARK : BOX}" stroke="${dark ? DARK : EDGE}" stroke-width="1.2"${o.dash ? ' stroke-dasharray="4 3"' : ''}/>`;
    const lines = [title].concat(sub ? [].concat(sub) : []);
    const lh = 16, top = y + h / 2 - (lines.length - 1) * lh / 2 + 4;
    const save = s; s = '';
    lines.forEach((l, i) => t(cx, top + i * lh - (i === 0 && lines.length > 1 ? 1 : 0), l, i === 0
      ? { fs: o.big ? 17 : 13.5, w: 600, c: dark ? '#F4F5EF' : INK }
      : { fs: 11, c: dark ? '#B9BCB2' : SUB }));
    g += s; s = save;
    if (o.href) g = `<a href="${o.href}" aria-label="${title}">${g}<rect class="hit" x="${x - 2}" y="${y - 2}" width="${w + 4}" height="${h + 4}" rx="7"/></a>`;
    s += g;
  };
  const path = (d, o = {}) => {
    s += `<path d="${d}" fill="none" stroke="${o.c || INK}" stroke-width="${o.sw || 1.4}"${o.dash ? ` stroke-dasharray="${o.dash}"` : ''}${o.end ? ` marker-end="url(#${o.end})"` : ''}${o.start ? ` marker-start="url(#${o.start})"` : ''} stroke-linejoin="round"/>`;
  };
  const dot = (x, y, c = INK) => { s += `<circle cx="${x}" cy="${y}" r="2.8" fill="${c}"/>`; };
  const mk = (id, c) => `<marker id="${id}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,1 L9,5 L0,9 Z" fill="${c}"/></marker>`;

  // 分区底色
  s += `<rect x="10" y="10" width="940" height="364" rx="10" fill="${BAND}"/><rect x="10" y="386" width="940" height="164" rx="10" fill="${BAND}"/>`;
  t(26, 32, '数据与外设', { a: 'start', fs: 12, w: 600, c: SUB });
  t(26, 408, '供电', { a: 'start', fs: 12, w: 600, c: SUB });
  // 图例
  const lg = (x, label, c, dash, sw) => { path(`M${x},28 h26`, { c, dash, sw }); t(x + 32, 32, label, { a: 'start', fs: 11, c: SUB }); };
  lg(650, '数据', INK); lg(730, '唤醒', WAKE, '5 4'); lg(810, '供电', PWR, null, 2.2);

  // ---------- 数据与外设 ----------
  box(330, 80, 220, 140, 'ESP32-S3', ['WROOM-1 · N16R8', '16MB Flash · 8MB PSRAM'], { dark: true, big: true, href: 'peripherals.html#mcu' });

  box(30, 70, 170, 64, '电脑端程序', '采集 · 书库 · 朗读合成', { href: 'firmware.html#pc' });
  path('M200,96 H328', { end: 'a', start: 'a' }); t(265, 89, 'Wi-Fi · HTTP', { fs: 11, c: SUB });
  path('M200,122 H328', { end: 'a', dash: '5 4' }); t(265, 115, 'USB CDC · 烧录', { fs: 11, c: SUB });

  box(30, 160, 170, 50, '侧键 ×3', 'KEY1～KEY3 · 按下为低', { href: 'peripherals.html#orient' });
  path('M200,185 H328', { end: 'a' }); t(265, 178, 'GPIO5～7', { fs: 11, c: SUB, mono: true });

  box(700, 84, 220, 42, '4.26″ 墨水屏', 'SSD1677 · 800×480', { href: 'display.html#schematic' });
  box(700, 136, 220, 42, '功放', 'NS4168 → 喇叭', { href: 'peripherals.html#audio' });
  box(700, 188, 220, 42, 'TF 卡', 'SDMMC 1-bit · Q2 断电', { href: 'peripherals.html#tf' });
  [[105, 'SPI'], [157, 'I2S'], [209, 'SDMMC']].forEach(([y, l]) => { path(`M550,${y} H698`, { end: 'a' }); t(624, y - 6, l, { fs: 11, c: SUB, mono: true }); });

  // I2C 总线
  path('M440,220 V258'); path('M395,258 H845'); dot(440, 258);
  t(452, 246, 'I2C · 4.7k 上拉', { a: 'start', fs: 11, c: SUB });
  [[330, 'PCF8563', '时钟 · 0x51', 'rtc'], [480, 'STCC4', 'CO2 · 0x64', 'co2'], [630, 'SC7A20H', '姿态 · 0x19', 'accel'], [780, 'CW2015', '电量计 · 0x62', 'gauge']].forEach(([x, n, sub, a], i) => {
    const cx = x + 65;
    path(`M${cx},258 V276`); if (i > 0 && i < 3) dot(cx, 258);
    box(x, 278, 130, 50, n, sub, { href: (a === 'gauge' ? 'power.html#' : 'peripherals.html#') + a });
  });

  // 唤醒通道：PCF8563、CW2015（线与到 RTC_INT）、SC7A20H（ACC_INT1/2）→ ESP32
  [395, 695, 845].forEach(x => { path(`M${x},328 V346`, { c: WAKE, dash: '5 4' }); });
  path('M845,346 H300 V208 H328', { c: WAKE, dash: '5 4', end: 'w' });
  dot(395, 346, WAKE); dot(695, 346, WAKE);
  t(545, 364, '唤醒：RTC_INT（PCF8563 与 CW2015 线与）· ACC_INT1 / ACC_INT2', { fs: 11, c: WAKE });

  // ---------- 供电 ----------
  const Y = 440, H = 52, M = Y + H / 2;
  box(30, Y, 110, H, 'Type-C', '5V · CC 5.1k', { href: 'power.html#usbin' });
  box(175, Y, 120, H, 'TP4056X', '充电 600mA', { href: 'power.html#charger' });
  box(330, Y, 130, H, '锂电池 454261', '1500mAh · 带保护板', { href: 'power.html#battery' });
  box(495, Y, 140, H, '负载共享', 'DSS34 + AO3401A', { href: 'power.html#loadshare' });
  box(670, Y, 90, H, '电源键', 'SW3 → U14', { href: 'power.html#switch' });
  box(800, Y, 120, H, 'RT9080-33', '3.3V LDO · 600mA', { dark: true, href: 'power.html#ldo' });
  [[140, 173], [295, 328], [460, 493], [635, 668], [760, 798]].forEach(([a, b]) => path(`M${a},${M} H${b}`, { c: PWR, sw: 2.2, end: 'p' }));
  t(780, M - 8, 'VSYS', { fs: 10.5, c: PWR, mono: true });
  // USB 直供系统
  path(`M85,${Y} V418 H565 V${Y - 2}`, { c: PWR, sw: 1.6, dash: '6 4', end: 'p' });
  t(325, 413, 'VBUS 经 DSS34 直供系统，插 USB 时不耗电池', { fs: 11, c: PWR });
  // VSYS 直供功放
  dot(780, M, PWR);
  path(`M780,${M} V522 H936 V157 H922`, { c: PWR, sw: 1.6, end: 'p' });
  t(930, 250, 'VSYS 直供功放', { a: 'end', fs: 11, c: PWR });
  // 3V3 和 USB 检测
  t(860, 538, '3V3 → ESP32、屏、传感器、TF 卡', { a: 'end', fs: 11, c: SUB });
  path(`M60,${Y + H} V530 H74`, { c: WAKE, dash: '5 4', sw: 1.2 });
  t(78, 534, 'VBUS 分压 → GPIO9 检测插 USB', { a: 'start', fs: 11, c: WAKE });

  const defs = `<defs>${mk('a', INK)}${mk('w', WAKE)}${mk('p', PWR)}</defs>`;
  return `<svg viewBox="0 0 960 560" xmlns="http://www.w3.org/2000/svg" font-family="PingFang SC, Microsoft YaHei, Noto Sans SC, sans-serif" role="img" aria-label="系统架构：ESP32-S3 连接电脑、墨水屏、功放、TF 卡、I2C 传感器和侧键；下方是 Type-C 到 3.3V 的供电链路">${defs}${s}</svg>`;
};
