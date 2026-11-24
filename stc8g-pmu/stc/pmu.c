/*
 * STC8G1K17A 电源管理固件（Keil C51）
 *
 * STC-ISP 下载选项：
 *   - 内部 IRC 频率选 22.1184MHz（下面的波特率、1ms 定时都按这个算）
 *   - 勾选“复位脚用作 I/O 口”（P5.4 要当 LDO_EN 用）
 *
 * 引脚：
 *   P5.4  LDO_EN    推挽输出，高 = 打开 3.3V
 *   P5.5  CHRG      准双向（弱上拉）输入，低 = 正在充电
 *   P3.0  RXD       开漏，外部 R10 10k 上拉到 3V3，经 R12 1k 接 ESP32 TX
 *   P3.1  TXD       开漏，外部 R9 10k 上拉到 3V3，经 R11 1k 接 ESP32 RX
 *   P3.2  KEY/INT0  准双向（弱上拉）输入，按下 = 低，掉电时下降沿唤醒
 *   P3.3  VBUS_DET  高阻输入（不能开上拉，否则会抬高分压点），高 = 插着 USB
 *
 * 按键：
 *   关机时按住 1s       → 开机
 *   开机时短按          → 发 KEY 给 ESP32
 *   开机时按住 3s       → 发 OFF_REQ，等 ESP32 回 POWER_OFF（最多 5s，超时强制断电）
 *   开机时按住 8s       → 直接断电（ESP32 死机时用）
 *
 * 串口协议（9600 8N1）：A5 | CMD | LEN | DATA... | SUM
 *   SUM = CMD + LEN + 所有 DATA 字节求和，取低 8 位
 *   STC → ESP32
 *     01 STATUS   LEN=4  flags, mV 高字节, mV 低字节, 开机原因
 *                        flags: bit0 插着 USB, bit1 充电中, bit2 已充满, bit3 电量低
 *                        mV 是 STC 的 VCC：不插 USB 时约等于电池电压；
 *                        插着 USB 时是 VSYS（约 4.6V），不代表电池电压
 *                        开机原因: 1 按键, 2 定时唤醒
 *                        状态变化时立即发，平时每秒发一次
 *     02 KEY      LEN=1  1 = 短按
 *     03 OFF_REQ  LEN=1  1 = 用户长按, 2 = 电量低
 *   ESP32 → STC
 *     81 GET_STATUS  LEN=0
 *     82 POWER_OFF   LEN=2  唤醒分钟数（高字节在前），0 = 只能按键开机
 *                           STC 收到后约 10ms 断开 3.3V
 */
#include <intrins.h>

typedef unsigned char u8;
typedef unsigned int  u16;
typedef unsigned long u32;

/* ---------- SFR（STC8G 手册） ---------- */
sfr P3        = 0xB0;
sfr P5        = 0xC8;
sfr P3M1      = 0xB1;
sfr P3M0      = 0xB2;
sfr P5M1      = 0xC9;
sfr P5M0      = 0xCA;
sfr AUXR      = 0x8E;
sfr PCON      = 0x87;
sfr SCON      = 0x98;
sfr SBUF      = 0x99;
sfr T2H       = 0xD6;
sfr T2L       = 0xD7;
sfr TMOD      = 0x89;
sfr TL0       = 0x8A;
sfr TH0       = 0x8C;
sfr TCON      = 0x88;
sfr IE        = 0xA8;
sfr WKTCL     = 0xAA;
sfr WKTCH     = 0xAB;
sfr ADC_CONTR = 0xBC;
sfr ADC_RES   = 0xBD;
sfr ADC_RESL  = 0xBE;
sfr ADCCFG    = 0xDE;

sbit LDO_EN   = P5^4;
sbit CHRG     = P5^5;
sbit RXD_PIN  = P3^0;
sbit TXD_PIN  = P3^1;
sbit KEY      = P3^2;
sbit VBUS_DET = P3^3;

