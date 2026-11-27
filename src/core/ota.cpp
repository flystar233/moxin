#include "ota.h"

#include "esp_ota_ops.h"

// 试运行这么久没出事就确认
static constexpr uint32_t OTA_TRIAL_MS = 30000;

// Arduino 框架自带的 bootloader 开了回滚，但框架在 setup() 之前就把新固件标成有效
// （esp32-hal-misc.c 的 initArduino），回滚等于没用。覆盖这个弱函数返回 true，改成由这里决定什么时候确认。
// esp32-hal-misc.c 是 C 文件，要 extern "C"。
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {

static bool s_pending = false;

void begin() {
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  s_pending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if (s_pending) Serial.printf("[升级] 新固件试运行（%s），%lu 秒内不出事就确认\n", run->label, (unsigned long)(OTA_TRIAL_MS / 1000));
  if (const esp_partition_t *bad = esp_ota_get_last_invalid_partition())
    Serial.printf("[升级] %s 里的新固件没跑稳，已退回现在这版（%s）\n", bad->label, run ? run->label : "?");
}

void confirm(const char *why) {
  if (!s_pending) return;
  esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  s_pending = false;
  Serial.printf("[升级] 新固件已确认（%s）%s\n", why, e == ESP_OK ? "" : esp_err_to_name(e));
}

void loop() {
  if (s_pending && millis() > OTA_TRIAL_MS) confirm("试运行满时间");
}

String state() {
  const esp_partition_t *run = esp_ota_get_running_partition();
  String s = String("程序区 ") + (run ? run->label : "?");
  if (s_pending) s += "，试运行中";
  if (esp_ota_get_last_invalid_partition()) s += "，上次升级已退回";
  return s;
}

}  // namespace ota
