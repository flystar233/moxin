#include "update.h"

#include "cJSON.h"
#include "config.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"

// IDF 自带的根证书包（编进了框架的 libmbedtls.a）。不 include esp_crt_bundle.h：
// WiFiClientSecure 里有个同名头文件，声明的是 arduino_ 开头的另一套函数
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

namespace net {

static esp_http_client_config_t httpConfig(const char *url) {
  esp_http_client_config_t c = {};
  c.url = url;
  c.crt_bundle_attach = esp_crt_bundle_attach;  // 校验服务器证书，防止被人冒充服务器推固件
  c.timeout_ms = 15000;
  c.buffer_size = 4096;
  c.buffer_size_tx = 1024;
  c.user_agent = "InkReader/" FW_VERSION;
  return c;
}

// "0.10.2" 这种按数字逐段比较：a 比 b 新返回正数
static int versionCompare(const String &a, const String &b) {
  const char *pa = a.c_str(), *pb = b.c_str();
  while (*pa || *pb) {
    long x = strtol(pa, (char **)&pa, 10), y = strtol(pb, (char **)&pb, 10);
    if (x != y) return x > y ? 1 : -1;
    if (*pa == '.') pa++;
    if (*pb == '.') pb++;
    if ((*pa && !isdigit((unsigned char)*pa)) || (*pb && !isdigit((unsigned char)*pb))) break;  // 后缀不比
  }
  return 0;
}

// 取一个小文件（version.json），跟随跳转
static bool httpGetSmall(const char *url, String &body, String *why) {
  static char buf[4096];
  esp_http_client_config_t cfg = httpConfig(url);
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (!h) { if (why) *why = "内存不够"; return false; }
  bool ok = false;
  for (int hop = 0; hop < 4; hop++) {
    esp_err_t e = esp_http_client_open(h, 0);
    if (e != ESP_OK) { if (why) *why = String("连不上升级服务器（") + esp_err_to_name(e) + "）"; break; }
    esp_http_client_fetch_headers(h);
    int code = esp_http_client_get_status_code(h);
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      esp_http_client_set_redirection(h);
      esp_http_client_close(h);
      continue;
    }
    if (code != 200) { if (why) *why = "升级服务器返回 " + String(code); break; }
    int n = 0, r;
    while (n < (int)sizeof(buf) - 1 && (r = esp_http_client_read(h, buf + n, sizeof(buf) - 1 - n)) > 0) n += r;
    buf[n] = 0;
    body = buf;
    ok = n > 0;
    if (!ok && why) *why = "版本信息是空的";
    break;
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  return ok;
}

static String jsonStr(cJSON *root, const char *key) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
  return cJSON_IsString(v) && v->valuestring ? String(v->valuestring) : String();
}

int fwCheck(FwInfo &info, String *why) {
  if (strstr(OTA_URL, "example.com")) { if (why) *why = "还没设置升级服务器（config.h 的 OTA_URL）"; return -1; }
  String body;
  if (!httpGetSmall(OTA_URL, body, why)) return -1;
  cJSON *root = cJSON_Parse(body.c_str());
  if (!root) { if (why) *why = "版本信息格式不对"; return -1; }
  info.version = jsonStr(root, "version");
  info.url = jsonStr(root, "url");
  info.notes = jsonStr(root, "notes");
  cJSON_Delete(root);
  if (!info.version.length() || !info.url.length()) { if (why) *why = "版本信息缺 version 或 url"; return -1; }
  if (!info.url.startsWith("http")) {  // 相对路径：接在 version.json 所在目录后面
    String base = OTA_URL;
    info.url = base.substring(0, base.lastIndexOf('/') + 1) + info.url;
  }
  Serial.printf("[升级] 服务器上 %s，本机 %s\n", info.version.c_str(), FW_VERSION);
  return versionCompare(info.version, FW_VERSION) > 0 ? 1 : 0;
}

bool fwInstall(const FwInfo &info, std::function<void(int)> progress, String *why) {
  esp_http_client_config_t http = httpConfig(info.url.c_str());
  esp_https_ota_config_t cfg = {};
  cfg.http_config = &http;
  esp_https_ota_handle_t h = nullptr;
  esp_err_t e = esp_https_ota_begin(&cfg, &h);  // 跟随跳转、检查镜像头都在这里面
  if (e != ESP_OK) {
    if (why) *why = String("下载不了（") + esp_err_to_name(e) + "）";
    return false;
  }
  int total = esp_https_ota_get_image_size(h);  // 服务器没给长度时是 -1
  uint32_t t0 = millis();
  while ((e = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
    if (progress && total > 0) progress((int)(100LL * esp_https_ota_get_image_len_read(h) / total));
  }
  if (e != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
    esp_https_ota_abort(h);
    if (why) *why = e != ESP_OK ? String("下载中断（") + esp_err_to_name(e) + "）" : String("没下载完整");
    return false;
  }
  e = esp_https_ota_finish(h);  // 校验镜像（SHA256），通过才把下次启动改到新程序区
  if (e != ESP_OK) {
    if (why) *why = e == ESP_ERR_OTA_VALIDATE_FAILED ? String("固件校验不过") : String("写不进去（") + esp_err_to_name(e) + "）";
    return false;
  }
  Serial.printf("[升级] %s 下载完成，%d 字节，%lu ms\n", info.version.c_str(), total, (unsigned long)(millis() - t0));
  return true;
}

}  // namespace net