sbit EA  = IE^7;
sbit ES  = IE^4;
sbit ET0 = IE^1;
sbit EX0 = IE^0;
sbit TR0 = TCON^4;
sbit IE0 = TCON^1;
sbit IT0 = TCON^0;
sbit REN = SCON^4;
sbit TI  = SCON^1;
sbit RI  = SCON^0;

/* ---------- 参数 ---------- */
#define WKT_COUNT      29999u   /* 唤醒周期 = (29999+1) × 16 / 32kHz ≈ 15s；内部 32k 时钟误差较大，约 ±10% */
#define WKT_PER_MIN    4u
#define KEY_ON_MS      1000u
#define KEY_SHORT_MS   30u      /* 短于它当抖动 */
#define KEY_OFFREQ_MS  3000u
#define KEY_FORCE_MS   8000u
#define OFF_ACK_MS     5000u
#define STATUS_MS      1000u
#define VBAT_LOW_MV    3300u    /* 不插 USB 时低于它：拒绝开机 / 请求关机 */
#define VBAT_CUT_MV    3100u    /* 不插 USB 时低于它：直接断电 */
#define BGV_MV         1190UL   /* 内部基准电压，可换成芯片出厂校准值（见手册 ADC 章节） */

#define CMD_STATUS     0x01
#define CMD_KEY        0x02
#define CMD_OFF_REQ    0x03
#define CMD_GET_STATUS 0x81
#define CMD_POWER_OFF  0x82

#define BOOT_KEY       1
#define BOOT_TIMER     2

enum { ST_OFF, ST_ON, ST_WAIT_OFF };

/* ---------- 全局 ---------- */
static u8 state = ST_OFF;
static u8 boot_reason;
static u32 wake_left;                 /* 还要等几个 15s 唤醒周期 */

static volatile u16 ms_tick;
static volatile u8 rx_buf[16];
static volatile u8 rx_w;
static u8 rx_r;
static volatile u8 tx_busy;

static u8 key_lock, key_down, off_sent, last_fast = 0xFF, status_req;
static u16 t_key, t_wait, t_status;

static u8 ps, pcmd, plen, pi, psum, pdata[4];   /* 接收解析 */
static u8 off_cmd;
static u16 off_wake;

/* ---------- 中断 ---------- */
void int0_isr(void) interrupt 0 { }            /* 只用来把 MCU 从掉电模式叫醒 */

void timer0_isr(void) interrupt 1 { ms_tick++; }

void uart_isr(void) interrupt 4
{
    if (RI) { RI = 0; rx_buf[rx_w] = SBUF; rx_w = (rx_w + 1) & 15; }
    if (TI) { TI = 0; tx_busy = 0; }
}

/* ---------- 基础 ---------- */
static u16 now_ms(void)
{
    u16 t;
    ET0 = 0; t = ms_tick; ET0 = 1;
    return t;
}

static void delay_ms(u16 n)
{
    u16 t0 = now_ms();
    while ((u16)(now_ms() - t0) < n) ;
}

static void io_init(void)
{
    LDO_EN = 0;
    P5M1 &= ~0x30;                    /* P5.4 推挽，P5.5 准双向 */
    P5M0 = (P5M0 & ~0x20) | 0x10;

    RXD_PIN = 1; TXD_PIN = 1;
    P3M1 = (P3M1 & ~0x04) | 0x0B;     /* P3.0/P3.1 开漏(11)，P3.2 准双向(00)，P3.3 高阻(10) */
    P3M0 = (P3M0 & ~0x0C) | 0x03;
}

static void timer0_init(void)         /* 1ms，12T 模式，16 位自动重装 */
{
    AUXR &= 0x7F;
    TMOD &= 0xF0;
    TL0 = 0xCD; TH0 = 0xF8;           /* 65536 - 22118400/12/1000 = 0xF8CD */
    TR0 = 1; ET0 = 1;
}

