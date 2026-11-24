#pragma once
// 引脚、地址和可调参数。引脚按 eink-docs（v4.45）引脚页。

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
#define PIN_AMP_CTRL   48   // 高 = 开启

// ---------------- 墨水屏（现在接的是 4.2 寸 SSD1619A 测试屏） ----------------
#define EPD_PIN_MOSI 38
#define EPD_PIN_SCK  39
#define EPD_PIN_CS   40
#define EPD_PIN_DC   41
#define EPD_PIN_RST  42
#define EPD_PIN_BUSY 2
#define EPD_SPI_HZ   8000000
#define EPD_MIRROR_X 1          // 画面左右反了改这里
#define EPD_MIRROR_Y 0
#define EPD_PARTIAL_FRAMES 20   // 局刷驱动帧数：发灰就加大，残影重或太慢就减小（串口 pf 可改）
#define EPD_PARTIAL_WHITE_EXTRA 10  // 变白再多驱动的帧数：上一页笔画擦不干净（字看着发粗发虚）就加大
#define EPD_HAS_RED 0

// ---------------- 姿态 ----------------
// 加速度计静止时，朝上的那个轴读 +1g。下面两个定义说明板子的方向和加速度计轴的关系，
// 用串口命令 pose 看实测值后改：
//   TOP  = 竖拿（按键在右手边）时，指向机身"上方"的轴
//   KEYS = 指向按键那条边的轴（横放在支架上时按键朝上，这个轴读 +1g）
// 轴编号：0 = X，1 = Y，2 = Z；符号 +1 / -1
// 2026-11-08 在 PCB1 上校准（cal 命令）：上方 = -X，按键方向 = -Y。校准结果存在 NVS，优先用 NVS 里的
#define AX_TOP        0
#define AX_TOP_SIGN   (-1)
#define AX_KEYS       1
#define AX_KEYS_SIGN  (-1)
#define POSE_TILT_MIN_G  0.35f  // 板面内的重力分量小于它算平放（约倾斜 20° 以内）
#define POSE_DOMINANCE   1.5f   // 一个方向要比另一个方向大这么多倍才算（避开斜对角）
#define POSE_STABLE_MS   1500   // 姿态稳定这么久才切换（文档：1～2 秒）

// 每种姿态下屏幕的旋转（Adafruit_GFX setRotation 0～3）。4.2 寸测试屏没固定在板上，
// 方向不对就改这里；4.26 寸装进外壳后再按实物定一次。
#define ROT_DESK       0   // 横放，按键朝上（支架上的正常摆法）
#define ROT_DESK_FLIP  2   // 横放，按键朝下
#define ROT_READ       1   // 竖拿，按键在右
#define ROT_READ_FLIP  3   // 竖拿，按键在左（倒着拿）

// ---------------- 双击后背（软件检测，阈值单位 g） ----------------
// 2026-11-08 裸板实测：敲后背的冲击 0.4～3.8g（会顶到 ±4g 量程），之后板子和屏会振荡 100～200ms
#define TAP_THR_DEFAULT     0.60f  // 起跳门限：这个采样超过它、上一个采样低于它的一半才算一次敲击
#define TAP_RING_MS         150    // 敲击后这么久内是余振，不再判断
#define TAP_GAP_MIN_MS      180    // 两次敲击的最小间隔
#define TAP_GAP_MAX_MS      600    // 两次敲击的最大间隔
#define TAP_LOCKOUT_MS      700    // 触发后这么久内不再识别
#define TAP_SHAKE_COUNT     4      // TAP_SHAKE_WINDOW_MS 内起跳超过这么多次 = 在甩 / 走路
#define TAP_SHAKE_WINDOW_MS 1200
#define TAP_AFTER_POSE_MS   1000   // 姿态刚变化的这段时间里忽略敲击（文档要求）
#define TAP_FLAT_MIN_G      1.0f   // 平放时双击里至少一下要达到这个力度（11-10 实测平放手托敲 0.6～5g，第二下常偏轻）

// ---------------- 按键时序 ----------------
#define KEY_DEBOUNCE_MS     25
#define KEY_LONG_MS         800    // 长按（文档：约 0.8 秒）
#define KEY_REPEAT_MS       700    // 长按翻页的连续间隔
#define KEY_MIN_GAP_MS      200    // 两次短按翻页至少间隔（文档）
#define PWR_LONG_MS         3000   // 电源键长按关机 / 开机
#define KEY_STUCK_MS        10000  // 一直按着超过这么久当作卡住，停止连续翻页

// ---------------- 省电（浅睡）----------------
// 插着 USB 不睡（串口调试、烧录）；用电池时空闲这么久就浅睡，按键 / 敲击 / 插 USB / 定时器叫醒
#define IDLE_READ_MS        3000   // 阅读界面：翻完页 3 秒没动作就睡（敲击能把它叫醒）
#define IDLE_DESK_MS        2000   // 桌面界面
#define IDLE_MENU_MS        15000  // 菜单里多等一会
#define SLEEP_TICK_MS       2000   // 睡着时每 2 秒醒一下：看姿态有没有变、桌面要不要更新分钟
#define TICK_AWAKE_MS       30     // 定时醒来后保持清醒多久（够姿态任务采几次样）
#define SHOCK_WAKE_G        0.60f  // 敲击唤醒门限（加速度计高通后的冲击，单位 g）
#define TAP_WAIT_AFTER_SHOCK_MS 800  // 被敲醒后等第二下的时间
#define CPU_MHZ             80     // 不联网时 80MHz 够用，比 240MHz 省一半多

// ---------------- 刷新策略 ----------------
#define READ_FULL_EVERY     6      // 阅读模式每翻这么多页全刷一次（文档：5～10）
#define DESK_FULL_EVERY     30     // 桌面模式局刷这么多次全刷一次
#define GAUGE_POLL_MS       30000
#define CO2_POLL_MS         60000
