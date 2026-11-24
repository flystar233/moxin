#include "settings.h"

#include <Preferences.h>

#include "config.h"

static Settings s_set;
static Preferences s_nvs;
static bool s_open = false;

Settings &settings() { return s_set; }

static void defaults(Settings &s) {
  s.fontFamily = "wenkai";
  s.fontSize = BODY_FONT_DEFAULT;
  s.lineSpacing = 1;
  s.margin = 1;
  s.fullEvery = READ_FULL_EVERY;
  s.tapTurn = true;
  s.tapThr = TAP_THR_DEFAULT;
  s.pfBlack = EPD_PARTIAL_FRAMES;
  s.pfWhite = EPD_PARTIAL_WHITE_EXTRA;
  s.ttsModel = TTS_MODEL;
  s.ttsWs = "";
  s.ttsKey = "";
  s.ttsVoice = TTS_VOICE;
  s.ttsVolume = TTS_VOL_DEFAULT;
  s.ttsAutoPage = true;
  s.autoDesk = true;
  s.wifiOn = true;
  s.autoTime = true;
  s.lightSleep = true;
  s.deepSleepMin = DEEP_SLEEP_MIN_DEFAULT;
  s.calibrated = false;
  s.axTop = AX_TOP;
  s.sgTop = AX_TOP_SIGN;
  s.axKey = AX_KEYS;
  s.sgKey = AX_KEYS_SIGN;
}

void settingsLoad() {
  if (!s_open) s_open = s_nvs.begin("app", false);
  Settings &s = s_set;
  defaults(s);
  if (s_nvs.isKey("ffam")) {
    s.fontFamily = s_nvs.getString("ffam", s.fontFamily);
    s.fontSize = s_nvs.getUChar("fsz", s.fontSize);
  } else if (s_nvs.isKey("font")) {  // v0.1 存的是路径：/fonts/wenkai_18.efnt，"" = 系统字
    String p = s_nvs.getString("font", "");
    int slash = p.lastIndexOf('/'), u = p.lastIndexOf('_'), dot = p.lastIndexOf('.');
    if (p.length() == 0) s.fontFamily = "";
    else if (u > slash && dot > u) {
      s.fontFamily = p.substring(slash + 1, u);
      s.fontSize = p.substring(u + 1, dot).toInt();
    }
  }
  s.lineSpacing = s_nvs.getUChar("ls", s.lineSpacing) % 3;
  s.margin = s_nvs.getUChar("mg", s.margin) % 3;
  s.fullEvery = constrain(s_nvs.getInt("fullN", s.fullEvery), 1, 50);
  s.tapTurn = s_nvs.getBool("tap", s.tapTurn);
  s.tapThr = s_nvs.getFloat("thr", s.tapThr);
  s.pfBlack = s_nvs.getUChar("pfB", s.pfBlack);
  s.pfWhite = s_nvs.getUChar("pfW", s.pfWhite);
  s.ttsModel = s_nvs.getString("ttsModel", s.ttsModel);
  s.ttsWs = s_nvs.getString("ttsWs", s.ttsWs);
  s.ttsKey = s_nvs.getString("ttsKey", s.ttsKey);
  s.ttsVoice = s_nvs.getString("ttsVoice", s.ttsVoice);
  s.ttsVolume = constrain(s_nvs.getUChar("ttsVol", s.ttsVolume), 0, 100);
  s.ttsAutoPage = s_nvs.getBool("ttsPage", s.ttsAutoPage);
  s.autoDesk = s_nvs.getBool("autoDesk", s.autoDesk);
  s.wifiOn = s_nvs.getBool("wifiOn", s.wifiOn);
  s.autoTime = s_nvs.getBool("autoTime", s.autoTime);
  s.lightSleep = s_nvs.getBool("sleep", s.lightSleep);
  s.deepSleepMin = s_nvs.getUChar("deepMin", s.deepSleepMin);
  s.calibrated = s_nvs.isKey("axT");
  if (s.calibrated) {
    s.axTop = s_nvs.getInt("axT", s.axTop);
    s.sgTop = s_nvs.getInt("sgT", s.sgTop);
    s.axKey = s_nvs.getInt("axK", s.axKey);
    s.sgKey = s_nvs.getInt("sgK", s.sgKey);
  }
}

void settingsSave() {
  if (!s_open) s_open = s_nvs.begin("app", false);
  const Settings &s = s_set;
  s_nvs.putString("ffam", s.fontFamily);
  s_nvs.putUChar("fsz", s.fontSize);
  s_nvs.putUChar("ls", s.lineSpacing);
  s_nvs.putUChar("mg", s.margin);
  s_nvs.putInt("fullN", s.fullEvery);
  s_nvs.putBool("tap", s.tapTurn);
  s_nvs.putFloat("thr", s.tapThr);
  s_nvs.putUChar("pfB", s.pfBlack);
  s_nvs.putUChar("pfW", s.pfWhite);
  s_nvs.putString("ttsModel", s.ttsModel);
  s_nvs.putString("ttsWs", s.ttsWs);
  s_nvs.putString("ttsKey", s.ttsKey);
  s_nvs.putString("ttsVoice", s.ttsVoice);
  s_nvs.putUChar("ttsVol", s.ttsVolume);
  s_nvs.putBool("ttsPage", s.ttsAutoPage);
  s_nvs.putBool("autoDesk", s.autoDesk);
  s_nvs.putBool("wifiOn", s.wifiOn);
  s_nvs.putBool("autoTime", s.autoTime);
  s_nvs.putBool("sleep", s.lightSleep);
  s_nvs.putUChar("deepMin", s.deepSleepMin);
  if (s.calibrated) {
    s_nvs.putInt("axT", s.axTop);
    s_nvs.putInt("sgT", s.sgTop);
    s_nvs.putInt("axK", s.axKey);
    s_nvs.putInt("sgK", s.sgKey);
  }
}

void settingsReset() {
  Settings keep = s_set;
  defaults(s_set);
  // 姿态校准是这块板子的硬件参数，不随"恢复默认"清掉
  s_set.calibrated = keep.calibrated;
  s_set.axTop = keep.axTop; s_set.sgTop = keep.sgTop;
  s_set.axKey = keep.axKey; s_set.sgKey = keep.sgKey;
  s_set.ttsKey = keep.ttsKey;  // 语音服务的 API Key / 业务空间 ID 也不清（要重新输一遍太麻烦）
  s_set.ttsWs = keep.ttsWs;
  settingsSave();
}

int lineSpacingX10(uint8_t i) { return i == 0 ? 13 : i == 2 ? 18 : 15; }
const char *lineSpacingName(uint8_t i) { return i == 0 ? "紧凑" : i == 2 ? "宽松" : "标准"; }
const char *marginName(uint8_t i) { return i == 0 ? "窄" : i == 2 ? "宽" : "标准"; }
