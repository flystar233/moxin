// 墨水屏阅读器外壳 v1（亚克力面板版），对应 PCB_PCB1_1_2026-11-06
// 三件 + 键帽：亚克力面板（激光切割，DXF 另出）、中框（3D 打印）、后盖（3D 打印）、3 个键帽
// 两版：VARIANT = "fdm"（自己 FDM 0.4 喷嘴 + PLA）/ "master"（手板：光固化或 CNC，之后硅胶复模）
//       直接打开 eink-case-fdm.scad 或 eink-case-master.scad 就是对应那一版，改尺寸只改这个文件
//
// 坐标：x、y 用设计文档的背面视图坐标（元件面朝上，原点在板框外接矩形左上角，y 向下），
//       模型里 y 取负号，所以从 +z 往下看就是背面视图；z = 离 PCB 底面的高度（向元件面为正）。
// 导出：默认 part = "print"，F6 后 F7 导出的 STL 就是打印用的：中框、后盖、3 个键帽按打印方向平放、互不接触
//       单独导出：openscad -D 'part="frame"' -o frame.stl eink-case.scad（part 可选 print / keytest / frame / back / keys / acrylic / assembly）
//       keytest 是试打件：中框底边按键那一段 + 3 个键帽，先试松紧
//       assembly 是装配图（含亚克力、屏、电池、PCB 占位块），只用来看，不能打印

part = "print";
explode = 0;           // 装配图爆炸距离（mm）

/* ---------- 厚度叠层（从屏幕正面往后） ---------- */
ACR   = 1.0;   // 亚克力面板（11-13 由 1.5 改 1.0：整机薄 0.5，中框、后盖不受影响）
TAPE  = 0.15;  // 面板和屏幕、面板和中框之间的双面胶
SCR   = 0.926; // 墨水屏（11-08 换 GoodDisplay GDEY0426T82：TFT 0.5 + FPL 0.17 + PS 0.256，不含保护膜；原来按 0.85）
FOAM  = 0.6;   // 屏背面到 PCB 的泡棉
PCB   = 1.2;
ZONE  = 4.0;   // 元件区限高
BACK  = 1.6;   // 后盖（11-06 由 1.2 加到 1.6：第一套打出来强度不够，平板刚度约翻倍）

H_RIM  = -(FOAM + SCR);           // 中框前沿 = 屏幕正面 = -1.526（屏 0.85 时是 -1.45）
H_ACR0 = H_RIM - TAPE;            // 亚克力背面
H_FRONT = H_ACR0 - ACR;           // 整机正面 = -2.676（亚克力 1.5 时 -3.176）
H_BIN  = PCB + ZONE;              // 后盖内表面 = 5.2
H_BOUT = H_BIN + BACK;            // 后盖外表面 = 6.8
// 整机厚度 = H_BOUT - H_FRONT = 9.476（亚克力 1.5 时 9.976）；外形 118 × 67（11-12 屏错开 3mm 前是 115）

