# 墨水屏阅读器固件（ESP32-S3）

v0.3.0 · 2026-11-23

ESP32-S3 上的墨水屏阅读器：看书、桌面时钟/环境显示、语音朗读、Wi-Fi 传书和升级。

## 硬件

- 主控 ESP32-S3-WROOM-1-N16R8，屏 4.26 寸 GDEY0426T82（SSD1677，800×480）。
- 外设：加速度计、RTC、电量计、CO₂+温湿度、功放、TF 卡。
- 引脚和地址见 [`src/config.h`](src/config.h)。`EPD_MODEL` 可切回旧的 4.2 寸测试屏。

## 功能

**两种模式**：竖拿是阅读，横放是桌面；倒着拿时画面转 180°，翻页键对调。

**阅读**
- 读 TF 卡上的 TXT 和 EPUB（`/books/` 或根目录，最多两层子目录）。
- TXT 自动识别 UTF-8 / GBK / UTF-16；EPUB 只取文字。
- 自动识别章节、按目录跳转；排版处理缩进、两端对齐、标点避行首。
- 第一次打开会导入并排版，结果缓存到卡上 `/.reader/`，之后直接读，最多缓存 30 本。
- 阅读进度按正文位置记录，换字号不会丢失。

**桌面**
- 日期、农历、节气、电量、CO₂/温度/湿度、大时钟、在读进度。
- 没有 CO₂ 传感器时不显示右栏。

**字体**：正文用卡上 `/fonts/` 的点阵字库（默认霞鹜文楷 24 号），可换字体、字号、行距、页边距。界面用固件内置的霞鹜新晰黑，见 [`data/FONTS.md`](data/FONTS.md)。

**语音朗读**：见下文。

**省电**：用电池时自动浅睡；空闲超时后深睡，按键唤醒。插着 USB 不睡。电压过低会提示后关机。

## 按键

| 键 | 短按 | 长按 |
|---|---|---|
| 下一页 | 下一页 / 菜单下移 | 下一章 |
| 上一页 | 上一页 / 菜单上移 | 回本章开头，已在开头则上一章 |
| 菜单 | 打开菜单（菜单里是确认） | 阅读时打开目录；桌面时切到阅读 |
| 电源 | 全屏刷新 | 3 秒开机 / 关机 |

双击后背可以翻页（可在设置里关），朗读时是暂停。翻页翘板按中间会同时触发两个键，固件会忽略，按两端即可。

## 菜单

- **阅读时**：目录、语音朗读、朗读设置、跳转进度、书库、阅读设置、系统设置、切到桌面。
- **桌面时**：继续阅读、书库、系统设置。
- **阅读设置**：字体、字号、行距、页边距、全刷间隔、双击后背翻页。
- **朗读设置**：音色、音量、自动翻页、API Key、试听。
- **系统设置**：横放切桌面、自动休眠、设置时间、姿态校准、全屏刷新、关于本机、恢复默认。

## 语音朗读

调用阿里云百炼的 TTS，按句流式合成、边下边播，朗读时自动翻页。

| 模型 | 价格（元/万字符） | 说明 |
|---|---|---|
| `qwen3-tts-flash`（默认） | 0.8 | 7 个音色，够用 |
| `qwen-audio-3.1-tts-plus` | ≈1.44 | 音质更好，需业务空间 ID |

**配置**：系统设置 → Wi-Fi → 无线传输，在网页里填 API Key（`sk-` 开头）。也可以用串口：

```
tts key sk-xxxxxxxx
tts model plus|flash
tts ws llm-xxxxxxxx        # plus 用，tts ws clear 清除
```

**使用**：阅读时按菜单 → 语音朗读。上一页/下一页键短按切换句子，长按切换章节；长按菜单键停止。

**实现**（`src/voice/`）：`sentence.cpp` 切句，`tts_client.cpp` 逐句 HTTP 请求直接取 PCM，`sse_pcm.cpp` 解析 SSE/base64/WAV，`tts.cpp` 负责排队、PSRAM 环形缓冲和自动翻页。朗读期间 Wi-Fi 保持开启。

**注意**
- 按字符计费，一页约 3.6 分。同一句来回翻会重复请求。
- 只在 2.4GHz Wi-Fi 下工作，网络慢时句子之间会停顿。
- 用电池朗读很费电。
- 没有 API Key 时朗读不可用，其他功能不受影响。

先在电脑上验证 Key 和音频格式：

```
python tools/tts_probe.py sk-xxxxxxxx
```

## TF 卡

FAT32 格式。

```
/books/   TXT、EPUB，可放两层子目录；根目录下的也认
/fonts/   点阵字库，如 wenkai_18.efnt
```

没有读卡器时可以用串口传文件：`python tools/tfcard.py put 小说.txt /books/`。

## 编译与烧录

需要 PlatformIO，命令都在仓库根目录运行。

```
pio run                                              # 编译
pio run -e pcb1 -t upload --upload-port COM6         # 整片烧录
pio run -e pcb1_serial -t upload --upload-port COM6  # 串口升级
pio device monitor -b 115200                         # 串口
```

