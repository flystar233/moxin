#pragma once
// 引脚、地址和可调参数。引脚按 eink-docs（v4.45）引脚页。
// 用户能在菜单里改的设置（字号、行距、休眠时间……）不在这里，见 core/settings.h，这里只是它们的默认值。

#define FW_NAME    "墨水屏阅读器"
#define FW_VERSION "0.3.0"

// 菜单"系统设置 → 检查新版本"去这个地址取版本信息，必须是 HTTPS。文件格式见 net/update.h，用 tools/release.py 生成
#define OTA_URL    "https://inkreader-1317504240.cos.ap-guangzhou.myqcloud.com/inkreader/version.json"

// ---------------- 电源 ----------------
#define PIN_PWR_HOLD   4    // 高 = 保持供电（经 D7 拉 U14 的 ON）
#define PIN_PWR_KEY_N  1    // SW3 电源键，按下为低
#define PIN_USB_PRES   9    // 高 = 插着 USB

// ---------------- 按键（按下为低，外部 10k 上拉） ----------------
#define PIN_KEY_MENU   7    // SW6 菜单（KEY1）
#define PIN_KEY_PREV   6    // SW1 上一页（KEY2，翘板上半）
#define PIN_KEY_NEXT   5    // SW2 下一页（KEY3，翘板下半）

// ---------------- I2C ----------------
#define PIN_SDA        18
#define PIN_SCL        17
#define PIN_RTC_INT    16   // U6 INT 与 U10 ALRT 线与
#define PIN_ACC_INT1   15
#define PIN_ACC_INT2   8
#define I2C_HZ         400000
#define ADDR_ACC       0x19 // U8 SC7A20H
#define ADDR_RTC       0x51 // U6 PCF8563
#define ADDR_GAUGE     0x62 // U10 CW2015
#define ADDR_CO2       0x64 // U7 STCC4（温湿度来自它后面的 U11 SHT40）

// ---------------- TF 卡 ----------------
#define PIN_SD_PWR     10   // 低 = 接通卡电源
#define PIN_SD_CMD     11
#define PIN_SD_CLK     12
#define PIN_SD_D0      13

// ---------------- 功放 ----------------
#define PIN_AMP_CTRL   48   // 高 = 开启（U9 NS4168 CTRL，R16 100k 下拉）
#define PIN_I2S_BCLK   21
#define PIN_I2S_LRCLK  47
#define PIN_I2S_DIN    14

// ---------------- 墨水屏 ----------------
// 2026-11-12 换成 4.26 寸 GDEY0426T82（SSD1677，800×480）。4.2 寸测试屏的驱动还留着，改 EPD_MODEL 就能换回去
#define EPD_MODEL_SSD1619A_420 1   // 4.2 寸 400×300 测试屏
#define EPD_MODEL_SSD1677_426  2   // 4.26 寸 GDEY0426T82
#define EPD_MODEL EPD_MODEL_SSD1677_426
#define EPD_PIN_MOSI 38
#define EPD_PIN_SCK  39
#define EPD_PIN_CS   40
#define EPD_PIN_DC   41
#define EPD_PIN_RST  42
#define EPD_PIN_BUSY 2
#define EPD_SPI_HZ   8000000
#if EPD_MODEL == EPD_MODEL_SSD1677_426
#define BODY_FONT_DEFAULT 24    // 正文默认字号（像素）：219 PPI 的屏上 24 像素约 2.8mm
#else
#define BODY_FONT_DEFAULT 18
#endif
#if EPD_MODEL == EPD_MODEL_SSD1619A_420
#define EPD_MIRROR_X 1          // 画面左右反了改这里
#else
#define EPD_MIRROR_X 0
#endif
#define EPD_MIRROR_Y 0
#define EPD_PARTIAL_FRAMES 20   // 局刷驱动帧数默认值：发灰就加大，残影重或太慢就减小（串口 pf 可改）
#define EPD_PARTIAL_WHITE_EXTRA 10  // 变白再多驱动的帧数：上一页笔画擦不干净（字看着发粗发虚）就加大

// ---------------- 姿态 ----------------
// 加速度计静止时，朝上的那个轴读 +1g。下面两个定义说明板子的方向和加速度计轴的关系（默认值，
// 菜单里"姿态校准"的结果存在 NVS，优先用 NVS 里的）：
//   TOP  = 竖拿（按键在右手边）时，指向机身"上方"的轴
//   KEYS = 指向按键那条边的轴（横放在支架上时按键朝上，这个轴读 +1g）
// 轴编号：0 = X，1 = Y，2 = Z；符号 +1 / -1。2026-11-08 PCB1 校准：上方 = -X，按键方向 = -Y
#define AX_TOP        0
#define AX_TOP_SIGN   (-1)
#define AX_KEYS       1
#define AX_KEYS_SIGN  (-1)
#define POSE_TILT_MIN_G  0.35f  // 板面内的重力分量小于它算平放（约倾斜 20° 以内）
#define POSE_DOMINANCE   1.5f   // 一个方向要比另一个方向大这么多倍才算（避开斜对角）
#define POSE_STABLE_MS   1500   // 姿态稳定这么久才切换（文档：1～2 秒）

