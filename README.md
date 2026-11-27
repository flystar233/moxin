# 墨水屏阅读器固件

运行在 ESP32-S3 和 4.26 寸墨水屏上的阅读器固件，屏是 GDEY0426T82（SSD1677，800×480）。它能读 TXT 和 EPUB，用语音朗读，桌面上有时钟、便签、温湿度和 CO₂，电池供电时自动进入浅睡或深睡。本仓库只有固件，硬件设计不在这里。

版本 `0.3.0`，用 PlatformIO 和 Arduino 框架开发，下面的命令都在仓库根目录运行。

```
pio run                                    # 编译
pio run -t upload --upload-port COM6       # 整片烧录
pio run -e pcb1_serial -t upload           # 串口升级，板子跑着本固件时用
pio device monitor -b 115200
```

根目录的 `bootloader.bin`、`partitions.bin`、`firmware.bin` 是同一次编译的产物，空板分别写到 `0x0`、`0x8000`、`0x10000`。

**整片烧录时要按住电源键或短接 R33。** 烧录器复位芯片时，GPIO4（PWR_HOLD）会被放开，板子跟着断电。空板第一次烧录前先焊上 R33，确认开关机正常后可以去掉。去掉之后就只能用串口升级。

---

## 整体结构

```
主循环 app::loop()
  ├─ 收串口命令、重试失联的加速度计
  ├─ 从事件队列取按键 / 敲击 / 姿态事件，交给栈顶界面
  ├─ tts::tick()            朗读推进（取句、自动翻页）
  ├─ 传感器轮询、重画（脏标记）
  └─ maybeSleep()           判断浅睡 / 深睡
```

外设都跑在主循环里，只有按键、加速度计、TTS 合成、TTS 播放是独立任务或线程。墨水屏不能连续刷新，所以整个架构是“事件驱动 + 脏标记重画”：任何界面改动只登记刷新级别，下一次循环统一画一次。

## 界面栈

界面都是 `View` 子类，只实现两个方法：`draw()` 画自己，`onKey()` 处理按键。`src/app/app.cpp` 维护一个栈：

- 最底层是根界面，阅读用 `newReaderView`，桌面用 `newDeskView`
- 上面叠菜单、目录、提示框、确认框
- 关闭的界面先进 tombstone 队列，本轮结束再 `delete`。界面有可能在自己的 `onKey` 里被关掉，所以要延后删

重画用 `app::invalidate(Refresh)` 登记，级别取最高的：

| 级别 | 何时用 |
|---|---|
| `Partial` | 光标移动、数字变化、翻页 |
| `Full` | 切换界面、切换模式、弹窗关闭 |
| `Clean` | 电源键短按、残影明显时 |

`render()` 决定这一帧怎么刷。累计局刷次数超过当前界面的 `fullEvery()`，就升成全刷。低温下快速全刷的短波形驱动不到位，全刷会自动降级成更慢的标准波形。另外每刷 N 次快刷，强制做一次标准全刷。

## 省电与深睡

用电池时才睡，插着 USB 不睡，方便烧录和调试。

**浅睡**：停掉加速度计采样，配好唤醒源，然后调用 `esp_light_sleep_start()`。内存、PSRAM、屏帧缓存都保留，唤醒只要几毫秒，所以可以每 `SLEEP_TICK_MS` 醒一次，处理时钟和敲击配对。唤醒源有按键、USB 插入、加速度计 INT2（敲击）和定时器。卡住的按键不能当唤醒源，因为它一直是低电平，刚睡下就会醒。

**深睡**：内存全部丢失。睡前把最后一帧画的是什么状态存进 RTC 内存（`Resume` 结构）。醒来按它重画一遍，再调用 `assumeShown()`，把这帧当作局刷基准，否则每次醒来都要闪一次全刷。桌面模式每小时定时醒来更新时钟，阅读模式只在按键时醒。唤醒时丢失了按键事件，所以醒来后会把触发唤醒的那个按键补发一次短按。

深睡前必须调用 `ota::confirm()`，否则 bootloader 会认为固件没跑稳，回退到旧分区。

## 显示与刷新

`EpdPanel` 是驱动抽象层，继承 `Adafruit_GFX`，界面代码只用这组接口：

