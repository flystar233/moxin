/* PCB 3D 视图：three.js r128 渲染，自写轨道控制（拖动旋转、滚轮/双指缩放、双击复位）。
   坐标沿用布局图的背面视图：原点在左上角，x 向右、y 向下，单位 mm。两套布局之间做补间动画。 */
(function () {
  'use strict';
  const stage = document.getElementById('stage');
  if (!stage) return;
  const msgEl = stage.querySelector('.v-msg');
  const fail = t => { stage.classList.add('v-fail'); msgEl.textContent = t; };
  if (!window.BOARD) { fail('缺少布局数据 board-data.js。'); return; }
  if (!window.THREE) { fail('3D 视图需要联网加载 three.js。下面的平面图仍然可用。'); return; }
  const T = window.THREE;
  let renderer;
  try { renderer = new T.WebGLRenderer({ antialias: true, alpha: true }); } catch (e) { renderer = null; }
  if (!renderer || !renderer.getContext()) { fail('这个浏览器没有开启 WebGL，下面的平面图仍然可用。'); return; }

  const D = window.DOC, APP = window.APP;
  const ZONES = window.LAYOUT_ZONES || {};
  const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const W = 105, H = 62, BT = 1.2;
  const X = x => x - W / 2, Z = y => y - H / 2;
  const col = h => new T.Color(h).convertSRGBToLinear();
  const std = (c, o) => new T.MeshStandardMaterial(Object.assign({ color: col(c), roughness: .55, metalness: .05 }, o || {}));
  const M = {
    mask: std(0x1d5a3c, { roughness: .5 }), fr4: std(0xcbb98c, { roughness: .85 }),
    ic: std(0x1b1c1e, { roughness: .42, metalness: .1 }), metal: std(0xc9ccd1, { roughness: .28, metalness: .9 }),
    gold: std(0xd8ab4c, { roughness: .3, metalness: .9 }), goldSug: std(0xd8ab4c, { roughness: .3, metalness: .6, transparent: true, opacity: .45 }),
    cer: std(0xc7a26a, { roughness: .6 }), resb: std(0x232325, { roughness: .5 }), white: std(0xebe4d0, { roughness: .7 }),
    dark: std(0x0d0d0e, { roughness: .8 }), ind: std(0x3a3c40, { roughness: .6, metalness: .2 }), tant: std(0xd6902a, { roughness: .45 }),
    led: std(0x9ee6a8, { roughness: .2, emissive: col(0x2a8a40), emissiveIntensity: .7 }), modpcb: std(0x24543c, { roughness: .6 }),
    amber: std(0xc98a2e, { roughness: .45, side: T.DoubleSide, transparent: true, opacity: .92 }),
    edge: std(0x8c9196, { roughness: .2, metalness: .1 }), back: std(0x3a3d40, { roughness: .6 }),
    pouch: std(0xaeb5bd, { roughness: .4, metalness: .5 }), band: std(0x9a9ca0, { roughness: .4, metalness: .6 })
  };

  const { parts, HOLES, NOTCH, keepout, zoneRects, SLOTS } = window.BOARD;

  /* ---------- scene ---------- */
  const scene = new T.Scene();
  const camera = new T.PerspectiveCamera(32, 1, 1, 3000);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.outputEncoding = T.sRGBEncoding;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = T.PCFSoftShadowMap;
  stage.prepend(renderer.domElement);
  renderer.domElement.setAttribute('aria-label', 'PCB 3D 视图，拖动旋转，滚轮缩放');
  renderer.domElement.setAttribute('role', 'img');

  const hemi = new T.HemisphereLight(0xffffff, 0x3a4038, .75);
  scene.add(hemi);
  const sun = new T.DirectionalLight(0xffffff, .95);
  sun.position.set(-60, 140, 90);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  Object.assign(sun.shadow.camera, { left: -80, right: 80, top: 60, bottom: -60, near: 10, far: 400 });
  sun.shadow.bias = -0.0004;
  scene.add(sun);
  const fill = new T.DirectionalLight(0xdfe8ff, .35);
  fill.position.set(80, -60, -70);
  scene.add(fill);

  const box = (w, h, d, mat) => { const m = new T.Mesh(new T.BoxGeometry(w, h, d), mat); m.castShadow = true; m.receiveShadow = true; return m; };

  /* board: one mesh per layout (the v0.7 board has the battery window) */
  function rrect(path, x, y, w, h, r, hole) {
    // x, y, w, h in board mm (top-left origin); converted to shape coords (y up)
    const L = X(x), R = X(x + w), Tp = -Z(y), Bt = -Z(y + h);
    if (!hole) { path.moveTo(L + r, Bt); path.lineTo(R - r, Bt); path.quadraticCurveTo(R, Bt, R, Bt + r); path.lineTo(R, Tp - r); path.quadraticCurveTo(R, Tp, R - r, Tp); path.lineTo(L + r, Tp); path.quadraticCurveTo(L, Tp, L, Tp - r); path.lineTo(L, Bt + r); path.quadraticCurveTo(L, Bt, L + r, Bt); }
    else { path.moveTo(L + r, Bt); path.quadraticCurveTo(L, Bt, L, Bt + r); path.lineTo(L, Tp - r); path.quadraticCurveTo(L, Tp, L + r, Tp); path.lineTo(R - r, Tp); path.quadraticCurveTo(R, Tp, R, Tp - r); path.lineTo(R, Bt + r); path.quadraticCurveTo(R, Bt, R - r, Bt); path.lineTo(L + r, Bt); }
  }
  function roundPoly(path, pts, rs) {
    // pts in board mm; rs = corner radius per vertex
    const P = pts.map(([x, y]) => [X(x), -Z(y)]);
    const n = P.length;
    const at = (i, t) => { const a = P[i], b = P[(i + 1) % n]; const len = Math.hypot(b[0] - a[0], b[1] - a[1]); return [a[0] + (b[0] - a[0]) * t / len, a[1] + (b[1] - a[1]) * t / len]; };
    const lenOf = i => { const a = P[i], b = P[(i + 1) % n]; return Math.hypot(b[0] - a[0], b[1] - a[1]); };
    for (let i = 0; i < n; i++) {
      const prev = (i - 1 + n) % n, r = rs[i];
      const inPt = at(prev, lenOf(prev) - r), outPt = at(i, r);
      if (i === 0) path.moveTo(inPt[0], inPt[1]); else path.lineTo(inPt[0], inPt[1]);
      path.quadraticCurveTo(P[i][0], P[i][1], outPt[0], outPt[1]);
    }
  }
  function makeBoard(L) {
    const g = new T.Group();
    const shape = new T.Shape();
    if (L === 'B') roundPoly(shape, window.BOARD.OUTLINE.pts, window.BOARD.OUTLINE.r);
    else rrect(shape, 0, 0, W, H, 2, false);
    HOLES[L].forEach(([hx, hy]) => { const p = new T.Path(); p.absarc(X(hx), -Z(hy), 1.1, 0, Math.PI * 2, true); shape.holes.push(p); });
    const slot = new T.Path();
    const slotPts = SLOTS[L];
    slotPts.forEach(([sx, sy], i) => i ? slot.lineTo(X(sx), -Z(sy)) : slot.moveTo(X(sx), -Z(sy)));
    shape.holes.push(slot);
    const geo = new T.ExtrudeGeometry(shape, { depth: BT, bevelEnabled: false, curveSegments: 18 });
    geo.rotateX(-Math.PI / 2);
    const m = new T.Mesh(geo, [M.mask, M.fr4]);
    m.receiveShadow = true; m.castShadow = true;
    g.add(m);
    // MH1～MH4 是非金属化定位孔（Ø2.2），没有焊盘；只在旧布局里画成螺丝孔的金色焊盘
    if (L === 'A') HOLES[L].forEach(([hx, hy]) => {
      const ring = new T.Mesh(new T.RingGeometry(1.1, 1.85, 28), M.gold);
      ring.rotation.x = -Math.PI / 2; ring.position.set(X(hx), BT + .012, Z(hy)); g.add(ring);
    });
    return g;
  }
  const boards = { A: makeBoard('A'), B: makeBoard('B') };
  const boardGroup = new T.Group();
  boardGroup.add(boards.A, boards.B);
  scene.add(boardGroup);

  /* silkscreen texture */
  const S = 16;
  const silkCanvas = document.createElement('canvas');
  silkCanvas.width = W * S; silkCanvas.height = H * S;
  const silkTex = new T.CanvasTexture(silkCanvas);
  silkTex.encoding = T.sRGBEncoding;
  silkTex.anisotropy = renderer.capabilities.getMaxAnisotropy();
  const silk = new T.Mesh(new T.PlaneGeometry(W, H), new T.MeshStandardMaterial({ map: silkTex, transparent: true, depthWrite: false, roughness: .7 }));
  silk.rotation.x = -Math.PI / 2; silk.position.y = BT + .02; silk.receiveShadow = true;
  boardGroup.add(silk);

  /* parts */
  const partsGroup = new T.Group();
  scene.add(partsGroup);
  const targets = [];
  function leads(g, w, d, a, b, pitch) {
    [[a, -1], [b, 1]].forEach(([n, s]) => {
      for (let i = 0; i < n; i++) {
        const l = box(.4, .18, .7, M.metal);
        l.position.set((i - (n - 1) / 2) * pitch * (n === 1 ? 0 : 1), .09, s * (d / 2 + .3));
        g.add(l);
      }
    });
  }
  const body = (g, w, d, h, mat, y = 0) => { const m = box(w, h, d, mat); m.position.y = y + h / 2; g.add(m); return m; };
  const build = {
    module(p, g) {
      body(g, 18, 25.5, .8, M.modpcb);
      const can = body(g, 16.6, 18, 2.3, M.metal, .8); can.position.z = 25.5 / 2 - 9 - .7;
      for (let i = 0; i < 7; i++) { const s = box(.35, .05, 3.6, M.gold); s.position.set(-6 + i * 2, .83, -25.5 / 2 + 3.4); g.add(s); }
      for (let i = 0; i < 6; i++) { const s = box(2.35, .05, .35, M.gold); s.position.set(-5 + i * 2, .83, -25.5 / 2 + (i % 2 ? 1.7 : 5.1)); g.add(s); }
    },
    so(p, g) { body(g, p.w, p.d, p.h, M.ic, .12); leads(g, p.w, p.d, 4, 4, 1.27); },
    sot(p, g) { body(g, p.w, p.d, p.h, M.ic, .1); leads(g, p.w, p.d, p.pins[0], p.pins[1], .95); },
    qfn(p, g) { body(g, p.w, p.d, p.h, M.ic); },
    cap(p, g) { body(g, p.w * .7, p.d, p.h, M.cer); [-1, 1].forEach(s => { const e = box(p.w * .15, p.h, p.d, M.metal); e.position.set(s * p.w * .425, p.h / 2, 0); g.add(e); }); },
    res(p, g) { body(g, p.w * .7, p.d, p.h, M.resb); [-1, 1].forEach(s => { const e = box(p.w * .15, p.h, p.d, M.metal); e.position.set(s * p.w * .425, p.h / 2, 0); g.add(e); }); },
    ind(p, g) { body(g, p.w, p.d, p.h, M.ind); },
    diode(p, g) { body(g, p.w, p.d, p.h, M.ic); const b = box(p.w * .14, .03, p.d * .96, M.band); b.position.set(-p.w * .33, p.h + .015, 0); g.add(b); },
    tant(p, g) { body(g, p.w, p.d, p.h, M.tant); const b = box(p.w * .16, .03, p.d * .96, M.dark); b.position.set(-p.w * .34, p.h + .015, 0); g.add(b); },
    xtal(p, g) { body(g, p.w, p.d, p.h, M.metal); },
    led(p, g) { body(g, p.w, p.d, p.h, M.led); },
    tact(p, g) { body(g, p.w, p.d, p.h * .65, M.metal); const c = new T.Mesh(new T.CylinderGeometry(.8, .8, p.h * .35, 20), M.dark); c.position.y = p.h * .65 + p.h * .175; c.castShadow = true; g.add(c); },
    // v4.36 按 EVQPUC02K 图纸：推杆宽 2.6、厚 1.4（离 PCB 0.25～1.65，和本体顶面齐平），伸出本体 1.0
    sidekey(p, g) { body(g, p.w, p.d, p.h, M.metal); const a = box(2.6, 1.4, 1.1, M.dark); a.position.set(0, .95, -p.d / 2 - .45); g.add(a); },
    usbc(p, g) { body(g, p.w, p.d, p.h, M.metal); const m = box(p.w * .8, p.h * .5, .3, M.dark); m.position.set(0, p.h / 2, p.d / 2 - .1); g.add(m); },
    jst(p, g) { body(g, p.w, p.d, p.h, M.white); const c = box(p.w * .72, p.h * .55, .4, M.dark); c.position.set(0, p.h * .5, p.d / 2 - .15); g.add(c); },
    fpc(p, g) { body(g, p.w, p.d, p.h, M.ic); const f = box(1.3, .45, p.d * .96, M.white); f.position.set(p.w / 2 - .55, p.h + .22, 0); g.add(f); },
    tf(p, g) { body(g, p.w, p.d, p.h, M.metal); const sl = box(p.w * .78, .5, .3, M.dark); sl.position.set(0, p.h * .45, p.d / 2 - .1); g.add(sl); },
    spk(p, g) {
      body(g, p.w, p.d, p.h * .82, M.ic);
      const rim = box(p.w - .6, p.h * .18, p.d - .6, M.band); rim.position.y = p.h * .82 + p.h * .09; g.add(rim);
      const cone = box(p.w - 2.4, .06, p.d - 2.4, M.dark); cone.position.y = p.h + .03; g.add(cone);
      [-1.6, 1.6].forEach((z, i) => { const w = box(4, .5, .5, i ? M.resb : std(0xb3352a)); w.position.set(-p.w / 2 - 2, .25, z * .5); g.add(w); });
    },
    pads2(p, g) { [-1.25, 1.25].forEach(x => { const m = box(2, .06, 3, M.gold); m.position.set(x, .03, 0); g.add(m); }); },
    pads(p, g) { for (let i = 0; i < 4; i++) { const c = new T.Mesh(new T.CylinderGeometry(.75, .75, .06, 20), M.gold); c.position.set(-3.75 + i * 2.5, .03, 0); g.add(c); } },
    tp(p, g) { const c = new T.Mesh(new T.CylinderGeometry(.9, .9, .06, 24), p.sug ? M.goldSug : M.gold); c.position.y = .03; g.add(c); }
  };
  parts.forEach(p => {
    const g = new T.Group();
    build[p.type](p, g);
    g.traverse(o => { if (o.isMesh) { o.userData.part = p; targets.push(o); } });
    g.position.y = BT;
    p.g = g;
    partsGroup.add(g);
  });
  function place(p, a, b, t) {
    const lerp = (u, v) => u + (v - u) * t;
    p.g.position.x = X(lerp(a[0], b[0]));
    p.g.position.z = Z(lerp(a[1], b[1]));
    p.g.rotation.y = lerp(a[2], b[2]) * Math.PI / 180;
  }
  function showOnly(L) { parts.forEach(p => { if (p.onlyB) p.g.visible = L === 'B'; }); }

  /* keepout volume */
  const koMat = new T.MeshBasicMaterial({ color: 0xd04a3a, transparent: true, opacity: .14, depthWrite: false });
  const ko = new T.Mesh(new T.BoxGeometry(1, 1, 1), koMat);
  const koEdge = new T.LineSegments(new T.EdgesGeometry(new T.BoxGeometry(1, 1, 1)), new T.LineBasicMaterial({ color: 0xd04a3a, transparent: true, opacity: .7 }));
  ko.add(koEdge);
  scene.add(ko);
  function placeKo(a, b, t) {
    const L = i => a[i] + (b[i] - a[i]) * t;
    const kx = L(0), ky = L(1), kw = L(2), kd = L(3), kh = 3.8;
    ko.scale.set(kw, kh, kd);
    ko.position.set(X(kx + kw / 2), BT + kh / 2 + .05 + (typeof explode === 'number' ? explode * 8 : 0), Z(ky + kd / 2));
  }

  /* e-paper panel with a rendered UI on its front face */
  const scrCanvas = document.createElement('canvas');
  const SS = 12; scrCanvas.width = Math.round(105.33 * SS); scrCanvas.height = Math.round(62.37 * SS);
  const scrTex = new T.CanvasTexture(scrCanvas);
  scrTex.encoding = T.sRGBEncoding;
  scrTex.anisotropy = renderer.capabilities.getMaxAnisotropy();
  function drawScreen() {
    const c = scrCanvas.getContext('2d'), w = scrCanvas.width, h = scrCanvas.height, k = SS;
    c.fillStyle = '#DCDCD6'; c.fillRect(0, 0, w, h);
    // 显示区位置按规格书：贴图左边是没有 FPC 的一头，玻璃边到 AA 2.736，长边 3.346（v4.4）
    const ax = 2.736 * k, ay = 3.346 * k, aw = 92.8 * k, ah = 55.68 * k;
    c.fillStyle = '#ECECE7'; c.fillRect(ax, ay, aw, ah);
    c.fillStyle = '#141414';
    const serif = '"Noto Serif SC", "Songti SC", serif', sans = '"Noto Sans SC", "Microsoft YaHei", sans-serif';
    // 展示出图的阅读模式（window.STUDIO.screen = 'reader'）：竖拿，Type-C 在下、按键在右；画一页书
    if (window.STUDIO && window.STUDIO.screen === 'reader') {
      c.save(); c.translate(ax, ay + ah); c.rotate(-Math.PI / 2);
      const pw = ah, ph = aw, m = 4.2 * k, fs = 2.75 * k, lh = fs * 1.78;
      c.textBaseline = 'top'; c.fillStyle = '#5a5a56';
      c.font = `400 ${1.9 * k}px ${sans}`; c.fillText('朝花夕拾 · 从百草园到三味书屋', m, 3.4 * k);
      c.textAlign = 'right'; c.fillText('▶ 朗读中', pw - m, 3.4 * k); c.textAlign = 'left';
      c.fillRect(m, 6.6 * k, pw - 2 * m, .18 * k);
      c.fillStyle = '#161616'; c.font = `600 ${fs}px ${serif}`;
      const paras = ['我家的后面有一个很大的园，相传叫作百草园。现在是早已并屋子一起卖给朱文公的子孙了，连那最末次的相见也已经隔了七八年，其中似乎确凿只有一些野草；但那时却是我的乐园。',
        '不必说碧绿的菜畦，光滑的石井栏，高大的皂荚树，紫红的桑椹；也不必说鸣蝉在树叶里长吟，肥胖的黄蜂伏在菜花上，轻捷的叫天子忽然从草间直窜向云霄里去了。单是周围的短短的泥墙根一带，就有无限趣味。油蛉在这里低唱，蟋蟀们在这里弹琴。'];
      const per = Math.floor((pw - 2 * m) / fs);
      let y = 9.6 * k;
      paras.forEach(p => {
        const chars = ['　', '　', ...p];
        // 逐行排，标点不放行首（放不下就挤进上一行末尾）
        for (let i = 0; i < chars.length && y < ph - 9 * k;) { let n = per; if ('，。；：、！？）'.includes(chars[i + n] || '')) n++; c.fillText(chars.slice(i, i + n).join(''), m, y); i += n; y += lh; }
        y += lh * .35;
      });
      c.fillStyle = '#5a5a56'; c.font = `400 ${1.9 * k}px ${sans}`;
      c.fillRect(m, ph - 6.4 * k, (pw - 2 * m) * .23, .5 * k); c.fillStyle = '#bdbdb7'; c.fillRect(m + (pw - 2 * m) * .23, ph - 6.2 * k, (pw - 2 * m) * .77, .14 * k);
      c.fillStyle = '#5a5a56'; c.fillText('第 3 页 / 共 12 页', m, ph - 4.6 * k); c.textAlign = 'right'; c.fillText('23%', pw - m, ph - 4.6 * k); c.textAlign = 'left';
      c.restore(); scrTex.needsUpdate = true; return;
    }
    const now = new Date(), two = n => String(n).padStart(2, '0');
    c.font = `900 ${15 * k}px ${serif}`; c.textBaseline = 'top';
    c.fillText(two(now.getHours()) + ':' + two(now.getMinutes()), ax + 5 * k, ay + 4 * k);
    c.font = `500 ${3 * k}px ${sans}`;
    c.fillText((now.getMonth() + 1) + '月' + now.getDate() + '日 周' + '日一二三四五六'[now.getDay()], ax + 5.4 * k, ay + 21 * k);
    c.textAlign = 'right';
    c.font = `700 ${5 * k}px ${sans}`; c.fillText('晴 22°', ax + aw - 5 * k, ay + 5 * k);
    c.font = `400 ${2.8 * k}px ${sans}`; c.fillText('湿度 48%  室内 24.6°C', ax + aw - 5 * k, ay + 11.5 * k);
    c.textAlign = 'left';
    [['CPU', .37], ['GPU', .61], ['显存', .44]].forEach(([n, v], i) => {
      const y = ay + 28 * k + i * 6 * k;
      c.font = `500 ${2.8 * k}px ${sans}`; c.fillText(n, ax + 5 * k, y);
      const bx = ax + 15 * k, bw = aw - 32 * k;
      c.lineWidth = .35 * k; c.strokeRect(bx, y + .3 * k, bw, 2.6 * k);
      c.save(); c.beginPath(); c.rect(bx, y + .3 * k, bw * v, 2.6 * k); c.clip();
      for (let s = -10; s < bw * v / k + 10; s += 1.1) { c.beginPath(); c.moveTo(bx + s * k, y + 3 * k); c.lineTo(bx + (s + 2.6) * k, y); c.lineWidth = .3 * k; c.stroke(); }
      c.restore();
      c.fillText(Math.round(v * 100) + '%', ax + aw - 13 * k, y);
    });
    c.fillRect(ax + 5 * k, ay + 46 * k, aw - 10 * k, .35 * k);
    c.font = `500 ${2.8 * k}px ${sans}`; c.fillText('Claude Code  等待确认', ax + 5 * k, ay + 48 * k);
    c.textAlign = 'right'; c.fillText('今日 1.2M tokens', ax + aw - 5 * k, ay + 48 * k); c.textAlign = 'left';
    scrTex.needsUpdate = true;
  }
  const screenGroup = new T.Group();
  const panel = box(105.33, .85, 62.37, [M.edge, M.edge, M.back, new T.MeshStandardMaterial({ map: scrTex, roughness: .9 }), M.edge, M.edge]);
  panel.position.y = -.6 - .425;
  screenGroup.add(panel);
  scene.add(screenGroup);

  /* FPC tail wrapping around the right edge (back view) into FPC1 */
  function ribbon() {
    // 弯折半径 1.6mm，最外点在 x≈106.4，离外壳右侧内壁（108.5）约 2mm
    const cx = X(104.8), pts = [[X(104.4), -1.0], [cx, -1.0]];
    for (let i = 1; i <= 14; i++) { const a = -Math.PI / 2 + Math.PI * i / 14; pts.push([cx + 1.6 * Math.cos(a), .6 + 1.6 * Math.sin(a)]); }
    pts.push([X(101.9), 2.15]);
    const z0 = Z(31) - 6.25, z1 = Z(31) + 6.25; // FPC 宽 12.5，中心在板子中心线 y 31
    const pos = [], idx = [];
    pts.forEach(([x, y], i) => { pos.push(x, y, z0, x, y, z1); if (i) { const a = (i - 1) * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); } });
    const g = new T.BufferGeometry();
    g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
    g.setIndex(idx); g.computeVertexNormals();
    const m = new T.Mesh(g, M.amber); m.castShadow = true;
    return m;
  }
  const fpcTail = ribbon();
  scene.add(fpcTail);

  /* battery */
  const batCanvas = document.createElement('canvas'); batCanvas.width = 500; batCanvas.height = 340;
  (() => {
    const c = batCanvas.getContext('2d');
    c.fillStyle = '#B4BAC2'; c.fillRect(0, 0, 500, 340);
    c.fillStyle = '#E3C33A'; c.fillRect(0, 230, 500, 46);
    c.fillStyle = '#23272B'; c.font = '700 44px "IBM Plex Mono", monospace'; c.fillText('454261', 34, 90);
    c.font = '500 30px "IBM Plex Mono", monospace'; c.fillText('3.7V  1500mAh  Li-Po', 34, 150);
    c.font = '500 24px "Noto Sans SC", sans-serif'; c.fillText('自带保护板 · 过充 / 过放 / 过流 / 短路', 34, 262);
  })();
  const batTex = new T.CanvasTexture(batCanvas); batTex.encoding = T.sRGBEncoding;
  const battery = box(61, 4.5, 42, [M.pouch, M.pouch, new T.MeshStandardMaterial({ map: batTex, roughness: .45, metalness: .3 }), M.pouch, M.pouch, M.pouch]);
  // A：电池叠在元件上方；B：电池沉进开窗，压在屏背面的支撑片上
  const BAT = { A: [X(62), BT + 2.6 + 2.25, Z(31)], B: [X(NOTCH[0] + NOTCH[2] / 2), -.4 + 2.25, Z(NOTCH[1] + NOTCH[3] / 2)] };
  const batBase = new T.Vector3();
  function placeBat(a, b, t) { batBase.set(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t); battery.position.set(batBase.x, batBase.y + (typeof explode === 'number' ? explode * 30 : 0), batBase.z); }
  scene.add(battery);
  // 0.2mm 支撑片：贴在屏背面，覆盖整个开窗，电池粘在它上面（F16）
  const sheet = box(NOTCH[2] + 2, .2, NOTCH[3] + 2, new T.MeshStandardMaterial({ color: col(0xd9dde2), roughness: .35, metalness: .6, transparent: true, opacity: .85 }));
  sheet.position.set(X(NOTCH[0] + NOTCH[2] / 2 + 1), -.5, Z(NOTCH[1] + NOTCH[3] / 2 + 1)); // v4.6 缺角在左上，支撑片往下多出 1mm 搭在底边窄条上
  screenGroup.add(sheet);
  // ST2：v1.2 板子左边缩进 8.5mm 后，屏背面空出 8.5×19mm（v4.6 起在左下角），用和 PCB 等厚的泡棉垫托住
  const foam = box(8.5, 1.2, 19, new T.MeshStandardMaterial({ color: col(0x3b3d40), roughness: .95 }));
  foam.position.set(X(4.25), .2, Z(52.5));
  screenGroup.add(foam);

  /* ---------- enclosure（v4.44 外壳 v1，和 enclosure/eink-case.scad 同一套尺寸）----------
     亚克力面板（透明，背面印白框）贴在中框前沿，屏幕贴在面板背面；中框 = 四周侧壁 + 4 个螺丝柱 + 两头粘接台；
     后盖是一块平板，4 颗沉头螺丝从背面拧进中框的螺丝柱。PCB 不上螺丝，由后盖伸出的定位压柱插进 MH1、MH2 并压住。
     开孔都按 board-data.js 的 CASE 画；3D 里圆角做了简化（Type-C 开孔、充电灯窗画成方的）。 */
  const C = window.BOARD.CASE, Wt = C.wall, Y0 = C.rim, YB = C.back[0], c0 = C.clr.master, CP = C.cap;
  const shellMats = {
    body: std(0xe4e1d8, { roughness: .62 }), plate: std(0xe4e1d8, { roughness: .62 }), acr: std(0xd6e8f2, { roughness: .08, metalness: .0 }), print: std(0xf7f6f1, { roughness: .8 }),
    key: std(0xc9c4b7, { roughness: .5 }), keyDim: std(0xa9a395, { roughness: .6 }), screw: std(0x8d9196, { roughness: .3, metalness: .8 }),
    nylon: std(0xf1eee6, { roughness: .5 })
  };
  const shellBack = new T.Group(), shellFront = new T.Group(), shellPanel = new T.Group();
  const bx = (x0, x1, y0, y1, h0, h1, mat) => { const m = box(x1 - x0, h1 - h0, y1 - y0, mat); m.position.set(X((x0 + x1) / 2), (h0 + h1) / 2, Z((y0 + y1) / 2)); return m; };
  const cyl = (x, y, r, h0, h1, mat) => { const m = new T.Mesh(new T.CylinderGeometry(r, r, h1 - h0, 24), mat); m.position.set(X(x), (h0 + h1) / 2, Z(y)); m.castShadow = true; return m; };
  const posB = ref => (parts.find(p => p.ref === ref) || {}).B;
  // openings per wall: [from, to, h0, h1, kind], positions along the wall in board mm, heights above the PCB bottom
  const open = { top: [], right: [], bottom: [], left: [] };
  const KH = C.keyH, kx = C.keyX, rc = (kx.SW1 + kx.SW2) / 2, [RW, RH, RR] = CP.rock, MR = CP.round / 2;
  open.bottom.push([rc - RW / 2 - c0, rc + RW / 2 + c0, KH - RH / 2 - c0, KH + RH / 2 + c0, 'rrect']);   // 翘板开孔：圆角 R(1.2 + 间隙)，和键帽同心
  ['SW6', 'SW3'].forEach(r => open.bottom.push([kx[r] - MR - c0, kx[r] + MR + c0, KH - MR - c0, KH + MR + c0, 'round']));
  open.bottom.push([posB('CARD1')[0] - C.tf.w / 2, posB('CARD1')[0] + C.tf.w / 2, C.tf.hc - C.tf.h / 2, C.tf.hc + C.tf.h / 2, 'nail']);
  [C.spk, C.vent].forEach(S => { for (let i = 0; i < S.n; i++) { const x = S.xc + (i - (S.n - 1) / 2) * S.pitch; open.bottom.push([x - S.w / 2, x + S.w / 2, S.hc - S.h / 2, S.hc + S.h / 2]); } });
  // Type-C：中框在开孔上方开 U 形口直通后盖（上半圈在后盖舌头上），下半圈两侧另补两小块
  const uy = posB('USB0')[1], ub = C.usb, tw = ub.tongueW / 2, ug = ub.gap.master;
  // Type-C 开孔和充电灯窗所在的这一段右侧壁单独做成带圆角孔的板（下面 rightPlate），通用侧壁跳过这一段
  const rs0 = uy - tw - ug - 1, rs1 = C.led.y + 2.5;
  open.right.push([rs0, rs1, Y0, YB, 'custom']);
  function wall(side) {
    const along = side === 'top' || side === 'bottom';
    const a0 = (along ? C.x0 : C.y0) + C.r, a1 = (along ? C.x1 : C.y1) - C.r;
    const t0 = side === 'top' ? C.y0 : side === 'bottom' ? C.y1 - Wt : side === 'left' ? C.x0 : C.x1 - Wt, t1 = t0 + Wt;
    const seg = (s0, s1, h0, h1) => along ? bx(s0, s1, t0, t1, h0, h1, shellMats.body) : bx(t0, t1, s0, s1, h0, h1, shellMats.body);
    // 圆孔（菜单键、电源键）：这一段侧壁画成带圆孔的板，只用在顶边、底边
    // 圆角矩形孔（翻页翘板）：这一段侧壁画成带圆角孔的板，孔四周和键帽一样是均匀的间隙
    const rrSeg = (s0, s1, x0, x1, h0, h1, r) => {
      const sh = new T.Shape(); sh.moveTo(X(s0), Y0); sh.lineTo(X(s1), Y0); sh.lineTo(X(s1), YB); sh.lineTo(X(s0), YB); sh.lineTo(X(s0), Y0);
      const hp = new T.Path(), L = X(x0), R = X(x1);
      hp.moveTo(L + r, h0); hp.absarc(L + r, h0 + r, r, -Math.PI / 2, -Math.PI, true); hp.lineTo(L, h1 - r); hp.absarc(L + r, h1 - r, r, Math.PI, Math.PI / 2, true);
      hp.lineTo(R - r, h1); hp.absarc(R - r, h1 - r, r, Math.PI / 2, 0, true); hp.lineTo(R, h0 + r); hp.absarc(R - r, h0 + r, r, 0, -Math.PI / 2, true); hp.lineTo(L + r, h0);
      sh.holes.push(hp);
      const m = new T.Mesh(new T.ExtrudeGeometry(sh, { depth: Wt, bevelEnabled: false, curveSegments: 20 }), shellMats.body);
      m.position.z = Z(t0); m.castShadow = m.receiveShadow = true; return m;
    };
    const roundSeg = (s0, s1, hc, r) => {
      const sh = new T.Shape(); sh.moveTo(X(s0), Y0); sh.lineTo(X(s1), Y0); sh.lineTo(X(s1), YB); sh.lineTo(X(s0), YB); sh.lineTo(X(s0), Y0);
      const hole = new T.Path(); hole.absarc(X((s0 + s1) / 2), hc, r, 0, Math.PI * 2, true); sh.holes.push(hole);
      const m = new T.Mesh(new T.ExtrudeGeometry(sh, { depth: Wt, bevelEnabled: false, curveSegments: 28 }), shellMats.body);
      m.position.z = Z(t0); m.castShadow = m.receiveShadow = true; return m;
    };
    // 指甲槽：开孔下面那段侧壁分两层，内层 0.5 完整，外层 1.0 在开孔中间挖一个半椭圆缺口
    const nailSeg = (s0, s1, hTop) => {
      const xc = (s0 + s1) / 2, [nw, nh, nd] = C.tf.nail, rx = nw / 2, sh = new T.Shape();
      sh.moveTo(X(s0), Y0); sh.lineTo(X(s1), Y0); sh.lineTo(X(s1), hTop); sh.lineTo(X(xc + rx), hTop);
      sh.absellipse(X(xc), hTop, rx, nh, 0, -Math.PI, true); sh.lineTo(X(s0), hTop); sh.lineTo(X(s0), Y0);
      const m = new T.Mesh(new T.ExtrudeGeometry(sh, { depth: nd, bevelEnabled: false, curveSegments: 24 }), shellMats.body);
      m.position.z = Z(t1 - nd); m.castShadow = m.receiveShadow = true;
      return [bx(s0, s1, t0, t1 - nd, Y0, hTop, shellMats.body), m];
    };
    let cur = a0;
    open[side].slice().sort((p, q) => p[0] - q[0]).forEach(([o0, o1, h0, h1, kind]) => {
      const e0 = (kind === 'round' || kind === 'rrect') && along ? o0 - .3 : o0;
      if (e0 > cur) shellFront.add(seg(cur, e0, Y0, YB));
      if (kind === 'custom') { cur = o1; return; }
      if (kind === 'round' && along) shellFront.add(roundSeg(o0 - .3, o1 + .3, (h0 + h1) / 2, (o1 - o0) / 2));
      else if (kind === 'rrect' && along) shellFront.add(rrSeg(o0 - .3, o1 + .3, o0, o1, h0, h1, RR + c0));
      else if (kind === 'nail' && along) shellFront.add(...nailSeg(o0, o1, h0), seg(o0, o1, h1, YB));
      else { shellFront.add(seg(o0, o1, Y0, h0)); if (h1 < YB) shellFront.add(seg(o0, o1, h1, YB)); }
      cur = (kind === 'round' || kind === 'rrect') && along ? o1 + .3 : o1;
    });
    if (a1 > cur) shellFront.add(seg(cur, a1, Y0, YB));
  }
  ['top', 'right', 'bottom', 'left'].forEach(wall);
  // 右侧壁 Type-C 那一段：中框留开孔下半圈，开孔上方开 U 形口直通后盖；充电灯窗 Ø1.5 圆孔。
  // 板画在（板子 y，高度）平面里，形状 x 取 −Z(y)，拉伸方向是壁厚，再转 90° 放到右侧壁上
  const hw = ub.w / 2, ur = ub.r, ub0 = ub.hc - ub.h / 2, ub1 = ub.hc + ub.h / 2;
  const P = (y, h) => [-Z(y), h];
  const arcPts = (cy, ch, a0, a1) => { const o = []; for (let i = 0; i <= 8; i++) { const a = a0 + (a1 - a0) * i / 8; o.push(P(cy + ur * Math.cos(a), ch + ur * Math.sin(a))); } return o; };
  const wallPlate = (pts, holes, mat) => {
    const sh = new T.Shape(pts.map(([a, b]) => new T.Vector2(a, b)));
    (holes || []).forEach(hp => sh.holes.push(hp));
    const g = new T.ExtrudeGeometry(sh, { depth: Wt, bevelEnabled: false, curveSegments: 24 }); g.rotateY(Math.PI / 2);
    const m = new T.Mesh(g, mat); m.position.x = X(C.x1 - Wt); m.castShadow = m.receiveShadow = true; return m;
  };
  const ledHole = new T.Path(); ledHole.absarc(-Z(C.led.y), C.led.hc, C.led.d / 2, 0, Math.PI * 2, true);
  shellFront.add(wallPlate([P(rs0, Y0), P(rs1, Y0), P(rs1, YB), P(uy + tw + ug, YB), P(uy + tw + ug, ub.hc), P(uy + hw, ub.hc), P(uy + hw, ub0 + ur),
    ...arcPts(uy + hw - ur, ub0 + ur, 0, -Math.PI / 2), ...arcPts(uy - hw + ur, ub0 + ur, -Math.PI / 2, -Math.PI), P(uy - hw, ub.hc), P(uy - tw - ug, ub.hc), P(uy - tw - ug, YB), P(rs0, YB)],
    [ledHole], shellMats.body));

  // rounded corners: quarter rings (shape +y points to the board's top edge)
  [[C.x0 + C.r, C.y0 + C.r, Math.PI / 2], [C.x1 - C.r, C.y0 + C.r, 0], [C.x1 - C.r, C.y1 - C.r, -Math.PI / 2], [C.x0 + C.r, C.y1 - C.r, Math.PI]].forEach(([cx, cy, a]) => {
    const sh = new T.Shape();
    sh.absarc(0, 0, C.r, a, a + Math.PI / 2, false); sh.absarc(0, 0, C.r - Wt, a + Math.PI / 2, a, true);
    const g = new T.ExtrudeGeometry(sh, { depth: YB - Y0, bevelEnabled: false, curveSegments: 12 }); g.rotateX(-Math.PI / 2);
    const m = new T.Mesh(g, shellMats.body); m.position.set(X(cx), Y0, Z(cy)); m.castShadow = m.receiveShadow = true;
    shellFront.add(m);
  });
  const slab = (shape, h0, h1, mat) => { const g = new T.ExtrudeGeometry(shape, { depth: h1 - h0, bevelEnabled: false, curveSegments: 10 }); g.rotateX(-Math.PI / 2); const m = new T.Mesh(g, mat); m.position.y = h0; m.castShadow = m.receiveShadow = true; return m; };
  // 中框：螺丝柱、两头粘接台
  C.bosses.forEach(([x, y]) => shellFront.add(cyl(x, y, C.bossD / 2, Y0, YB, shellMats.body)));
  C.ledges.forEach(([x0, y0, x1, y1]) => shellFront.add(bx(x0, x1, y0, y1, Y0, Y0 + C.ledgeH, shellMats.body)));
  // 亚克力面板：透明板 + 背面白框（透光区不印）
  const outS = () => { const s = new T.Shape(); rrect(s, C.x0, C.y0, C.x1 - C.x0, C.y1 - C.y0, C.r, false); return s; };
  shellPanel.add(slab(outS(), C.acr[0], C.acr[1] - .03, shellMats.acr));
  const prS = outS(), pw = new T.Path(); rrect(pw, C.winCx - C.win[0] / 2, H / 2 - C.win[1] / 2, C.win[0], C.win[1], .01, true); prS.holes.push(pw);
  shellPanel.add(slab(prS, C.acr[1] - .03, C.acr[1], shellMats.print));
  // 键帽（从壳内装）：键身穿过开孔，靠后盖一侧的法兰片贴住侧壁内表面（离 PCB 底面 3.0～5.0）。3D 里法兰片下面的 45° 斜面省略
  const wIn = C.y1 - Wt, wOut = C.y1, [fl0, fl1] = CP.fl, fTop = KH + 2.85;
  const rrS = (x0, x1, h0, h1, r) => {
    const sh = new T.Shape(), L = X(x0), R = X(x1);
    sh.moveTo(L + r, h0); sh.lineTo(R - r, h0); sh.quadraticCurveTo(R, h0, R, h0 + r); sh.lineTo(R, h1 - r); sh.quadraticCurveTo(R, h1, R - r, h1);
    sh.lineTo(L + r, h1); sh.quadraticCurveTo(L, h1, L, h1 - r); sh.lineTo(L, h0 + r); sh.quadraticCurveTo(L, h0, L + r, h0);
    return sh;
  };
  const ext = (sh, y0, y1, mat) => { const m = new T.Mesh(new T.ExtrudeGeometry(sh, { depth: y1 - y0, bevelEnabled: false, curveSegments: 12 }), mat); m.position.z = Z(y0); m.castShadow = m.receiveShadow = true; return m; };
  const cylY = (x, hc, r, y0, y1, mat) => { const g = new T.CylinderGeometry(r, r, y1 - y0, 32); g.rotateX(Math.PI / 2); const m = new T.Mesh(g, mat); m.position.set(X(x), hc, Z((y0 + y1) / 2)); m.castShadow = true; return m; };
  const flange = (x, w) => [bx(x - w / 2 - CP.flW, x + w / 2 + CP.flW, fl0, fl1, C.cap.flMin, fTop, shellMats.key), bx(x - w / 2 + .3, x + w / 2 - .3, fl1 - .1, CP.in + .1, C.cap.flMin + .05, KH + Math.min(RH / 2, MR) - .1, shellMats.key)];
  // 翻页翘板 KC1：键身、正中横向凸棱、法兰片
  shellFront.add(ext(rrS(rc - RW / 2, rc + RW / 2, KH - RH / 2, KH + RH / 2, RR), CP.in, wOut + CP.proud.rock, shellMats.key));
  shellFront.add(bx(rc - CP.ridge.master[0] / 2, rc + CP.ridge.master[0] / 2, wOut + CP.proud.rock, wOut + CP.proud.rock + CP.ridge.master[1], KH - 1.1, KH + 1.1, shellMats.keyDim));
  shellFront.add(...flange(rc, RW));
  // 菜单键 KC2（外伸、表面微凹）、电源键 KC3（和壳面齐平、中间凸点）
  shellFront.add(cylY(kx.SW6, KH, MR, CP.in, wOut + CP.proud.menu, shellMats.key), cylY(kx.SW6, KH, MR * .62, wOut + CP.proud.menu - .05, wOut + CP.proud.menu + .02, shellMats.keyDim), ...flange(kx.SW6, CP.round));
  shellFront.add(cylY(kx.SW3, KH, MR, CP.in, wOut, shellMats.key), cylY(kx.SW3, KH, CP.dot.master[0] / 2, wOut, wOut + CP.dot.master[1], shellMats.keyDim), ...flange(kx.SW3, CP.round));
  // 后盖：平板（沉头螺丝孔、传感器通风槽）、PCB 定位压柱、Type-C 顶面压块和补开孔上半圈的舌头
  const bpS = outS();
  C.bosses.forEach(([x, y]) => { const p = new T.Path(); p.absarc(X(x), -Z(y), C.screw.hole / 2, 0, Math.PI * 2, true); bpS.holes.push(p); });
  const V = C.backVent;
  for (let i = 0; i < V.n; i++) { const p = new T.Path(), x = V.xc + (i - (V.n - 1) / 2) * V.pitch; rrect(p, x - V.w / 2, V.y[0], V.w, V.y[1] - V.y[0], .4, true); bpS.holes.push(p); }
  shellBack.add(slab(bpS, C.back[0], C.back[1], shellMats.plate));
  C.bosses.forEach(([x, y, kind]) => shellBack.add(cyl(x, y, C.screw.csk / 2 - .1, C.back[1] - .3, C.back[1] - .02, kind === 'nylon' ? shellMats.nylon : shellMats.screw)));
  HOLES.B.forEach(([hx, hy]) => { shellBack.add(cyl(hx, hy, C.post.shoulder / 2, BT - .1, YB, shellMats.body), cyl(hx, hy, C.post.tip / 2, .1, BT - .1, shellMats.body)); });
  const [px0, py0, px1, py1, ph] = C.press;
  shellBack.add(bx(px0, px1, py0, py1, ph, YB, shellMats.body));
  // 后盖舌头：补上 Type-C 开孔上半圈（两角 R1.6），和中框 U 形口之间留间隙
  shellBack.add(wallPlate([P(uy - tw, ub.hc + ug), P(uy - hw, ub.hc + ug), P(uy - hw, ub1 - ur), ...arcPts(uy - hw + ur, ub1 - ur, Math.PI, Math.PI / 2),
    ...arcPts(uy + hw - ur, ub1 - ur, Math.PI / 2, 0), P(uy + hw, ub.hc + ug), P(uy + tw, ub.hc + ug), P(uy + tw, YB), P(uy - tw, YB)], [], shellMats.body));
  scene.add(shellBack, shellFront, shellPanel);

  let shellMode = 'ghost';
  function setShell(m) {
    shellMode = m;
    shellBack.visible = shellFront.visible = shellPanel.visible = m !== 'off';
    const ghost = m === 'ghost';
    [[shellMats.body, .3], [shellMats.plate, .12], [shellMats.acr, .18], [shellMats.print, .5], [shellMats.key, .55], [shellMats.keyDim, .6], [shellMats.screw, .6], [shellMats.nylon, .6]].forEach(([mat, o]) => {
      mat.transparent = ghost; mat.opacity = ghost ? o : 1; mat.depthWrite = !ghost; mat.needsUpdate = true;
    });
    [shellBack, shellFront, shellPanel].forEach(g => g.traverse(o => { if (o.isMesh) o.castShadow = !ghost; }));
    // 亚克力始终半透明，能看到后面的屏幕；背面的白色丝印框不透明
    Object.assign(shellMats.acr, { transparent: true, opacity: ghost ? .18 : .32, depthWrite: false }); shellMats.acr.needsUpdate = true;
    shellPanel.traverse(o => { if (o.isMesh && o.material === shellMats.acr) o.castShadow = false; });
  }

  /* hover / selection outline */
  const sel = new T.LineSegments(new T.EdgesGeometry(new T.BoxGeometry(1, 1, 1)), new T.LineBasicMaterial({ color: 0xe0a060 }));
  const hov = new T.LineSegments(new T.EdgesGeometry(new T.BoxGeometry(1, 1, 1)), new T.LineBasicMaterial({ color: 0xe0a060, transparent: true, opacity: .55 }));
  sel.visible = hov.visible = false;
  scene.add(sel, hov);
  const tmpBox = new T.Box3(), tmpV = new T.Vector3(), tmpC = new T.Vector3();
  function fitOutline(o, p) {
    if (!p) { o.visible = false; return; }
    tmpBox.setFromObject(p.g); tmpBox.getSize(tmpV); tmpBox.getCenter(tmpC);
    o.scale.set(tmpV.x + .6, tmpV.y + .6, tmpV.z + .6); o.position.copy(tmpC); o.visible = true;
  }

  /* ---------- silkscreen drawing ---------- */
  const layers = { parts: true, labels: true, zones: false, keep: true, screen: true, batt: true, fpc: true };
  let layout = 'B', tween = null;
  function drawSilk(L) {
    const c = silkCanvas.getContext('2d');
    c.clearRect(0, 0, silkCanvas.width, silkCanvas.height);
    c.save(); c.scale(S, S);
    if (layers.keep) {
      const [kx, ky, kw, kd] = keepout[L];
      c.save(); c.beginPath(); c.rect(kx, ky, kw, kd); c.clip();
      c.strokeStyle = 'rgba(240,120,100,.75)'; c.lineWidth = .25;
      for (let s = -70; s < 120; s += 1.4) { c.beginPath(); c.moveTo(s, 0); c.lineTo(s + 70, 70); c.stroke(); }
      c.restore();
      c.setLineDash([.8, .5]); c.strokeStyle = '#F07864'; c.lineWidth = .3; c.strokeRect(kx + .15, ky + .15, kw - .3, kd - .3); c.setLineDash([]);
      c.fillStyle = '#F7A493'; c.font = '600 1.5px "Noto Sans SC", sans-serif';
      c.save();
      if (kd > kw) { c.translate(kx + kw / 2 + .5, ky + kd / 2); c.rotate(-Math.PI / 2); c.textAlign = 'center'; c.fillText('天线净空 · 各层不铺铜', 0, 0); }
      else { c.fillText('天线净空 · 各层不铺铜', kx + 1, ky + kd - 1); }
      c.restore();
    }
    if (layers.zones) {
      c.setLineDash([1, .6]); c.strokeStyle = '#F0C79B'; c.lineWidth = .28; c.fillStyle = '#F0C79B'; c.font = '600 1.6px "Noto Sans SC", sans-serif';
      Object.entries(zoneRects[L]).forEach(([k, [x, y, w, h]]) => { c.strokeRect(x + .2, y + .2, w - .4, h - .4); c.fillText((ZONES[k] || {}).t || k, x + .8, y + h - .9); });
      c.setLineDash([]);
    }
    c.fillStyle = '#F4F4EC'; c.textAlign = 'center'; c.textBaseline = 'bottom';
    parts.forEach(p => {
      if (p.onlyB && L !== 'B') return;
      const [cx, cy, rot] = p[L];
      const rot90 = Math.abs(rot) % 180 === 90;
      const fw = rot90 ? p.d : p.w, fd = rot90 ? p.w : p.d;
      // component outline for parts that sit on pads
      if (!['tp', 'pads'].includes(p.type)) { c.strokeStyle = 'rgba(244,244,236,.55)'; c.lineWidth = .12; c.strokeRect(cx - fw / 2 - .25, cy - fd / 2 - .25, fw + .5, fd + .5); }
      if (!layers.labels) return;
      const big = ['module', 'so', 'usbc', 'fpc', 'jst', 'ind', 'sidekey', 'pads'].includes(p.type);
      c.font = `600 ${big ? 1.55 : 1.0}px "IBM Plex Mono", Consolas, monospace`;
      if (p.type === 'module') { c.font = '600 2.2px "IBM Plex Mono", monospace'; c.save(); c.translate(cx + (rot90 ? 3 : 0), cy + (rot90 ? 0 : 4)); c.fillText('U2', 0, 0); c.restore(); return; }
      c.fillText(p.ref, cx, cy - fd / 2 - .4);
    });
    c.textAlign = 'center'; c.font = '600 1.35px "IBM Plex Mono", monospace'; c.fillStyle = 'rgba(244,244,236,.85)';
    c.fillText('E-PAPER MONITOR · ' + (L === 'A' ? 'v0.4' : 'v3.3'), L === 'A' ? 92 : 48, L === 'A' ? 46.9 : 52.6);
    HOLES[L].forEach(([hx, hy], i) => { c.font = '600 1.1px "IBM Plex Mono", monospace'; c.fillText('MH' + (i + 1), hx, hy - 2.1); });
    c.restore();
    silkTex.needsUpdate = true;
  }

  /* ---------- HTML labels ---------- */
  const labelHost = document.getElementById('vLabels');
  const labelRefs = []; // 浮动文字标签已取消，元件信息在点击后的右侧面板里看
  const labels = labelRefs.map(ref => {
    const p = parts.find(x => x.ref === ref);
    const it = APP.refMap.get(ref);
    const n = document.createElement('span');
    n.className = 'vlab'; n.textContent = ref + (it ? ' ' + it.fn : '');
    labelHost.append(n);
    return { p, n };
  });
  const proj = new T.Vector3();
  function updateLabels(w, h) {
    const show = layers.labels && layers.parts && camera.position.y > 8;
    labels.forEach(({ p, n }) => {
      if (!show) { n.style.opacity = 0; return; }
      proj.set(p.g.position.x, p.g.position.y + partsGroup.position.y + p.h + 2.5, p.g.position.z).project(camera);
      const x = (proj.x * .5 + .5) * w, y = (-proj.y * .5 + .5) * h;
      n.style.transform = `translate(${x}px, ${y}px) translate(-50%, -100%)`;
      n.style.opacity = proj.z < 1 ? 1 : 0;
    });
  }

  /* ---------- orbit control ---------- */
  // [方位角, 俯仰角, 距离, 看向的点（板子 x、高度、板子 y），缺省为板子中心]；keys 是 v4.7 加的键帽近景，从底边斜上方看
  const VIEWS = { iso: [-.62, .95, 158], top: [0, .02, 150], screen: [Math.PI, Math.PI - .02, 150], side: [0, Math.PI / 2 - .1, 130], keys: [-.22, 1.42, 70, [61, 3, 64]] };
  const lookAt0 = new T.Vector3(0, 1.5, 0), look = lookAt0.clone(), lookT = lookAt0.clone();
  const cam = { th: VIEWS.iso[0], ph: VIEWS.iso[1], r: VIEWS.iso[2] };
  const tgt = Object.assign({}, cam);
  let auto = !reduce;
  function setView(k) {
    const v = VIEWS[k];
    let d = v[0] - tgt.th; d = Math.atan2(Math.sin(d), Math.cos(d));
    tgt.th += d; tgt.ph = v[1]; tgt.r = v[2];
    if (v[3]) lookT.set(X(v[3][0]), v[3][1], Z(v[3][2])); else lookT.copy(lookAt0);
  }
  const canvas = renderer.domElement;
  const ptrs = new Map();
  let drag = null, pinch0 = 0, downAt = null;
  canvas.addEventListener('pointerdown', e => {
    canvas.setPointerCapture(e.pointerId);
    ptrs.set(e.pointerId, [e.clientX, e.clientY]);
    downAt = [e.clientX, e.clientY];
    if (ptrs.size === 1) drag = [e.clientX, e.clientY];
    if (ptrs.size === 2) { const [a, b] = [...ptrs.values()]; pinch0 = Math.hypot(a[0] - b[0], a[1] - b[1]); }
    setAuto(false);
    stage.classList.add('dragging');
  });
  canvas.addEventListener('pointermove', e => {
    if (ptrs.has(e.pointerId)) ptrs.set(e.pointerId, [e.clientX, e.clientY]);
    if (ptrs.size === 2) {
      const [a, b] = [...ptrs.values()]; const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
      if (pinch0) tgt.r = Math.min(420, Math.max(30, tgt.r * pinch0 / d));
      pinch0 = d; return;
    }
    if (drag) {
      const dx = e.clientX - drag[0], dy = e.clientY - drag[1];
      drag = [e.clientX, e.clientY];
      tgt.th -= dx * .0085; tgt.ph = Math.min(Math.PI - .02, Math.max(.02, tgt.ph - dy * .0085));
      hideTip();
    } else hoverAt(e);
  });
  const end = e => {
    ptrs.delete(e.pointerId);
    if (ptrs.size < 2) pinch0 = 0;
    if (!ptrs.size) {
      stage.classList.remove('dragging');
      if (drag && downAt && Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]) < 5) clickAt(e);
      drag = null;
    }
  };
  canvas.addEventListener('pointerup', end);
  canvas.addEventListener('pointercancel', end);
  canvas.addEventListener('pointerleave', () => { if (!drag) { hideTip(); fitOutline(hov, null); } });
  canvas.addEventListener('wheel', e => { e.preventDefault(); tgt.r = Math.min(420, Math.max(30, tgt.r * Math.exp(e.deltaY * .0012))); setAuto(false); }, { passive: false });
  canvas.addEventListener('dblclick', () => { setView(curView); });

  /* ---------- hover & click ---------- */
  const ray = new T.Raycaster(), ndc = new T.Vector2();
  const tip = document.getElementById('vTip');
  function pick(e) {
    if (!layers.parts) return null;
    const rct = canvas.getBoundingClientRect();
    ndc.set((e.clientX - rct.left) / rct.width * 2 - 1, -(e.clientY - rct.top) / rct.height * 2 + 1);
    ray.setFromCamera(ndc, camera);
    const hit = ray.intersectObjects(targets, false).find(h => h.object.userData.part.g.visible);
    return hit ? hit.object.userData.part : null;
  }
  let hovered = null;
  function hoverAt(e) {
    const p = pick(e);
    if (p !== hovered) { hovered = p; fitOutline(hov, p); canvas.style.cursor = p ? 'pointer' : ''; }
    // v3.7 起悬停只描边、改鼠标指针，不再弹文字框；点击后在右侧面板看详情
    hideTip();
  }
  function hideTip() { tip.hidden = true; }
  let selected = null;
  function clickAt(e) { const p = pick(e); select(p); }
  function select(p) {
    selected = p; fitOutline(sel, p);
    if (p) setAuto(false);
    renderInfo();
  }

  /* ---------- info panel ---------- */
  const info = document.getElementById('vInfo');
  const el = APP.el;
  function renderInfo() {
    info.innerHTML = '';
    if (!selected) return;
    const p = selected, it = APP.refMap.get(p.ref), z = ZONES[p.zone] || {};
    const [cx, cy, rot] = p[layout];
    info.append(el('div', { class: 'vi-k', text: z.t || p.zone }),
      el('div', { class: 'vi-ref', text: p.ref }),
      el('div', { class: 'vi-t', text: it ? it.fn : '' }),
      it ? el('div', { class: 'vi-part' }, el('span', { class: 'mono', text: it.part }), ' · ' + it.pkg) : null,
      el('div', { class: 'vi-pos mono', text: `x ${cx.toFixed(1)}  y ${cy.toFixed(1)}  ${rot}°  ·  ${p.w}×${p.d}×${p.h} mm` }),
      it && it.note ? el('p', { text: it.note }) : null,
      z.rules ? el('ul', null, z.rules.map(r => el('li', { text: r }))) : null,
      el('div', { class: 'vi-links' }, el('a', { href: 'bom.html#ref-' + p.ref, text: '器件清单 →' }),
        el('button', { type: 'button', class: 'linklike', text: '取消选择', onclick: () => select(null) })));
    APP.enhance(info);
  }

  /* ---------- controls ---------- */
  let curView = 'iso';
  APP.bindSeg(document.getElementById('viewSeg'), v => { curView = v; setView(v); setAuto(false); });
  APP.bindSeg(document.getElementById('shellSeg'), v => setShell(v));
  setShell(shellMode);
  const autoBtn = document.getElementById('autoBtn');
  function setAuto(on) { auto = on; autoBtn.setAttribute('aria-pressed', String(on)); }
  autoBtn.addEventListener('click', () => setAuto(!auto));
  setAuto(auto);
  const ex = document.getElementById('explode');
  let explode = 0;
  ex.addEventListener('input', () => { explode = +ex.value / 100; document.getElementById('explodeO').textContent = Math.round(explode * 100) + '%'; applyLayers(); });
  document.querySelectorAll('[data-layer]').forEach(inp => {
    inp.checked = layers[inp.dataset.layer];
    inp.addEventListener('change', () => { layers[inp.dataset.layer] = inp.checked; applyLayers(); drawSilk(layout); });
  });
  function applyLayers() {
    partsGroup.visible = layers.parts;
    ko.visible = layers.keep;
    screenGroup.visible = layers.screen;
    battery.visible = layers.batt;
    fpcTail.visible = layers.fpc && layers.screen && explode < .02;
    screenGroup.position.y = -explode * 24;
    partsGroup.position.y = explode * 8;
    battery.position.y = batBase.y + explode * 30;
    sheet.visible = boards.B.visible; foam.visible = boards.B.visible;
    shellBack.position.y = explode * 40;
    shellFront.position.y = -explode * 34;
    shellPanel.position.y = -explode * 46;
    ko.position.y = BT + 1.95 + explode * 8;
    if (!layers.parts) { select(null); fitOutline(hov, null); }
    else { fitOutline(sel, selected); }
  }
  function switchLayout(L) {
    if (L === layout) return;
    const from = layout; layout = L;
    tween = { from, to: L, t0: performance.now(), dur: reduce ? 1 : 1100 };
    silk.material.opacity = .25;
    renderInfo();
  }

  /* ---------- size + loop ---------- */
  let vw = 1, vh = 1;
  function resize() {
    const r = stage.getBoundingClientRect();
    vw = Math.max(1, r.width); vh = Math.max(1, r.height);
    renderer.setSize(vw, vh, false);
    camera.aspect = vw / vh; camera.updateProjectionMatrix();
  }
  if ('ResizeObserver' in window) new ResizeObserver(resize).observe(stage); else addEventListener('resize', resize);
  resize();
  let visible = true;
  if ('IntersectionObserver' in window) new IntersectionObserver(es => { visible = es[0].isIntersecting; }).observe(stage);

  parts.forEach(p => place(p, p[layout], p[layout], 0));
  placeKo(keepout[layout], keepout[layout], 0);
  placeBat(BAT[layout], BAT[layout], 0);
  boards.A.visible = layout === 'A'; boards.B.visible = layout === 'B'; showOnly(layout);
  drawSilk(layout); drawScreen(); applyLayers(); renderInfo();
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(() => { drawSilk(layout); drawScreen(); });
  setInterval(drawScreen, 60000);
  stage.classList.add('v-ready');

  const ease = t => t < .5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
  let last = performance.now();
  function frame(now) {
    requestAnimationFrame(frame);
    const dt = Math.min(64, now - last); last = now;
    if (!visible) return;
    if (auto) tgt.th += dt * .00018;
    if (tween) {
      let t = (now - tween.t0) / tween.dur;
      if (t >= 1) t = 1;
      const e = ease(t);
      parts.forEach(p => place(p, p[tween.from], p[tween.to], e));
      placeKo(keepout[tween.from], keepout[tween.to], e);
      if (selected) fitOutline(sel, selected);
      placeBat(BAT[tween.from], BAT[tween.to], e);
      if (t >= .5) { boards.A.visible = tween.to === 'A'; boards.B.visible = tween.to === 'B'; sheet.visible = boards.B.visible; foam.visible = boards.B.visible; showOnly(tween.to); }
      if (t === 1) { tween = null; drawSilk(layout); silk.material.opacity = 1; renderInfo(); }
    }
    const k = reduce ? 1 : 1 - Math.pow(.001, dt / 1000 * 4);
    cam.th += (tgt.th - cam.th) * k; cam.ph += (tgt.ph - cam.ph) * k; cam.r += (tgt.r - cam.r) * k;
    const s = Math.sin(cam.ph);
    look.lerp(lookT, k);
    camera.position.set(look.x + cam.r * s * Math.sin(cam.th), look.y - 1.5 + cam.r * Math.cos(cam.ph), look.z + cam.r * s * Math.cos(cam.th));
    camera.lookAt(look);
    camera.updateMatrixWorld();
    updateLabels(vw, vh);
    renderer.render(scene, camera);
  }
  // 展示出图（eink-brief/tools/studio.html 用）：页面先设 window.STUDIO，这里直接摆好相机、外壳、图层和爆炸量，不自动旋转
  const SD = window.STUDIO;
  if (SD) {
    setAuto(false);
    if (SD.shell) setShell(SD.shell);
    Object.assign(layers, SD.layers || {});
    explode = SD.explode || 0;
    applyLayers(); drawSilk(layout);
    Object.assign(cam, { th: SD.th, ph: SD.ph, r: SD.r }); Object.assign(tgt, cam);
    if (SD.look) { lookT.set(X(SD.look[0]), SD.look[1], Z(SD.look[2])); look.copy(lookT); }
    if (SD.fov) { camera.fov = SD.fov; camera.updateProjectionMatrix(); }
    if (SD.up) camera.up.set(0, SD.up, 0);   // 从屏幕一面拍时把“上”翻过来，画面是正的
    if (SD.upv) camera.up.set(SD.upv[0], SD.upv[1], SD.upv[2]);   // 竖拿的画面：让电池那头朝上
    if (SD.light === 'screen') {             // 从屏幕一面拍：主光、天光都换到屏幕这一侧
      sun.position.set(70, -150, 80); fill.position.set(-90, -40, -80); fill.intensity = .45;
      hemi.color.set(0xffffff); hemi.groundColor.set(0x6a6e66); hemi.position.set(0, -1, 0); hemi.intensity = .5; sun.intensity = .62;
    }
    sun.shadow.mapSize.set(4096, 4096);
    renderer.setClearColor(0x000000, 0);
    renderer.toneMapping = T.ACESFilmicToneMapping; renderer.toneMappingExposure = SD.exposure || 1.0;   // 压住白色外壳的高光
  }
  requestAnimationFrame(frame);
})();
