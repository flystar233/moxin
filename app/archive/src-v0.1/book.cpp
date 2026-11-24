#include "book.h"

#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>

#include "config.h"
#include "guoqinlun.h"
#include "hw.h"

static const uint32_t MAX_BOOK_BYTES = 4608 * 1024;  // PSRAM 8MB：书最多 4.5MB，留出字库（最大约 2.7MB）和别的

static bool s_sdOk = false;
static std::vector<String> s_paths;   // TF 卡上的 .txt
static int s_cur = -1;                // 在书单里的下标：-1 = 内置，-2 = 卡上的书但现在书单里找不到（卡拔了）
static String s_curPath = "<内置>";    // 当前书的路径，进度按它存，换卡、拔卡都不影响
static String s_title, s_status;
static char *s_buf = nullptr;         // PSRAM 里的整本书（内置时指向 flash）
static uint32_t s_len = 0;
static bool s_ownBuf = false;

static Layout s_L{};
static std::vector<uint32_t> s_pages;  // 每页起点（字节位置）
static int s_page = 0;
static Preferences s_prefs;

// ---------------------------------------------------------------------
uint32_t utf8Next(const char *s, uint32_t n, uint32_t &i) {
  uint8_t c = s[i];
  int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
  if (i + len > n) len = 1;
  uint32_t cp = len == 1 ? (c < 0x80 ? c : '?') : (c & (0xFF >> (len + 1)));
  for (int k = 1; k < len; k++) {
    uint8_t b = s[i + k];
    if ((b & 0xC0) != 0x80) { len = 1; cp = '?'; break; }
    cp = (cp << 6) | (b & 0x3F);
  }
  i += len;
  return cp;
}

static bool isSpaceCp(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == 0x3000 || cp == 0xA0; }

// 行尾允许挂出去的标点，避免它们出现在行首
static bool isClosingPunct(uint32_t cp) {
  static const uint32_t P[] = {0xFF0C, 0x3002, 0x3001, 0xFF1B, 0xFF1A, 0xFF1F, 0xFF01, 0xFF09,
                               0x201D, 0x2019, 0x300B, 0x300D, 0x300F, 0x3011, ',', '.', ';', ':', '!', '?', ')'};
  for (uint32_t p : P)
    if (p == cp) return true;
  return false;
}

// 从 start 开始排最多 maxLines 行，返回下一页的起点
static uint32_t layoutFrom(uint32_t start, TextLine *out, int maxLines, int *outCount) {
  const char *t = s_buf;
  const uint32_t n = s_len;
  uint32_t pos = start;
  bool para = start == 0 || t[start - 1] == '\n';
  int cnt = 0;
  while (pos < n && cnt < maxLines) {
    if (t[pos] == '\n' || t[pos] == '\r') { pos++; para = true; continue; }
    if (para) {  // 段首：跳过原文自带的空格、全角空格缩进
      uint32_t j = pos;
      while (j < n) {
        uint32_t k = j;
        if (!isSpaceCp(utf8Next(t, n, k))) break;
        j = k;
      }
      pos = j;
      if (pos >= n || t[pos] == '\n' || t[pos] == '\r') continue;  // 空行
    }
    TextLine ln{pos, 0, para};
    int w = para ? s_L.indentWidth : 0;
    para = false;
    while (pos < n && t[pos] != '\n' && t[pos] != '\r') {
      uint32_t j = pos;
      uint32_t cp = utf8Next(t, n, j);
      int cw = s_L.width(cp);
      if (w + cw > s_L.maxWidth && pos > ln.start && !isClosingPunct(cp)) break;
      w += cw;
      pos = j;
      if (w > s_L.maxWidth) break;  // 挂出去的标点只许一个
    }
    ln.len = pos - ln.start;
    if (out) out[cnt] = ln;
    cnt++;
  }
  if (outCount) *outCount = cnt;
  return pos;
}

// ---------------------------------------------------------------------
static String keyFor(const String &path) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (size_t i = 0; i < path.length(); i++) h = (h ^ (uint8_t)path[i]) * 16777619u;
  char k[12];
  snprintf(k, sizeof(k), "p%08lx", (unsigned long)h);
  return k;
}

static String curPath() { return s_curPath; }

static void saveProgress() {
  if (s_pages.empty()) return;
  s_prefs.putUInt(keyFor(curPath()).c_str(), s_pages[s_page]);
}