```cpp
refreshFull()       // 全刷
refreshClean()      // 标准全刷，清残影
refreshPartial()    // 局刷，只驱动变了的像素
forcePartialRect()  // 屏上内容未知时，下一帧先完整重刷一个矩形
assumeShown()       // 认定屏上当前就是缓冲区内容
```

SSD1677 的局刷用控制器自带的比较功能：0x24 写新帧，0x26 写旧帧，芯片自己算差异，耗时约 0.6 秒。换屏时只需新增一个子类，再改 `config.h` 里的 `EPD_MODEL`。

界面按 400×300 的小屏写死，画的时候用 `gfx::px()` 乘 1.6，所以所有界面都能适配大屏。字体是点阵的，分 `Ui`、`Small`、`Value`、`Clock` 几个槽位。大屏上用固件内置的对应字号，没有的字回落到已加载的正文字库。

## 阅读器

阅读器是代码量最大的一块，分三步。

**（1）导入**（`import.cpp`）

只在书第一次打开时做。TXT 自动判断编码是 UTF-8、GBK 还是 UTF-16，再转成 UTF-8；EPUB 用 tinfl 解压，取出纯文字。结果和章节信息写入卡上的 `/.reader/<hash>.txt` 和 `.bkm`。文件大小、修改时间或开头内容变了，就重新导入。

**（2）排版分页**（`layout.cpp`）

结果按版面缓存。排版器吃一个 `TextSpan`，也就是内存里的一段正文，不要求整本在内存。分页结果是一串页起点的字节位置，按版面签名存成 `.pg`。换回用过的字号时直接读缓存，不用重排。

**（3）阅读窗口**（`window.cpp`）

正文很大时不能整本加载，`TextWindow` 只保留阅读位置附近的一段：

- 整本不超过 4.5MB 就整本加载，窗口退化为普通指针
- 超过 4.5MB 就开一个全书 20% 大小的窗口，下限 1MB，上限 4.5MB。翻出窗口时从卡上重读
- 新旧窗口重叠的部分在内存里用 `memmove` 搬，只从卡上读新的一半。顺着往后读时，开销减半

进度按正文中的字节偏移记录，和版面无关，所以换字号后还停在原来的位置。

## 语音朗读（`src/voice/`）

两个模型：**Edge 朗读**（免费，微软 Edge 的在线朗读接口）和 **qwen-audio-3.1-tts-plus**（阿里云百炼，要 Key，音质更好）。都是一句一次请求，边收边播。

```
主线程 tts::tick()     按句切分 → 排队 → 跟随播放位置自动翻页
合成任务              逐句请求 → 解出 PCM → 写入环形缓冲
播放任务              从环形缓冲取数据 → I2S（24kHz、16bit、单声道）
```

- `sentence.cpp`：按 `。！？…；` 切句，引号算在句末。没有标点的诗在换行处切，太长的句子在逗号处断
- `tts_client.cpp`：Edge 走 WebSocket（`speech.platform.bing.com`），整个朗读会话复用一条连接；plus 走 `SpeechSynthesizer`，HTTP 流式返回 PCM
- `edge_drm.cpp`：算 Edge 的 `Sec-MS-GEC` v2 令牌（CS64 + 改过初始状态的 SHA-256，按 5 分钟一档），设备时钟必须准
- Edge 只输出 MP3/Opus，所以 `tts_client.cpp` 里用 libhelix 把 MP3 解成 PCM（`DevLab_LibHelix`）
- `sse_pcm.cpp`：plus 的 SSE 字节流里增量查找 base64 音频，剥掉 WAV 头。查找不依赖块边界，数据跨块也能找到
- 流里每句 PCM 的字节区间都会记下来，所以暂停时“正在读哪句”也是准的

环形缓冲能存约 3～7 秒音频，后面的一两句在后台提前合成。朗读期间 Wi-Fi 常开，CPU 提到 240MHz（TLS 和 MP3 解码要用），读完回落。

Edge 朗读不需要 Key，但要求设备时间正确（开机自动联网对时）。plus 需要百炼账号、API Key 和业务空间 ID，填一次存在 NVS 里；没有这些也不影响 Edge。

## 加速度计（`src/input/motion.cpp`）

SC7A20H，采样任务以 200Hz 运行，做两件事：