static void uart_init(void)           /* 9600，定时器 2 做波特率发生器，1T */
{
    SCON = 0x40;                      /* 模式 1，先不开接收 */
    AUXR |= 0x05;                     /* S1ST2 = 1 选 T2，T2x12 = 1 */
    T2L = 0xC0; T2H = 0xFD;           /* 65536 - 22118400/4/9600 = 0xFDC0 */
    AUXR |= 0x10;                     /* 启动 T2 */
}

/* ---------- ADC：测内部 1.19V 基准，反推 VCC ---------- */
static u16 adc_once(void)
{
    ADC_RES = 0; ADC_RESL = 0;
    ADC_CONTR = 0x80 | 0x40 | 0x0F;   /* 电源 + 启动 + 第 15 通道 */
    _nop_(); _nop_();
    while (!(ADC_CONTR & 0x20)) ;
    ADC_CONTR &= ~0x20;
    return ((u16)(ADC_RES & 0x03) << 8) | ADC_RESL;
}

static u16 read_vcc_mv(void)
{
    u8 i;
    u16 sum = 0;

    ADCCFG = 0x2F;                    /* 结果右对齐，最慢转换速度 */
    ADC_CONTR = 0x80 | 0x0F;          /* 打开 ADC 电源，等它稳定 */
    delay_ms(2);
    adc_once();                       /* 第一次结果丢掉 */
    for (i = 0; i < 8; i++) sum += adc_once();
    ADC_CONTR = 0;                    /* 关 ADC 省电 */

    sum >>= 3;
    return sum ? (u16)(BGV_MV * 1024 / sum) : 0;
}

/* ---------- 串口发送 ---------- */
static void uart_put(u8 b)
{
    while (tx_busy) ;
    tx_busy = 1;
    SBUF = b;
}

static void send_frame(u8 cmd, u8 *d, u8 n)
{
    u8 i, sum = cmd + n;

    if (state == ST_OFF) return;      /* 关机时串口中断关着，发了会卡死 */
    uart_put(0xA5); uart_put(cmd); uart_put(n);
    for (i = 0; i < n; i++) { uart_put(d[i]); sum += d[i]; }
    uart_put(sum);
}

static void send_byte_cmd(u8 cmd, u8 v) { send_frame(cmd, &v, 1); }

/* ---------- 电源 ---------- */
static void power_on(u8 reason)
{
    boot_reason = reason;
    LDO_EN = 1;
    RI = 0; TI = 0; tx_busy = 0;
    rx_r = rx_w; ps = 0; off_cmd = 0; status_req = 1;
    key_lock = 1; key_down = 0;       /* 开机那次长按松开之前不算按键 */
    last_fast = 0xFF;
    t_status = now_ms();
    REN = 1; ES = 1;
    state = ST_ON;
}

static void power_off(u16 wake_min)
{
    while (tx_busy) ;
    ES = 0; REN = 0;
    LDO_EN = 0;
    wake_left = (u32)wake_min * WKT_PER_MIN;
    state = ST_OFF;
}

static u8 vcc_ok(void)
{
    return VBUS_DET || read_vcc_mv() >= VBAT_LOW_MV;
}

static void enter_wait_off(u16 t)
{
    if (state == ST_ON) { state = ST_WAIT_OFF; t_wait = t; }
}

/* ---------- 接收解析 ---------- */
static void handle(u8 cmd)
{
    if (cmd == CMD_GET_STATUS) {
        status_req = 1;
    } else if (cmd == CMD_POWER_OFF && plen == 2) {
        off_wake = ((u16)pdata[0] << 8) | pdata[1];
        off_cmd = 1;
    }
}

static void rx_feed(u8 b)
{
    switch (ps) {
    case 0: if (b == 0xA5) ps = 1; break;
    case 1: pcmd = b; psum = b; ps = 2; break;
    case 2:
        plen = b; psum += b; pi = 0;
        if (b > sizeof(pdata)) ps = 0;
        else ps = b ? 3 : 4;
        break;
    case 3: pdata[pi++] = b; psum += b; if (pi >= plen) ps = 4; break;
    case 4: if (b == psum) handle(pcmd); ps = 0; break;
    }
}