/* ---------- 外形 ---------- */
// 11-06 侧壁 1.5 → 2.0：两条长边离屏幕玻璃只有 0.3，只能往外加，外形高 66 → 67（Y0 −2.5、Y1 64.5）；
// 两条短边离玻璃 3.3，往里加，宽仍 115。Type-C 接口前端（x 108.02）正好落在开孔里，不用另挖
// 11-12 屏幕相对 PCB 往远离排线那头（−x）错开 SCR_DX：GDEY0426T82 的 FPC 多出来的一截在排线头那点空当里折不下。
// 玻璃、透光区、远端两颗螺丝柱和远端侧壁跟着挪；PCB、按键、Type-C 等不动，外形 115 → 118。
SCR_DX = -3.0;
X0 = -5 + SCR_DX; Y0 = -2.5; X1 = 110; Y1 = 64.5; R = 4; WALL = 2.0;
GLASS = [-0.165 + SCR_DX, 105.165 + SCR_DX, -0.185, 62.185];   // 屏幕玻璃外框
WIN   = [2.071 + SCR_DX, 95.871 + SCR_DX, 2.661, 59.341];      // 显示区 AA 四周各放 0.5（印刷透光区 / 开窗）
BOSS  = [[-2.7 + SCR_DX, 2], [107.7, 2], [107.7, 60], [-2.7 + SCR_DX, 60]];  // [1] 在天线旁，用尼龙螺丝
VARIANT = "fdm";              // 入口文件会覆盖它
FDM = VARIANT == "fdm";
BOSS_D = 4.5; PILOT_D = FDM ? 1.7 : 1.6; PILOT_L = 5.5;   // ST2.0 自攻底孔；FDM 打出来的孔偏小 0.1 左右
MH = [[72.38, 25.91], [34.28, 52.55]];       // PCB 定位孔 Ø2.2（按 11-06 钻孔，和 11-02 相同）
// 屏 FPC 绕玻璃和 PCB 右端板边弯折的空当（右头粘接台在这一段断开）。
// GDEY0426T82：FPC 弯折区宽 23.5、离玻璃（正面视图）左边 17.84 → 背面坐标 y 17.7～41.2；
// 加位置 ±0.3、宽度 +0.2 公差，两边再各留约 0.4。原来按 12.5 尾巴居中留的是 18～44。
FPC_Y0 = 14.8; FPC_Y1 = 42.2;   // 11-12 用户试装：排线槽往天线那边再开 2（原 16.8）

/* ---------- 器件位置（坐标文件 Mid X + 8.5、−Mid Y；按键用定位孔中心） ---------- */
SW6 = 23.87; SW1 = 35.91; SW2 = 43.91; SW3 = 55.48;  // 菜单 / 上一页 / 下一页 / 电源
ROCK = (SW1 + SW2) / 2;
KH = 2.15;                       // 键帽中心高度 = PCB 1.2 + 推杆中心 0.95
TF_X = 68.571;                   // CARD1
USB_Y = 45.339; USB_H = 2.85;    // USB0 中心（板上 1.2 + 3.26/2）
// Type-C 开孔上半圈：中框在这里开 U 形口通到后盖，由后盖伸下来的舌头补上（原来开孔顶上只剩 0.4，FDM 是悬空薄条、复模易撕）
TC_HW = 9.7 / 2 + 1.0;           // 舌头半宽：开孔两边各多 1.0
TC_Z0 = USB_H;                   // 分模高度 = 开孔中心，下半圈在中框、上半圈在舌头
TC_G  = FDM ? 0.15 : 0.1;        // 舌头和 U 形口的间隙
LED_Y = 51.689;                  // LED1 侧发光（PCB 上的位置）
LED_WIN_Y = 54.0;                // 充电灯窗口：从 LED 正前方往远离 Type-C 挪 2.3。USB-C 插头胶壳最宽 12.35，会盖到 y 51.5，
                                 // 原来正对 LED 的窗口（50.8～52.6）插着线时会被挡；挪开后离胶壳边 1.7，LED 侧发光约 120°，偏 23° 仍够亮
SPK_C = 96.9; SPK_N = 3;         // 出声槽（底边）：中心对准喇叭 LS1（16×9，x 89～105），3 条，间距 1.8
SENS_X = 12.95;                  // U7 STCC4、U11 SHT40

CLR = FDM ? 0.3 : 0.2;    // 键帽和开孔每边间隙（FDM 0.3；手板复模 0.2，PU 复模件有收缩和披锋；以后开注塑模可到 0.15）
$fn = 48;