static void scanDir(const char *dir) {
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (f.isDirectory()) continue;
    String name = f.name();
    String lower = name;
    lower.toLowerCase();
    if (!lower.endsWith(".txt") || name.startsWith(".") || name.startsWith("_")) continue;
    String full = String(dir);
    if (!full.endsWith("/")) full += "/";
    full += name;
    s_paths.push_back(full);
    if (s_paths.size() >= 64) break;
  }
}

// TF 卡只在列目录、读书的时候上电挂载，用完卸载并关掉卡电源（书已经整本在 PSRAM 里），省电
static bool sdMount() {
  sdPower(true);
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) return true;
  SD_MMC.end();
  sdPower(false);
  return false;
}

static void sdUnmount() {
  SD_MMC.end();
  // 先把 CLK、CMD、D0 拉低再关卡电源，免得从信号线反灌给卡（文档：固件约定 CARD1）
  for (int p : {PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0}) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  sdPower(false);
}

bool storageMount() { return sdMount(); }
void storageUnmount() { sdUnmount(); }

bool bookStorageBegin() {
  s_prefs.begin("reader", false);
  s_sdOk = sdMount();
  if (!s_sdOk) {
    Serial.println("[书] 没有 TF 卡或挂载失败，只用内置文章");
    return false;
  }
  scanDir("/books");
  scanDir("/");
  Serial.printf("[书] TF 卡 %llu MB，找到 %u 本 TXT\n", SD_MMC.cardSize() >> 20, (unsigned)s_paths.size());
  for (auto &p : s_paths) Serial.printf("      %s\n", p.c_str());
  sdUnmount();
  return true;
}

// 重新扫描 TF 卡（打开菜单时调用）：卡可以随时换，没有插卡检测，就每次进菜单看一眼。
// 正在读的书已经整本在 PSRAM 里，拔卡也能接着读；它在新书单里的位置按路径找回来。
bool bookRescan() {
  String cur = s_cur == -1 ? String() : s_curPath;
  s_paths.clear();
  s_sdOk = sdMount();
  if (s_sdOk) {
    scanDir("/books");
    scanDir("/");
    sdUnmount();
  }
  int found = -1;
  for (int i = 0; i < (int)s_paths.size(); i++)
    if (s_paths[i] == cur) found = i;
  if (s_cur != -1) s_cur = found >= 0 ? found : -2;  // 卡拔了或书删了：-2，内存里的书照样能读，进度照样存
  Serial.printf("[书] 重新扫描：%s，%u 本\n", s_sdOk ? "TF 卡在" : "没有 TF 卡", (unsigned)s_paths.size());
  return s_sdOk;
}

int bookCount() { return s_paths.size(); }

String bookTitleAt(int i) {
  if (i < 0) return GUOQINLUN_TITLE;
  String p = s_paths[i];
  int slash = p.lastIndexOf('/');
  String n = p.substring(slash + 1);
  int dot = n.lastIndexOf('.');
  return dot > 0 ? n.substring(0, dot) : n;
}

static void freeBuf() {
  if (s_ownBuf && s_buf) free(s_buf);
  s_buf = nullptr;
  s_len = 0;
  s_ownBuf = false;
}

static bool loadBuiltin() {
  freeBuf();
  s_buf = (char *)GUOQINLUN_TEXT;
  s_len = strlen(GUOQINLUN_TEXT);
  s_cur = -1;
  s_curPath = "<内置>";
  s_title = GUOQINLUN_TITLE;
  return true;
}

// 粗查是不是 UTF-8：GBK 的中文在 UTF-8 里大多是非法序列
static bool looksUtf8(const char *b, uint32_t n) {
  uint32_t bad = 0, cnt = 0;
  for (uint32_t i = 0; i < n && cnt < 20000; cnt++) {
    uint32_t j = i;
    uint32_t cp = utf8Next(b, n, j);
    if (cp == '?' && b[i] != '?') bad++;
    i = j;
  }
  return bad * 50 < cnt;  // 坏字节少于 2%
}