// 每种姿态下屏幕的旋转（Adafruit_GFX setRotation 0～3）。4.2 寸测试屏没固定在板上，
// 方向不对就改这里。2026-11-12 换 4.26 寸后实物确认：横放正确，竖拿原来是倒的，READ 两个对调
#define ROT_DESK       0   // 横放，按键朝上（支架上的正常摆法）
#define ROT_DESK_FLIP  2   // 横放，按键朝下
#define ROT_READ       3   // 竖拿，按键在右
#define ROT_READ_FLIP  1   // 竖拿，按键在左（倒着拿）

// ---------------- 双击后背（软件检测，阈值单位 g） ----------------
// 2026-11-08 裸板实测：敲后背的冲击 0.4～3.8g（会顶到 ±4g 量程），之后板子和屏会振荡 100～200ms
#define TAP_THR_DEFAULT     0.60f  // 起跳门限：这个采样超过它、上一个采样低于它的一半才算一次敲击
#define TAP_RING_MS         150    // 敲击后这么久内是余振，不再判断
#define TAP_GAP_MIN_MS      150    // 两次敲击的最小间隔（11-13 装壳实测手敲双击 170～235ms，180 会丢；不低于 TAP_RING_MS）
#define TAP_GAP_MAX_MS      600    // 两次敲击的最大间隔
#define TAP_LOCKOUT_MS      450    // 触发后这么久内不再识别（11-13：连续翻页节奏约 1.1 秒，700 会吞掉下一次的第一下）
#define TAP_SHAKE_COUNT     4      // TAP_SHAKE_WINDOW_MS 内起跳超过这么多次 = 在甩 / 走路
#define TAP_SHAKE_WINDOW_MS 1200
#define TAP_AFTER_POSE_MS   1000   // 姿态刚变化的这段时间里忽略敲击（文档要求）
#define TAP_FLAT_MIN_G      1.0f   // 平放时双击里至少一下要达到这个力度（11-10 实测平放手托敲 0.6～5g，第二下常偏轻）
#define TAP_IGNORE_AFTER_KEY_MS 800  // 按键的震动会被当成敲击，按键前后这段时间不认

// ---------------- 按键时序 ----------------
#define KEY_DEBOUNCE_MS     25
#define KEY_LONG_MS         800    // 长按（文档：约 0.8 秒）
#define KEY_REPEAT_MS       700    // 长按翻页的连续间隔
#define KEY_MIN_GAP_MS      200    // 两次短按翻页至少间隔（文档）
#define PWR_LONG_MS         3000   // 电源键长按关机 / 开机
#define KEY_STUCK_MS        10000  // 一直按着超过这么久当作卡住，停止连续翻页

// ---------------- 省电 ----------------
// 浅睡：插着 USB 不睡（串口调试、烧录）；用电池时空闲这么久就浅睡，按键 / 敲击 / 插 USB / 定时器叫醒
#define IDLE_READ_MS        3000   // 阅读界面：翻完页 3 秒没动作就睡（敲击能把它叫醒）
#define IDLE_DESK_MS        2000   // 桌面界面
#define IDLE_MENU_MS        15000  // 菜单里多等一会
#define SLEEP_TICK_MS       2000   // 浅睡时每 2 秒醒一下：看姿态有没有变、桌面要不要更新分钟
#define TICK_AWAKE_MS       30     // 定时醒来后保持清醒多久（够姿态任务采几次样）
#define SHOCK_WAKE_G        0.60f  // 敲击唤醒门限（加速度计高通后的冲击，单位 g）
#define TAP_WAIT_AFTER_SHOCK_MS 800  // 被敲醒后等第二下的时间
#define CPU_MHZ             80     // 不联网时 80MHz 够用，比 240MHz 省一半多
// 深睡：用电池时这么多分钟没有操作就深睡（设置里可改，0 = 不深睡）。
// 阅读界面深睡后按键叫醒，从卡上重读书接着翻；桌面界面每分钟醒一次更新时钟。
#define DEEP_SLEEP_MIN_DEFAULT 10

// ---------------- 电池（用 U10 的 VCELL，不用 SOC：电池模型没写入前 SOC 不准） ----------------
#define BAT_LOW_MV          3600   // 低于它显示"电量低"
#define BAT_CUTOFF_MV       3300   // 用电池时低于它：刷"请充电"并关机（连续两次读数都低才算）

