# 墨水屏阅读器固件

ESP32-S3 + 4.26 寸墨水屏（GDEY0426T82，SSD1677，800×480）。本仓库只有固件，硬件设计不在其中。

版本 `0.3.0`，PlatformIO + Arduino 框架，命令都在仓库根目录运行。

```
pio run                                    # 编译
pio run -t upload --upload-port COM6       # 整片烧录
pio run -e pcb1_serial -t upload           # 串口升级（板子在跑本固件时）
pio device monitor -b 115200
```

根目录的 `bootloader.bin` / `partitions.bin` / `firmware.bin` 是同一次编译的产物，空板按 `0x0` / `0x8000` / `0x10000` 写入。

**整片烧录要按住电源键或短接 R33**：烧录器复位芯片时 GPIO4（PWR_HOLD）会放开导致断电。空板第一次烧录前焊上 R33，确认开关机正常后可去掉，之后只能用串口升级。

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

所有外设都跑在主循环里，只有按键、加速度计、TTS 合成、TTS 播放是独立任务/线程。墨水屏不能连续刷，所以**整个架构是"事件驱动 + 脏标记重画"**，任何界面改动只登记刷新级别，下一次循环统一画一次。

## 界面栈

界面都是 `View` 子类，只实现两个方法：`draw()` 画自己，`onKey()` 处理按键。`src/app/app.cpp` 维护一个栈：

- 最底层是根界面（阅读 `newReaderView` 或桌面 `newDeskView`）
- 上面叠菜单、目录、提示框、确认框
- 关闭的界面先进 tombstone 队列，本轮结束再 `delete`（可能在它自己的 `onKey` 里被关）

重画走 `app::invalidate(Refresh)` 登记，级别取最高：

| 级别 | 何时用 |
|---|---|
| `Partial` | 光标移动、数字变化、翻页 |
| `Full` | 切换界面、切换模式、弹窗关闭 |
| `Clean` | 电源键短按、残影明显时 |

`render()` 决定这一帧怎么刷：累计局刷次数超过当前界面的 `fullEvery()` 就升成全刷；全刷在低温下会自动降级为更慢的标准波形（快速全刷的短波形冬天驱动不到位），并且每 N 次快刷强制一次标准全刷。

## 省电与深睡

用电池时才睡，插 USB 不睡（方便烧录和调试）。

**浅睡**：停加速度计采样，配好唤醒源后 `esp_light_sleep_start()`。内存、PSRAM、屏帧缓存都保留，唤醒只要几毫秒，因此可以每 `SLEEP_TICK_MS` 醒一次处理时钟和敲击配对。唤醒源有按键、USB 插入、加速度计 INT2（敲击）、定时器。卡住的按键不能当唤醒源（一直是低电平会导致刚睡就醒）。

**深睡**：内存全丢。睡前把"最后一帧画的是什么状态"存进 RTC 内存（`Resume` 结构），醒来按它重画一遍并调用 `assumeShown()`，把这帧当作局刷基准——否则每次醒来都要闪一次全刷。桌面模式每小时定时醒来更新时钟；阅读模式只在按键时醒。醒来的那个按键会被补发一次短按事件（事件在唤醒过程中已经丢了）。

深睡前必须 `ota::confirm()`，否则 bootloader 会认为固件没跑稳而回退。

## 显示与刷新

`EpdPanel` 是驱动抽象层，继承 `Adafruit_GFX`，界面代码只用这个接口：

```cpp
refreshFull()       // 全刷
refreshClean()      // 标准全刷，清残影
refreshPartial()    // 局刷，只驱动变了的像素
forcePartialRect()  // 屏上内容未知时，下一帧先完整重刷一个矩形
assumeShown()       // 认定屏上当前就是缓冲区内容
```

SSD1677 的局刷用控制器自带的比较：0x24 写新帧、0x26 写旧帧，芯片自己算差异，约 0.6 秒。换屏只需新增一个子类，改 `config.h` 的 `EPD_MODEL`。

界面尺寸按 400×300 的小屏写死，画的时候用 `gfx::px()` 乘 1.6，所有界面自动适配大屏。字体是点阵的，分 `Ui` / `Small` / `Value` / `Clock` 等槽位，大屏上换成固件内置的对应字号，没有的字回落到已加载的正文字库。

## 阅读器

这是代码量最大的一块，分三步：

**1. 导入**（`import.cpp`）——只做一次
TXT 自动判定 UTF-8 / GBK / UTF-16 并转成 UTF-8；EPUB 用 tinfl 解压取纯文字。结果和章节信息写入卡上 `/.reader/<hash>.txt` 和 `.bkm`。文件大小、修改时间或开头内容变了就重新导入。

**2. 排版分页**（`layout.cpp`）——按版面缓存
排版器吃一个 `TextSpan`（内存里的一段正文），不要求整本在内存。分页结果是一串页起点字节位置，按版面签名存成 `.pg`，换回用过的字号直接读，不用重排。

