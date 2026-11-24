#include "motion.h"

#include "config.h"
#include "events.h"
#include "hw.h"

// SC7A20H：数据寄存器和 LIS2DH 同址（0x20 CTRL_REG1、0x23 CTRL_REG4、0x28～0x2D 数据）。
// 敲击寄存器 0x38～0x3D 和 ST 的不兼容，这一版不用，敲击在下面用软件检测。
// 不碰保留区 0x00～0x0B、0x40～0x5F。

// 轴向映射：默认取 config.h，校准后从 NVS 读（motionSetAxes）
static volatile int s_axTop = AX_TOP, s_sgTop = AX_TOP_SIGN, s_axKey = AX_KEYS, s_sgKey = AX_KEYS_SIGN;
static volatile int s_axN = 3 - AX_TOP - AX_KEYS;  // 垂直板面的轴
static volatile float s_raw[3] = {0, 0, 0};       // 低通后的传感器三轴（g），校准用

static volatile uint8_t s_pose = POSE_UNKNOWN;
static volatile bool s_flat = false;
static volatile uint32_t s_poseAt = 0;
static volatile float s_gT = 0, s_gK = 0, s_gN = 0;
static float s_scale = 1.0f / 8192.0f;   // ±4g：8192 LSB/g（启动时按实测 1g 校验）

static volatile bool s_tapOn = true;
static volatile bool s_tapDbg = false;
static volatile float s_tapThr = TAP_THR_DEFAULT;

static bool readRaw(int16_t v[3]) {
  uint8_t b[6];
  if (!regRead(ADDR_ACC, 0xA8, b, 6)) return false;  // 0x28 | 0x80 自动递增
  v[0] = (int16_t)(b[0] | (b[1] << 8));
  v[1] = (int16_t)(b[2] | (b[3] << 8));
  v[2] = (int16_t)(b[4] | (b[5] << 8));
  return true;
}

// 只看板面内的重力分量：斜躺着拿（比如 45°）也能认出来。
// 板面内分量太小 = 平放；两个方向差不多大 = 斜对角，都不算（保持原姿态）。
static uint8_t classify(float t, float k, float n, bool &flat) {
  (void)n;
  float inplane = sqrtf(t * t + k * k);
  flat = inplane < POSE_TILT_MIN_G;
  if (flat) return POSE_UNKNOWN;
  if (fabsf(t) >= fabsf(k) * POSE_DOMINANCE) return t > 0 ? POSE_READ : POSE_READ_FLIP;
  if (fabsf(k) >= fabsf(t) * POSE_DOMINANCE) return k > 0 ? POSE_DESK : POSE_DESK_FLIP;
  return POSE_UNKNOWN;
}

// ---------------- 软件双击检测 ----------------
// 敲后壳时，垂直板面的轴上会瞬间（一个采样内）冲出一个很大的值，之后板子会振荡一两百毫秒。
// 所以只认"起跳"：这个采样超过门限、上一个采样还不到门限的一半；起跳后 TAP_RING_MS 内是余振，不再判断。
// 手拿着晃动是慢慢变大的，不会一步起跳；一直在甩时起跳很密，按次数排除。
// 两次起跳间隔在 TAP_GAP_MIN_MS～TAP_GAP_MAX_MS 之间算双击。
struct TapState {
  float base = 0, prevAd = 0, firstAd = 0;
  uint32_t lastOnset = 0, firstTapAt = 0, lockUntil = 0, shakeUntil = 0;
  bool haveFirst = false;
  uint32_t onsets[8] = {0};
  uint8_t on = 0;
};
static TapState ts;