// ---------------- 刷新策略 ----------------
#define READ_FULL_EVERY     10     // 阅读模式每翻这么多页全刷一次默认值（文档：5～10；11-13 用户嫌 6 页一次卡顿，改 10）
// 全刷平时用"快速全刷"（温度寄存器写成 90°C，用高温短波形，约 2 秒、只闪一两下）。
// 冷天颗粒走不到位、残影也清得没那么干净，所以：
#define FAST_FULL_MIN_C     12.0f  // 室温（SHT40）低于它就用标准全刷
#define FAST_FULL_MAX       10     // 每 10 次全刷里第 10 次用标准全刷（约 4 秒），彻底清一次残影（11-13 由 5 改 10）
#define DESK_FULL_EVERY     30     // 桌面模式局刷这么多次全刷一次
#define MENU_FULL_EVERY     40     // 菜单里移动光标局刷这么多次全刷一次
#define GAUGE_POLL_MS       30000
#define CO2_POLL_MS         60000

// ---------------- 书 ----------------
#define BOOK_WINDOW_MAX     (4608 * 1024)  // 内存里最多放这么多正文（4.5MB）：整本放得下就整本，放不下只放阅读位置附近一段（书本身多大都行）
#define BODY_FONT_ARENA     (2816 * 1024)  // 正文字库固定区 2.75MB（霞鹜文楷 28 号 2.32MB）。更大的字库放不下
#define BOOK_MAX_FILES      200
#define CHAPTER_MAX         20000

// ---------------- 语音朗读（Edge 朗读 / 阿里云 qwen-audio-3.1-tts-flash） ----------------
// 按句请求：发一句文本，服务端流式返回 24kHz 16bit 单声道 PCM，收到就送 I2S 放出来；
// 同时后台预合成后面一两句，句子之间听不出停顿。
//
// Edge 朗读：免费，直接连微软 Edge 浏览器的在线朗读接口（WebSocket，整个会话复用一条连接）。
//   不需要 Key，但要求设备时间正确：令牌 Sec-MS-GEC 是按当前时间（5 分钟一档）
//   算出来的 SHA-256，时钟差超过几分钟就会被拒。系统开机会自动联网对时。
// flash：阿里云百炼的 qwen-audio-3.1-tts-flash，音质比 Edge 好，按字符计费。
//   要 API Key（系统设置 → Wi-Fi → 无线传输 的网页里填，或串口 tts key sk-...）
//   和业务空间 ID（百炼控制台右上角图标里复制，或串口 tts ws <ID>）。
#define TTS_MODEL_EDGE         "edge"
#define TTS_EDGE_HOST          "speech.platform.bing.com"
#define TTS_EDGE_PATH          "/consumer/speech/synthesize/readaloud/edge/v1"
#define TTS_EDGE_TRUSTED_TOKEN "6A5AA1D4EAFF4E9FB37E23D68491D6F4"
#define TTS_EDGE_GEC_VERSION   "2-143.0.3650.75"
#define TTS_EDGE_ORIGIN        "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold"
#define TTS_EDGE_UA            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0"
#define TTS_EDGE_FORMAT        "audio-24khz-48kbitrate-mono-mp3"  // 服务端只支持 MP3/Opus，设备上用 helix 解成 PCM
#define TTS_VOICE_EDGE         "zh-CN-YunyangNeural"       // 默认音色：云扬（男，播音）
// 另一个可选模型：专业级、音质更好（北京），免费额度以控制台为准。
// 它走另一套端点，地址里要带业务空间 ID：
//   https://<业务空间ID>.cn-beijing.maas.aliyuncs.com/api/v1/services/audio/tts/SpeechSynthesizer
#define TTS_MODEL_FLASH        "qwen-audio-3.1-tts-flash"
#define TTS_MODEL_FLASH_OLD   "qwen-audio-3.1-tts-plus"  // 旧固件存的名字，读设置时自动迁移
#define TTS_FLASH_HOST_SUFFIX  ".cn-beijing.maas.aliyuncs.com"
#define TTS_FLASH_PATH         "/api/v1/services/audio/tts/SpeechSynthesizer"
#define TTS_VOICE_FLASH        "longanhuan_v3.1"
// 参考价（元/万字符，仅用于串口/设置里估算花费）；Edge 朗读免费
#define TTS_PRICE_FLASH        1.44f
#define TTS_RATE      24000         // 流式 PCM 采样率（两个模型都按 24k 要）
#define TTS_MAX_CHARS 48            // 一次合成最多多少字（短句才能多句预读，句子之间才不卡）
#define TTS_MIN_CHARS 2             // 一句至少多少字，再短就接到下一行
#define TTS_RING_KB   640           // 预读缓冲（PSRAM，约 13 秒，能装两三句），分不到会往下降
#define TTS_VOL_DEFAULT 70          // 默认音量 %
#define TTS_SPEED_DEFAULT 1.0f      // 默认语速（1.0 = 原速）
#define TTS_SPEED_MIN 0.5f          // 最慢 0.5 倍
#define TTS_SPEED_MAX 2.0f          // 最快 2 倍
#define TTS_PAUSE_STOP_MS (5 * 60 * 1000)  // 暂停这么久自动结束（关 Wi-Fi 省电）

