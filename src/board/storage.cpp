#include "storage.h"

#include "config.h"
#include "power.h"

static int s_refs = 0;
static bool s_lastOk = false;

bool sdMount() {
  if (s_refs > 0) { s_refs++; return true; }
  sdPower(true);
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  // 先试 40MHz（读书、读字库快一倍），不行再退回 20MHz
  static int s_freq = SDMMC_FREQ_HIGHSPEED;
  s_lastOk = SD_MMC.begin("/sdcard", true, false, s_freq);
  if (!s_lastOk && s_freq != SDMMC_FREQ_DEFAULT) {
    SD_MMC.end();
    s_freq = SDMMC_FREQ_DEFAULT;
    s_lastOk = SD_MMC.begin("/sdcard", true, false, s_freq);
    Serial.println("[卡] 40MHz 挂不上，改用 20MHz");
  }
  if (!s_lastOk) {
    SD_MMC.end();
    for (int p : {PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0}) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
    sdPower(false);
    return false;
  }
  s_refs = 1;
  return true;
}

void sdUnmount() {
  if (s_refs <= 0) return;
  if (--s_refs > 0) return;
  SD_MMC.end();
  // 先把 CLK、CMD、D0 拉低再关卡电源，免得从信号线反灌给卡
  for (int p : {PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0}) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  sdPower(false);
}

bool sdLastMountOk() { return s_lastOk; }

std::vector<DirEntry> sdList(const char *dir, size_t maxEntries) {
  std::vector<DirEntry> out;
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) return out;
  String base = dir;
  if (!base.endsWith("/")) base += "/";
  for (File f = d.openNextFile(); f && out.size() < maxEntries; f = d.openNextFile()) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    out.push_back({base + n, n, (uint32_t)f.size(), f.isDirectory()});
  }
  return out;
}

bool sdReadInto(const String &path, uint8_t *buf, uint32_t cap, uint32_t &len) {
  len = 0;
  File f = SD_MMC.open(path);
  if (!f) return false;
  uint32_t size = f.size();
  if (size > cap) return false;
  uint32_t got = 0;
  while (got < size) {
    int r = f.read(buf + got, min<uint32_t>(32768, size - got));
    if (r <= 0) break;
    got += r;
  }
  len = got;
  return got == size;
}

uint8_t *sdReadAll(const String &path, uint32_t &len, uint32_t maxBytes) {
  len = 0;
  File f = SD_MMC.open(path);
  if (!f) return nullptr;
  uint32_t size = f.size();
  if (size > maxBytes) return nullptr;
  uint8_t *buf = (uint8_t *)ps_malloc(size + 1);
  if (!buf) return nullptr;
  uint32_t got = 0;
  while (got < size) {
    int r = f.read(buf + got, min<uint32_t>(32768, size - got));
    if (r <= 0) break;
    got += r;
  }
  if (got != size) { free(buf); return nullptr; }
  buf[got] = 0;
  len = got;
  return buf;
}