static void tapStep(float n, uint32_t now) {
  float d = n - ts.base;
  float ad = fabsf(d);
  bool ringing = now - ts.lastOnset < TAP_RING_MS;
  if (!ringing) ts.base += 0.02f * d;  // 余振期间不更新基线

  // 按键前后的震动不算敲击
  if (now - g_lastKeyActivity < TAP_IGNORE_AFTER_KEY_MS) {
    ts.haveFirst = false;
    ts.prevAd = ad;
    return;
  }
  bool onset = !ringing && ad >= s_tapThr && ts.prevAd < s_tapThr * 0.5f;
  ts.prevAd = ad;
  if (!onset) return;
  ts.lastOnset = now;

  // 最近 TAP_SHAKE_WINDOW_MS 里起跳太多次：在甩或在走路
  ts.onsets[ts.on++ % 8] = now;
  int recent = 0;
  for (uint32_t t : ts.onsets)
    if (t && now - t <= TAP_SHAKE_WINDOW_MS) recent++;
  if (recent > TAP_SHAKE_COUNT) {
    ts.shakeUntil = now + 1000;
    ts.haveFirst = false;
  }
  bool shaking = now < ts.shakeUntil;
  if (s_tapDbg) Serial.printf("[敲击] 起跳 %.2fg%s\n", ad, shaking ? "（在晃，忽略）" : "");
  if (shaking || now < ts.lockUntil) return;

  uint32_t gap = now - ts.firstTapAt;
  if (ts.haveFirst && gap >= TAP_GAP_MIN_MS && gap <= TAP_GAP_MAX_MS) {
    ts.haveFirst = false;
    ts.lockUntil = now + TAP_LOCKOUT_MS;
    // 平放（手托着或放桌上）也认双击，但两下都要够重，免得碰一下桌子就翻页
    bool strongOk = !s_flat || fmaxf(ts.firstAd, ad) >= TAP_FLAT_MIN_G;  // 至少有一下够重
    bool poseOk = poseIsReading(s_pose) && now - s_poseAt >= TAP_AFTER_POSE_MS;
    const char *why = !s_tapOn ? "（敲击翻页已关，忽略）"
                    : !poseOk  ? "（不在阅读模式或刚换姿态，忽略）"
                    : !strongOk ? "（平放时敲得太轻，忽略）"
                                : " → 翻页";
    if (s_tapDbg) Serial.printf("[敲击] 双击（间隔 %lums）%s%s\n", (unsigned long)gap, s_flat ? "［平放］" : "", why);
    if (s_tapOn && poseOk && strongOk) postEvent(EV_TAP2);
  } else if (!ts.haveFirst || gap > TAP_GAP_MAX_MS) {
    ts.haveFirst = true;
    ts.firstTapAt = now;
    ts.firstAd = ad;
  }
}

// ---------------- 任务 ----------------
// 浅睡前主循环让任务停在这里（不在 I2C 传输中间），醒来再放行
static volatile bool s_pauseReq = false, s_paused = false;

static void motionTask(void *) {
  TickType_t last = xTaskGetTickCount();
  uint8_t cand = POSE_UNKNOWN;
  uint32_t candAt = 0, lastSample = 0;
  float lp[3] = {0, 0, 0};
  bool first = true;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
    if (s_pauseReq) {
      s_paused = true;
      while (s_pauseReq) vTaskDelay(1);
      s_paused = false;
      last = xTaskGetTickCount();  // 睡了一段，不要补跑错过的周期
      continue;
    }
    int16_t r[3];
    if (!readRaw(r)) continue;
    uint32_t now = millis();
    float g[3] = {r[0] * s_scale, r[1] * s_scale, r[2] * s_scale};
    // 刚从浅睡醒来（两次采样隔了很久）：低通直接取当前值，敲击基线也重新对准，免得旧值造成误判
    bool gap = lastSample && now - lastSample > 50;
    lastSample = now;
    if (gap) { ts.base = g[s_axN]; ts.prevAd = 0; }
    for (int i = 0; i < 3; i++) { lp[i] = (first || gap) ? g[i] : lp[i] + 0.04f * (g[i] - lp[i]); s_raw[i] = lp[i]; }
    float t = lp[s_axTop] * s_sgTop, k = lp[s_axKey] * s_sgKey, n = lp[s_axN];
    s_gT = t; s_gK = k; s_gN = n;

    bool flat;
    uint8_t p = classify(t, k, n, flat);
    s_flat = flat;
    if (first) {  // 开机直接取当前姿态
      first = false;
      if (p != POSE_UNKNOWN) { s_pose = p; s_poseAt = now; }
    } else if (p != POSE_UNKNOWN && p != s_pose) {
      if (p != cand) { cand = p; candAt = now; }
      else if (now - candAt >= POSE_STABLE_MS) {
        s_pose = p;
        s_poseAt = now;
        postEvent(EV_POSE, p);
      }
    } else {
      cand = POSE_UNKNOWN;
    }

    tapStep(g[s_axN], now);
  }
}