/* ---------- 工具 ---------- */
// 文档坐标下的圆角矩形（y 向下），输出到模型的 2D（y 取负）
module rr(x0, y0, x1, y1, r) {
  translate([x0 + r, -y1 + r]) offset(r = r) square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]);
}
module rect(x0, y0, x1, y1) { translate([x0, -y1]) square([x1 - x0, y1 - y0]); }
module slab(z0, z1) { translate([0, 0, z0]) linear_extrude(z1 - z0) children(); }
// 底边侧壁上开孔：2D 形状画在（沿壁 x, 高度 z）平面里，原点放在孔中心
module thru_bottom(cx, cz, y_in = 62.2, depth = 3) {
  translate([cx, -y_in, cz]) rotate([90, 0, 0]) linear_extrude(depth) children();
}
// 右侧短边开孔：2D 形状画在（沿壁 −y, 高度 z）平面里
module thru_right(cy, cz) {
  translate([X1 - WALL - 0.3, -cy, cz]) rotate([90, 0, 90]) linear_extrude(WALL + 1) children();
}
module rrect2(w, h, r) { offset(r = r) square([w - 2 * r, h - 2 * r], center = true); }
module usb_hole() { thru_right(USB_Y, USB_H) rrect2(9.7, 3.9, 1.6); }   // 中框、后盖舌头共用，开孔轮廓连续

/* ---------- 键帽轮廓 ---------- */
ROCK_W = 16; ROCK_H = 3.2; ROCK_R = 1.2;
RND_D = 3.6;
module prof_rock(c = 0) { rrect2(ROCK_W + 2 * c, ROCK_H + 2 * c, ROCK_R + c); }
module prof_round(c = 0) { circle(d = RND_D + 2 * c); }

/* ================= 中框 ================= */
module frame() {
  difference() {
    union() {
      // 四周侧壁
      slab(H_RIM, H_BIN) difference() {
        rr(X0, Y0, X1, Y1, R);
        rr(X0 + WALL, Y0 + WALL, X1 - WALL, Y1 - WALL, R - WALL);
      }
      // 螺丝柱
      for (b = BOSS) translate([b[0], -b[1], H_RIM]) cylinder(d = BOSS_D, h = H_BIN - H_RIM);
      // 两头的粘接台（和屏幕玻璃同一平面，给面板多一点粘胶面积）；右头中段留给屏 FPC 弯折
      slab(H_RIM, H_RIM + 0.6) {
        rect(X0 + WALL - 0.01, Y0 + WALL, GLASS[0] - 0.3, Y1 - WALL);
        rect(GLASS[1] + 0.3, Y0 + WALL, X1 - WALL + 0.01, FPC_Y0);
        rect(GLASS[1] + 0.3, FPC_Y1, X1 - WALL + 0.01, Y1 - WALL);
      }
    }
    // 螺丝底孔：ST2.0 自攻（天线旁那颗攻 M2 牙配尼龙螺丝）
    for (b = BOSS) translate([b[0], -b[1], H_BIN - PILOT_L]) cylinder(d = PILOT_D, h = PILOT_L + 0.1);

    // ---- 底边：菜单键、翻页翘板、电源键、TF 卡、出声槽、传感器侧通风 ----
    thru_bottom(SW6, KH) prof_round(CLR);
    thru_bottom(ROCK, KH) prof_rock(CLR);
    thru_bottom(SW3, KH) prof_round(CLR);
    thru_bottom(TF_X, 2.3) rrect2(12.0, 2.4, 0.3);
    // TF 卡指甲槽：靠屏幕面一侧半椭圆 宽 6 × 高 1.6，从壳面挖进 1.0
    translate([TF_X, -(Y1 - 1.0), 1.3]) rotate([90, 0, 0]) linear_extrude(1.5)
      intersection() { scale([3, 1.6]) circle(1); translate([-3, -1.6]) square([6, 1.6]); }
    for (i = [0 : SPK_N - 1]) thru_bottom(SPK_C + (i - (SPK_N - 1) / 2) * 1.8, 3.25) rrect2(0.9, 2.7, 0.44);
    for (i = [-1 : 1]) thru_bottom(SENS_X + i * 1.5, 3.0) rrect2(0.8, 2.4, 0.39);

    // ---- 右侧短边：Type-C、充电灯窗 ----
    usb_hole();
    slab(TC_Z0, H_BIN + 0.1) rect(X1 - WALL - 0.3, USB_Y - TC_HW - TC_G, X1 + 0.5, USB_Y + TC_HW + TC_G);   // U 形口
    thru_right(LED_WIN_Y, 1.7) circle(d = 1.5);   // 圆孔 Ø1.5（11-06 用户定；之前 Ø1.8，最早是 1.8 × 1.2 方孔）
  }
}

