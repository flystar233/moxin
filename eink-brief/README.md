# 墨水屏阅读器 · 网站

产品网站：前 6 页是给所有人看的产品介绍（阅读器是主要功能，桌面状态屏是第二种用法），“资料”下面是给工程师的工程资料。数字、表格和图从 `../eink-docs`（设计笔记）和 `../enclosure`（外壳 3D 模型）自动生成，整个目录是纯静态页面，直接放到任何静态网站托管上就行。

## 页面

| 页面 | 内容 |
|---|---|
| `index.html` 首页 | 竖拿看书的主图、四个数字、阅读 / 朗读 / 桌面三张卡片、设计入口 |
| `reader.html` 阅读 | 书库与 TF 卡、单手翻页、朗读、离线与进度同步 |
| `desk.html` 桌面 | Claude Code 状态、电脑负载、时间、CO2 与温湿度 |
| `design.html` 设计 | 分层爆炸图、电路板、细节、系统架构、设计历程 |
| `specs.html` 规格 | 技术规格表 |
| `docs.html` 资料 | 工程资料入口 |
| `electrical.html` · `mechanical.html` · `case.html` · `bom.html` · `test.html` | 工程资料：电气、结构、外壳制造、器件清单（附嘉立创 `bom.csv`）、打样测试 |

网站页的版式在 `assets/site.css`，内容在 `src/<页面>.html`，页面列表在 `tools/build.js` 的 `SITE`；工程资料页沿用设计笔记的 `style.css`。

## 更新

改完 `eink-docs` 或 `enclosure` 之后，在本目录运行：

```
node tools/build.js
```

- 数字、表格和图都从 `eink-docs` 读：`assets/data.js`、`assets/board-data.js`（外壳尺寸在它的 `CASE` 里）、`tools/*.js`，以及布局页、概览页里的几张表。
- 外壳制造页的下载文件（STL、DXF、SCAD）构建时从 `../enclosure` 复制到 `files/case/`。
- 图在 `assets/img/studio-*.png`，用设计笔记的 3D 视图出：`tools/studio.html` 按机位摆好相机，`tools/shoot.js` 用 Edge 无头截图（透明背景），`tools/trim.py` 裁掉空白边。外壳或布局改了以后重出：

  ```
  node tools/shoot.js              # 全部 9 张，也可以只写 reader / readerf / hero / front / explode / pcb / back / keys / usb
  python tools/trim.py assets/img/studio-*.png   # 要 pymupdf
  ```

  机位（相机角度、距离、外壳显示、爆炸量、曝光）写在 `shoot.js` 的 SHOTS 里。

- 本目录只维护 `src/*.html` 里的说明文字和页面结构。说明文字里写死的数值（外形尺寸、电气参数等）不会自动跟着变，改了设计要顺手核对。
- 设计笔记里的修订标记（v4.x）、评审编号（F1 等）和"见某页"的互引，生成时会自动去掉。
- 上线时把整个目录（除 `src`、`tools`）传到静态托管即可，页面不依赖 `eink-docs` 和 `enclosure`。字体从 Google Fonts 加载，国内访问慢的话可以把字体文件放到本地。
