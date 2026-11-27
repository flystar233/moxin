#include "motion.h"

#include "config.h"
#include "events.h"
#include "board/i2c.h"

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
static constexpr float ACC_LSB_PER_G = 8192.0f;  // ±4g
static int s_shockThreshold = 0;

static volatile bool s_tapOn = true;
static volatile bool s_readingUi = false;  // 现在是阅读界面（由主循环告诉）
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

static bool writeChecked(uint8_t reg, uint8_t value) {
  uint8_t actual = 0;
  return regWrite(ADDR_ACC, reg, value) &&
         regRead(ADDR_ACC, reg, &actual, 1) && actual == value;
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

static uint8_t currentGravity() {
  float t = s_raw[s_axTop] * s_sgTop, k = s_raw[s_axKey] * s_sgKey, n = s_raw[s_axN];
  s_gT = t; s_gK = k; s_gN = n;
  bool flat;
  uint8_t p = classify(t, k, n, flat);
  s_flat = flat;
  return p;
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
  uint32_t keyAt = g_lastKeyActivity;
  if (keyAt && now - keyAt < TAP_IGNORE_AFTER_KEY_MS) {
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
    // 平放（手托着或放桌上）也认双击，至少一下要够重，免得碰一下桌子就翻页。
    bool strongOk = !s_flat || fmaxf(ts.firstAd, ad) >= TAP_FLAT_MIN_G;  // 至少有一下够重
    // 看界面而不是看姿态：平放在桌上、或者关了"横放自动切桌面"躺着看时，姿态不是"竖拿"，也要能敲
    bool poseOk = s_readingUi && now - s_poseAt >= TAP_AFTER_POSE_MS;
    const char *why = !s_tapOn ? "（敲击翻页已关，忽略）"
                    : !poseOk  ? "（不在阅读界面或刚换姿态，忽略）"
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
  int normal = s_axN;
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
    float g[3] = {r[0] / ACC_LSB_PER_G, r[1] / ACC_LSB_PER_G, r[2] / ACC_LSB_PER_G};
    // 浅睡后重新对准基线，保留 INT2 已经记下的第一下。
    bool gap = lastSample && now - lastSample > 50;
    lastSample = now;
    int axis = s_axN;
    if (axis != normal) { normal = axis; ts = TapState{}; ts.base = g[axis]; }
    if (first || gap) { ts.base = g[axis]; ts.prevAd = 0; }
    for (int i = 0; i < 3; i++) { lp[i] = (first || gap) ? g[i] : lp[i] + 0.04f * (g[i] - lp[i]); s_raw[i] = lp[i]; }
    first = false;
    uint8_t p = currentGravity();
    if (p != POSE_UNKNOWN && p != s_pose) {
      if (p != cand) { cand = p; candAt = now; }
      else if (now - candAt >= POSE_STABLE_MS) {
        s_pose = p;
        s_poseAt = now;
        postEvent(EV_POSE, p);
      }
    } else {
      cand = POSE_UNKNOWN;
    }

    tapStep(g[axis], now);
  }
}

bool motionBegin() {
  uint8_t who = 0;
  if (!regRead(ADDR_ACC, 0x0F, &who, 1) || who != 0x11) return false;
  // 先用样板验证过的 100Hz 唤醒，再切 200Hz。任何关键配置失败都交给上层重试。
  if (!writeChecked(0x20, 0x57) || !writeChecked(0x23, 0x90) ||
      !writeChecked(0x25, 0x00) || !writeChecked(0x22, 0x00) ||
      !writeChecked(0x21, 0x00) || !writeChecked(0x24, 0x00) ||
      !writeChecked(0x30, 0x00) || !writeChecked(0x34, 0x00)) {
    Serial.println("[姿态] SC7A20H 配置读回失败");
    return false;
  }
  delay(10);
  if (!writeChecked(0x20, 0x67)) {
    Serial.println("[姿态] SC7A20H 切换 200Hz 失败");
    return false;
  }
  delay(25);
  float sum[3] = {};
  int samples = 0;
  for (int i = 0; i < 9 && samples < 4; i++) {
    int16_t r[3];
    if (readRaw(r)) {
      float g[3] = {r[0] / ACC_LSB_PER_G, r[1] / ACC_LSB_PER_G, r[2] / ACC_LSB_PER_G};
      float magnitudeSquared = g[0]*g[0] + g[1]*g[1] + g[2]*g[2];
      if (magnitudeSquared > 0.09f && magnitudeSquared < 6.25f) {
        for (int axis = 0; axis < 3; ++axis) sum[axis] += g[axis];
        ++samples;
      }
    }
    delay(5);
  }
  if (samples < 4) {
    Serial.println("[姿态] SC7A20H 没有足够的有效采样");
    return false;
  }
  for (int axis = 0; axis < 3; ++axis) s_raw[axis] = sum[axis] / samples;
  s_pose = currentGravity();
  s_poseAt = millis();
  ts = TapState{};
  s_shockThreshold = 0;
  pinMode(PIN_ACC_INT2, INPUT_PULLDOWN);  // SC7A20H 无中断时输出为高阻
  if (xTaskCreatePinnedToCore(motionTask, "motion", 4096, nullptr, 3, nullptr, 0) != pdPASS) return false;
  Serial.println("[姿态] SC7A20H 已开始采样（200Hz / ±4g）");
  return true;
}

// ---------------- 浅睡相关 ----------------
bool motionPause(bool on) {
  s_pauseReq = on;
  if (!on) return true;
  uint32_t t0 = millis();
  while (!s_paused && millis() - t0 < 35) delay(1);
  return s_paused;
}

uint32_t motionTapWaitMs(uint32_t now) {
  if (!ts.haveFirst) return 0;
  uint32_t gap = now - ts.firstTapAt;
  // 正好 600ms 仍可配对；超过这个窗口才允许浅睡。
  return gap <= TAP_GAP_MAX_MS ? TAP_GAP_MAX_MS + 1 - gap : 0;
}

// 使用样板测试验证过的中断发生器 1 → INT2 (GPIO8)。
// 高通只作用于中断，姿态和软件双击仍读原始数据。
bool motionShockArm() {
  const int threshold = constrain((int)(SHOCK_WAKE_G / 0.032f + 0.5f), 1, 127);
  if (s_shockThreshold != threshold) {
    if (!writeChecked(0x25, 0x00) || !writeChecked(0x21, 0x01) ||
        !writeChecked(0x32, threshold) || !writeChecked(0x33, 0x00) ||
        !writeChecked(0x30, 0x3F) || !writeChecked(0x24, 0x08) ||
        !writeChecked(0x25, 0x40)) return false;
    s_shockThreshold = threshold;
  }
  uint8_t v;
  if (!regRead(ADDR_ACC, 0x26, &v, 1) || !regRead(ADDR_ACC, 0x31, &v, 1)) {
    s_shockThreshold = 0;
    return false;
  }
  delay(3);
  return digitalRead(PIN_ACC_INT2) == LOW;
}

// GPIO 唤醒后确认中断源，记下第一下并清锁存。硬件只知道冲击达到门限，不知道峰值。
bool motionSeedTap(uint32_t now) {
  if (digitalRead(PIN_ACC_INT2) != HIGH) return false;
  uint8_t src;
  if (!regRead(ADDR_ACC, 0x31, &src, 1) || !(src & 0x40)) return false;
  ts.haveFirst = true;
  ts.firstTapAt = now;
  ts.lastOnset = now;  // 余振期内不再算起跳
  ts.firstAd = s_shockThreshold * 0.032f;
  return true;
}

void motionSetAxes(int axTop, int sgTop, int axKey, int sgKey, bool initialPose) {
  if (axTop < 0 || axTop > 2 || axKey < 0 || axKey > 2 || axTop == axKey ||
      (sgTop != -1 && sgTop != 1) || (sgKey != -1 && sgKey != 1)) return;
  if (axTop == s_axTop && sgTop == s_sgTop && axKey == s_axKey && sgKey == s_sgKey) return;
  s_axTop = axTop; s_sgTop = sgTop; s_axKey = axKey; s_sgKey = sgKey;
  s_axN = 3 - axTop - axKey;
  // 开机用现有样本判断首屏；运行中重新校准时仍由采样任务确认、通知界面。
  s_pose = initialPose ? currentGravity() : POSE_UNKNOWN;
  s_poseAt = millis();
}
void motionRaw(float g[3]) { for (int i = 0; i < 3; i++) g[i] = s_raw[i]; }

uint8_t motionPose() { return s_pose; }
bool motionFlat() { return s_flat; }
uint32_t motionPoseChangedAt() { return s_poseAt; }
void motionGravity(float &t, float &k, float &n) { t = s_gT; k = s_gK; n = s_gN; }
void motionTapEnable(bool on) { s_tapOn = on; }
void motionSetReadingUi(bool on) { s_readingUi = on; }
bool motionTapEnabled() { return s_tapOn; }
void motionTapDebug(bool on) { s_tapDbg = on; }
void motionSetTapThr(float g) { s_tapThr = g; }
float motionTapThr() { return s_tapThr; }

// 深睡前：加速度计降到 10Hz 低功耗，不出中断（醒来 motionBegin 会重新配置）
void motionLowPower() {
  if (!motionPause(true)) return;
  regWrite(ADDR_ACC, 0x22, 0x00);  // INT1 不映射
  regWrite(ADDR_ACC, 0x25, 0x00);  // INT2 不映射
  regWrite(ADDR_ACC, 0x20, 0x2F);  // CTRL_REG1：10Hz，低功耗，XYZ
}