- **姿态**：低通滤波后看哪个轴朝上，映射成竖拿、横放、倒置。轴到机身方向的映射可以在菜单里校准，存进 NVS。倒置时按键会自动对调
- **敲击**：用冲击加静默两个窗口识别双击。检测在加速度计里完成，不靠 MCU 软件采样，深睡时也能靠中断锁存，省电

## Wi-Fi（`src/net/`）

平时关闭，用完就关。

- **配网**：`wifi.cpp` 开 SoftAP 并劫持 DNS，在网页里选 SSID、填密码，连上后顺便对时
- **传输**：`transfer.cpp` 起 HTTP 服务（`WebServer`），浏览器里可以传书、传字库、上传固件、填 TTS Key
- **对时**：每天第一次有机会时在后台走 NTP。失败后这一小时不再重试，电量低于 3.5V 时不对时

Wi-Fi 使用期间 `net::busy()` 返回 true，主循环据此禁止进入睡眠。

## 升级（`src/core/ota.cpp`）

分区表把 16MB flash 分成两个 app 分区。串口、网页、联网三种升级方式走同一套逻辑：

1. 写入**另一个**分区
2. 做 SHA256 校验
3. 切换启动分区，重启

新固件第一次启动时处于**试运行**状态，30 秒后或者关机、深睡前会自动调用 `ota::confirm()` 确认。确认之前如果重启过（崩溃、按 RESET、断电），bootloader 就回退到原分区。所以升级后 30 秒内不要按 RESET。

串口升级由 `xfer.cpp` 实现：按 16384 字节分块传，每块回执，最后整体校验 CRC32。电脑端是 `tools/fwupdate.py`。

## 传书

两条路，都不用读卡器：

- **USB（不用 Wi-Fi）**：`python tools/usbput.py`，浏览器打开 `http://127.0.0.1:8765/`，把 TXT / EPUB 拖进去。本机的小服务走 USB 串口，按 `fput` 协议写卡（16KB 一块、每块回执、整文件 CRC32），和设备自带的无线传输是同一套底层协议。设备插着 USB、跑着本固件就能用。
- **Wi-Fi**：设备上进 系统设置 → Wi-Fi → 无线传输，屏幕会显示网址和二维码；浏览器里能传书、传字库、传固件，也能改朗读设置。
- 命令行版是 `tools/tfcard.py`（`put / ls / rm / df`），适合批量或写脚本。

只收 `.txt`、`.epub`（字库是 `.efnt`），中文文件名可以；USB 传的时候别拔线，设备要是浅睡了工具会自动重试。

## TF 卡

FAT32 格式。

```
/books/            TXT、EPUB，可以放两层子目录，根目录下的也认
/fonts/            .efnt 点阵字库
/.reader/          固件生成的阅读缓存，不要手动改
```

字库用 `tools/makefont.py` 从 TTF 生成，按字号自动选单色或超采样渲染。固件内置的界面字库和生成参数见 [`data/FONTS.md`](data/FONTS.md)。

## 代码结构

```
src/
  main.cpp      启动流程 → app::begin()
  config.h      引脚、地址、默认值
  board/        电源与深睡、I2C、RTC、电量计、CO₂、温湿度、TF 卡
  display/      墨水屏驱动（EpdPanel 抽象）
  input/        按键任务、加速度计任务
  text/         UTF-8、GBK 表、点阵字库
  reader/       导入、分章、排版、阅读窗口、书库、进度
  voice/        切句、流式 PCM、TTS 客户端、会话调度
  ui/           gfx 画图层、各界面
  app/          界面栈、事件分发、刷新策略、省电、串口命令
  core/         设置（NVS）、文件传输、OTA
data/           嵌入固件的字库
art/            关机画面原图
tools/          字库、传文件（USB 网页 / 命令行）、截图、发版、测试脚本
```

`src/reader/`、`src/voice/sentence.cpp` 和 `src/voice/sse_pcm.cpp` 是纯 C++，不依赖 Arduino，可以直接在电脑上编译测试：

```
g++ -O2 -std=c++17 -Isrc -Itools/readertest tools/readertest/main.cpp \
    src/reader/{import,chapters,layout,window,epub}.cpp src/text/decode.cpp -lz -o readertest
./readertest 书.txt

cl /nologo /utf-8 /EHsc /std:c++17 /Isrc tools\ttstest\main.cpp src\voice\sentence.cpp src\voice\sse_pcm.cpp
```

改阅读器或朗读代码后，先跑对应的测试。