/* ---------- 关机状态：睡觉，等按键或定时器 ---------- */
static void sleep_once(void)
{
    IE0 = 0; EX0 = 1;
    if (wake_left) {
        WKTCL = (u8)WKT_COUNT;
        WKTCH = (u8)(WKT_COUNT >> 8) | 0x80;    /* bit7 = 打开掉电唤醒定时器 */
    } else {
        WKTCH = 0;
    }
    PCON |= 0x02;                     /* 进入掉电模式 */
    _nop_(); _nop_(); _nop_(); _nop_();
    EX0 = 0;
}

static void off_step(void)
{
    u16 t0;

    sleep_once();

    if (!KEY) {                       /* 按键唤醒：按满 1s 才开机 */
        t0 = now_ms();
        while (!KEY) {
            if ((u16)(now_ms() - t0) >= KEY_ON_MS) {
                if (vcc_ok()) power_on(BOOT_KEY);
                break;
            }
        }
        return;
    }

    if (wake_left && --wake_left == 0 && vcc_ok())
        power_on(BOOT_TIMER);
}

/* ---------- 开机状态 ---------- */
static void on_step(void)
{
    u16 t = now_ms(), held, mv;
    u8 f, fast;

    while (rx_r != rx_w) { rx_feed(rx_buf[rx_r]); rx_r = (rx_r + 1) & 15; }

    if (off_cmd) {                    /* ESP32 说可以关了 */
        off_cmd = 0;
        delay_ms(10);
        power_off(off_wake);
        return;
    }

    /* 按键 */
    if (key_lock) {
        if (KEY) key_lock = 0;
    } else if (!KEY) {
        if (!key_down) { key_down = 1; t_key = t; off_sent = 0; }
        held = t - t_key;
        if (held >= KEY_FORCE_MS) { power_off(0); return; }
        if (held >= KEY_OFFREQ_MS && !off_sent) {
            send_byte_cmd(CMD_OFF_REQ, 1);
            off_sent = 1;
            enter_wait_off(t);
        }
    } else if (key_down) {
        key_down = 0;
        held = t - t_key;
        if (held >= KEY_SHORT_MS && held < KEY_OFFREQ_MS) send_byte_cmd(CMD_KEY, 1);
    }

    /* 等 ESP32 回复超时 → 强制断电 */
    if (state == ST_WAIT_OFF && (u16)(t - t_wait) >= OFF_ACK_MS) { power_off(0); return; }

    /* USB / 充电状态一变就上报 */
    fast = (VBUS_DET ? 1 : 0) | (CHRG ? 2 : 0);
    if (fast != last_fast) { last_fast = fast; status_req = 1; }

    if (status_req || (u16)(t - t_status) >= STATUS_MS) {
        u8 d[4];
        status_req = 0;
        t_status = t;
        mv = read_vcc_mv();

        f = 0;
        if (VBUS_DET) f = CHRG ? 0x05 : 0x03;          /* USB + 已充满 / USB + 充电中 */
        else if (mv < VBAT_LOW_MV) f = 0x08;

        d[0] = f; d[1] = (u8)(mv >> 8); d[2] = (u8)mv; d[3] = boot_reason;
        send_frame(CMD_STATUS, d, 4);

        if (!(f & 0x01)) {
            if (mv < VBAT_CUT_MV) { power_off(0); return; }
            if (f & 0x08) {
                if (state == ST_ON) send_byte_cmd(CMD_OFF_REQ, 2);
                enter_wait_off(t);
            }
        }
    }
}

void main(void)
{
    io_init();
    timer0_init();
    uart_init();
    IT0 = 1;                          /* INT0 下降沿 */
    EA = 1;

    while (1) {
        if (state == ST_OFF) off_step();
        else on_step();
    }
}
