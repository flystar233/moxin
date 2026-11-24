// 主题切换：跟随系统 → 浅色 → 深色，存在本机浏览器里（和设计笔记共用同一个设置）
(function () {
  'use strict';
  const root = document.documentElement, btn = document.getElementById('themeBtn');
  const labels = { system: '跟随系统', light: '浅色', dark: '深色' };
  const get = () => { try { return JSON.parse(localStorage.getItem('eink.theme')) || 'system'; } catch (e) { return 'system'; } };
  const set = v => { try { localStorage.setItem('eink.theme', JSON.stringify(v)); } catch (e) { /* 隐私模式下存不了，只在本页生效 */ } };
  let cur = get();
  const apply = () => {
    if (cur === 'light' || cur === 'dark') root.dataset.theme = cur; else delete root.dataset.theme;
    if (btn) btn.textContent = '主题：' + labels[cur];
  };
  apply();
  if (btn) btn.addEventListener('click', () => { cur = cur === 'system' ? 'light' : cur === 'light' ? 'dark' : 'system'; set(cur); apply(); });
  // 当前页签滚到可见处（窄屏时页签横向滚动）
  const tabs = document.querySelector('.tabs'), on = tabs && tabs.querySelector('[aria-current]');
  if (on) { const o = on.offsetLeft - tabs.offsetLeft + on.offsetWidth - tabs.clientWidth; if (o > 0) tabs.scrollLeft = o + 40; }
})();