仓库根目录的 `bootloader.bin`、`partitions.bin`、`firmware.bin` 是同一次编译的产物，空板可用 esptool 按 `0x0` / `0x8000` / `0x10000` 一次写入。

> **整片烧录**要按住电源键或短接 R33，否则复位瞬间断电。空板第一次烧录前需焊 R33；确认开关机正常后可去掉——之后只能用串口升级。串口升级不需要碰 R33 和电源键。

**三种升级方式**（串口、网页、联网）都写进另一个程序区，校验通过才切换，失败会保留原固件。升级后新固件试运行 30 秒才确认，**期间不要按 RESET**，否则自动回退。

**联网升级**：设备上手动点"检查新版本"才联网。
1. 把 `src/config.h` 的 `OTA_URL` 改成自己的 HTTPS 地址（**默认是作者的服务器，自行发布务必替换**）。
2. 改 `FW_VERSION` → `pio run` → `python tools/release.py --notes "说明"`，生成 `dist/` 下的 bin 和 version.json。
3. 先传 bin，再传 version.json。

### 串口命令

115200，输入 `help` 看全部。

| 命令 | 作用 |
|---|---|
| `n` `p` `m` | 模拟下一页 / 上一页 / 菜单 |
| `shot` | 截图，用 `python tools/shot.py COM6 out.png` 保存 |
| `book`、`toc`、`goto 50` | 列书、看目录、跳到 50% |
| `font wenkai 18` | 换字库 |
| `info`、`set` | 状态、当前设置 |
| `deep 15` | 立即深睡，15 秒后醒 |
| `time 2026-11-10 21:30:00` | 对时 |
| `pf 20 10` | 调局刷波形 |
| `beep 1000 500 30`、`scale`、`sweep` | 测喇叭 |
| `tts`、`tts play`、`tts stop` | 朗读状态、开始、停止 |
| `tts say 一段文字` | 直接合成试听 |
| `wifi`、`xfer` | 配网、无线传输 |

## Wi-Fi

平时关闭，只在需要时打开：

- **配网**：设备开 `InkReader-xxxx` 热点，扫码或访问 192.168.4.1 选 Wi-Fi（只支持 2.4GHz）。
- **无线传输**：浏览器打开设备地址，可传书、传字库、传 firmware.bin 升级、填 TTS Key。
- **自动对时**：每天联网对时一次，电池低于 3.5V 不对时。

代码在 `src/net/`。

## 维护

**做字库**：`python tools/makefont.py 字体.ttf --sizes 14 16 18 20 --name wenkai --out 目录`。默认按字号自动选渲染方式（≤16 用 mono，≥18 用超采样），`--thr` 调粗细。需要 pillow、fonttools。

**关机画面**：替换 `art/shutdown.jpg` 后运行 `python tools/art_from_image.py art/shutdown.jpg 预览目录`，生成 `src/ui/art_shutdown.h`。文字位置在脚本开头的 `LAND_TEXT` / `PORT_TEXT`。

**电脑上跑测试**：改阅读器或朗读代码后先跑对应的测试。

```
# 阅读器：导入、分章、分页、阅读窗口
g++ -O2 -std=c++17 -Isrc -Itools/readertest tools/readertest/main.cpp src/reader/import.cpp \
    src/reader/chapters.cpp src/reader/layout.cpp src/reader/window.cpp src/reader/epub.cpp \
    src/text/decode.cpp -lz -o readertest
./readertest 书.txt

# 语音：切句、SSE/WAV 解析
cl /nologo /utf-8 /EHsc /std:c++17 /Isrc tools\ttstest\main.cpp src/voice/sentence.cpp src/voice/sse_pcm.cpp
```

## 代码结构

```
src/
  main.cpp   开机流程
  config.h   引脚、地址、默认值
  board/     电源、I2C、RTC、电量计、CO₂、TF 卡
  display/   墨水屏驱动（EpdPanel 统一接口）
  input/     按键任务、加速度计任务
  text/      UTF-8、GBK、点阵字库
  reader/    导入、分章、排版、阅读窗口、书库和进度
  voice/     切句、流式 PCM、TTS 客户端和会话
  ui/        画图、列表、各界面
  app/       界面栈、按键分发、刷新策略、省电、串口命令
  core/      设置（NVS）、串口传文件
data/        嵌入固件的字库
art/         关机画面原图
tools/       字库、传文件、截图、发版、测试脚本
```

界面是一个栈：最底层是根界面，上面叠菜单、目录、提示框。每个界面只实现 `draw()` 和 `onKey()`，需要重画时调用 `app::invalidate()`。全刷、局刷、休眠的统一决策在 `src/app/app.cpp`。深睡醒来内存已清空，所以睡前把最后一帧的状态存进 RTC 内存，醒来据此重画作为局刷基准，避免闪一次全刷。

## 已知限制

- 深睡后需要按键唤醒，还没做拿起自动切换。
- 朗读不支持调速。
- 未实现电脑状态、Claude Code 状态显示。
