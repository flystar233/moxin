// 展示页的 3D 出图：用 Edge 无头模式打开 tools/studio.html，按下面每个机位截一张透明背景的 PNG 到 assets/img/。
// 用法：node tools/shoot.js [名字…]（不写名字就全部重出）。需要 Windows 自带的 Edge，第一次要联网加载 three.js。
const { spawnSync } = require('child_process');
const fs = require('fs'), path = require('path'), os = require('os');

const root = path.resolve(__dirname, '..');
const EDGE = ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe'].find(p => fs.existsSync(p));
if (!EDGE) throw new Error('找不到 Edge');
const studio = 'file:///' + path.join(__dirname, 'studio.html').replace(/\\/g, '/');
const out = path.join(root, 'assets', 'img');
fs.mkdirSync(out, { recursive: true });

// 相机：th 方位（0 = 从按键那条边看）、ph 天顶角（0 = 从元件面正上方，π = 从屏幕正面）、r 距离，look = [板子 x, 高度, 板子 y]
const all = { parts: true, labels: true, zones: false, keep: false, screen: true, batt: true, fpc: true };
const SHOTS = {
  reader:  { size: [1100, 1500], cfg: { exposure: 1.15, th: Math.PI / 2 - 0.42, ph: Math.PI - 0.72, r: 330, fov: 22, upv: [-1, 0, 0], light: 'screen', screen: 'reader', shell: 'solid', layers: all, look: [52.5, -1, 31] } },
  readerf: { size: [1000, 1500], cfg: { exposure: 1.15, th: Math.PI / 2, ph: Math.PI - 0.02, r: 300, fov: 22, upv: [-1, 0, 0], light: 'screen', screen: 'reader', shell: 'solid', layers: all, look: [52.5, 0, 31] } },
  hero:    { size: [1600, 1000], cfg: { exposure: 1.15, th: Math.PI - 0.55, ph: Math.PI - 0.95, r: 285, fov: 22, up: -1, light: 'screen', shell: 'solid', layers: all, look: [52.5, -1, 31] } },
  front:   { size: [1600, 1000], cfg: { exposure: 1.15, th: Math.PI, ph: Math.PI - 0.02, r: 250, fov: 22, up: -1, light: 'screen', shell: 'solid', layers: all, look: [52.5, 0, 31] } },
  explode: { size: [1600, 1300], cfg: { th: -0.85, ph: 1.28, r: 400, exposure: 0.9, fov: 22, shell: 'solid', explode: 1, layers: all, look: [52.5, 3, 31] } },
  pcb:     { size: [1600, 1050], cfg: { th: -0.5, ph: 0.82, r: 265, exposure: 1.05, fov: 22, shell: 'off', layers: Object.assign({}, all, { screen: false, fpc: false }), look: [52.5, 1, 31] } },
  back:    { size: [1600, 1000], cfg: { th: -0.62, ph: 0.95, r: 285, exposure: 0.85, fov: 22, shell: 'solid', layers: all, look: [52.5, 2, 31] } },
  keys:    { size: [1700, 620], cfg: { exposure: 0.95, th: -0.1, ph: Math.PI / 2 - 0.17, r: 158, fov: 20, shell: 'solid', layers: all, look: [52.5, 2.4, 64] } },
  usb:     { size: [1400, 900], cfg: { th: Math.PI / 2 - 0.3, ph: Math.PI - 1.38, r: 85, fov: 22, up: -1, light: 'screen', shell: 'solid', layers: all, look: [110, 2.2, 46] } }
};

const want = process.argv.slice(2).length ? process.argv.slice(2) : Object.keys(SHOTS);
for (const name of want) {
  const s = SHOTS[name];
  if (!s) { console.log('没有这个机位：', name); continue; }
  const file = path.join(out, 'studio-' + name + '.png');
  try { fs.unlinkSync(file); } catch (e) { /* 第一次出图 */ }
  const prof = fs.mkdtempSync(path.join(os.tmpdir(), 'studio-'));
  const args = ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--allow-file-access-from-files', '--hide-scrollbars',
    '--default-background-color=00000000', '--force-device-scale-factor=1.5', '--virtual-time-budget=15000',
    `--window-size=${s.size[0]},${s.size[1]}`, `--user-data-dir=${prof}`, `--screenshot=${file}`, studio + '#' + encodeURIComponent(JSON.stringify(s.cfg))];
  spawnSync(EDGE, args, { timeout: 120000 });
  // Edge 有时在后台进程里才把图写完，等一会儿
  for (let i = 0; i < 60 && !fs.existsSync(file); i++) spawnSync(process.execPath, ['-e', 'setTimeout(()=>{},1000)']);
  console.log(fs.existsSync(file) ? 'shot' : 'FAILED', name, fs.existsSync(file) ? fs.statSync(file).size : '');
}
