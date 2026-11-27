# 固件里嵌的界面字体

| 文件 | 字体 | 字号 | 收的字 | 用途 |
|---|---|---|---|---|
| `ui_big.efnt` | 霞鹜新晰黑 LXGW Neo XiHei v1.309 | 26 | GB2312（约 8100 字） | 菜单、标题、提示框 |
| `ui_small.efnt` | 同上 | 20 | 同上 | 页眉页脚、说明文字 |
| `ui_han_20.efnt` / `ui_han_26.efnt` | Noto Sans SC | 20 / 26 | 对应主字库缺少的统一汉字和常用标点等 | 界面补字，开机即可用 |
| `desk_clock.efnt` | Barlow Condensed SemiBold | 224 | `0123456789:-` | 桌面大时钟 |
| `desk_num54.efnt` | Barlow Condensed Medium | 54 | `0123456789.:%-+` | 桌面读数、跳转进度、设置时间 |
| `desk_num34.efnt` | 同上 | 34 | 同上 | 桌面读书进度 |
| `desk_num24.efnt` | 同上 | 24 | 同上 | 桌面电量 |
| `desk_note.efnt` | 霞鹜新晰黑 | 60 | `还没对时` | 桌面没对时的大字 |

大屏界面先用主字库，再用同字号的固件补字。两者都缺的字仍可由已加载的正文字库补齐。

## 重新生成

`tools/makefont.py`（要 pillow、fonttools）。Barlow 的 TTF 放在 `tools/fonts/`；霞鹜新晰黑太大（7MB）没放进来，
从 https://github.com/lxgw/LxgwNeoXiHei/releases 下载 `LXGWNeoXiHei.ttf`。

```
python tools/makefont.py LXGWNeoXiHei.ttf --sizes 20 26 --name ui --gb2312 --out 输出目录
python tools/makefont.py tools/fonts/BarlowCondensed-SemiBold.ttf --sizes 224 --name clock --mode ss --chars "0123456789:-" --out 输出目录
python tools/makefont.py tools/fonts/BarlowCondensed-Medium.ttf --sizes 54 34 24 --name num --mode ss --chars "0123456789.:%-+" --out 输出目录
python tools/makefont.py LXGWNeoXiHei.ttf --sizes 60 --name note --mode ss --chars "还没对时" --out 输出目录
```

生成的文件按上表改名放到这里。

补字用 Noto Sans SC 2.04。Windows 11 的字体可直接用 `C:/Windows/Fonts/NotoSansSC-VF.ttf`，也可从 [Noto CJK](https://github.com/notofonts/noto-cjk) 获取。重新生成补字之前，先把主字库放在 `data/` 下。

```
python tools/makefont.py C:/Windows/Fonts/NotoSansSC-VF.ttf --sizes 20 --name ui_han --missing-from data/ui_small.efnt --mode aa --out data
python tools/makefont.py C:/Windows/Fonts/NotoSansSC-VF.ttf --sizes 26 --name ui_han --missing-from data/ui_big.efnt --mode aa --out data
```

## 许可

- LXGW Neo XiHei 由 LXGW 制作，以 SIL Open Font License 1.1 发布（https://github.com/lxgw/LxgwNeoXiHei）。
- Barlow 由 The Barlow Project Authors 制作，以 SIL Open Font License 1.1 发布（https://github.com/jpt/barlow，许可全文见 `tools/fonts/Barlow-OFL.txt`）。
- 固件补字基于 Noto Sans SC，以 SIL Open Font License 1.1 发布，许可全文见 `tools/fonts/NotoSansSC-OFL.txt`。

OFL 允许把字体嵌入软件、和软件一起分发；转换后的点阵文件同样遵守 OFL，不能单独出售。