bool bookOpen(int i) {
  if (i < 0 || i >= (int)s_paths.size() || !s_sdOk) {
    loadBuiltin();
    s_status = "内置文章";
    s_prefs.putString("last", "");
    return true;
  }
  if (!sdMount()) { s_status = "TF 卡挂载失败"; return false; }
  File f = SD_MMC.open(s_paths[i]);
  if (!f) { s_status = "打不开文件"; sdUnmount(); return false; }
  uint32_t size = f.size();
  if (size > MAX_BOOK_BYTES) { s_status = "文件太大（>4.5MB）"; f.close(); sdUnmount(); return false; }
  char *buf = (char *)ps_malloc(size + 1);
  if (!buf) { s_status = "内存不够"; f.close(); sdUnmount(); return false; }
  uint32_t t0 = millis(), got = 0;
  while (got < size) {
    int r = f.read((uint8_t *)buf + got, min<uint32_t>(32768, size - got));
    if (r <= 0) break;
    got += r;
  }
  f.close();
  sdUnmount();
  buf[got] = 0;
  uint32_t skip = (got >= 3 && (uint8_t)buf[0] == 0xEF && (uint8_t)buf[1] == 0xBB && (uint8_t)buf[2] == 0xBF) ? 3 : 0;
  if (!looksUtf8(buf + skip, got - skip)) {
    free(buf);
    s_status = "不是 UTF-8 编码（请转成 UTF-8 再放进卡里）";
    Serial.printf("[书] %s：%s\n", s_paths[i].c_str(), s_status.c_str());
    return false;
  }
  freeBuf();
  if (skip) memmove(buf, buf + skip, got - skip + 1);
  s_buf = buf;
  s_len = got - skip;
  s_ownBuf = true;
  s_cur = i;
  s_curPath = s_paths[i];
  s_title = bookTitleAt(i);
  s_status = "已打开";
  s_prefs.putString("last", s_paths[i]);
  Serial.printf("[书] 打开 %s，%lu 字节，读卡 %lu ms\n", s_paths[i].c_str(), (unsigned long)s_len,
                (unsigned long)(millis() - t0));
  return true;
}

bool bookOpenLast() {
  String last = s_prefs.getString("last", "");
  for (int i = 0; i < (int)s_paths.size(); i++)
    if (s_paths[i] == last && bookOpen(i)) return true;
  if (last.length() == 0 && s_prefs.isKey("last")) return bookOpen(-1);  // 上次看的就是内置
  for (int i = 0; i < (int)s_paths.size(); i++)
    if (bookOpen(i)) return true;
  return bookOpen(-1);
}

int bookCurrentIndex() { return s_cur; }
String bookTitle() { return s_title; }
String bookStatus() { return s_status; }
const char *bookText() { return s_buf; }
uint32_t bookSize() { return s_len; }

void bookPaginate(const Layout &L) {
  s_L = L;
  s_pages.clear();
  uint32_t t0 = millis();
  uint32_t pos = 0;
  while (pos < s_len) {
    s_pages.push_back(pos);
    uint32_t next = layoutFrom(pos, nullptr, L.linesPerPage, nullptr);
    if (next <= pos) break;
    pos = next;
    // 剩下的只有空白就不再开新页
    uint32_t j = pos;
    while (j < s_len) {
      uint32_t k = j;
      uint32_t cp = utf8Next(s_buf, s_len, k);
      if (!(isSpaceCp(cp) || cp == '\n' || cp == '\r')) break;
      j = k;
    }
    if (j >= s_len) break;
  }
  if (s_pages.empty()) s_pages.push_back(0);
  // 按保存的字节位置找回页码
  uint32_t saved = s_prefs.getUInt(keyFor(curPath()).c_str(), 0);
  s_page = 0;
  for (int p = 0; p < (int)s_pages.size(); p++)
    if (s_pages[p] <= saved) s_page = p;
  Serial.printf("[书] 《%s》分页 %d 页，用时 %lu ms，从第 %d 页开始\n", s_title.c_str(), (int)s_pages.size(),
                (unsigned long)(millis() - t0), s_page + 1);
}

int bookPageCount() { return s_pages.size(); }
int bookPage() { return s_page; }

void bookGoto(int page) {
  if (page < 0) page = 0;
  if (page >= (int)s_pages.size()) page = s_pages.size() - 1;
  s_page = page;
  saveProgress();
}

int bookPageLines(int page, TextLine *out, int maxLines) {
  if (page < 0 || page >= (int)s_pages.size()) return 0;
  int cnt = 0;
  layoutFrom(s_pages[page], out, maxLines, &cnt);
  return cnt;
}
