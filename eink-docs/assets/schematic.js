/* 原理图：按功能分 6 张，v4.22 起每张的摆放和连线照嘉立创 EDA 原理图画。同一张图里的连接画成导线；
   跨图的连接用端口标签（同名即相连），电源和地用符号。
   主控模组的引脚和网络读 data.js 的引脚表。坐标单位是 SVG 用户单位，图宽默认 1000，可按图指定。
   两端器件：[类型, 位号, 取值, x, y, 方向, 网络1, 网络2]，方向 v = 网络1 在上，u = 在下，h = 在左，hr = 在右；
   二极管网络1 是阳极。导线 wire(网络, 点…)：点可以是 '位号.引脚' 或 [x, y]，相邻两点不在一条线上时先横后竖。 */
(function () {
  'use strict';
  const D = window.DOC;
  const POWER = new Set(['VBUS', 'VBAT', 'VSYS_IN', 'VSYS', '3V3', 'SD_VCC']);
  const NC = 'NC';

  /* ---------- sheet definitions ---------- */
  const sheets = [];
  // v4.22 起各图按嘉立创 EDA 原理图的摆放重画；w 是图宽（默认 1000）
  const sheet = (id, title, h, items, notes, ext, w) => sheets.push({ id, title, h, items, notes: notes || [], ext, w: w || 1000 });
  // pn：可选的引脚名（默认 1、2，二极管 A、K），一颗双二极管拆成两个符号画时用
  const two = (t, ref, val, x, y, o, n1, n2, lp, pn) => ({ k: 'two', t, ref, val, x, y, o, n: [n1, n2], lp, pn });
  const tp = (ref, x, y, net, up) => ({ k: 'tp', ref, x, y, net, up });
  // MOS 管方向：v 竖放（栅极在左）；h 横放、栅极在下、S 在左；hm 横放、栅极在下、S 在右；hu 横放、栅极在上、S 在左
  const fet = (t, ref, val, x, y, g, top, bot, o) => ({ k: 'fet', t, ref, val, x, y, g, top, bot, o: o || 'v' });
  // 芯片：L、R 是左右两边的引脚，null 表示空一行；opt.T、opt.B 是上下两边的引脚 [脚号, 名称, 网络, 距左边框的距离]；
  // opt.pitch 是左右引脚间距（默认 20），opt.h 指定方框高度
  const ic = (ref, val, x, y, w, L, R, opt) => ({ k: 'ic', ref, val, x, y, w, L, R: R || [], o: opt || {} });
  const port = (net, x, y, dx, dy, vert) => ({ k: 'port', net, x, y, dx, dy, vert });
  // 单刀双掷开关：网络依次是 2 脚（公共端，在上）、1 脚（左下）、3 脚（右下）；o = 'u' 上下翻转（公共端在下，同 EDA 的 SW3）
  const sw3 = (ref, val, x, y, com, n1, n3, o) => ({ k: 'sw3', ref, val, x, y, n: [com, n1, n3], o });
  const wire = (net, ...pts) => ({ k: 'wire', net, pts });
  const note = (x, y, text) => ({ k: 'note', x, y, text });
  // 虚线分块框，标签写在框的左下方
  const frame = (x, y, w, h, label) => ({ k: 'frame', x, y, w, h, label });

  // SCH 1：EDA 左上角的 Type-C、USBLC6 和下面的 TP4056、电池座。网络名对照：EDA 的 +5V = VBUS，BAT+ = VBAT，DP/DM = USB_DP/USB_DN
  sheet('pwr-in', '电源输入与充电', 800, [
    ic('D1', 'USBLC6-2SC6', 545, 20, 120, [['4', 'I/O2', 'USB_DP'], ['5', 'VBUS', 'VBUS'], ['6', 'I/O1', 'USB_DN']], [['3', 'I/O2', 'USB_DP'], ['2', 'GND', 'GND'], ['1', 'I/O1', 'USB_DN']]),
    ic('USB0', 'TYPE-C 16P', 270, 110, 180,
      [['A1B12', 'GND', 'GND'], ['A4B9', 'VBUS', 'VBUS'], ['A5', 'CC1', 'CC1'], ['A6', 'DP1', 'USB_DP'], ['A7', 'DN1', 'USB_DN'], ['A8', 'SBU1', NC]],
      [['B8', 'SBU2', NC], ['B7', 'DN2', 'USB_DN'], ['B6', 'DP2', 'USB_DP'], ['B5', 'CC2', 'CC2'], ['B1A12', 'GND', 'GND'], ['B4A9', 'VBUS', 'VBUS']],
      { pitch: 25, vx: 410, vy: 345, T: [['12', '', 'GND', 50], ['14', '', 'GND', 130]], B: [['13', '', 'GND', 50], ['15', '', 'GND', 130]] }),
    // 外壳脚：上面 12、14 并到一个地符号，下面 13、15 并到地线
    wire('GND', 'USB0.14', [400, 75], [320, 75]), wire('GND', 'USB0.12', [320, 55]), port('GND', 320, 55, 0, -1),
    // CC1 下拉：R2 上端接 A1B12 的地，下端绕回 A5
    two('R', 'R2', '5.1k', 80, 215, 'v', 'GND', 'CC1'),
    wire('GND', 'USB0.A1B12', [80, 164]), wire('GND', 'R2.1', [80, 140]), port('GND', 80, 140, 0, -1),
    wire('CC1', 'R2.2', [80, 270], [150, 270], [150, 214], 'USB0.A5'),
    // CC2 下拉：B5 绕上去接 R1，R1 下端和 B1A12 并到地
    two('R', 'R1', '5.1k', 790, 215, 'v', 'CC2', 'GND'),
    wire('CC2', 'USB0.B5', [560, 239], [560, 160], [790, 160], 'R1.1'),
    wire('GND', 'USB0.B1A12', [790, 264]), wire('GND', 'R1.2', [790, 300]), port('GND', 790, 300, 0, 1),
    // VBUS 横线上挂 C34、C35、D3
    two('C', 'C34', '1µF', 590, 345, 'v', 'VBUS', 'GND', 'l'), two('C', 'C35', '10µF', 650, 345, 'v', 'VBUS', 'VBUS_DMP'),
    two('TVS', 'D3', 'SMF5.0A', 720, 345, 'u', 'GND', 'VBUS'),
    wire('VBUS', 'USB0.B4A9', [745, 289]), port('VBUS', 745, 289, 1, 0),
    wire('VBUS', [590, 289], 'C34.1'), wire('VBUS', [650, 289], 'C35.1'), wire('VBUS', [720, 289], 'D3.K'),
    // 地线：13、15 往下并到一根竖线，C34、R25、D3 的地都接过来
    wire('GND', 'USB0.15', [400, 350], [320, 350]), wire('GND', 'USB0.13', [320, 460]),
    wire('GND', 'C34.2', [590, 400], [320, 400]),
    two('R', 'R25', '1Ω', 470, 430, 'h', 'GND', 'VBUS_DMP'),
    wire('GND', 'R25.1', [320, 430]), wire('GND', [320, 430], [290, 430]), port('GND', 290, 430, -1, 0), wire('VBUS_DMP', 'R25.2', [650, 430], 'C35.2'),
    wire('GND', 'D3.A', [720, 460], [320, 460]),
    // TP4056：左边 R20、C1、C8，右边充电灯、电池电容和电池座
    ic('U1', 'TP4056X-42-ESOP8', 365, 515, 200,
      [null, ['1', 'TEMP', 'GND'], ['2', 'PROG', 'PROG'], ['3', 'GND', 'GND'], ['4', 'VCC', 'VBUS']],
      [['9', 'EP', 'GND'], ['8', 'CE', 'VBUS'], ['7', 'CHRG#', 'CHRG_N'], ['6', 'STDBY#', NC], ['5', 'BAT', 'VBAT']], { pitch: 25 }),
    wire('GND', 'U1.1', [320, 560], [320, 540], [108, 540], [108, 610]),
    two('R', 'R20', '2k', 200, 575, 'h', 'GND', 'PROG', 'a'),
    wire('GND', 'R20.1', [108, 575]), wire('PROG', 'R20.2', [290, 575], [290, 585], 'U1.2'),
    wire('GND', 'U1.3', [50, 610]), port('GND', 50, 610, -1, 0),
    two('C', 'C1', '10µF', 186, 655, 'v', 'GND', 'VBUS', 'l'), two('C', 'C8', '100nF', 260, 655, 'v', 'GND', 'VBUS', 'l'),
    wire('GND', 'C1.1', [186, 610]), wire('GND', 'C8.1', [260, 610]),
    wire('VBUS', 'C1.2', [186, 705]), wire('VBUS', 'C8.2', [260, 705]),
    wire('VBUS', [50, 705], [320, 705], [320, 635], 'U1.4'), port('VBUS', 50, 705, -1, 0),
    wire('GND', 'U1.9', [610, 535], [610, 505]), port('GND', 610, 505, 0, -1),
    two('LED', 'LED1', '红', 736, 488, 'v', 'LED_A', 'CHRG_N'), two('R', 'R19', '2k', 806, 415, 'h', 'LED_A', 'VBUS', 'a'),
    wire('CHRG_N', 'U1.7', [736, 585], 'LED1.K'), wire('LED_A', 'LED1.A', [736, 415], 'R19.1'),
    ic('U3', 'MX1.25 2P', 855, 615, 50, [['1', 'BAT+', 'VBAT'], ['2', 'BAT−', 'GND']], [['4', '', 'GND'], ['3', '', 'GND']], { pitch: 25 }),
    wire('VBAT', 'U1.5', 'U3.1'),
    two('C', 'C2', '100nF', 640, 690, 'v', 'VBAT', 'GND'), two('C', 'C7', '10µF', 725, 690, 'v', 'VBAT', 'GND'),
    wire('VBAT', 'C2.1', [640, 635]), wire('VBAT', 'C7.1', [725, 635]),
    wire('GND', 'C2.2', [640, 750], [810, 750]), wire('GND', 'C7.2', [725, 750]),
    wire('GND', 'U3.2', [810, 660], [810, 780]), port('GND', 810, 780, 0, 1),
    wire('GND', 'U3.4', [945, 635], [945, 700]), wire('GND', 'U3.3', [945, 660]), port('GND', 945, 700, 0, 1),
    two('BAT', 'BT1', '454261', 985, 560, 'v', 'VBAT', 'GND')
  ], ['摆放和连线按嘉立创 EDA 原理图（V1.0，2026-10-21）。网络名沿用文档：EDA 里的 +5V 就是 VBUS，BAT+ 就是 VBAT，DP、DM 就是 USB_DP、USB_DN。', '充电电流 I = 1200 / R20(kΩ) ≈ 600mA。CHRG 是开漏输出，充电时拉低，点亮充电灯 LED1（VBUS → R19 2k → LED1 → CHRG），关机状态也能显示；不接 MCU。STDBY 不接。', 'D1 USBLC6-2SC6：3、4 脚（I/O2）接 D+，1、6 脚（I/O1）接 D−，5 VBUS，2 GND；两组通道对称，哪组走 D+ 都可以。USB0 的外壳脚 12～15 都接地。', 'C34 1µF 直接接 VBUS、紧挨 USB0，负责高频去耦；C35 10µF 的地端经 R25 1Ω 接地，给插拔振铃加阻尼，稳态没有电流（参考 silkscreen-pcb Rev 1.01 的 R84）。电池座 U3 的 3、4 脚是固定脚，接地。BT1 是电池本体，EDA 里没有画。'], null, 1040);

  // SCH 2：EDA 左边中部的负载共享、电源开关和 LDO；v4.26 起电源开关按新方案画（负载开关 + 电源键），EDA 里还要跟着改
  sheet('pwr-sw', '电源开关、负载共享与稳压', 680, [
    port('VBUS', 40, 150, 0, -1), wire('VBUS', [40, 150], 'R21.1'),
    two('SD', 'D2', 'DSS34', 150, 225, 'h', 'VBUS', 'VSYS_IN'), wire('VBUS', [40, 225], 'D2.A'),
    // v4.26 负载开关 U14（TPS22917）：CT 经 C38 接 VIN 做软启动，QOD 接 VOUT 关断时放电，ON 由电源键和 ESP32 一起控制
    ic('U14', 'TPS22917', 290, 205, 80, [['1', 'VIN', 'VSYS_IN']], [['6', 'VOUT', 'VSYS'], ['5', 'QOD', 'VSYS']],
      { r0: 20, h: 80, vx: 290, vy: 198, T: [['4', 'CT', 'CT', 60]], B: [['3', 'ON', 'PWR_EN', 15], ['2', 'GND', 'GND', 60]] }),
    wire('VSYS_IN', 'D2.K', 'U14.1'), wire('VSYS', 'U14.5', [405, 245]),
    // C38 接在 CT 和 VIN 之间（不是接地）：上端直接标 VSYS_IN，一眼看出接的是 VIN
    two('C', 'C38', '2.2nF', 350, 135, 'v', 'VSYS_IN', 'CT'), wire('CT', 'U14.4', 'C38.2'),
    wire('VSYS_IN', 'C38.1', [350, 95]), port('VSYS_IN', 350, 95, 0, -1),
    note(366, 162, 'C38：CT 脚 → VIN（VSYS_IN），不接地'),
    two('C', 'C39', '10µF', 230, 140, 'v', 'VSYS_IN', 'GND', 'l'),
    wire('VSYS_IN', 'C39.1', [230, 100], [265, 100], [265, 225]),
    wire('GND', 'C39.2', [230, 185]), port('GND', 230, 185, 0, 1),
    wire('GND', 'U14.2', [350, 315]), port('GND', 350, 315, 0, 1),
    // ON 由三块电路决定（v4.26）：默认 R30 拉低 = 关；① 按住 SW3，经 D7 的 1→3 拉高 = 开；② ESP32 的 PWR_HOLD 经 D7 的 2→3 锁住；
    // ③ U15 把按键转成 PWR_KEY_N 给 GPIO1。D7 是 BAV70（一颗 SOT-23 里两个共阴二极管），这里拆成两个符号画
    frame(282, 432, 70, 100, '默认关'), frame(385, 432, 115, 130, 'ESP32 保持（GPIO4）'), frame(515, 432, 105, 205, '按键开机'), frame(630, 450, 345, 190, '读按键 → GPIO1'),
    note(315, 412, 'ON（PWR_EN）：任一路为高 → U14 导通；都放开 → R30 拉到 0，断开'),
    two('R', 'R30', '1M', 305, 470, 'v', 'PWR_EN', 'GND'),
    wire('PWR_EN', 'U14.3', 'R30.1'), wire('GND', 'R30.2', [305, 515]), port('GND', 305, 515, 0, 1),
    // v4.35 开发跳线 R33：0603 0Ω 默认不贴，贴上等于一直按着电源键（接 VSYS_IN，不能接 VSYS：那样一开机就自锁、关不掉）
    frame(190, 432, 80, 140, '开发跳线 · 不贴'),
    two('R', 'R33', '0Ω DNP', 230, 470, 'v', 'PWR_EN', 'VSYS_IN', 'l'),
    wire('PWR_EN', [305, 420], [230, 420], 'R33.1'),
    wire('VSYS_IN', 'R33.2', [230, 550]), port('VSYS_IN', 230, 550, 0, 1),
    // 按键这一半的阴极用坐标连（两个半边的 3 脚同名）
    two('D', 'D7', 'BAV70 1→3', 540, 470, 'u', 'PWR_KEY', 'PWR_EN', null, ['1', '3']),
    two('D', 'D7', 'BAV70 2→3', 420, 470, 'u', 'PWR_HOLD', 'PWR_EN', null, ['2', '3']),
    wire('PWR_EN', [305, 420], [540, 420], [540, 440]), wire('PWR_EN', 'D7.3', [420, 420]),
    wire('PWR_HOLD', 'D7.2', [420, 540]), port('PWR_HOLD', 420, 540, 0, 1),
    two('SW', 'SW3', 'EVQPUC02K', 540, 570, 'v', 'PWR_KEY', 'VSYS_IN'),
    wire('VSYS_IN', 'SW3.2', [540, 610]), port('VSYS_IN', 540, 610, 0, 1),
    fet('N', 'U15', '2N7002', 760, 520, 'PWR_KEY', 'PWR_KEY_N', 'GND'),
    wire('PWR_KEY', 'D7.1', [540, 520], 'U15.G'), wire('PWR_KEY', 'SW3.1', [540, 520]),
    // 同图内的导线不自动标名字，这根是理解电路的关键，单独贴一个标签
    port('PWR_KEY', 595, 520, 0, -1),
    two('R', 'R31', '100k', 650, 570, 'v', 'PWR_KEY', 'GND'), wire('PWR_KEY', 'R31.1', [650, 520]),
    wire('GND', 'R31.2', [650, 615]), port('GND', 650, 615, 0, 1),
    wire('GND', 'U15.S', [760, 565]), port('GND', 760, 565, 0, 1),
    two('R', 'R32', '10k', 860, 520, 'v', 'PWR_KEY_N', '3V3'),
    wire('PWR_KEY_N', 'U15.D', [760, 470], [860, 470], 'R32.1'), wire('3V3', 'R32.2', [860, 565]), port('3V3', 860, 565, 0, 1),
    wire('PWR_KEY_N', [860, 470], [900, 470]), port('PWR_KEY_N', 900, 470, 1, 0),
    fet('P', 'Q1', 'AO3401', 200, 295, 'VBUS', 'VSYS_IN', 'VBAT', 'hm'),
    wire('VSYS_IN', 'Q1.S', [265, 295], [265, 225]), wire('VBUS', 'Q1.G', [200, 375], [40, 375]),
    two('R', 'R21', '10k', 40, 410, 'v', 'VBUS', 'GND'),
    wire('VSYS', 'U14.6', 'U4.1'), wire('VSYS', [405, 225], [405, 290]), port('VSYS', 405, 290, 0, 1),
    two('C', 'C9', '10µF', 440, 255, 'v', 'VSYS', 'GND'), wire('VSYS', 'C9.1', [440, 225]),
    ic('U4', 'RT9080-33GJ5', 628, 205, 150, [['1', 'VIN', 'VSYS'], ['2', 'GND', 'GND'], ['3', 'EN', 'VSYS']], [['5', 'VOUT', '3V3'], ['4', 'NC', NC]]),
    wire('VSYS', [480, 225], [480, 265], 'U4.3'),
    wire('GND', 'U4.2', [550, 245], [550, 338]), wire('GND', 'C9.2', [440, 338], [890, 338]), wire('GND', [550, 338], [550, 365]), port('GND', 550, 365, 0, 1),
    wire('3V3', 'U4.5', [950, 225]), port('3V3', 950, 225, 1, 0),
    two('C', 'C10', '10µF', 890, 255, 'v', '3V3', 'GND'), wire('3V3', 'C10.1', [890, 225]), wire('GND', 'C10.2', [890, 338])
  ], ['电源通路：D2（插 USB 时）和 Q1（用电池时）汇合成 VSYS_IN（EDA 里这个节点没有起名，关机时也有电），经负载开关 U14（TPS22917）到 VSYS。U14 的 ON 为高就导通，为低就断开；CT 经 C38 2.2nF 接 VIN，让 VSYS 约 5～9ms 上升（冲击电流约 30mA）；QOD 接 VOUT，关断时把 VSYS 很快放到 0；C39 是输入电容。', 'ON 由图下方三块电路决定（v4.26，取代拨动开关）：默认 R30 把 ON 拉到 0，关机。① 按住电源键 SW3，PWR_KEY 变成 VSYS_IN 的电压，经 D7 的 1→3 把 ON 拉高，开机。② ESP32 起来后把 GPIO4（PWR_HOLD）拉高，经 D7 的 2→3 锁住 ON，松手也不掉电；拉低就关机。③ U15 把按键转成 PWR_KEY_N 给 GPIO1（R32 上拉，按下为低），固件据此计时；R31 让松手时 PWR_KEY 回到 0。D7 是 BAV70（安世 BAV70,215），一颗 SOT-23 里两个共阴二极管，图上拆成两个画：两路谁高听谁的，关机时按键的电压也进不了没电的 ESP32。用硅管不用肖特基：ON 低于 0.35V 才保证关断。', 'R33 是开发跳线（0603 0Ω，默认不贴）：ON 直接接 VSYS_IN，贴上等于一直按着电源键，第一次烧录、救砖、手动 BOOT + RESET 都不用按 SW3；这时长按关机和 RESET 硬关机都失效，量产不贴。','怎么用：按满 3 秒才算开机，不满就放开 PWR_HOLD 断电；开机后长按 3 秒，固件先刷关机画面，等松手后拉低 PWR_HOLD，U14 关断，屏上留着关机画面。关机时 ON 没人拉高，插 USB 只充电不开机；U1 接在 U14 前面，关机也能充电。', 'Q1 栅极直接接 VBUS：插 USB 时栅源电压等于 D2 的压降（约 +0.35V），Q1 关断，系统经 D2 供电；拔掉后 R21 把栅极拉到地，Q1 导通，电池接管。', 'U4 是 RT9080-33GJ5（v4.32 起，TSOT-23-5，引脚和原 HE9073A33M5R 相同）：EN（3 脚）高电平使能，CMOS 输入不能悬空，直接接 VSYS 让 LDO 常开；4 脚 NC。引脚号已对照规格书 DS9080-09。v4.42 起不设测试点，测量直接点在器件焊盘上（见布局页“测量点”）。']);

  // SCH 3：EDA 中上的模组。上边 41～27、下边 1～14、右边 26～15，网络取自 data.js 的引脚表
  const modPin = p => [String(p.pin), p.pin === 41 ? 'GND' : p.name, p.net && p.net !== '—' ? p.net : (p.name === 'GND' || p.name === 'EPAD' ? 'GND' : NC)];
  const pinOf = n => modPin(D.pins.find(p => p.pin === n));
  const U2T = [41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27].map((n, i) => pinOf(n).concat(Math.round(172 + i * 21.07)));
  const U2B = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14].map((n, i) => pinOf(n).concat(Math.round(202 + i * 20.4)));
  const U2R = [26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15].map(pinOf);
  sheet('mcu', '主控', 620, [
    ic('U2', 'ESP32-S3-WROOM-1-N16R8', 20, 120, 560, [], U2R, { r0: 32, h: 304, T: U2T, B: U2B, vin: true }),
    wire('GND', 'U2.41', [192, 80], [213, 80], 'U2.40'), wire('GND', [192, 80], [160, 80]), port('GND', 160, 80, -1, 0),
    // 1 脚接地、2 脚 3V3，下面横放 C6、C5
    two('C', 'C6', '100nF', 182, 516, 'h', 'GND', '3V3'), two('C', 'C5', '22µF', 182, 578, 'h', 'GND', '3V3'),
    wire('GND', 'U2.1', [222, 475], [141, 475]), wire('GND', [141, 475], [110, 475]), port('GND', 110, 475, -1, 0),
    wire('GND', 'C6.1', [141, 516]), wire('GND', [141, 475], [141, 578], 'C5.1'),
    wire('3V3', 'U2.2', [242, 516], 'C6.2'), wire('3V3', [242, 516], [242, 578], 'C5.2'), wire('3V3', [242, 578], [270, 578]), port('3V3', 270, 578, 1, 0),
    // 右边三组：GPIO0 上拉和 BOOT 键、USB 检测分压、EN 上拉和 RST 键
    two('R', 'R26', '10k', 780, 190, 'v', '3V3', 'BOOT'), two('SW', 'BOOT', 'BOOT', 780, 290, 'v', 'BOOT', 'GND'),
    wire('BOOT', 'R26.2', 'BOOT.1'), wire('BOOT', [780, 235], [810, 235]), port('BOOT', 810, 235, 1, 0),
    two('R', 'R28', '100k', 920, 200, 'v', 'VBUS', 'USB_PRESENT'), two('R', 'R29', '150k', 920, 330, 'v', 'USB_PRESENT', 'GND'),
    wire('USB_PRESENT', 'R28.2', 'R29.1'), wire('USB_PRESENT', [920, 265], [890, 265]), port('USB_PRESENT', 890, 265, -1, 0),
    two('R', 'R27', '10k', 1010, 190, 'v', '3V3', 'ESP_EN'),
    two('SW', 'RESET', 'RST', 1050, 240, 'h', 'ESP_EN', 'GND'), two('C', 'C32', '1µF', 1050, 300, 'h', 'ESP_EN', 'GND'),
    wire('ESP_EN', 'R27.2', [1010, 300], 'C32.1'), wire('ESP_EN', [1010, 240], 'RESET.1'),
    wire('GND', 'RESET.2', [1080, 320]), wire('GND', 'C32.2', [1080, 300]), port('GND', 1080, 320, 0, 1),
    wire('ESP_EN', [1010, 240], [990, 240]), port('ESP_EN', 990, 240, -1, 0),
    // 调试焊盘：v4.29 按 EDA 是 RX、TX 两个焊盘
    ic('H3', '调试焊盘', 680, 470, 50, [['1', 'RX', 'U0RXD'], ['2', 'TX', 'U0TXD']])
  ], ['摆放按嘉立创 EDA 原理图：模组上边 41～27、下边 1～14、右边 26～15。网络名沿用文档：EDA 的 EN、GPIO0、RX、TX 分别是这里的 ESP_EN、BOOT、U0RXD、U0TXD，EPD_SCL 是 EPD_SCK。', 'IO35～IO37 被 Octal PSRAM 占用；IO45、IO46 是 strapping 脚，悬空即为默认低电平。IO3（15 脚）v4.29 去掉麦克风后又空着。', 'C5、C6 紧靠模组 3V3 脚（PCB 上靠过孔连 L3 的 3V3），R27、C32 是 EN 的 RC 延时；C32 挪到 EN 脚旁，兼作 EN 去耦，不另加 100nF（v4.39）。GPIO0 除了模组内部的弱上拉，另加 R26 10k 上拉，上电时更不容易被干扰误入下载模式。H3 是 RX、TX 两个贴片焊盘，看开机日志、串口烧录用，GND 就近取。'], null, 1100);

  // SCH 4：EDA 右边的屏 FPC（右）和下面的升压电路（左）
  const J1P = [['1', 'NC', NC], ['2', 'GDR', 'GDR'], ['3', 'RESE', 'RESE'], ['4', 'NC', NC], ['5', 'VSH2', 'VSH2'], ['6', 'GND', 'GND'], ['7', 'GND', 'GND'], ['8', 'BS1', 'GND'],
    ['9', 'BUSY', 'EPD_BUSY'], ['10', 'RST', 'EPD_RST'], ['11', 'DC', 'EPD_DC'], ['12', 'CSB', 'EPD_CS'], ['13', 'SCL', 'EPD_SCK'], ['14', 'SDA', 'EPD_MOSI'], ['15', 'VDDIO', '3V3'], ['16', 'VCC', '3V3'],
    ['17', 'VSS', 'GND'], ['18', 'VDD', 'EPD_VDD'], ['19', 'VPP', 'EPD_VPP'], ['20', 'VSH1', 'VSH1'], ['21', 'VGH', 'VGH'], ['22', 'VSL', 'VSL'], ['23', 'VGL', 'VGL'], ['24', 'VCOM', 'VCOM']];
  // FPC 右边一排电容：[位号, 取值, 网络, 脚号, 列 x]
  const fpcCaps = [['C19', '1µF', 'EPD_VDD', 18, 700], ['C15', '1µF', 'EPD_VPP', 19, 646], ['C20', '4.7µF', 'VSH1', 20, 700], ['C16', '1µF', 'VGH', 21, 646],
    ['C21', '4.7µF', 'VSL', 22, 700], ['C17', '1µF', 'VGL', 23, 646], ['C22', '1µF', 'VCOM', 24, 700]];
  const fpcRow = n => 141 + (n - 1) * 26;
  const epdCaps = [];
  fpcCaps.forEach(([ref, val, net, pin, x]) => epdCaps.push(two('C', ref, val, x, fpcRow(pin), 'h', 'GND', net, 'a'),
    wire('GND', ref + '.1', [576, fpcRow(pin)]), wire(net, ref + '.2', 'FPC1.' + pin)));
  sheet('epd', '墨水屏（SSD1677，按官方转接板）', 860, [
    // 升压与负压电荷泵
    port('3V3', 75, 80, 0, -1), two('L', 'L1', '10µH', 170, 200, 'h', '3V3', 'SW_NODE'),
    wire('3V3', [75, 80], [75, 200], 'L1.1'),
    two('C', 'C4', '22µF', 75, 323, 'v', '3V3', 'GND'), wire('3V3', [75, 200], 'C4.1'), wire('GND', 'C4.2', [75, 441]),
    wire('SW_NODE', 'L1.2', [265, 200]),
    fet('N', 'U13', '2N7002K', 265, 281, 'GDR', 'SW_NODE', 'RESE'), wire('SW_NODE', [265, 200], 'U13.D'),
    two('R', 'R13', '2.2Ω', 265, 372, 'v', 'RESE', 'GND'),
    wire('RESE', 'U13.S', [265, 333], 'R13.1'), wire('RESE', [265, 333], [330, 333]), port('RESE', 330, 333, 1, 0), wire('GND', 'R13.2', [265, 441]),
    two('R', 'R12', '10k', 165, 330, 'v', 'GDR', 'GND'),
    wire('GDR', 'U13.G', [165, 281], 'R12.1'), wire('GDR', [165, 281], [120, 281]), port('GDR', 120, 281, -1, 0), wire('GND', 'R12.2', [165, 441]),
    two('C', 'C11', '4.7µF', 265, 165, 'u', 'SW_NODE', 'CP_NODE'), wire('SW_NODE', [265, 200], 'C11.1'),
    two('SD', 'D5', 'MBR0530', 365, 107, 'h', 'CP_NODE', 'GND'), two('SD', 'D4', 'MBR0530', 365, 47, 'hr', 'VGL', 'CP_NODE'),
    wire('CP_NODE', 'C11.2', [265, 107], 'D5.A'), wire('CP_NODE', [265, 107], [265, 47], 'D4.K'),
    two('C', 'C33', '4.7µF', 456, 77, 'v', 'VGL', 'GND'),
    wire('VGL', 'D4.A', 'C33.1'), wire('VGL', 'C33.1', [490, 47]), port('VGL', 490, 47, 1, 0),
    wire('GND', 'D5.K', 'C33.2'), wire('GND', 'C33.2', [490, 107]), port('GND', 490, 107, 1, 0),
    two('SD', 'D6', 'MBR0530', 365, 200, 'h', 'SW_NODE', 'VGH'), wire('SW_NODE', [265, 200], 'D6.A'),
    wire('VGH', 'D6.K', [456, 200]), wire('VGH', [456, 200], [490, 200]), port('VGH', 490, 200, 1, 0),
    two('C', 'C12', '4.7µF', 456, 320, 'v', 'VGH', 'GND'), wire('VGH', [456, 200], 'C12.1'), wire('GND', 'C12.2', [456, 441]),
    wire('GND', [75, 441], [456, 441]), wire('GND', [265, 441], [265, 465]), port('GND', 265, 465, 0, 1),
    // FPC1 和电容
    ic('FPC1', 'FPC24-T1S1-2030-A', 840, 94, 80, J1P, [], { pitch: 26, r0: 47, h: 678, vx: 725, vy: 785, T: [['26', '', 'GND', 70]], B: [['25', '', 'GND', 70]] }),
    wire('GND', 'FPC1.26', [910, 50], [657, 50]), wire('GND', [657, 50], [657, 28]), port('GND', 657, 28, 0, -1),
    wire('GND', [657, 50], [657, fpcRow(8)], 'FPC1.8'), wire('GND', 'FPC1.6', [657, fpcRow(6)]), wire('GND', 'FPC1.7', [657, fpcRow(7)]),
    two('C', 'C13', '4.7µF', 735, fpcRow(5), 'h', 'GND', 'VSH2', 'a'), wire('GND', 'C13.1', [657, fpcRow(5)]), wire('VSH2', 'C13.2', 'FPC1.5'),
    port('3V3', 716, 465, 0, -1), wire('3V3', [716, 465], [716, fpcRow(16)], 'FPC1.16'), wire('3V3', [716, fpcRow(15)], 'FPC1.15'),
    two('C', 'C3', '22µF', 646, 485, 'h', 'GND', '3V3', 'a'), wire('3V3', 'C3.2', [716, 485]),
    two('C', 'C14', '1µF', 646, fpcRow(16), 'h', 'GND', '3V3', 'a'), wire('3V3', 'C14.2', [716, fpcRow(16)]),
    wire('GND', 'C3.1', [576, 485]), wire('GND', 'C14.1', [576, fpcRow(16)]), wire('GND', 'FPC1.17', [576, fpcRow(17)]),
    wire('GND', 'FPC1.25', [910, 810], [576, 810]), wire('GND', [576, 485], [576, 830]), port('GND', 576, 830, 0, 1),
    port('VGH', 780, fpcRow(21), 0, -1), port('VGL', 780, fpcRow(23), 0, -1)
  ].concat(epdCaps), ['摆放按嘉立创 EDA 原理图：左边是升压和负压电荷泵，右边是屏的 FPC1 和电容。器件和取值按官方转接板（osptek 4.26 V1.0）逐一核对过，只有 D4 改为阳极接 VGL（官方 PDF 画反了，与 silkscreen-pcb 一致）。左右两边靠 VGH、VGL、GDR、RESE 同名标签相连；FPC1 的 9～14 脚经 EPD_BUSY、EPD_RST、EPD_DC、EPD_CS、EPD_SCK、EPD_MOSI 接到主控页（EDA 里叫 EPD_SCL）。', 'C11～C13、C16、C17、C20～C22、C33 都是 25V 耐压（C4 在 3V3 上，普通耐压即可）；D4、D5 方向不能反。FPC1 的 25、26 是固定脚，接地。各器件的取值说明在墨水屏页。']);

  // SCH 5：EDA 中下的电量计、I2C 总线和三颗传感器，右上是按键
  const keys = [];
  [['R22', 'SW6', 'KEY1', 838, 855], ['R23', 'SW1', 'KEY2', 924, 940], ['R24', 'SW2', 'KEY3', 997, 1015]].forEach(([r, s, n, xs, xr]) => {
    keys.push(two('R', r, '10k', xr, 100, 'v', '3V3', n), two('SW', s, n, xs, 190, 'v', n, 'GND'),
      wire(n, r + '.2', [xr, 140], [xs, 140]), wire(n, [xs, 140], s + '.1'), wire(n, [xs, 140], [xs - 14, 140]), port(n, xs - 14, 140, -1, 0));
  });
  sheet('sens', '电量计、传感器与按键', 600, [
    // 电量计 U10（EDA 里在 I2C 总线上方）
    port('GND', 118, 53, -1, 0), wire('GND', [118, 53], [316, 53]),
    two('C', 'C30', '1µF', 153, 99, 'v', 'GND', 'CW_CELL'), wire('GND', 'C30.1', [153, 53]),
    two('R', 'R17', '470Ω', 100, 146, 'h', 'VBAT', 'CW_CELL'), wire('CW_CELL', 'R17.2', [153, 146]), wire('CW_CELL', 'C30.2', [153, 146]),
    two('C', 'C31', '1µF', 316, 95, 'v', 'GND', 'CW_VDD'), wire('GND', 'C31.1', [316, 53]),
    two('R', 'R18', '100Ω', 260, 125, 'h', 'VBAT', 'CW_VDD', 'a'), wire('CW_VDD', 'R18.2', 'C31.2'),
    ic('U10', 'CW2015CHBD', 445, 51, 253, [null, ['3', 'VDD', 'CW_VDD'], ['2', 'CELL', 'CW_CELL'], ['1', 'CTG', 'GND'], ['4', 'GND', 'GND']],
      [['5', 'ALRT#', 'RTC_INT'], ['8', 'SDA', 'I2C_SDA'], ['7', 'SCL', 'I2C_SCL'], ['6', 'QSTRT', 'GND'], ['9', 'EP', 'GND']], { pitch: 18, r0: 18 }),
    wire('CW_VDD', 'C31.2', [363, 125], [363, 87], 'U10.3'), wire('CW_CELL', [153, 146], [380, 146], [380, 105], 'U10.2'),
    wire('GND', 'U10.1', [410, 123], [410, 180], [735, 180]), wire('GND', 'U10.4', [410, 141]),
    wire('GND', 'U10.6', [735, 123], [735, 180]), wire('GND', 'U10.9', [735, 141]), wire('GND', [735, 180], [735, 200]), port('GND', 735, 200, 0, 1),
    wire('I2C_SDA', 'U10.8', [780, 87], [780, 372], 'U8.2'), wire('I2C_SCL', 'U10.7', [760, 105], [760, 303]),
    // I2C 总线和上拉
    port('I2C_SDA', 88, 282, -1, 0), port('I2C_SCL', 88, 303, -1, 0),
    wire('I2C_SDA', [88, 282], [780, 282]), wire('I2C_SCL', [88, 303], [990, 303]),
    two('R', 'R4', '4.7k', 240, 257, 'h', '3V3', 'I2C_SCL', 'a'), wire('I2C_SCL', 'R4.2', [293, 257], [293, 303]),
    two('R', 'R5', '4.7k', 348, 257, 'h', 'I2C_SDA', '3V3', 'a'), wire('I2C_SDA', 'R5.1', [310, 257], [310, 282]),
    // RTC U6、晶振 X1、INT 上拉 R3
    ic('U6', 'PCF8563DTR', 175, 360, 142, [['1', 'OSCI', 'RTC_XI'], ['2', 'OSCO', 'RTC_XO'], ['3', 'INT#', 'RTC_INT'], ['4', 'VSS', 'GND']],
      [['8', 'VDD', 'VBAT'], ['7', 'CLKOUT', NC], ['6', 'SCL', 'I2C_SCL'], ['5', 'SDA', 'I2C_SDA']], { pitch: 18, r0: 20 }),
    two('XTAL', 'X1', '32.768kHz', 93, 415, 'v', 'RTC_XI', 'RTC_XO', 'l'),
    wire('RTC_XI', 'X1.1', [93, 380], 'U6.1'), wire('RTC_XO', 'X1.2', [93, 460], [125, 460], [125, 398], 'U6.2'),
    // v4.36 补画 C41：X1 的负载电容，接 OSCI（RTC_XI）和地；OSCO 一侧用 PCF8563 内部约 25pF
    two('C', 'C41', '18pF', 40, 540, 'v', 'RTC_XI', 'GND', 'l'),
    wire('RTC_XI', 'C41.1', [40, 500]), port('RTC_XI', 40, 500, 0, -1),
    wire('GND', 'C41.2', [40, 580]), port('GND', 40, 580, 0, 1),
    two('R', 'R3', '10k', 100, 510, 'v', 'RTC_INT', '3V3'),
    wire('RTC_INT', 'U6.3', [140, 416], [140, 471], [100, 471], 'R3.1'), wire('RTC_INT', [100, 471], [75, 471]), port('RTC_INT', 75, 471, -1, 0),
    wire('GND', 'U6.4', [153, 490]), port('GND', 153, 490, 0, 1),
    wire('I2C_SCL', 'U6.6', [383, 416], [383, 303]), wire('I2C_SDA', 'U6.5', [397, 434], [397, 282]),
    // v4.29 CO2 U7（STCC4）和给它做温湿度补偿的 U11（SHT40，挂在 U7 的 I2C 主口 SDA_C、SCL_C 上）
    ic('U7', 'STCC4-D-R3', 470, 360, 110, [['1', 'SCL', 'I2C_SCL'], ['7', 'VDD', '3V3'], ['6', 'VDDIO', '3V3'], ['3', 'ADDR', 'GND']],
      [['5', 'SDA', 'I2C_SDA'], ['2', 'SDA_C', 'SHT_SDA'], ['8', 'SCL_C', 'SHT_SCL'], ['4', 'VSS', 'GND']], { pitch: 18, r0: 20 }),
    wire('I2C_SCL', 'U7.1', [412, 380], [412, 303]), wire('I2C_SDA', 'U7.5', [632, 380], [632, 282]),
    wire('GND', 'U7.3', [440, 434]), port('GND', 440, 434, 0, 1), wire('GND', 'U7.4', [612, 434], [612, 448]), port('GND', 612, 448, 0, 1),
    ic('U11', 'SHT40-AD1B', 640, 378, 60, [['1', 'SDA', 'SHT_SDA'], ['2', 'SCL', 'SHT_SCL']], [['3', 'VDD', '3V3'], ['4', 'VSS', 'GND']], { pitch: 18, r0: 20 }),
    wire('SHT_SDA', 'U7.2', 'U11.1'), wire('SHT_SCL', 'U7.8', 'U11.2'),
    wire('3V3', 'U11.3', [730, 398], [730, 372]), port('3V3', 730, 372, 0, -1), wire('GND', 'U11.4', [735, 416]), port('GND', 735, 416, 1, 0),
    // 姿态 U8
    ic('U8', 'SC7A20HTR', 810, 354, 150, [['2', 'SDx', 'I2C_SDA'], ['1', 'SDO', '3V3'], ['3', 'VDDIO', '3V3'], ['4', 'NC', 'GND'], ['5', 'INT1', 'ACC_INT1'], ['6', 'INT2', 'ACC_INT2']],
      [['12', 'SCx', 'I2C_SCL'], ['11', 'NC', NC], ['10', 'CS', '3V3'], ['9', 'GND', 'GND'], ['8', 'GNDIO', 'GND'], ['7', 'VDD', '3V3']], { pitch: 18, r0: 18 }),
    wire('3V3', 'U8.1', [772, 390], [772, 408], 'U8.3'), wire('3V3', [772, 390], [750, 390], [750, 350]), port('3V3', 750, 350, 0, -1),
    wire('I2C_SCL', 'U8.12', [990, 372], [990, 303]),
    wire('GND', 'U8.9', [1000, 426], [1000, 444], 'U8.8'), wire('GND', [1000, 435], [1015, 435]), port('GND', 1015, 435, 1, 0),
    // 去耦电容
    two('C', 'C18', '100nF', 416, 514, 'v', '3V3', 'GND'), two('C', 'C23', '100nF', 356, 514, 'v', 'VBAT', 'GND'), two('C', 'C24', '100nF', 537, 514, 'v', '3V3', 'GND'),
    wire('3V3', 'C18.1', 'C24.1'), wire('VBAT', 'C23.1', [356, 474]), port('VBAT', 356, 474, 0, -1), wire('3V3', 'C18.1', [416, 468]), port('3V3', 416, 468, 0, -1),
    wire('GND', 'C18.2', 'C24.2'), wire('GND', 'C23.2', [416, 544]), wire('GND', 'C24.2', [537, 560]), port('GND', 537, 560, 0, 1),
    two('C', 'C40', '100nF', 690, 520, 'v', '3V3', 'GND'), wire('3V3', 'C40.1', [690, 488]), port('3V3', 690, 488, 0, -1), wire('GND', 'C40.2', [690, 556]), port('GND', 690, 556, 0, 1),
    note(300, 592, 'C23、C18、C24、C40 分别紧靠 U6、U7、U8、U11 的电源脚'),
    // 按键上拉的 3V3 横线
    wire('3V3', 'R22.1', [855, 55], [1015, 55], 'R24.1'), wire('3V3', 'R23.1', [940, 55]), wire('3V3', [940, 55], [940, 40]), port('3V3', 940, 40, 0, -1),
    wire('GND', 'SW1.2', [838, 251], [997, 251]), wire('GND', 'SW2.2', [924, 251]), wire('GND', 'SW6.2', [997, 251]), wire('GND', [997, 251], [1015, 251], [1015, 265]), port('GND', 1015, 265, 0, 1)
  ].concat(keys), ['摆放按嘉立创 EDA 原理图：电量计在 I2C 总线上方，三颗传感器挂在总线下面，按键在右上。网络名沿用文档：EDA 的 BAT+ 是 VBAT。', 'v4.29 U7 由 AHT20 换成 STCC4（CO2，地址 0x64，ADDR 接地）。U11 SHT40-AD1B（0x44）只接 U7 的 SDA_C、SCL_C，不上主总线；这两根线 U7 内部有 10k 上拉，两颗挨着放时不加外部上拉。VDDIO 和 VDD 接同一路 3V3，保证上电时 VDDIO 已有电。', 'U10 CW2015 负责电池电压和电量，VDD、CELL 直接接电池 VBAT，关机后不断电，由固件让它睡眠，约 0.5µA（v4.43，原来一直工作约 15µA）；R18 100Ω 接 VDD、R17 470Ω 接 CELL，按规格书 Fig10（已核对）。ALRT 是开漏输出，和 U6 的 INT 线与到 RTC_INT（GPIO16），共用 R3 上拉。', 'C41 18pF 是晶振 X1 的负载电容，接 U6 的 OSCI（RTC_XI）和地，OSCO 一侧用芯片内部约 25pF；样板上测一天走时，偏快加大、偏慢减小。U6 PCF8563 的 VDD 和 U10 一样直接接电池 VBAT（v4.27，原来接 3V3），C23 跟着接 VBAT：长按关机后 3V3 没电，U6 仍然走时，约 0.25µA。I2C 上拉和 RTC_INT 上拉仍接 3V3：高电平门限 0.7 × VDD，电池 4.2V 时约 2.94V，3.3V 能识别；电池低于 3.3V 时 I2C 高电平略高于 VDD，在 VDD + 0.5V 的输入耐压以内。关机时总线被拉到 0V，U6 只看到总线空闲。', 'U7 STCC4 引脚按 Sensirion 规格书表 5（透视顶视图）；U11 SHT40 是 1 SDA、2 SCL、3 VDD、4 VSS。U8 按士兰微 SC7A20H 说明书第 5 页：SDO 接高 → 地址 0x19，CS 接高 → I2C。', '按键按下为低，三个都能做 ext1 唤醒：SW6 菜单（KEY1，长按朗读或切换模式）、SW1 上一页（KEY2）、SW2 下一页（KEY3）。EVQPUC02K 的 1、3 脚内部相连接按键线，2、4 脚内部相连接地，这里每边只画一个脚。线与线交叉处没有圆点的表示不相连。'], null, 1060);

  // SCH 6：EDA 左下的功放和 TF 卡
  const sdPull = [['R6', 319], ['R7', 375], ['R8', 429], ['R9', 484], ['R10', 565]];
  const tf = [];
  sdPull.forEach(([r, x], i) => tf.push(two('R', r, '10k', x, 385, 'v', 'SD_VCC', ['SD_D1', 'SD_D0', 'SD_CMD', 'SD_D3', 'SD_D2'][i]), wire('SD_VCC', r + '.1', [x, 338])));
  sheet('audio', '音频与 TF 卡', 690, [
    // 功放 U9 和喇叭焊盘 H4（v4.27 取代原来的喇叭座）
    ic('U9', 'NS4168', 243, 103, 168, [null, ['1', 'CTRL', 'AMP_CTRL'], ['2', 'LRCLK', 'I2S_LRCLK'], ['3', 'BCLK', 'I2S_BCLK'], ['4', 'SDATA', 'I2S_DIN']],
      [['6', 'VDD', 'VSYS'], ['8', 'VOP', 'SPK_P'], ['7', 'GND', 'GND'], ['9', 'EP', 'GND'], ['5', 'VON', 'SPK_N']], { pitch: 23, r0: 28 }),
    wire('AMP_CTRL', 'U9.1', [70, 154]), port('AMP_CTRL', 70, 154, -1, 0),
    two('R', 'R16', '100k', 97, 205, 'v', 'AMP_CTRL', 'GND', 'l'), wire('AMP_CTRL', [97, 154], 'R16.1'),
    wire('VSYS', 'U9.6', [455, 131], [455, 40], [700, 40]), port('VSYS', 700, 40, 1, 0),
    two('C', 'C29', '100nF', 478, 80, 'v', 'VSYS', 'GND'), two('C', 'C28', '10µF', 535, 80, 'v', 'VSYS', 'GND'), two('C', 'C36', '22µF', 600, 80, 'v', 'VSYS', 'GND'), two('C', 'C37', '22µF', 660, 80, 'v', 'VSYS', 'GND'),
    wire('VSYS', [478, 40], 'C29.1'), wire('VSYS', [535, 40], 'C28.1'), wire('VSYS', [600, 40], 'C36.1'), wire('VSYS', [600, 40], [660, 40], 'C37.1'),
    wire('GND', 'C29.2', [478, 121], [600, 121], 'C36.2'), wire('GND', 'C37.2', [660, 121], [600, 121]), wire('GND', 'C28.2', [535, 121]),
    wire('GND', 'U9.7', [478, 177], [478, 121]), wire('GND', 'U9.9', [445, 200], [445, 177]), wire('GND', [478, 177], [510, 177]), port('GND', 510, 177, 1, 0),
    ic('H4', '喇叭焊盘', 610, 134, 58, [['1', '+', 'SPK_P'], ['2', '−', 'SPK_N']], [], { pitch: 24, r0: 20, h: 70, vx: 700, vy: 215 }),
    wire('SPK_P', 'U9.8', 'H4.1'), wire('SPK_N', 'U9.5', [570, 223], [570, 178], 'H4.2'),
    two('SPK', 'LS1', '8Ω 1W', 790, 166, 'v', 'SPK_P', 'SPK_N'),
    // TF 卡：引脚在卡座右边，上面一排上拉，右上是卡电源开关 Q2
    ic('CARD1', 'TF-115-BCP9', 27, 336, 167, [], [['13', 'GND', 'GND'], ['12', 'GND', 'GND'], ['11', 'GND', 'GND'], ['10', 'GND', 'GND'], ['6', 'VSS', 'GND'], ['4', 'VDD', 'SD_VCC'],
      ['8', 'DAT1', 'SD_D1'], ['7', 'DAT0', 'SD_D0'], ['5', 'CLK', 'SD_CLK_J'], ['9', 'CD', NC], ['3', 'CMD', 'SD_CMD'], ['2', 'CD/DAT3', 'SD_D3'], ['1', 'DAT2', 'SD_D2']], { pitch: 23, r0: 28, h: 329 }),
    wire('GND', 'CARD1.13', [230, 364], [230, 320]), wire('GND', 'CARD1.12', [230, 387]), wire('GND', 'CARD1.11', [230, 410]), wire('GND', 'CARD1.10', [230, 433]),
    wire('GND', 'CARD1.6', [230, 456], [230, 364]), port('GND', 230, 320, 0, -1),
    wire('SD_VCC', 'CARD1.4', [262, 479], [262, 338], 'Q2.D'),
    wire('SD_D1', 'R6.2', [319, 502], 'CARD1.8'),
    wire('SD_D0', 'R7.2', [375, 525], 'CARD1.7'), wire('SD_D0', [375, 525], [395, 525]), port('SD_D0', 395, 525, 1, 0),
    wire('SD_CMD', 'R8.2', [429, 425], [469, 425], [469, 594]), wire('SD_CMD', 'CARD1.3', [490, 594]), port('SD_CMD', 490, 594, 1, 0),
    wire('SD_D3', 'R9.2', [484, 435], [527, 435], [527, 617], 'CARD1.2'),
    wire('SD_D2', 'R10.2', [565, 445], [610, 445], [610, 640], 'CARD1.1'),
    two('R', 'R11', '33Ω', 305, 561, 'h', 'SD_CLK_J', 'SD_CLK'), wire('SD_CLK_J', 'CARD1.5', [250, 548], [250, 561], 'R11.1'),
    two('C', 'C26', '100nF', 642, 368, 'v', 'SD_VCC', 'GND'), two('C', 'C25', '10µF', 709, 368, 'v', 'SD_VCC', 'GND'),
    wire('SD_VCC', 'C26.1', [642, 338]), wire('SD_VCC', 'C25.1', [709, 338]),
    wire('GND', 'C26.2', [642, 410], [709, 410], 'C25.2'), wire('GND', [709, 410], [709, 425]), port('GND', 709, 425, 0, 1),
    fet('P', 'Q2', 'AO3401A', 803, 338, 'SD_PWR_G', '3V3', 'SD_VCC', 'hm'),
    two('R', 'R14', '10k', 754, 576, 'v', 'SD_PWR_G', 'SD_PWR'), two('R', 'R15', '100k', 800, 421, 'h', 'SD_PWR_G', '3V3'), two('C', 'C27', '100nF', 800, 504, 'h', 'SD_PWR_G', '3V3'),
    wire('SD_PWR_G', 'Q2.G', [803, 385], [754, 385], 'R14.1'), wire('SD_PWR_G', 'R15.1', [754, 421]), wire('SD_PWR_G', 'C27.1', [754, 504]),
    wire('3V3', 'Q2.S', [900, 338]), port('3V3', 900, 338, 1, 0), wire('3V3', 'R15.2', [873, 421], [873, 338]), wire('3V3', 'C27.2', [873, 504], [873, 421])
  ].concat(tf), ['摆放按嘉立创 EDA 原理图：上面是功放，下面是 TF 卡。EDA 里没有画喇叭本体 LS1，这里画在喇叭焊盘 H4 右边，靠 SPK_P、SPK_N 标签相连。v4.27 去掉原来的喇叭座（MX1.25 卧贴），喇叭线直接焊在 H4 的两个贴片焊盘上。', 'U9 NS4168 是 ESOP-8，引脚号按 ChipSourceTek 版规格书 V1.3；底部散热焊盘 EP（9 脚）接地，多打过孔到地平面。VDD 接 VSYS，不接 3V3；VOP、VON 是桥式输出，两端都不能接地。', 'GPIO10 拉低时经 R14 给 Q2 栅极放电，C27 让栅压慢慢下降，Q2 约 1ms 才完全导通，C25 的充电电流被限制在 50mA 左右（v4.27 C27 由 47nF 改 100nF），3V3 不会被拉跌（F18）。R11 33Ω 串在 CLK 上，PCB 上放在模组那一端（待办）。上拉电阻接卡电源 SD_VCC，不接 3V3：R8 CMD、R7 DAT0、R6 DAT1、R10 DAT2、R9 DAT3。卡座 10～13 是外壳脚，接地；9 脚 CD 不接。'], null, 1000);

  /* ---------- geometry ---------- */
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const tw = s => [...String(s)].reduce((a, c) => a + (c.charCodeAt(0) > 255 ? 10 : 5.9), 0);
  const ROT = { v: 0, u: 180, h: -90, hr: 90 };
  const rot = (deg, px, py) => { const a = deg * Math.PI / 180, c = Math.round(Math.cos(a)), s = Math.round(Math.sin(a)); return [px * c - py * s, px * s + py * c]; };
  const isDiode = t => t === 'D' || t === 'SD' || t === 'TVS' || t === 'LED';

  const parts = {}, nets = {}, problems = [];
  const conn = (net, ref, pin, sheetId) => { if (net === NC) return; (nets[net] = nets[net] || []).push({ ref, pin, sheet: sheetId }); };

  /* ---------- symbols ---------- */
  const SYM = {
    R: '<line x1="0" y1="-30" x2="0" y2="-14"/><rect class="bd" x="-5" y="-14" width="10" height="28"/><line x1="0" y1="14" x2="0" y2="30"/>',
    C: '<line x1="0" y1="-30" x2="0" y2="-4"/><line x1="-10" y1="-4" x2="10" y2="-4" class="th"/><line x1="-10" y1="4" x2="10" y2="4" class="th"/><line x1="0" y1="4" x2="0" y2="30"/>',
    CP: '<line x1="0" y1="-30" x2="0" y2="-4"/><line x1="-10" y1="-4" x2="10" y2="-4" class="th"/><path d="M-10,7 Q0,1 10,7" class="th"/><line x1="0" y1="4" x2="0" y2="30"/><text class="pm" x="-15" y="-7">+</text>',
    L: '<line x1="0" y1="-30" x2="0" y2="-16"/><path d="M0,-16 a4,4 0 0 1 0,8 a4,4 0 0 1 0,8 a4,4 0 0 1 0,8 a4,4 0 0 1 0,8"/><line x1="0" y1="16" x2="0" y2="30"/>',
    FB: '<line x1="0" y1="-30" x2="0" y2="-8"/><rect class="fl" x="-5" y="-8" width="10" height="16"/><line x1="0" y1="8" x2="0" y2="30"/>',
    D: '<line x1="0" y1="-30" x2="0" y2="-8"/><path class="fl" d="M-8,-8 L8,-8 L0,6 Z"/><line x1="-9" y1="6" x2="9" y2="6" class="th"/><line x1="0" y1="6" x2="0" y2="30"/>',
    SD: '<line x1="0" y1="-30" x2="0" y2="-8"/><path class="fl" d="M-8,-8 L8,-8 L0,6 Z"/><path d="M-11,3 L-11,6 L11,6 L11,9" class="th"/><line x1="0" y1="6" x2="0" y2="30"/>',
    TVS: '<line x1="0" y1="-30" x2="0" y2="-8"/><path class="fl" d="M-8,-8 L8,-8 L0,6 Z"/><path d="M-11,9 L-8,6 L8,6 L11,3" class="th"/><line x1="0" y1="6" x2="0" y2="30"/>',
    LED: '<line x1="0" y1="-30" x2="0" y2="-8"/><path class="fl" d="M-8,-8 L8,-8 L0,6 Z"/><line x1="-8" y1="6" x2="8" y2="6" class="th"/><line x1="0" y1="6" x2="0" y2="30"/><path d="M10,-4 l7,-5 M14,-9 l3,0 l0,3 M10,2 l7,-5 M14,-3 l3,0 l0,3"/>',
    SW: '<line x1="0" y1="-30" x2="0" y2="-10"/><circle class="pt0" cx="0" cy="-10" r="2"/><circle class="pt0" cx="0" cy="10" r="2"/><line x1="0" y1="10" x2="0" y2="30"/><line x1="-9" y1="-12" x2="-9" y2="12" class="th"/><line x1="-9" y1="0" x2="-15" y2="0"/>',
    SWT: '<line x1="0" y1="-30" x2="0" y2="-10"/><circle class="pt0" cx="0" cy="-10" r="2"/><circle class="pt0" cx="0" cy="10" r="2"/><line x1="0" y1="10" x2="0" y2="30"/><line x1="0" y1="-10" x2="-9" y2="8" class="th"/>',
    XTAL: '<line x1="0" y1="-30" x2="0" y2="-10"/><line x1="-8" y1="-10" x2="8" y2="-10" class="th"/><rect class="bd" x="-6" y="-6" width="12" height="12"/><line x1="-8" y1="10" x2="8" y2="10" class="th"/><line x1="0" y1="10" x2="0" y2="30"/>',
    BAT: '<line x1="0" y1="-30" x2="0" y2="-4"/><line x1="-11" y1="-4" x2="11" y2="-4"/><line x1="-5" y1="3" x2="5" y2="3" class="th2"/><line x1="0" y1="3" x2="0" y2="30"/><text class="pm" x="-17" y="-6">+</text>',
    SPK: '<line x1="0" y1="-30" x2="0" y2="-10"/><rect class="bd" x="-5" y="-10" width="10" height="20"/><path class="bd" d="M5,-10 L14,-18 L14,18 L5,10 Z"/><line x1="0" y1="10" x2="0" y2="30"/>'
  };

  function netSym(x, y, dx, dy, net, vert) {
    if (net === NC) return `<g class="ncx"><line x1="${x - 4}" y1="${y - 4}" x2="${x + 4}" y2="${y + 4}"/><line x1="${x - 4}" y1="${y + 4}" x2="${x + 4}" y2="${y - 4}"/></g>`;
    // 芯片上下两边的信号标签竖排（同 EDA），文字从下往上读
    if (vert && dy && net !== 'GND' && !POWER.has(net)) {
      const w = tw(net) + 8, h = 13, ry = dy > 0 ? y : y - w;
      return `<g class="nl sig" data-net="${esc(net)}"><rect class="tag" x="${x - h / 2}" y="${ry}" width="${h}" height="${w}" rx="2"/><text transform="translate(${x + 3.4} ${ry + w / 2}) rotate(-90)" text-anchor="middle">${esc(net)}</text></g>`;
    }
    const d = `data-net="${esc(net)}"`;
    if (net === 'GND') {
      const ex = x + dx * 6, ey = y + dy * 6, px = dy, py = dx;
      const bar = (o, l) => `<line x1="${ex + dx * o - px * l}" y1="${ey + dy * o - py * l}" x2="${ex + dx * o + px * l}" y2="${ey + dy * o + py * l}"/>`;
      return `<g class="nl gnd" ${d}><line x1="${x}" y1="${y}" x2="${ex}" y2="${ey}"/>${bar(0, 8)}${bar(3, 5)}${bar(6, 2)}</g>`;
    }
    if (POWER.has(net)) {
      const ex = x + dx * 8, ey = y + dy * 8, px = dy, py = dx, w = tw(net);
      const tx = dx ? ex + dx * 4 : ex, ty = dy ? ey + dy * (dy > 0 ? 12 : 4) : ey + 3.5;
      const anchor = dx > 0 ? 'start' : dx < 0 ? 'end' : 'middle';
      return `<g class="nl pw" ${d}><line x1="${x}" y1="${y}" x2="${ex}" y2="${ey}"/><line x1="${ex - px * 7}" y1="${ey - py * 7}" x2="${ex + px * 7}" y2="${ey + py * 7}"/>` +
        `<rect class="hit" x="${(dx > 0 ? tx : dx < 0 ? tx - w : tx - w / 2) - 2}" y="${ty - 10}" width="${w + 4}" height="13"/><text x="${tx}" y="${ty}" text-anchor="${anchor}">${esc(net)}</text></g>`;
    }
    const w = tw(net) + 8, h = 13;
    let rx, ry;
    if (dx) { rx = dx > 0 ? x : x - w; ry = y - h / 2; } else { rx = x - w / 2; ry = dy > 0 ? y : y - h; }
    return `<g class="nl sig" ${d}><rect class="tag" x="${rx}" y="${ry}" width="${w}" height="${h}" rx="2"/><text x="${rx + w / 2}" y="${ry + 9.6}" text-anchor="middle">${esc(net)}</text></g>`;
  }

  /* ---------- build one sheet: parts → terminals → wires → labels → junction dots ---------- */
  function buildSheet(sh) {
    const T = {}, partSvg = [], extra = [], frames = [];
    const addT = (ref, key, x, y, dx, dy, net) => { T[ref + '.' + key] = { x, y, dx, dy, net }; };
    sh.items.forEach(it => {
      if (it.k === 'frame') { frames.push(`<rect x="${it.x}" y="${it.y}" width="${it.w}" height="${it.h}" rx="4" fill="none" stroke="#A3A69C" stroke-width="1" stroke-dasharray="5 3"/><text x="${it.x + 4}" y="${it.y + it.h + 14}" font-size="11" font-weight="600" fill="#62675f">${esc(it.label)}</text>`); return; }
      if (it.k === 'note') { extra.push(`<text class="nt" x="${it.x}" y="${it.y}">${esc(it.text)}</text>`); return; }
      if (it.k === 'port' || it.k === 'wire') return;
      const rec = parts[it.ref] = parts[it.ref] || { ref: it.ref, val: it.val, sheet: sh.id, pins: [] };
      let g = '';
      if (it.k === 'two') {
        g = `<g transform="translate(${it.x} ${it.y}) rotate(${ROT[it.o]})">${SYM[it.t]}</g>`;
        it.n.forEach((net, i) => {
          const [ox, oy] = rot(ROT[it.o], 0, i ? 30 : -30), [dx, dy] = rot(ROT[it.o], 0, i ? 1 : -1);
          const pin = it.pn ? it.pn[i] : isDiode(it.t) ? (i ? 'K' : 'A') : String(i + 1);
          addT(it.ref, pin, it.x + ox, it.y + oy, dx, dy, net);
          rec.pins.push({ pin, net }); conn(net, it.ref, pin, sh.id);
        });
        const vert = it.o === 'v' || it.o === 'u', off = it.t === 'LED' || it.t === 'SPK' ? 22 : 13;
        if (vert && it.lp === 'l') g += `<text class="rf" x="${it.x - 13}" y="${it.y - 3}" text-anchor="end">${it.ref}</text><text class="vl" x="${it.x - 13}" y="${it.y + 9}" text-anchor="end">${esc(it.val)}</text>`;
        else if (vert && it.lp === 'u') g += `<text class="rf" x="${it.x + off}" y="${it.y - 13}">${it.ref}</text><text class="vl" x="${it.x + off}" y="${it.y - 1}">${esc(it.val)}</text>`;
        else if (vert) g += `<text class="rf" x="${it.x + off}" y="${it.y - 3}">${it.ref}</text><text class="vl" x="${it.x + off}" y="${it.y + 9}">${esc(it.val)}</text>`;
        else if (it.lp) g += `<text class="rf" x="${it.x}" y="${it.lp === 'a' ? it.y - 11 : it.y + 19}" text-anchor="middle">${it.ref}<tspan class="vl"> ${esc(it.val)}</tspan></text>`;
        else g += `<text class="rf" x="${it.x}" y="${it.y - 13}" text-anchor="middle">${it.ref}</text><text class="vl" x="${it.x}" y="${it.y + 21}" text-anchor="middle">${esc(it.val)}</text>`;
      } else if (it.k === 'tp') {
        const s = it.up ? 1 : -1, ty = it.y - s * 14;
        g = `<circle class="bd" cx="${it.x}" cy="${it.y + s * 6}" r="5"/><line x1="${it.x}" y1="${it.y + s * 1}" x2="${it.x}" y2="${ty}"/><text class="rf" x="${it.x + 9}" y="${it.up ? it.y + 14 : it.y - 8}">${it.ref}</text>`;
        addT(it.ref, '1', it.x, ty, 0, -s, it.net);
        rec.val = '测试点'; rec.pins.push({ pin: '1', net: it.net }); conn(it.net, it.ref, '1', sh.id);
      } else if (it.k === 'fet') {
        const P = it.t === 'P', deg = { v: 0, h: -90, hm: -90, hu: 90 }[it.o], flip = it.o === 'hm' || it.o === 'hu';
        // 先旋转再左右翻转，hm、hu 用来对上 EDA 里源极在左还是在右
        const tf = (lx, ly) => { const [px, py] = rot(deg, lx, ly); return [flip ? -px : px, py]; };
        let b = `<line x1="-30" y1="0" x2="-12" y2="0"/><line x1="-12" y1="-12" x2="-12" y2="12" class="th"/>`;
        b += [-9, 0, 9].map(o => `<line x1="-7" y1="${o - 3.5}" x2="-7" y2="${o + 3.5}" class="th"/>`).join('');
        b += `<polyline points="-7,-9 0,-9 0,-30"/><polyline points="-7,9 0,9 0,30"/><line x1="-7" y1="0" x2="0" y2="0"/>`;
        b += P ? `<path class="fl" d="M-1,0 l-5,-3 l0,6 Z"/>` : `<path class="fl" d="M-6,0 l5,-3 l0,6 Z"/>`;
        b += `<line x1="0" y1="0" x2="0" y2="${P ? -9 : 9}"/>`;
        g = `<g transform="translate(${it.x} ${it.y})${flip ? ' scale(-1 1)' : ''} rotate(${deg})">${b}</g>`;
        const lab = (lx, ly, t) => { const [px, py] = tf(lx, ly); g += `<text class="pn" x="${it.x + px}" y="${it.y + py}" text-anchor="middle">${t}</text>`; };
        lab(6, -20, P ? 'S' : 'D'); lab(6, 26, P ? 'D' : 'S'); lab(-24, -5, 'G');
        [['G', it.g, -30, 0, -1, 0], [P ? 'S' : 'D', it.top, 0, -30, 0, -1], [P ? 'D' : 'S', it.bot, 0, 30, 0, 1]].forEach(([pin, net, lx, ly, ldx, ldy]) => {
          const [px, py] = tf(lx, ly), [ddx, ddy] = tf(ldx, ldy);
          addT(it.ref, pin, it.x + px, it.y + py, ddx, ddy, net); rec.pins.push({ pin, net }); conn(net, it.ref, pin, sh.id);
        });
        g += it.o === 'v'
          ? `<text class="rf" x="${it.x + 13}" y="${it.y - 3}">${it.ref}</text><text class="vl" x="${it.x + 13}" y="${it.y + 9}">${esc(it.val)}</text>`
          : it.o === 'hu'
            ? `<text class="rf" x="${it.x}" y="${it.y + 24}" text-anchor="middle">${it.ref}</text><text class="vl" x="${it.x}" y="${it.y + 36}" text-anchor="middle">${esc(it.val)}</text>`
            : `<text class="rf" x="${it.x}" y="${it.y - 20}" text-anchor="middle">${it.ref}<tspan class="vl"> ${esc(it.val)}</tspan></text>`;
      } else if (it.k === 'sw3') {
        const fy = it.o === 'u' ? -1 : 1;
        g = `<g transform="translate(${it.x} ${it.y})${fy < 0 ? ' scale(1 -1)' : ''}"><line x1="0" y1="-30" x2="0" y2="-10"/><circle class="pt0" cx="0" cy="-10" r="2"/><circle class="pt0" cx="-20" cy="10" r="2"/><circle class="pt0" cx="20" cy="10" r="2"/><polyline points="-20,10 -20,30"/><polyline points="20,10 20,30"/><line x1="0" y1="-10" x2="-18" y2="7" class="th"/></g>`;
        [['2', 0, -30 * fy, 0, -fy], ['1', -20, 30 * fy, 0, fy], ['3', 20, 30 * fy, 0, fy]].forEach(([pin, lx, ly, dx, dy], i) => {
          const net = it.n[i];
          addT(it.ref, pin, it.x + lx, it.y + ly, dx, dy, net); rec.pins.push({ pin, net }); conn(net, it.ref, pin, sh.id);
          g += `<text class="pn" x="${it.x + lx + (lx < 0 ? -5 : 5)}" y="${it.y + ly + (ly < 0 ? 10 : -10)}" text-anchor="${lx < 0 ? 'end' : 'start'}">${pin}</text>`;
        });
        g += `<text class="rf" x="${it.x + 42}" y="${it.y + 2}">${it.ref}</text><text class="vl" x="${it.x + 42}" y="${it.y + 14}">${esc(it.val)}</text>`;
      } else if (it.k === 'ic') {
        const o = it.o, pitch = o.pitch || 20, rows = Math.max(it.L.length, it.R.length), x = it.x, y = it.y, w = it.w;
        const top = o.T && o.T.length ? 34 : 0, h = o.h || (o.r0 != null ? o.r0 : top + 20) + (rows - 1) * pitch + 20;
        g = `<rect class="bx" x="${x}" y="${y}" width="${w}" height="${h}" rx="2"/>`;
        const side = (list, left) => list.forEach((e, i) => {
          if (!e) return;
          const [num, name, net, dup] = e;
          const py = y + (o.r0 != null ? o.r0 : top + 20) + i * pitch, x0 = left ? x : x + w, x1 = left ? x - 22 : x + w + 22;
          g += `<line class="stub" x1="${x0}" y1="${py}" x2="${x1}" y2="${py}"${net !== NC ? ` data-net="${esc(net)}"` : ''}/>`;
          if (num && !dup) g += `<text class="pn" x="${left ? x - 4 : x + w + 4}" y="${py - 3}" text-anchor="${left ? 'end' : 'start'}">${esc(num)}</text>`;
          if (name) g += `<text class="pm" x="${left ? x + 5 : x + w - 5}" y="${py + 3.5}" text-anchor="${left ? 'start' : 'end'}">${esc(name)}</text>`;
          const key = num || name;
          addT(it.ref, key, x1, py, left ? -1 : 1, 0, net);
          if (name && name !== key && !T[it.ref + '.' + name]) T[it.ref + '.' + name] = Object.assign({ alias: it.ref + '.' + key }, T[it.ref + '.' + key]);
          if (!dup) { rec.pins.push({ pin: key, name, net }); conn(net, it.ref, key, sh.id); }
        });
        side(it.L, true); side(it.R, false);
        // 上下两边：引脚竖着出，名称在框内竖排，网络标签竖排（同 EDA 的模组画法）
        const vside = (list, up) => (list || []).forEach(([num, name, net, off]) => {
          const px = x + off, y0 = up ? y : y + h, y1 = up ? y - 22 : y + h + 22;
          g += `<line class="stub" x1="${px}" y1="${y0}" x2="${px}" y2="${y1}"${net !== NC ? ` data-net="${esc(net)}"` : ''}/>`;
          g += `<text class="pn" x="${px - 3}" y="${up ? y - 4 : y + h + 4}" text-anchor="${up ? 'start' : 'end'}" transform="rotate(-90 ${px - 3} ${up ? y - 4 : y + h + 4})">${esc(num)}</text>`;
          if (name) g += `<text class="pm" x="${px + 3.5}" y="${up ? y + 5 : y + h - 5}" text-anchor="${up ? 'end' : 'start'}" transform="rotate(-90 ${px + 3.5} ${up ? y + 5 : y + h - 5})">${esc(name)}</text>`;
          addT(it.ref, num, px, y1, 0, up ? -1 : 1, net); T[it.ref + '.' + num].vert = true;
          rec.pins.push({ pin: num, name, net }); conn(net, it.ref, num, sh.id);
        });
        vside(o.T, true); vside(o.B, false);
        g += `<text class="rf" x="${x}" y="${y - (o.T && o.T.length ? 26 : 6)}">${it.ref}</text>` + (o.vin
          ? `<text class="vl" x="${x + 10}" y="${y + h - 10}">${esc(it.val)}</text>`
          : o.vx != null
            ? `<text class="vl" x="${o.vx}" y="${o.vy}">${esc(it.val)}</text>`
            : `<text class="vl" x="${x}" y="${y + h + (o.B && o.B.length ? 36 : 13)}">${esc(it.val)}</text>`);
      }
      partSvg.push(`<g class="pt${it.k === 'ic' ? ' ic' : ''}" data-ref="${it.ref}" tabindex="0" role="button" aria-label="${it.ref}">${g}</g>`);
    });
    if (sh.ext) return null;

    // wires
    const segs = [], wired = new Set();
    const pt = (p, net) => {
      if (Array.isArray(p)) return p;
      const t = T[p];
      if (!t) { problems.push(`${sh.title}：找不到端点 ${p}`); return [0, 0]; }
      if (t.net !== net) problems.push(`${sh.title}：${p} 是 ${t.net}，导线标的是 ${net}`);
      wired.add(t.alias || p); return [t.x, t.y];
    };
    let wireSvg = '';
    sh.items.filter(it => it.k === 'wire').forEach(it => {
      const P = it.pts.map(p => pt(p, it.net));
      const path = [P[0]];
      for (let i = 1; i < P.length; i++) {
        const [ax, ay] = path[path.length - 1], [bx, by] = P[i];
        if (ax !== bx && ay !== by) path.push([bx, ay]);
        path.push([bx, by]);
      }
      for (let i = 1; i < path.length; i++) segs.push([...path[i - 1], ...path[i]]);
      wireSvg += `<polyline class="wr" data-net="${esc(it.net)}" points="${path.map(p => p.join(',')).join(' ')}"/>`;
    });
    // labels for terminals that no wire reaches, plus explicit ports
    let labSvg = '';
    Object.entries(T).forEach(([k, t]) => { if (!t.alias && !wired.has(k)) labSvg += netSym(t.x, t.y, t.dx, t.dy, t.net, t.vert); });
    sh.items.filter(it => it.k === 'port').forEach(it => { labSvg += netSym(it.x, it.y, it.dx, it.dy, it.net, it.vert); });
    // junction dots: three or more wire ends / pass-throughs meet at a point
    const onSeg = (x, y, s) => (s[0] === s[2] && x === s[0] && y > Math.min(s[1], s[3]) && y < Math.max(s[1], s[3])) || (s[1] === s[3] && y === s[1] && x > Math.min(s[0], s[2]) && x < Math.max(s[0], s[2]));
    const termAt = (x, y) => [...wired].some(k => T[k].x === x && T[k].y === y);
    const seen = new Set(); let dots = '';
    segs.forEach(s => [[s[0], s[1]], [s[2], s[3]]].forEach(([x, y]) => {
      const key = x + ',' + y; if (seen.has(key)) return; seen.add(key);
      const n = segs.filter(q => (q[0] === x && q[1] === y) || (q[2] === x && q[3] === y)).length + segs.filter(q => onSeg(x, y, q)).length * 2 + (termAt(x, y) ? 1 : 0);
      if (n >= 3) dots += `<circle class="dot" cx="${x}" cy="${y}" r="2.6"/>`;
    }));
    return `<svg class="gen" viewBox="0 0 ${sh.w} ${sh.h}" role="group" aria-label="${esc(sh.title)}原理图">${frames.join('')}<g class="wires">${wireSvg}</g>${partSvg.join('')}${labSvg}${dots}${extra.join('')}</svg>`;
  }

  /* ---------- page wiring ---------- */
  const host = document.getElementById('schSheets');
  if (!host) return;
  const { el } = window.APP;
  sheets.forEach((sh, i) => {
    const svg = buildSheet(sh);
    const fig = el('figure', { class: 'fig sch', id: 'sch-' + sh.id });
    const surf = el('div', { class: 'fig-surface' + (sh.ext ? ' ext' : '') });
    if (sh.ext) { const src = document.getElementById(sh.ext); if (src) { surf.append(...src.childNodes); src.remove(); } } else surf.innerHTML = svg;
    fig.append(el('div', { class: 'sch-title' }, el('span', { class: 'mono', text: 'SCH ' + (i + 1) + ' / ' + sheets.length }), ' ', sh.title), surf,
      sh.notes.length ? el('ul', { class: 'plain sch-notes' }, sh.notes.map(n => el('li', { text: n }))) : null,
      el('div', { class: 'sch-info', 'aria-live': 'polite' }));
    host.append(fig);
  });

  const all = sel => document.querySelectorAll(sel);
  let pinned = null;
  function hl(net) { all('.sch [data-net]').forEach(n => n.classList.toggle('hl', !!net && n.dataset.net === net)); }
  function clearSel() { all('.sch .pt.sel').forEach(n => n.classList.remove('sel')); all('.sch-info').forEach(b => { b.innerHTML = ''; }); }
  const infoBox = node => node.closest('.sch').querySelector('.sch-info');
  const sheetOf = node => node.closest('.sch').id.slice(4);
  function jump(c, net) {
    const t = document.querySelector(`#sch-${c.sheet} .pt[data-ref="${c.ref}"]`);
    if (t) { t.scrollIntoView({ block: 'center', behavior: 'smooth' }); showPart(c.ref, t); if (net) hl(net); }
    else { const f = document.getElementById('sch-' + c.sheet); if (f) { f.scrollIntoView({ block: 'start', behavior: 'smooth' }); showPart(c.ref, f.querySelector('.fig-surface')); } }
  }
  function showNet(net, node) {
    clearSel(); pinned = net; hl(net);
    const list = nets[net] || [], here = sheetOf(node);
    infoBox(node).append(el('div', { class: 'sch-h' }, el('b', { class: 'mono', text: net }), el('span', { class: 'muted', text: ` · ${list.length} 个连接` + (POWER.has(net) ? ' · 电源' : net === 'GND' ? ' · 地' : '') })),
      el('div', { class: 'sch-chips' }, list.map(c => {
        const b = el('button', { type: 'button', class: 'sch-chip' + (c.sheet !== here ? ' far' : ''), text: c.ref + '.' + c.pin, title: c.sheet !== here ? '在另一张图上' : '' });
        b.addEventListener('click', e => { e.stopPropagation(); jump(c, net); });
        return b;
      })));
  }
  function showPart(ref, node) {
    clearSel(); hl(null); pinned = null;
    if (node.classList.contains('pt')) node.classList.add('sel');
    const p = parts[ref], it = window.APP.refMap.get(ref), box = infoBox(node);
    if (!p) return;
    box.append(el('div', { class: 'sch-h' }, el('b', { class: 'mono', text: ref }), ' ', it ? it.fn : '', el('span', { class: 'muted', text: it ? ' · ' + it.part + ' · ' + it.pkg : '' })),
      it && it.note ? el('p', { class: 'muted', style: 'margin:4px 0 6px;font-size:.85rem', text: it.note }) : null,
      el('div', { class: 'sch-chips' }, p.pins.map(q => {
        const b = el('button', { type: 'button', class: 'sch-chip', text: q.pin + (q.name && q.name !== q.pin ? ' ' + q.name : '') + ' → ' + (q.net === NC ? '不接' : q.net) });
        if (q.net !== NC) b.addEventListener('click', e => { e.stopPropagation(); showNet(q.net, node); });
        return b;
      })),
      el('a', { href: (p.sheet === 'epd' ? 'display.html#schematic' : 'bom.html#ref-' + ref), style: 'font-size:.85rem', text: p.sheet === 'epd' ? '在墨水屏页查看取值说明 →' : '在器件清单中查看 →' }));
  }
  host.addEventListener('mouseover', e => { const n = e.target.closest('[data-net]'); if (!pinned) hl(n ? n.dataset.net : null); });
  host.addEventListener('mouseout', e => { if (!pinned && !(e.relatedTarget && e.relatedTarget.closest && e.relatedTarget.closest('[data-net]'))) hl(null); });
  host.addEventListener('click', e => {
    const n = e.target.closest('.nl, .stub, .wr');
    if (n && n.dataset.net) { showNet(n.dataset.net, n); return; }
    const p = e.target.closest('.pt');
    if (p) { showPart(p.dataset.ref, p); return; }
    if (e.target.closest('svg')) { clearSel(); pinned = null; hl(null); }
  });
  host.addEventListener('keydown', e => { if ((e.key === 'Enter' || e.key === ' ') && e.target.matches('.pt')) { e.preventDefault(); showPart(e.target.dataset.ref, e.target); } });

  /* ---------- connectivity check + net list ---------- */
  const skip = new Set(['MH1', 'MH2', 'MH3', 'MH4', 'ST1', 'ST2', 'ST3', 'KC1', 'KC2', 'KC3']);
  const bomRefs = new Set(D.bom.flatMap(b => b.refs).filter(r => !skip.has(r)));
  const schRefs = new Set(Object.keys(parts));
  const single = Object.entries(nets).filter(([, l]) => l.length < 2).map(([n]) => n);
  const missing = [...bomRefs].filter(r => !schRefs.has(r));
  const extra = [...schRefs].filter(r => !bomRefs.has(r));
  const pinMismatch = D.pins.filter(p => p.net && p.net !== '—' && !(nets[p.net] || []).some(c => c.ref === 'U2' && c.pin === String(p.pin)));
  const chk = document.getElementById('schCheck');
  if (chk) {
    const ok = !single.length && !missing.length && !extra.length && !pinMismatch.length && !problems.length;
    const row = (label, items) => el('tr', null, el('td', { text: label }), el('td', { class: 'r mono', text: String(items.length) }),
      el('td', null, items.length ? items.join('、') : el('span', { class: 'tag calc', text: '无' })));
    chk.append(
      el('div', { class: 'verdict ' + (ok ? 'ok' : 'warn') }, ok ? `${schRefs.size} 个器件、${Object.keys(nets).length} 个网络，全部连通，导线两端的网络一致，和器件清单、引脚表一致。` : '有需要处理的项，见下表。'),
      el('div', { class: 'tbl-wrap' }, el('table', null,
        el('thead', null, el('tr', null, el('th', { text: '检查项' }), el('th', { class: 'r', text: '数量' }), el('th', { text: '明细' }))),
        el('tbody', null,
          row('只有一端连接的网络', single), row('清单里有、图上没画的器件', missing),
          row('图上有、清单里没有的器件', extra), row('与引脚表不一致的模组引脚', pinMismatch.map(p => p.pin + ' ' + p.name)),
          row('导线接错网络或找不到端点', problems)))));
  }
  const pt = document.getElementById('pwrTable');
  if (pt) {
    const PWR = {
      VBUS: ['4.75～5.25V', 'Type-C USB0 输入，经 D3、D1 保护；给充电芯片 U1 和充电灯 LED1，经 D2 给 VSYS_IN，直接驱动 Q1 栅极（R21 下拉），经 R28/R29 做 USB 检测'],
      VBAT: ['3.0～4.2V', '电池 U3（经自带保护板）；接充电芯片 U1 的 BAT 端、Q1 的 D 极，经 R18/R17 给电量计 U10，直接给时钟 U6（关机也走时）'],
      VSYS_IN: ['插 USB ≈4.6V，电池 3.0～4.2V', '负载共享汇合点：插 USB 时经 D2，用电池时经 Q1；经负载开关 U14 到 VSYS（U14 的 ON 由电源键 SW3 和 ESP32 的 PWR_HOLD 控制），也接 SW3、C38、C39'],
      VSYS: ['同 VSYS_IN，关机时断开', '系统主电源，U14 之后；给 LDO U4 和功放 U9，大电流不经过 LDO'],
      '3V3': ['3.3V（低电量时跟随下降）', 'LDO U4 输出；给主控 U2、墨水屏、传感器、上拉电阻和 TF 卡开关 Q2'],
      SD_VCC: ['3.3V，可关断', 'Q2 之后的 TF 卡电源，GPIO10 拉低时接通']
    };
    const sheetNo = id => sheets.findIndex(s => s.id === id) + 1;
    Object.entries(PWR).forEach(([n, [v, use]]) => {
      const list = nets[n] || [];
      const where = [...new Set(list.map(c => c.sheet))].sort((a, b) => sheetNo(a) - sheetNo(b)).map(id => 'SCH ' + sheetNo(id) + ' ' + sheets[sheetNo(id) - 1].title).join('、');
      const tr = el('tr', null, el('td', { class: 'm', text: n }), el('td', { text: v }), el('td', { text: use }), el('td', { text: where || '—' }), el('td', { class: 'r', text: String(list.length) }));
      tr.addEventListener('click', () => { const t = document.querySelector(`.sch [data-net="${CSS.escape(n)}"]`); if (t) { t.scrollIntoView({ block: 'center', behavior: 'smooth' }); showNet(n, t); } });
      pt.append(tr);
    });
  }
  const nt = document.getElementById('netTable');
  if (nt) {
    const order = n => n === 'GND' ? 0 : POWER.has(n) ? 1 : 2;
    Object.keys(nets).sort((a, b) => order(a) - order(b) || a.localeCompare(b)).forEach(n => {
      const tr = el('tr', null, el('td', { class: 'm', text: n }), el('td', { class: 'r', text: String(nets[n].length) }), el('td', { text: nets[n].map(c => c.ref + '.' + c.pin).join('  ') }));
      tr.addEventListener('click', () => { const t = document.querySelector(`.sch [data-net="${CSS.escape(n)}"]`); if (t) { t.scrollIntoView({ block: 'center', behavior: 'smooth' }); showNet(n, t); } else jump(nets[n][0], n); });
      nt.append(tr);
    });
  }
  window.SCH = { sheets, parts, nets, problems };
})();