/* ================= 后盖 ================= */
module back() {
  difference() {
    union() {
      slab(H_BIN, H_BOUT) rr(X0, Y0, X1, Y1, R);
      // PCB 定位压柱：台阶 Ø3.4 压住 PCB 顶面（压进泡棉 0.1），头部 Ø1.9 插进 Ø2.2 定位孔
      for (m = MH) translate([m[0], -m[1], 0]) {
        translate([0, 0, PCB - 0.1]) cylinder(d = 3.4, h = H_BIN - PCB + 0.1 + 0.01);
        translate([0, 0, 0.1]) cylinder(d1 = 1.5, d2 = 1.9, h = 0.4);
        translate([0, 0, 0.5]) cylinder(d = 1.9, h = PCB - 0.6);
      }
      // Type-C 顶面压块：离接口外壳顶面 0.1，拔插时接口不会被撬起
      slab(PCB + 3.26 + 0.1, H_BIN + 0.01) rect(100.6, 41.9, 106.6, 48.8);
      // Type-C 舌头：塞进中框 U 形口，补上开孔上半圈；插头往上撬的力直接给后盖
      slab(TC_Z0 + TC_G, H_BIN + 0.01) rect(X1 - WALL, USB_Y - TC_HW, X1, USB_Y + TC_HW);
    }
    usb_hole();
    // 沉头螺丝孔：通孔 Ø2.3，沉头 Ø4.0
    for (b = BOSS) translate([b[0], -b[1], 0]) {
      translate([0, 0, H_BIN - 0.1]) cylinder(d = 2.3, h = BACK + 0.2);
      translate([0, 0, H_BOUT - 0.85]) cylinder(d1 = 2.3, d2 = 4.0, h = 0.86);
    }
    // 传感器通风槽：5 条 0.9 × 9，对着 U7、U11
    for (k = [-2 : 2]) slab(H_BIN - 0.1, H_BOUT + 0.1)
      rr(SENS_X + k * 1.5 - 0.45, 47.5, SENS_X + k * 1.5 + 0.45, 56.5, 0.44);
  }
}

