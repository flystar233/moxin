// Wi-Fi 配网界面：二维码 ① 连设备热点，② 打开配网网页（手机没自动弹出时用）；下面是进度
#include <vector>

#include "gfx.h"
#include "net/wifi.h"
#include "qrcode.h"
#include "text/utf8.h"
#include "views.h"
#include "widgets.h"

static int P(int v) { return gfx::px(v); }

// ---- 二维码（ESP-IDF 自带的 qrcode 组件）----
static uint8_t s_qr[61 * 61];
static int s_qrN = 0;

static void qrGrab(esp_qrcode_handle_t q) {
  s_qrN = esp_qrcode_get_size(q);
  if (s_qrN > 61) { s_qrN = 0; return; }
  for (int y = 0; y < s_qrN; y++)
    for (int x = 0; x < s_qrN; x++) s_qr[y * s_qrN + x] = esp_qrcode_get_module(q, x, y);
}

// 在 (cx, y) 居中画一个不超过 maxPx 的二维码（含 4 格白边），返回实际边长
static int drawQr(const String &text, int cx, int y, int maxPx) {
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = qrGrab;
  cfg.max_qrcode_version = 10;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  s_qrN = 0;
  esp_qrcode_generate(&cfg, text.c_str());
  if (!s_qrN) return 0;
  int m = maxPx / (s_qrN + 8);
  if (m < 2) m = 2;
  int side = (s_qrN + 8) * m, x0 = cx - side / 2;
  gfx::d().drawRect(x0, y, side, side, EPD_BLACK);  // 细边框，看得出码的范围
  for (int r = 0; r < s_qrN; r++)
    for (int c = 0; c < s_qrN; c++)
      if (s_qr[r * s_qrN + c]) gfx::d().fillRect(x0 + (c + 4) * m, y + (r + 4) * m, m, m, EPD_BLACK);
  return side;
}

static std::vector<String> wrap(const String &s, Font f, int maxW, int maxLines = 3) {
  return ui::wrap(s, f, maxW, maxLines, true);
}

static void frame(const char *title, const char *hint) { ui::frame(title, hint); }

class WifiSetupView : public View {
 public:
  WifiSetupView() { started_ = net::provStart(); }
  ~WifiSetupView() override { net::provStop(); }
  const char *name() const override { return "wifi-setup"; }
  bool acceptsPose() const override { return false; }   // 配网时转一下屏幕不要把它关了
  uint32_t idleMs() const override { return 600000; }   // 配网时不浅睡（Wi-Fi 会断）

  void onTick() override {
    net::provLoop();
    net::ProvState st = net::provState();
    if (st == net::PROV_OK && !okAt_) okAt_ = millis();
    // 连上以后热点再留 20 秒，让手机网页看到结果，然后关掉省电
    if (okAt_ && millis() - okAt_ > 20000 && st != net::PROV_OFF) net::provStop();
  }

  void onKey(Key k, Press p) override {
    if (okAt_ || (k == Key::Menu && p == Press::Long) || !started_) app::pop();
  }

  void draw() override {
    const int W = gfx::W(), H = gfx::H();
    if (!started_) {
      frame("Wi-Fi 配网", "按任意键返回");
      gfx::textCenter(W / 2, H / 2, "开不了热点，请重试");
      return;
    }
    if (okAt_) { drawOk(); return; }
    frame("Wi-Fi 配网", "长按菜单键退出");
    const bool land = W > H;
    const int th = ui::titleH(), fh = ui::footH();
    const int sl = gfx::lineH(Font::Small) + P(4);
    String wifiQr = "WIFI:T:WPA;S:" + net::provApSsid() + ";P:" + net::provApPass() + ";;";
    String urlQr = "http://192.168.4.1/";
    String cap1 = "热点 " + net::provApSsid() + "　密码 " + net::provApPass();  // 短一点，竖屏也放得下，密码不会被截掉

    int statusY;
    if (land) {
      // 左右两栏
      int colW = W / 2, y = th + P(8) + gfx::ascent(Font::Small);
      gfx::textCenter(colW / 2, y, "① 系统相机扫码连热点（微信扫不行）", Font::Small);
      gfx::textCenter(colW + colW / 2, y, "② 网页没自动弹出就扫这个", Font::Small);
      int qy = y + P(6);
      int qmax = min(colW - P(40), H - fh - qy - sl * 3 - P(10));
      int s1 = drawQr(wifiQr, colW / 2, qy, qmax);
      int s2 = drawQr(urlQr, colW + colW / 2, qy, qmax * 3 / 4);
      int cy = qy + max(s1, s2) + P(4) + gfx::ascent(Font::Small);
      gfx::textCenter(colW / 2, cy, gfx::fit(cap1, Font::Small, colW - P(8)), Font::Small);
      gfx::textCenter(colW + colW / 2, cy, "或浏览器打开 192.168.4.1", Font::Small);
      statusY = cy + sl;
    } else {
      // 上下排
      int y = th + P(8) + gfx::ascent(Font::Small);
      gfx::textCenter(W / 2, y, "① 系统相机扫码连热点（微信扫不行）", Font::Small);
      int s1 = drawQr(wifiQr, W / 2, y + P(6), min(W - P(120), P(190)));
      y += P(6) + s1 + P(4) + gfx::ascent(Font::Small);
      gfx::textCenter(W / 2, y, gfx::fit(cap1, Font::Small, W - 2 * ui::margin()), Font::Small);
      y += sl + P(6);
      gfx::textCenter(W / 2, y, "② 网页没自动弹出就扫这个", Font::Small);
      int s2 = drawQr(urlQr, W / 2, y + P(6), P(120));
      y += P(6) + s2 + P(4) + gfx::ascent(Font::Small);
      gfx::textCenter(W / 2, y, "或浏览器打开 192.168.4.1", Font::Small);
      statusY = y + sl;
    }
    // 进度
    const int M = ui::margin();
    int boxY = statusY - gfx::ascent(Font::Small) + P(4), boxH = H - fh - P(6) - boxY;
    gfx::d().drawRect(M, boxY, W - 2 * M, boxH, EPD_BLACK);
    auto lines = wrap(net::provMessage(), Font::Small, W - 4 * M, max(1, boxH / sl));
    int ly = boxY + (boxH - (int)lines.size() * sl) / 2 + gfx::ascent(Font::Small) + P(2);
    for (auto &l : lines) { gfx::textCenter(W / 2, ly, l, Font::Small); ly += sl; }
  }