**3. 阅读窗口**（`window.cpp`）——内存限制的核心
正文很大时不能整本加载，`TextWindow` 只保留阅读位置附近的一段：

- 整本放得下（≤ 4.5MB）就整本，窗口退化为普通指针
- 放不下就开全书 20%（1MB～4.5MB），翻出窗口时从卡上重读
- 新旧窗口重叠部分在内存里 `memmove`，只从卡上读新的一半——顺着往后读时开销减半

进度按**正文中的字节偏移**记录，与版面无关，所以换字号后还在原位。

## 语音朗读（`src/voice/`）

对接阿里云百炼，一句一次 HTTP 请求，边收边播。

```
主线程 tts::tick()     按句切分 → 排队 → 跟随播放位置自动翻页
合成任务              逐句请求 → 解析 PCM → 写入环形缓冲
播放任务              从环形缓冲取 → I2S (24kHz/16bit/单声道)
```

- `sentence.cpp`：按 `。！？…；` 切句，引号算进句末，无标点的诗在换行处切，超长句在逗号处断
- `tts_client.cpp`：flash 走多模态生成接口返回 WAV，plus 走 `SpeechSynthesizer` 直接要 PCM。两者都开 `X-DashScope-SSE` 流式返回
- `sse_pcm.cpp`：在字节流里增量找 base64 的音频数据，剥掉 WAV 头，不区分块边界
- 每句 PCM 在流里的字节区间都记录，所以"正在读哪句"在暂停时也是准的

环形缓冲约 3～7 秒，后面一两句在后台提前合成。朗读期间 Wi-Fi 常开、CPU 提到 240MHz（TLS 和 base64），结束回落。

需要百炼账号和 API Key，填一次存在 NVS。没有 Key 时只有朗读不可用。

## 加速度计（`src/input/motion.cpp`）

SC7A20H，200Hz 采样任务，做两件事：

- **姿态**：低通后看哪个轴朝上，映射到竖拿 / 横放 / 倒置。轴到机身方向的映射可在菜单里校准并存入 NVS。倒置时按键会自动对调。
- **敲击**：用冲击加静默窗口识别双击，独立于 MCU 软件采样——深睡时靠加速度计中断锁存，省电。

## Wi-Fi（`src/net/`）

平时关闭，用完就关。

- **配网**：`wifi.cpp` 开 SoftAP + DNS 劫持，网页里选 SSID、填密码，连上后顺便对时
- **传输**：`transfer.cpp` 起 HTTP 服务（`WebServer`），浏览器可传书、传字库、上传固件升级、填 TTS Key
- **对时**：每天首次有机会时后台 NTP，失败后当小时不再试，电量低于 3.5V 不对时

`net::busy()` 在 Wi-Fi 使用中返回 true，主循环据此禁止进入睡眠。

## 升级（`src/core/ota.cpp`）

分区表把 16MB flash 分成两个 app 分区。三种升级方式（串口、网页、联网）都走同一套逻辑：

1. 写入**另一个**分区
2. SHA256 校验
3. 切换启动分区、重启

新固件首次启动进入**试运行**状态，30 秒后或关机/深睡前自动调用 `ota::confirm()` 确认。确认前如果重启过（崩溃、RESET、断电），bootloader 回退到原分区。所以升级后 30 秒内不要按 RESET。

串口升级由 `xfer.cpp` 实现：按 4096 字节分块传、每块回执、整体 CRC32 校验，`tools/fwupdate.py` 是电脑端。

## TF 卡

FAT32。

```
/books/            TXT、EPUB，可两层子目录，根目录下的也认
/fonts/            .efnt 点阵字库
/.reader/          固件生成的阅读缓存，不要手动改
```

字库用 `tools/makefont.py` 从 TTF 生成，按字号自动选单色或超采样渲染。固件内的界面字库和生成参数见 [`data/FONTS.md`](data/FONTS.md)。

## 代码结构

```
src/
  main.cpp      启动流程 → app::begin()
  config.h      引脚、地址、默认值
  board/        电源与深睡、I2C、RTC、电量计、CO₂、TF 卡
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
tools/          字库、传文件、截图、发版、测试脚本
```

`src/reader/` 和 `src/voice/sentence.cpp`、`sse_pcm.cpp` 是纯 C++，不依赖 Arduino，可以直接在电脑上编译测试：

```
g++ -O2 -std=c++17 -Isrc -Itools/readertest tools/readertest/main.cpp \
    src/reader/{import,chapters,layout,window,epub}.cpp src/text/decode.cpp -lz -o readertest
./readertest 书.txt

cl /nologo /utf-8 /EHsc /std:c++17 /Isrc tools\ttstest\main.cpp src\voice\sentence.cpp src\voice\sse_pcm.cpp
```

改阅读器或朗读代码后先跑对应测试。