/* ================= 键帽 ================= */
// 局部坐标：x 沿侧壁（相对键中心），y = 文档 y（往壳外为正），z 相对键中心高度
// 推杆顶端在 y 62.7（本体前沿 61.7 + 伸出 1.0），键帽内表面放在 62.8，空 0.1
// 法兰片只做在开关本体顶面（离 PCB 底面 2.85）以上，按下 0.4 也碰不到开关；翘板两端的法兰片在开关外侧，可以往下做到 PCB 顶面以上
CAP_IN = 62.8; FL0 = 61.8; FL1 = 62.5;
module at_key(cx) { translate([cx, 0, KH]) mirror([0, 1, 0]) children(); }
module ext_y(y0, y1) { translate([0, y1, 0]) rotate([90, 0, 0]) linear_extrude(y1 - y0) children(); }
// 键帽分三层沿 y 拉伸：A 法兰层（y 61.8～62.5）、B 连接层（62.4～62.9）、C 键身（62.8～壳外）。
// 每层的截面先在 2D 里合并好再一次拉伸；B 比轮廓缩 0.05、和 A、C 各重叠 0.1，层间没有共面、共边的面，导出的 STL 是封闭实体。
// A、B 合并后再用 45° 斜面切底：法兰片下面是斜的，键帽侧躺（z 朝上）打印时不用支撑；按下 0.4 时斜面也碰不到开关本体。
// 上沿法兰比开孔两端各宽 0.8，翘板以另一端的法兰为支点转，不再做两端往下伸的法兰片（FDM 打不出悬空的那段）。
CAP_FLAT = FDM ? -1.6 : -1.8;   // 键帽底面（相对键中心）：翘板本来就是平的；FDM 版圆键削平 0.2，侧躺打印时贴得住平台；手板版不削
module cap(kind) {
  W = kind == "rock" ? ROCK_W : RND_D;
  proud = kind == "rock" ? 0.6 : kind == "menu" ? 0.5 : 0.0;
  out = Y1 + proud;
  band0 = 3.0 - KH;          // 开关本体顶面 2.85 再留 0.15
  // 法兰从键身宽度开始、两端 45° 放宽到比开孔每边多 0.8（侧躺打印时两端不悬空）
  hw0 = kind == "rock" ? W / 2 - 0.1 : sqrt(pow(RND_D / 2, 2) - pow(band0, 2));
  hw1 = W / 2 + 0.8;
  rw = FDM ? 0.8 : 0.6; rh = FDM ? 0.4 : 0.3;        // 翘板凸棱宽、高（手板版 0.6 × 0.3）
  dish = FDM ? 0.3 : 0.2; rs = (1.5 * 1.5 + dish * dish) / (2 * dish);   // 菜单键凹面深度、球半径
  module prof() { intersection() { if (kind == "rock") prof_rock(); else prof_round(); translate([-10, CAP_FLAT]) square([20, 10]); } }
  module band() { intersection() { prof(); translate([-10, band0]) square([20, 10]); } }
  difference() {
    union() {
      // C 键身
      ext_y(CAP_IN, out) prof();
      // A + B，再切 45° 底：留下 v + y ≥ band0 + CAP_IN 的部分
      intersection() {
        union() {
          ext_y(FL1 - 0.1, CAP_IN + 0.1) offset(delta = -0.05) band();
          ext_y(FL0, FL1) union() {
            band();
            polygon([[-hw0, band0], [hw0, band0], [hw1, band0 + hw1 - hw0], [hw1, 2.85],
                     [-hw1, 2.85], [-hw1, band0 + hw1 - hw0]]);
          }
        }
        translate([0, CAP_IN, band0]) rotate([-45, 0, 0]) translate([-15, -15, 0]) cube([30, 30, 10]);
      }
      // 翘板正中横向凸棱（盲按找中点）：FDM 做 0.8 宽（两道线）、0.4 高
      if (kind == "rock") ext_y(out - 0.1, out + rh) translate([-rw / 2, -1.1]) square([rw, 2.2]);
      // 电源键中心凸点：FDM Ø1.0 高 0.3，手板 Ø0.8 高 0.2
      if (kind == "power") translate([0, out - 0.1, 0]) rotate([-90, 0, 0])
        cylinder(d = FDM ? 1.0 : 0.8, h = (FDM ? 0.3 : 0.2) + 0.1, $fn = 24);
    }
    // 菜单键表面微凹（FDM 0.3，手板 0.2）
    if (kind == "menu") translate([0, out + rs - dish, 0]) sphere(r = rs, $fn = 64);
  }
}
module keys() {
  at_key(SW6) cap("menu");
  at_key(ROCK) cap("rock");
  at_key(SW3) cap("power");
}

/* ================= 亚克力面板（DXF 另出，这里只用于装配图） ================= */
module acrylic() {
  color([0.85, 0.92, 1, 0.45]) slab(H_FRONT, H_ACR0) rr(X0, Y0, X1, Y1, R);
  // 背印白色边框（透光区 = WIN）
  color([0.98, 0.98, 0.97]) slab(H_ACR0 - 0.02, H_ACR0) difference() { rr(X0, Y0, X1, Y1, R); rect(WIN[0], WIN[2], WIN[1], WIN[3]); }
}