bool motionBegin() {
  uint8_t who = 0, ver = 0;
  if (!regRead(ADDR_ACC, 0x0F, &who, 1) || who != 0x11) return false;
  regRead(ADDR_ACC, 0x70, &ver, 1);
  regWrite(ADDR_ACC, 0x20, 0x77);  // CTRL_REG1：400Hz，XYZ
  regWrite(ADDR_ACC, 0x22, 0x00);  // CTRL_REG3：INT1 不映射
  regWrite(ADDR_ACC, 0x23, 0x90);  // CTRL_REG4：BDU，±4g
  regWrite(ADDR_ACC, 0x25, 0x00);  // CTRL_REG6：INT2 不映射
  regWrite(ADDR_ACC, 0x30, 0x00);  // INT1_CFG 关
  delay(30);
  // 按实测 1g 校验量程（万一 ±4g 没设上就按 ±2g 算）
  float m = 0;
  for (int i = 0; i < 10; i++) {
    int16_t r[3];
    if (readRaw(r)) m += sqrtf((float)r[0] * r[0] + (float)r[1] * r[1] + (float)r[2] * r[2]);
    delay(4);
  }
  m /= 10;
  s_scale = (m > 12000) ? 1.0f / 16384.0f : 1.0f / 8192.0f;
  Serial.printf("[姿态] SC7A20H 版本 0x%02X，静止原始幅值 %.0f → %s\n", ver, m, m > 12000 ? "±2g" : "±4g");
  xTaskCreatePinnedToCore(motionTask, "motion", 4096, nullptr, 3, nullptr, 0);
  delay(20);
  return true;
}

// ---------------- 浅睡相关 ----------------
void motionPause(bool on) {
  s_pauseReq = on;
  if (on) {
    uint32_t t0 = millis();
    while (!s_paused && millis() - t0 < 30) delay(1);
  }
}

// 冲击中断：中断发生器 2 用高通滤波后的数据（去掉重力），任一轴超过门限就锁存并拉高 INT2（GPIO8）。
// 输出数据寄存器不滤波（FDS = 0），姿态和软件敲击检测照常用原始数据。
// 寄存器按 LIS2DH 兼容的定义（文档：数据和中断寄存器大多同址）；★ SC7A20H 上实测确认
void motionShockConfig(float thrG) {
  int ths = (int)(thrG / 0.032f + 0.5f);   // ±4g 时 1LSB ≈ 32mg
  if (ths < 1) ths = 1;
  if (ths > 127) ths = 127;
  regWrite(ADDR_ACC, 0x21, 0x02);  // CTRL_REG2：HPIS2 = 1，只给中断发生器 2 用高通
  regWrite(ADDR_ACC, 0x36, ths);   // INT2_THS
  regWrite(ADDR_ACC, 0x37, 0x00);  // INT2_DURATION
  regWrite(ADDR_ACC, 0x34, 0x3F);  // INT2_CFG：XYZ 高低都开，OR
  regWrite(ADDR_ACC, 0x24, 0x02);  // CTRL_REG5：LIR_INT2，锁存
  regWrite(ADDR_ACC, 0x25, 0x20);  // CTRL_REG6：I2_IA2，发生器 2 → INT2 脚，高电平有效
  pinMode(PIN_ACC_INT2, INPUT_PULLDOWN);  // 没中断时 SC7A20H 的 INT 脚是高阻（11-08 实测）
}

// 睡前清锁存、重置高通基准；返回 INT2 是否已经回到低（可以当唤醒源）
bool motionShockArm() {
  uint8_t v;
  regRead(ADDR_ACC, 0x26, &v, 1);  // 读 REFERENCE：高通滤波器立刻以当前值为基准
  regRead(ADDR_ACC, 0x35, &v, 1);  // 读 INT2_SRC：清锁存
  delay(3);
  return digitalRead(PIN_ACC_INT2) == LOW;
}

// 被冲击叫醒：第一下已经在睡着时发生了，当作"第一次敲击"记下，接着用软件等第二下
void motionSeedTap() {
  uint32_t now = millis();
  ts.haveFirst = true;
  ts.firstTapAt = now;
  ts.lastOnset = now;  // 余振期内不再算起跳
}

void motionSetAxes(int axTop, int sgTop, int axKey, int sgKey) {
  s_axTop = axTop; s_sgTop = sgTop; s_axKey = axKey; s_sgKey = sgKey;
  s_axN = 3 - axTop - axKey;
  s_pose = POSE_UNKNOWN;  // 下一次稳定后重新确认
}
void motionRaw(float g[3]) { for (int i = 0; i < 3; i++) g[i] = s_raw[i]; }

uint8_t motionPose() { return s_pose; }
bool motionFlat() { return s_flat; }
uint32_t motionPoseChangedAt() { return s_poseAt; }
void motionGravity(float &t, float &k, float &n) { t = s_gT; k = s_gK; n = s_gN; }
void motionTapEnable(bool on) { s_tapOn = on; }
bool motionTapEnabled() { return s_tapOn; }
void motionTapDebug(bool on) { s_tapDbg = on; }
void motionSetTapThr(float g) { s_tapThr = g; }
float motionTapThr() { return s_tapThr; }
