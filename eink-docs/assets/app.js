/* 全站共享：页签导航、主题、目录、评审项链接、标题栏、本地存储、复制、折线图 */
(function () {
  'use strict';
  const D = window.DOC;
  const root = document.documentElement;

  /* ---------- storage ---------- */
  const store = {
    get(k, d) { try { const v = localStorage.getItem('eink.' + k); return v == null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem('eink.' + k, JSON.stringify(v)); } catch (e) { /* storage unavailable */ } }
  };

  const el = (tag, attrs, ...kids) => {
    const n = document.createElement(tag);
    if (attrs) for (const [k, v] of Object.entries(attrs)) {
      if (v == null || v === false) continue;
      if (k === 'class') n.className = v;
      else if (k === 'text') n.textContent = v;
      else if (k === 'html') n.innerHTML = v;
      else if (k.startsWith('on')) n.addEventListener(k.slice(2), v);
      else n.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat()) if (c != null) n.append(c.nodeType ? c : document.createTextNode(c));
    return n;
  };

  const sheetId = document.body.dataset.sheet;
  const sheetIdx = D.sheets.findIndex(s => s.id === sheetId);
  const pad = n => String(n).padStart(2, '0');
  const sheetFile = id => (D.sheets.find(s => s.id === id) || {}).file || 'index.html';

  /* ---------- review state ---------- */
  const review = {
    all() { return store.get('review', {}); },
    get(id) { return this.all()[id] || { s: 'todo', note: '' }; },
    set(id, patch) { const a = this.all(); a[id] = Object.assign(this.get(id), patch); store.set('review', a); document.dispatchEvent(new CustomEvent('review-change')); },
    decisions() { return store.get('decisions', {}); },
    setDecision(id, patch) { const a = this.decisions(); a[id] = Object.assign(a[id] || {}, patch); store.set('decisions', a); document.dispatchEvent(new CustomEvent('review-change')); }
  };

  /* ---------- theme ---------- */
  const themeLabels = { system: '跟随系统', light: '浅色', dark: '深色' };
  // 只在用户明确选择过主题时改动 data-theme；"跟随系统"时保留宿主页面原本的设置
  let themeSetByUs = false;
  function applyTheme(t) {
    if (t === 'light' || t === 'dark') { root.dataset.theme = t; themeSetByUs = true; }
    else if (themeSetByUs) { delete root.dataset.theme; themeSetByUs = false; }
  }
  applyTheme(store.get('theme', 'system'));

  /* ---------- sheet bar ---------- */
  function buildBar() {
    let cur = store.get('theme', 'system');
    const existing = document.querySelector('header.bar');
    if (existing) { wireBar(existing, existing.querySelector('#themeBtn'), existing.querySelector('.bar-prog')); return; }
    const tabs = el('nav', { class: 'tabs', 'aria-label': '图纸' },
      D.sheets.map((s, i) => el('a', { href: s.file, 'aria-current': s.id === sheetId ? 'page' : null },
        el('span', { class: 'n', text: pad(i + 1) }), s.title)));
    const btn = el('button', { class: 'theme-btn', type: 'button', id: 'themeBtn', 'aria-label': '切换主题' }, '主题：' + themeLabels[cur]);
    const bar = el('header', { class: 'bar' },
      el('div', { class: 'wrap' },
        el('a', { class: 'brand', href: 'index.html' }, el('span', { class: 'brand-mark', 'aria-hidden': 'true' }), el('span', { class: 'brand-name', text: '墨水屏监控器 · 阅读器' })),
        tabs, btn));
    const prog = el('div', { class: 'bar-prog', 'aria-hidden': 'true' });
    bar.append(prog);
    document.body.prepend(bar);
    wireBar(bar, btn, prog);
  }
  function wireBar(bar, btn, prog) {
    let cur = store.get('theme', 'system');
    btn.textContent = '主题：' + themeLabels[cur];
    btn.addEventListener('click', () => {
      cur = cur === 'system' ? 'light' : cur === 'light' ? 'dark' : 'system';
      store.set('theme', cur); btn.textContent = '主题：' + themeLabels[cur];
      applyTheme(cur);
    });
    let raf = 0;
    const onScroll = () => {
      if (raf) return;
      raf = requestAnimationFrame(() => {
        raf = 0;
        const h = document.documentElement.scrollHeight - innerHeight;
        prog.style.transform = 'scaleX(' + (h > 0 ? Math.min(1, scrollY / h) : 0) + ')';
      });
    };
    addEventListener('scroll', onScroll, { passive: true }); onScroll();
  }

  /* ---------- sheet header: eyebrow + finding chips ---------- */
  function fillHead() {
    const eb = document.querySelector('.eyebrow[data-auto]');
    if (eb && sheetIdx >= 0) {
      eb.innerHTML = '';
      eb.append('图纸 ', el('b', { text: pad(sheetIdx + 1) }), ' / ' + pad(D.sheets.length) + ' · ' + D.meta.rev + ' · ' + D.meta.status);
    }
    const hc = document.querySelector('.head-chips[data-auto]');
    if (hc) {
      const fs = D.findings.filter(f => f.sheet === sheetId);
      if (!fs.length) hc.remove();
      else fs.forEach(f => {
        const sv = D.severities[f.sev];
        hc.append(el('a', { class: 'sev ' + sv.cls, href: '#' + (f.anchor || ''), 'data-f': f.id, style: 'text-decoration:none' }, f.id + ' ' + sv.label));
      });
    }
    buildSheetCard();
  }

  /* ---------- sheet card: number, counts, review progress ---------- */
  function renderProg(prog, fs) {
    const st = fs.map(f => review.get(f.id).s);
    const done = st.filter(s => s === 'done').length, agree = st.filter(s => s === 'agree').length, dis = st.filter(s => s === 'disagree').length;
    prog.innerHTML = '';
    prog.append(el('span', null, '本页评审已处理', el('b', { text: (done + agree + dis) + ' / ' + fs.length })),
      el('div', { class: 'sh-bar', 'aria-hidden': 'true' },
        el('i', { style: `width:${done / fs.length * 100}%;background:var(--ok)` }),
        el('i', { style: `width:${agree / fs.length * 100}%;background:var(--mask)` }),
        el('i', { style: `width:${dis / fs.length * 100}%;background:var(--crit)` })));
  }
  function buildSheetCard() {
    const head = document.querySelector('.sheet-head');
    if (!head || sheetIdx < 0) return;
    const staticProg = head.querySelector('.sh-card .sh-prog');
    if (head.querySelector('.sh-card')) {
      if (staticProg) {
        const fs = staticProg.dataset.f.split(' ').map(id => D.findings.find(f => f.id === id)).filter(Boolean);
        const r = () => renderProg(staticProg, fs);
        r(); document.addEventListener('review-change', r);
      }
      return;
    }
    const text = el('div', { class: 'sh-text' }, ...head.childNodes);
    head.append(text);
    const main = document.querySelector('.main');
    const secs = main ? main.querySelectorAll('section.sec').length : 0;
    const inter = main ? main.querySelectorAll('.panel, .pinfig').length + [...main.querySelectorAll('.fig-surface')].filter(f => f.querySelector('.hit, .zone')).length : 0;
    const fs = sheetId === 'review' ? D.findings : D.findings.filter(f => f.sheet === sheetId);
    const stat = (k, v) => el('div', null, el('dt', { text: k }), el('dd', { text: String(v) }));
    const card = el('aside', { class: 'sh-card', 'aria-label': '本页概况' },
      el('div', { class: 'sh-no', text: pad(sheetIdx + 1), style: fs.length ? null : 'grid-row: span 1' }),
      el('dl', { class: 'sh-stats', style: fs.length ? null : 'border-bottom:0' }, stat('章节', secs), stat('评审项', fs.length),
        inter ? stat('交互', inter) : stat('表格', main ? main.querySelectorAll('table').length : 0)));
    if (fs.length) {
      const prog = el('div', { class: 'sh-prog' });
      const render = () => {
        const st = fs.map(f => review.get(f.id).s);
        const done = st.filter(s => s === 'done').length, agree = st.filter(s => s === 'agree').length, dis = st.filter(s => s === 'disagree').length;
        prog.innerHTML = '';
        prog.append(el('span', null, '本页评审已处理', el('b', { text: (done + agree + dis) + ' / ' + fs.length })),
          el('div', { class: 'sh-bar', 'aria-hidden': 'true' },
            el('i', { style: `width:${done / fs.length * 100}%;background:var(--ok)` }),
            el('i', { style: `width:${agree / fs.length * 100}%;background:var(--mask)` }),
            el('i', { style: `width:${dis / fs.length * 100}%;background:var(--crit)` })));
      };
      render();
      document.addEventListener('review-change', render);
      card.append(prog);
    }
    head.append(card);
  }

  /* ---------- e-ink style flash when a live readout changes ---------- */
  function watchReadouts() {
    if (!('MutationObserver' in window)) return;
    const hit = n => { n.classList.remove('tick'); void n.offsetWidth; n.classList.add('tick'); };
    const mo = new MutationObserver(ms => {
      const seen = new Set();
      ms.forEach(m => {
        const t = (m.target.nodeType === 1 ? m.target : m.target.parentElement);
        const n = t && t.closest('.ro-value, .verdict');
        if (n && !seen.has(n)) { seen.add(n); hit(n); }
      });
    });
    document.querySelectorAll('.ro-value, .verdict').forEach(n => mo.observe(n, { childList: true, characterData: true, subtree: true }));
  }

  /* ---------- section numbers + rail ---------- */
  function buildRail() {
    const main = document.querySelector('.main');
    const rail = document.querySelector('.rail');
    if (!main) return;
    const secs = [...main.querySelectorAll('section.sec[id]')];
    secs.forEach((s, i) => {
      const h = s.querySelector(':scope > h2');
      if (h && !h.querySelector('.n')) h.prepend(el('span', { class: 'n', text: (sheetIdx + 1) + '.' + (i + 1) }));
    });
    if (!rail) return;
    if (rail.hasAttribute('data-static')) {
      const ol = rail.querySelector('.toc');
      if (ol && 'IntersectionObserver' in window) {
        const links = new Map([...ol.querySelectorAll('a')].map(a => [a.getAttribute('href').slice(1), a]));
        const vis = new Set();
        const io = new IntersectionObserver(es => {
          es.forEach(e => e.isIntersecting ? vis.add(e.target.id) : vis.delete(e.target.id));
          const first = secs.find(s => vis.has(s.id));
          if (first) links.forEach((a, id) => a.setAttribute('aria-current', String(id === first.id)));
        }, { rootMargin: '-15% 0px -65% 0px' });
        secs.forEach(s => io.observe(s));
      }
      const upd = () => rail.querySelectorAll('.rail-findings a[data-f]').forEach(a => {
        const st = review.get(a.dataset.f).s, n = a.querySelector('.st');
        if (n) n.textContent = st !== 'todo' ? ' · ' + D.reviewStates.find(x => x.k === st).label : '';
      });
      upd(); document.addEventListener('review-change', upd);
      return;
    }
    if (secs.length > 1) {
      const ol = el('ol', { class: 'toc' }, secs.map((s, i) => {
        const h = s.querySelector(':scope > h2');
        const t = h ? [...h.childNodes].filter(n => !(n.classList && n.classList.contains('n'))).map(n => n.textContent).join('').trim() : s.id;
        return el('li', null, el('a', { href: '#' + s.id }, el('span', { class: 'n', text: (sheetIdx + 1) + '.' + (i + 1) }), t));
      }));
      rail.append(el('div', { class: 'toc-box' }, el('h4', { text: '本页' }), ol));
      if ('IntersectionObserver' in window) {
        const links = new Map([...ol.querySelectorAll('a')].map(a => [a.getAttribute('href').slice(1), a]));
        const vis = new Set();
        const io = new IntersectionObserver(es => {
          es.forEach(e => e.isIntersecting ? vis.add(e.target.id) : vis.delete(e.target.id));
          const first = secs.find(s => vis.has(s.id));
          if (first) links.forEach((a, id) => a.setAttribute('aria-current', String(id === first.id)));
        }, { rootMargin: '-15% 0px -65% 0px' });
        secs.forEach(s => io.observe(s));
      }
    }
    const fs = D.findings.filter(f => f.sheet === sheetId);
    if (fs.length) {
      const box = el('div', { class: 'rail-findings' });
      const render = () => {
        box.innerHTML = '';
        fs.forEach(f => {
          const st = review.get(f.id).s;
          const sv = D.severities[f.sev];
          box.append(el('a', { href: 'review.html#' + f.id, 'data-f': f.id },
            el('span', { class: 'sev ' + sv.cls, text: f.id }),
            el('span', null, f.title, st !== 'todo' ? el('span', { class: 'muted', text: ' · ' + D.reviewStates.find(x => x.k === st).label }) : null)));
        });
      };
      render();
      document.addEventListener('review-change', render);
      rail.append(el('div', null, el('h4', { text: '本页评审项' }), box));
    }
  }

  /* ---------- pager（v3.8 起去掉了标题栏和页脚说明） ---------- */
  function buildFoot() {
    const slot = document.querySelector('[data-auto-foot]');
    if (!slot || sheetIdx < 0) return;
    const prev = D.sheets[sheetIdx - 1], next = D.sheets[sheetIdx + 1];
    const pager = el('nav', { class: 'pager', 'aria-label': '上一张与下一张图纸' },
      prev ? el('a', { href: prev.file }, el('span', { text: '← ' + pad(sheetIdx) }), prev.title) : null,
      next ? el('a', { class: 'next', href: next.file }, el('span', { text: pad(sheetIdx + 2) + ' →' }), next.title) : null);
    slot.append(pager);
  }

  /* ---------- reference index ---------- */
  const refMap = new Map();
  D.bom.forEach(it => it.refs.forEach(r => refMap.set(r, it)));

  /* ---------- 正文里的位号和评审项 ----------
     v3.7 起不再有悬浮卡片：位号只是标签，不可点击；评审项编号点击跳到评审页。 */
  function enhance(scope) {
    (scope || document).querySelectorAll('a.fx:not([data-bound]), a[data-f]:not([data-bound])').forEach(a => {
      const id = a.dataset.f || a.textContent.trim();
      a.dataset.bound = '1';
      if (!a.getAttribute('href')) a.href = 'review.html#' + id;
      if (a.classList.contains('fx')) a.dataset.state = review.get(id).s;
    });
  }
  document.addEventListener('review-change', () => {
    document.querySelectorAll('a.fx').forEach(a => { a.dataset.state = review.get(a.dataset.f || a.textContent.trim()).s; });
  });

  /* ---------- toast + copy ---------- */
  function toast(msg) {
    const t = el('div', { class: 'toast', role: 'status', text: msg });
    document.body.append(t);
    setTimeout(() => t.remove(), 2200);
  }
  function copyText(text, fallbackHost, okMsg) {
    const fallback = () => {
      if (!fallbackHost) return;
      fallbackHost.innerHTML = '';
      const ta = el('textarea', { readonly: true, 'aria-label': '复制内容' });
      ta.value = text;
      fallbackHost.append(el('p', { class: 'muted', style: 'font-size:.85rem;margin:0 0 6px', text: '浏览器不允许直接写入剪贴板，已选中下面的文字，按 Ctrl+C 复制。' }), ta);
      ta.focus(); ta.select();
    };
    try {
      navigator.clipboard.writeText(text).then(() => toast(okMsg || '已复制'), fallback);
    } catch (e) { fallback(); }
  }

  /* ---------- line chart ---------- */
  const NS = 'http://www.w3.org/2000/svg';
  const sv = (tag, attrs) => { const n = document.createElementNS(NS, tag); for (const k in attrs) n.setAttribute(k, attrs[k]); return n; };
  function lineChart(host, o) {
    const first = !host.dataset.drawn;
    host.dataset.drawn = '1';
    host.innerHTML = '';
    const W = o.width || 640, H = o.height || 260, m = Object.assign({ t: 16, r: 92, b: 36, l: 48 }, o.margin || {});
    const X = v => m.l + (v - o.x.min) / (o.x.max - o.x.min) * (W - m.l - m.r);
    const Y = v => H - m.b - (v - o.y.min) / (o.y.max - o.y.min) * (H - m.t - m.b);
    const svg = sv('svg', { viewBox: `0 0 ${W} ${H}`, role: 'img', 'aria-label': o.label || '' });
    const grid = sv('g', { class: 'grid' }), axis = sv('g', { class: 'axis' });
    o.y.ticks.forEach(v => {
      grid.append(sv('line', { x1: m.l, x2: W - m.r, y1: Y(v), y2: Y(v) }));
      const t = sv('text', { x: m.l - 8, y: Y(v) + 4, 'text-anchor': 'end' }); t.textContent = o.y.fmt ? o.y.fmt(v) : v; axis.append(t);
    });
    o.x.ticks.forEach(v => {
      const t = sv('text', { x: X(v), y: H - m.b + 18, 'text-anchor': 'middle' }); t.textContent = o.x.fmt ? o.x.fmt(v) : v; axis.append(t);
    });
    svg.append(grid);
    svg.append(sv('line', { class: 'base', x1: m.l, x2: W - m.r, y1: Y(o.y.min), y2: Y(o.y.min) }));
    if (o.x.title) { const t = sv('text', { x: W - m.r, y: H - 4, 'text-anchor': 'end', class: 'lbl' }); t.textContent = o.x.title; svg.append(t); }
    if (o.y.title) { const t = sv('text', { x: m.l, y: m.t - 4, class: 'lbl' }); t.textContent = o.y.title; svg.append(t); }
    (o.bands || []).forEach(b => {
      const y0 = Y(Math.min(b.y1, o.y.max)), y1 = Y(Math.max(b.y0, o.y.min));
      svg.append(sv('rect', { class: 'band', x: m.l, y: y0, width: W - m.l - m.r, height: Math.max(0, y1 - y0) }));
      const t = sv('text', { class: 'dl', x: W - m.r + 6, y: (y0 + y1) / 2 + 4 }); t.textContent = b.label; svg.append(t);
    });
    (o.thresholds || []).forEach(th => {
      svg.append(sv('line', { class: 'thr', x1: m.l, x2: W - m.r, y1: Y(th.y), y2: Y(th.y) }));
      const t = sv('text', { class: 'thr-l', x: W - m.r + 6, y: Y(th.y) + 4 }); t.textContent = th.label; svg.append(t);
    });
    svg.append(axis);
    o.series.forEach(s => {
      const d = s.pts.map((p, i) => (i ? 'L' : 'M') + X(p[0]).toFixed(1) + ',' + Y(Math.min(Math.max(p[1], o.y.min), o.y.max)).toFixed(1)).join('');
      svg.append(sv('path', first ? { class: 'line draw', d, pathLength: 1, style: `stroke:${s.color}` } : { class: 'line', d, style: `stroke:${s.color}` }));
      if (s.label !== false) {
        const last = s.pts[s.pts.length - 1];
        const t = sv('text', { class: 'dl', x: X(last[0]) + 6, y: Y(Math.min(last[1], o.y.max)) + 4 }); t.textContent = s.name; svg.append(t);
      }
    });
    (o.markers || []).forEach(mk => {
      svg.append(sv('line', { class: 'xh', x1: X(mk.x), x2: X(mk.x), y1: m.t, y2: H - m.b, 'stroke-dasharray': '2 3' }));
      mk.ys.forEach(p => svg.append(sv('circle', { class: 'dot', cx: X(mk.x), cy: Y(Math.min(p.y, o.y.max)), r: 5, style: `fill:${p.color}` })));
    });
    // hover crosshair
    const xh = sv('line', { class: 'xh', x1: 0, x2: 0, y1: m.t, y2: H - m.b, visibility: 'hidden' });
    const ov = sv('rect', { x: m.l, y: m.t, width: W - m.l - m.r, height: H - m.t - m.b, fill: 'transparent', tabindex: 0, 'aria-label': '移动查看数值' });
    svg.append(xh, ov);
    host.append(svg);
    const tip = el('div', { class: 'pop', hidden: true, style: 'position:absolute;pointer-events:none' });
    host.style.position = 'relative';
    host.append(tip);
    const xs = o.series[0].pts.map(p => p[0]);
    function at(clientX) {
      const r = svg.getBoundingClientRect();
      const vx = (clientX - r.left) / r.width * W;
      let best = 0, bd = Infinity;
      xs.forEach((x, i) => { const dd = Math.abs(X(x) - vx); if (dd < bd) { bd = dd; best = i; } });
      const x = xs[best];
      xh.setAttribute('x1', X(x)); xh.setAttribute('x2', X(x)); xh.setAttribute('visibility', 'visible');
      tip.innerHTML = '';
      tip.append(el('div', { class: 'pop-k', text: (o.x.tipFmt || o.x.fmt || String)(x) }));
      o.series.forEach(s => {
        const p = s.pts[best];
        tip.append(el('div', { style: 'display:flex;gap:8px;align-items:center' },
          el('i', { style: `display:inline-block;width:12px;height:2px;background:${s.color}` }),
          el('b', { class: 'mono', text: (o.y.tipFmt || o.y.fmt || String)(p[1]) }), el('span', { class: 'pop-k', text: s.name })));
      });
      tip.hidden = false;
      const px = X(x) / W * r.width;
      tip.style.left = Math.min(Math.max(px + 12, 0), r.width - tip.offsetWidth) + 'px';
      tip.style.top = '8px';
    }
    ov.addEventListener('pointermove', e => at(e.clientX));
    ov.addEventListener('pointerleave', () => { tip.hidden = true; xh.setAttribute('visibility', 'hidden'); });
    ov.addEventListener('focus', () => { const r = svg.getBoundingClientRect(); at(r.left + r.width / 2); });
    ov.addEventListener('blur', () => { tip.hidden = true; xh.setAttribute('visibility', 'hidden'); });
    if (o.legend) {
      host.append(el('div', { class: 'legend' }, o.series.map(s => el('span', null, el('i', { style: `background:${s.color}` }), s.name))));
    }
  }

  /* ---------- calculator helpers ---------- */
  function bindSeg(seg, cb) {
    const btns = [...seg.querySelectorAll('button')];
    btns.forEach(b => b.addEventListener('click', () => { btns.forEach(x => x.setAttribute('aria-pressed', String(x === b))); cb(b.dataset.v); }));
    const on = btns.find(b => b.getAttribute('aria-pressed') === 'true') || btns[0];
    return on ? on.dataset.v : null;
  }
  function verdict(node, cls, text) { node.className = 'verdict ' + cls; node.textContent = text; }
  const css = name => getComputedStyle(root).getPropertyValue(name).trim();

  /* ---------- hash highlight ---------- */
  function flashHash() {
    const id = decodeURIComponent(location.hash.slice(1));
    if (!id) return;
    const t = document.getElementById(id);
    if (t && t.tagName === 'TR') { t.classList.remove('flash'); void t.offsetWidth; t.classList.add('flash'); }
  }

  window.APP = { D, store, el, review, enhance, toast, copyText, lineChart, bindSeg, verdict, css, sheetFile, flashHash, refMap };

  document.addEventListener('DOMContentLoaded', () => {
    buildBar(); buildRail(); fillHead(); buildFoot();
    enhance();
    watchReadouts();
    window.addEventListener('hashchange', flashHash);
  });
})();