/* ---------- 装配图里的参考件（不导出） ---------- */
PCB_PTS = [[62.98, 0], [105.02, 0], [105.02, 39.12], [107.02, 39.12], [107.02, 50.54], [105.02, 50.54],
           [105.02, 62.1], [8.5, 62.1], [8.5, 43.05], [62.98, 43.05]];
module refs() {
  color([0.92, 0.92, 0.88]) slab(H_RIM, H_RIM + SCR) rect(GLASS[0], GLASS[2], GLASS[1], GLASS[3]);
  color([0.11, 0.42, 0.27]) slab(0, PCB) polygon([for (p = PCB_PTS) [p[0], -p[1]]]);
  color([0.6, 0.6, 0.65]) slab(-0.6 + 0.5, -0.6 + 0.5 + 4.5) rect(1, 0.5, 62, 42.5);        // 454261 电池
  color([0.3, 0.3, 0.3]) slab(PCB, PCB + 3.26) rect(99.97, 40.87, 107.27 + 0.75, 49.81);   // USB0
  for (s = [SW6, SW1, SW2, SW3]) color([0.75, 0.75, 0.78]) slab(PCB, PCB + 1.65) rect(s - 2.35, 58.2, s + 2.35, 61.7);
}

/* ---------- 打印排版：所有零件底面在 z = 0，互相隔开 ≥ 8mm ---------- */
module print_plate() {
  // 中框：粘亚克力的前沿朝下，保证粘接面平
  translate([0, 0, -H_RIM]) frame();
  // 后盖：外表面朝下，压柱和压块朝上
  translate([0, -140, H_BOUT]) rotate([180, 0, 0]) back();
  if (FDM) {
    // 键帽侧躺：底边（离 PCB 最近的那条边）贴平台，按键面朝 −y 竖着；法兰下面是 45° 斜面，不用支撑。切片时加 3mm brim
    translate([0, -230, -CAP_FLAT]) { translate([SW6, 0, 0]) cap("menu"); translate([ROCK, 0, 0]) cap("rock"); translate([SW3, 0, 0]) cap("power"); }
  } else {
    // 手板版：法兰片那面（最里面，y = 61.8）朝下，外表面朝上，凸棱、凸点、凹面不贴平台（手板厂会自己摆方向）
    translate([0, -160, -FL0]) rotate([-90, 0, 0]) keys();
  }
}

/* ---------- 试打件：中框底边按键那一段 + 3 个键帽，先试键帽松紧再打整个中框 ---------- */
module keytest() {
  translate([0, 0, -H_RIM]) intersection() {
    frame();
    translate([18, -(Y1 + 0.5), H_RIM - 1]) cube([44, WALL + 5, 10]);
  }
  translate([0, -133, -CAP_FLAT]) { translate([SW6, 0, 0]) cap("menu"); translate([ROCK, 0, 0]) cap("rock"); translate([SW3, 0, 0]) cap("power"); }
}

// 控制台里会打印这一行，导出前看一眼是不是想要的版本和零件
echo(str(">>> 导出：VARIANT = ", VARIANT, "（", FDM ? "FDM 版" : "手板版", "），part = ", part));

if (part == "print") print_plate();
else if (part == "keytest") keytest();
else if (part == "frame") frame();
else if (part == "back") back();
else if (part == "keys") keys();
else if (part == "acrylic") slab(H_FRONT, H_ACR0) rr(X0, Y0, X1, Y1, R);
else {
  translate([0, 0, -2 * explode]) acrylic();
  refs();
  color([0.93, 0.93, 0.9]) frame();
  color([0.78, 0.76, 0.7]) translate([0, explode, 0]) keys();
  color([0.93, 0.93, 0.9, 0.85]) translate([0, 0, 2 * explode]) back();
}