 private:
  void drawOk() {
    const int W = gfx::W(), H = gfx::H();
    frame("Wi-Fi 配网", "按任意键返回");
    const int maxW = W - 2 * ui::margin() - 2 * P(16);
    int y = H / 2 - P(50);
    gfx::textCenter(W / 2, y, "Wi-Fi 已连接");
    gfx::textCenter(W / 2 + 1, y, "Wi-Fi 已连接");
    y += P(14) + gfx::ascent(Font::Small);
    y = ui::paragraph(W / 2, y, net::provMessage(), Font::Small, maxW, 3);
    y += P(10);
    ui::paragraph(W / 2, y, "账号已保存，以后每天自动对时。传书、升级固件在 Wi-Fi 菜单的\"无线传输\"里", Font::Small,
                  maxW, 3);
  }

  bool started_ = false;
  uint32_t okAt_ = 0;
};

View *newWifiSetupView() { return new WifiSetupView(); }

// =====================================================================
// 无线传输：屏上显示网址和二维码，浏览器打开就能上传书 / 字库 / 固件
// =====================================================================
class TransferView : public View {
 public:
  TransferView() { ok_ = net::transferStart(&why_); }
  ~TransferView() override { net::transferStop(); }
  const char *name() const override { return "transfer"; }
  bool acceptsPose() const override { return false; }
  uint32_t idleMs() const override { return 600000; }  // 传输时不浅睡

  void onTick() override {
    net::transferLoop();
    if (net::transferVersion() != ver_) {
      ver_ = net::transferVersion();
      app::invalidate(Refresh::Partial);
    }
  }

  void onKey(Key k, Press p) override {
    if (net::transferRebooting()) return;
    if (k == Key::Menu) app::pop();
    (void)p;
  }

  void draw() override {
    const int W = gfx::W(), H = gfx::H(), M = ui::margin();
    frame("无线传输", "按菜单键结束（Wi-Fi 随之关闭）");
    if (!ok_) {
      gfx::textCenter(W / 2, H / 2, "开不了：" + why_);
      return;
    }
    const int th = ui::titleH(), fh = ui::footH();
    const int sl = gfx::lineH(Font::Small) + P(4);
    int y = th + P(8) + gfx::ascent(Font::Small);
    String qr;
    if (net::transferViaAp()) {
      gfx::textCenter(W / 2, y, "① 系统相机扫码连设备热点", Font::Small);
      qr = "WIFI:T:WPA;S:" + net::provApSsid() + ";P:" + net::provApPass() + ";;";
    } else {
      gfx::textCenter(W / 2, y, "电脑或手机连 " + net::savedSsid() + "，浏览器打开", Font::Small);
      qr = net::transferUrl();
    }
    // 网址（大字）
    y += sl + P(6);
    String url = net::transferUrl();
    url.replace("http://", "");
    if (url.endsWith("/")) url.remove(url.length() - 1);
    if (net::transferViaAp()) {
      gfx::textCenter(W / 2, y, "② 连上后浏览器打开 " + url, Font::Small);
    } else {
      gfx::textCenter(W / 2, y + P(4), url);
      gfx::textCenter(W / 2 + 1, y + P(4), url);
    }
    y += P(10);
    int q = drawQr(qr, W / 2, y, min(W, H) - P(170));
    y += q + P(4) + gfx::ascent(Font::Small);
    if (net::transferViaAp()) gfx::textCenter(W / 2, y, "热点 " + net::provApSsid() + "　密码 " + net::provApPass(), Font::Small);
    else gfx::textCenter(W / 2, y, "扫码直接打开（也可以试 inkreader.local）", Font::Small);
    y += sl;
    gfx::textCenter(W / 2, y, gfx::fit("网页里能传书（TXT、EPUB）、字库、升级固件", Font::Small, W - 2 * M), Font::Small);
    // 最近一件事
    int boxY = y + P(10), boxH = H - fh - P(6) - boxY;
    if (boxH > sl) {
      gfx::d().drawRect(M, boxY, W - 2 * M, boxH, EPD_BLACK);
      auto lines = wrap(net::transferMessage(), Font::Small, W - 4 * M, max(1, boxH / sl));
      int ly = boxY + (boxH - (int)lines.size() * sl) / 2 + gfx::ascent(Font::Small) + P(2);
      for (auto &l : lines) { gfx::textCenter(W / 2, ly, l, Font::Small); ly += sl; }
    }
  }

 private:
  bool ok_ = false;
  String why_;
  uint32_t ver_ = 0;
};

View *newTransferView() { return new TransferView(); }
