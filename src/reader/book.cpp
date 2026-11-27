#include "book.h"

#include <algorithm>

#include "board/storage.h"
#include "config.h"
#include "guoqinlun.h"
#include "import.h"
#include "layout.h"
#include "library.h"
#include "window.h"

// =====================================================================
// 状态
// =====================================================================
static String s_path, s_title, s_err, s_info;
static BookMeta s_m;
static bool s_open = false, s_builtin = false;
static String s_base;            // 缓存文件的公共前缀 /.reader/<编号>（内置为空）

// 窗口：正文里阅读位置附近的一段在内存里（内置文章本来就在 flash 里，用 s_builtinSpan）
static TextWindow s_tw(ps_malloc);
static TextSpan s_builtinSpan;
static const TextSpan &curSpan() { return s_builtin ? s_builtinSpan : s_tw.span(); }

static Layout s_L{};
static bool s_paged = false;
static std::vector<uint32_t> s_pages;  // 每页起点（字节位置）
static int s_page = 0;

static BookProgressFn s_progress = nullptr;

static const char *CACHE_DIR = "/.reader";
static const uint32_t META_MAGIC = 0x324D4B42;   // "BKM2"
static const uint32_t PAGES_MAGIC = 0x31504B42;  // "BKP1"
static const uint32_t IMPORT_VERSION = 1;        // 导入规则变了就加一，旧缓存自动重建
static const uint32_t CACHE_KEEP = 30;           // 卡上最多留几本书的缓存

void bookSetProgress(BookProgressFn fn) { s_progress = fn; }

// 进度回调：最多两秒刷一次提示框（局刷本身要 0.7 秒）
struct ProgressCtx {
  const char *what;
  uint32_t lastMs;
  int lastPct;
};
static bool onProgress(void *p, uint32_t done, uint32_t total) {
  ProgressCtx *c = (ProgressCtx *)p;
  int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
  if (s_progress && pct != c->lastPct && millis() - c->lastMs > 2000) {
    s_progress(c->what, pct);
    c->lastMs = millis();
    c->lastPct = pct;
  }
  return true;
}

static bool fail(const char *why) {
  s_err = why;
  Serial.printf("[书] 打不开：%s\n", why);
  return false;
}

// =====================================================================
// 卡上的缓存：<编号>.txt 正文、.bkm 目录信息、-<版面>.pg 分页表
// =====================================================================
static String cacheBase(const String &path) {
  uint64_t h = 1469598103934665603ull;  // FNV-1a 64
  for (size_t i = 0; i < path.length(); i++) h = (h ^ (uint8_t)path[i]) * 1099511628211ull;
  char b[40];
  snprintf(b, sizeof(b), "%s/%08lx%08lx", CACHE_DIR, (unsigned long)(h >> 32), (unsigned long)h);
  return b;
}

// 原文件的指纹：大小、修改时间、开头 4KB 的哈希
struct SrcStamp {
  uint32_t size = 0, time = 0, hash = 0;
  bool operator==(const SrcStamp &o) const { return size == o.size && time == o.time && hash == o.hash; }
};
static SrcStamp stampOf(File &f) {
  SrcStamp s;
  s.size = f.size();
  s.time = (uint32_t)f.getLastWrite();
  uint8_t b[512];
  uint32_t h = 2166136261u, left = 4096;
  f.seek(0);
  while (left) {
    int r = f.read(b, left < sizeof(b) ? left : sizeof(b));
    if (r <= 0) break;
    for (int i = 0; i < r; i++) h = (h ^ b[i]) * 16777619u;
    left -= r;
  }
  f.seek(0);
  s.hash = h;
  return s;
}

// ---- 二进制读写小工具 ----
struct Out {
  std::string s;
  void u8(uint8_t v) { s.push_back((char)v); }
  void u16(uint16_t v) { s.append((const char *)&v, 2); }
  void u32(uint32_t v) { s.append((const char *)&v, 4); }
  void str(const std::string &v) { u32(v.size()); s.append(v); }
};
struct In {
  const uint8_t *p, *e;
  bool ok = true;
  In(const uint8_t *begin, const uint8_t *end) : p(begin), e(end) {}
  bool need(size_t n) { if ((size_t)(e - p) < n) ok = false; return ok; }
  uint8_t u8() { if (!need(1)) return 0; return *p++; }
  uint16_t u16() { uint16_t v = 0; if (need(2)) { memcpy(&v, p, 2); p += 2; } return v; }
  uint32_t u32() { uint32_t v = 0; if (need(4)) { memcpy(&v, p, 4); p += 4; } return v; }
  std::string str() {
    uint32_t n = u32();
    if (!need(n)) return "";
    std::string v((const char *)p, n);
    p += n;
    return v;
  }
};

static bool writeFile(const String &path, const std::string &data) {
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;
  bool ok = f.write((const uint8_t *)data.data(), data.size()) == data.size();
  f.close();
  return ok;
}

static bool saveMeta(const String &base, const String &src, const SrcStamp &st, const BookMeta &m) {
  Out o;
  o.u32(META_MAGIC);
  o.u32(IMPORT_VERSION);
  o.u32(st.size);
  o.u32(st.time);
  o.u32(st.hash);
  o.str(src.c_str());  // 原文件路径放前面：清缓存时只读开头就知道是哪本书
  o.u32(m.len);
  o.u8(m.tocFromBook);
  o.str(m.title);
  o.str(m.author);
  o.str(m.info);
  o.u32(m.ch.size());
  for (auto &c : m.ch) { o.u32(c.off); o.u32(c.tOff); o.u16(c.tLen); }
  o.str(m.titles);
  o.u32(m.heads.size());
  for (uint32_t h : m.heads) o.u32(h);
  return writeFile(base + ".bkm", o.s);
}

static bool loadMeta(const String &base, const SrcStamp &st, BookMeta &m) {
  uint32_t len = 0;
  uint8_t *buf = sdReadAll(base + ".bkm", len, 8 * 1024 * 1024);
  if (!buf) return false;
  In in{buf, buf + len};
  bool ok = in.u32() == META_MAGIC && in.u32() == IMPORT_VERSION;
  SrcStamp s;
  s.size = in.u32();
  s.time = in.u32();
  s.hash = in.u32();
  ok = ok && s == st;
  if (ok) {
    m.clear();
    in.str();  // 原文件路径
    m.len = in.u32();
    m.tocFromBook = in.u8();
    m.title = in.str();
    m.author = in.str();
    m.info = in.str();
    uint32_t n = in.u32();
    if (in.need((size_t)n * 10)) {
      m.ch.resize(n);
      for (auto &c : m.ch) { c.off = in.u32(); c.tOff = in.u32(); c.tLen = in.u16(); }
    }
    m.titles = in.str();
    n = in.u32();
    if (in.need((size_t)n * 4)) {
      m.heads.resize(n);
      for (auto &h : m.heads) h = in.u32();
    }
    ok = in.ok;
  }
  free(buf);
  if (!ok) return false;
  File t = SD_MMC.open(base + ".txt");  // 正文还在、长度对得上
  ok = t && t.size() == m.len;
  if (t) t.close();
  return ok;
}

static String pagesPath(const Layout &L) {
  char b[16];
  snprintf(b, sizeof(b), "-%08lx.pg", (unsigned long)L.signature());
  return s_base + b;
}

static bool loadPages(const Layout &L) {
  uint32_t len = 0;
  uint8_t *buf = sdReadAll(pagesPath(L), len, 4 * 1024 * 1024);
  if (!buf) return false;
  In in{buf, buf + len};
  bool ok = in.u32() == PAGES_MAGIC && in.u32() == s_m.len && in.u32() == L.signature();
  uint32_t n = in.u32();
  ok = ok && n > 0 && in.need((size_t)n * 4);
  if (ok) {
    s_pages.resize(n);
    memcpy(s_pages.data(), in.p, (size_t)n * 4);
  }
  free(buf);
  return ok;
}

static void savePages(const Layout &L) {
  Out o;
  o.u32(PAGES_MAGIC);
  o.u32(s_m.len);
  o.u32(L.signature());
  o.u32(s_pages.size());
  o.s.append((const char *)s_pages.data(), s_pages.size() * 4);
  if (!writeFile(pagesPath(L), o.s)) Serial.println("[书] 分页表存不进卡");
}

// 删掉一本书的全部缓存（base = /.reader/<编号>）
static int removeCache(const String &base) {
  int n = 0;
  String name = base.substring(base.lastIndexOf('/') + 1);
  for (auto &e : sdList(CACHE_DIR, 2000))
    if (!e.dir && e.name.startsWith(name) && SD_MMC.remove(e.path)) n++;
  return n;
}

// 缓存太多了：按最近打开的顺序，删掉最久没读的书的缓存
static void trimCache() {
  std::vector<std::pair<uint32_t, String>> books;  // (最近打开的序号, base)
  for (auto &e : sdList(CACHE_DIR, 2000)) {
    if (e.dir || !e.name.endsWith(".bkm")) continue;
    File f = SD_MMC.open(e.path);
    uint8_t head[600];
    int r = f ? f.read(head, sizeof(head)) : 0;
    if (f) f.close();
    In in{head, head + (r > 0 ? r : 0)};
    in.u32(); in.u32(); in.u32(); in.u32(); in.u32();
    String src = in.str().c_str();
    books.push_back({in.ok ? libraryRecentSeq(src) : 0, e.path.substring(0, e.path.length() - 4)});
  }
  if (books.size() <= CACHE_KEEP) return;
  std::sort(books.begin(), books.end(), [](const std::pair<uint32_t, String> &a, const std::pair<uint32_t, String> &b) { return a.first < b.first; });
  for (size_t i = 0; i + CACHE_KEEP < books.size(); i++) {
    if (books[i].second == s_base) continue;
    Serial.printf("[书] 缓存太多，删掉 %s\n", books[i].second.c_str());
    removeCache(books[i].second);
  }
}

int bookCacheClear() {
  SdSession sd;
  if (!sd.ok) return 0;
  int n = 0;
  for (auto &e : sdList(CACHE_DIR, 2000))
    if (!e.dir && SD_MMC.remove(e.path)) n++;
  return n;
}

// ---- 导入 ----
struct FileSink : TextSink {
  File &f;
  explicit FileSink(File &file) : f(file) {}
  bool write(const char *p, uint32_t n) override { return f.write((const uint8_t *)p, n) == n; }
};
struct FileBytes : ByteSource {
  File &f;
  explicit FileBytes(File &file) : f(file) {}
  uint32_t size() override { return f.size(); }
  int read(uint8_t *buf, uint32_t n) override { return f.read(buf, n); }
};
struct FileEpub : EpubSource {
  File &f;
  explicit FileEpub(File &file) : f(file) {}
  uint32_t size() override { return f.size(); }
  bool read(uint32_t off, void *buf, uint32_t n) override { return f.seek(off) && f.read((uint8_t *)buf, n) == (int)n; }
};
struct FileText : TextReader {
  File &f;
  explicit FileText(File &file) : f(file) {}
  bool read(uint32_t off, char *buf, uint32_t n) override { return f.seek(off) && f.read((uint8_t *)buf, n) == (int)n; }
};

static bool isEpubPath(const String &p) {
  String l = p;
  l.toLowerCase();
  return l.endsWith(".epub");
}

static bool importBook(const String &path, File &src, const String &base, const SrcStamp &st, BookMeta &m) {
  uint32_t t0 = millis();
  SD_MMC.mkdir(CACHE_DIR);
  removeCache(base);  // 旧的正文、分页表都作废
  String tmp = base + ".tmp";
  File o = SD_MMC.open(tmp, FILE_WRITE);
  if (!o) return fail("卡上建不了缓存文件（卡满了？）");
  FileSink sink(o);
  ProgressCtx pc{"正在导入", millis(), -1};
  std::string err;
  bool ok;
  if (isEpubPath(path)) {
    FileEpub in(src);
    ok = importEpub(in, sink, m, err, CHAPTER_MAX, onProgress, &pc);
  } else {
    FileBytes in(src);
    ok = importTxt(in, sink, m, err, CHAPTER_MAX, onProgress, &pc);
  }
  o.close();
  if (!ok) {
    SD_MMC.remove(tmp);
    s_err = err.c_str();
    Serial.printf("[书] 导入失败：%s\n", err.c_str());
    return false;
  }
  if (!SD_MMC.rename(tmp, base + ".txt") || !saveMeta(base, path, st, m)) {
    removeCache(base);
    return fail("缓存写不进卡（卡满了？）");
  }
  Serial.printf("[书] 导入 %s：%s → %lu 字节，%u 章（%s），%lu ms\n", path.c_str(), m.info.c_str(), (unsigned long)m.len,
                (unsigned)m.ch.size(), m.tocFromBook ? "书里的目录" : "按标题认", (unsigned long)(millis() - t0));
  return true;
}

// =====================================================================
// 窗口
// =====================================================================
// 整本放得下就整本；放不下就 20%，最少 1MB，最多 BOOK_WINDOW_MAX
static uint32_t windowSize(uint32_t len) {
  if (len <= BOOK_WINDOW_MAX) return len;
  uint32_t w = len / 5;
  if (w < 1024 * 1024) w = 1024 * 1024;
  if (w > BOOK_WINDOW_MAX) w = BOOK_WINDOW_MAX;
  return w;
}

// 从卡上的缓存正文读：真要读时才挂卡、开文件
struct SdText : TextReader {
  const String path;
  SdSession *sd = nullptr;
  File f;
  bool used = false;
  explicit SdText(const String &p) : path(p) {}
  ~SdText() {
    if (f) f.close();
    delete sd;
  }
  bool read(uint32_t off, char *buf, uint32_t n) override {
    if (!used) {
      used = true;
      sd = new SdSession();
      if (sd->ok) f = SD_MMC.open(path);
    }
    return f && f.seek(off) && f.read((uint8_t *)buf, n) == (int)n;
  }
};

// 保证正文 [from, to) 在窗口里
static bool ensureWindow(uint32_t from, uint32_t to) {
  if (s_builtin || s_tw.covers(from, to)) return true;
  uint32_t t0 = millis();
  SdText rd(s_base + ".txt");
  if (!s_tw.ensure(from, to, rd)) {
    s_err = s_tw.capacity() ? "读不了卡上的缓存" : "内存不够";
    Serial.printf("[书] 窗口取不到：%s\n", s_err.c_str());
    return false;
  }
  const TextSpan &w = s_tw.span();
  Serial.printf("[书] 窗口 %lu～%lu KB（共 %lu KB），%lu ms\n", (unsigned long)(w.base / 1024),
                (unsigned long)(w.end() / 1024), (unsigned long)(s_m.len / 1024), (unsigned long)(millis() - t0));
  return true;
}

// =====================================================================
// 打开
// =====================================================================
static void closeBook() {
  s_tw.reset(0, 0);
  s_open = s_builtin = s_paged = false;
  s_pages.clear();
  s_m.clear();
  s_base = "";
}

static void openBuiltin() {
  closeBook();
  importMemory(GUOQINLUN_TEXT, strlen(GUOQINLUN_TEXT), s_m, CHAPTER_MAX);
  s_builtinSpan.p = GUOQINLUN_TEXT;
  s_builtinSpan.base = 0;
  s_builtinSpan.len = s_builtinSpan.total = s_m.len;
  s_path = BUILTIN_PATH;
  s_title = GUOQINLUN_TITLE;
  s_info = "内置文章";
  s_open = s_builtin = true;
}

// 打开书时（读卡、转码、解 EPUB）CPU 开到 240MHz，完了恢复
struct CpuBoost {
  uint32_t old;
  CpuBoost() : old(getCpuFrequencyMhz()) { setCpuFrequencyMhz(240); }
  ~CpuBoost() { setCpuFrequencyMhz(old); }
};

bool bookOpen(const String &path) {
  if (path == BUILTIN_PATH) { openBuiltin(); return true; }
  CpuBoost boost;
  uint32_t t0 = millis();
  SdSession sd;
  if (!sd.ok) return fail("没有 TF 卡");
  File f = SD_MMC.open(path);
  if (!f || f.isDirectory()) return fail("找不到这个文件");
  if (f.size() == 0) return fail("是空文件");
  const String base = cacheBase(path);
  const SrcStamp st = stampOf(f);
  BookMeta m;
  bool cached = loadMeta(base, st, m);
  if (!cached && !importBook(path, f, base, st, m)) return false;
  f.close();

  closeBook();
  s_m = std::move(m);
  s_base = base;
  s_path = path;
  s_title = s_m.title.length() ? String(s_m.title.c_str()) : titleFromPath(path);
  s_info = s_m.info.c_str();
  s_err = "";
  s_open = true;
  s_tw.reset(s_m.len, windowSize(s_m.len));
  if (!cached) trimCache();
  // 整本放得下就现在整本读进来（以后翻页都不用碰卡）；放不下的等知道读到哪再取
  if (s_m.len <= BOOK_WINDOW_MAX && !ensureWindow(0, s_m.len)) {
    closeBook();
    openBuiltin();
    return fail("内存不够");
  }
  Serial.printf("[书] 打开 %s：%s，%lu 字节，%u 章，%s，%lu ms\n", path.c_str(), s_info.c_str(), (unsigned long)s_m.len,
                (unsigned)s_m.ch.size(), cached ? "用缓存" : "新导入", (unsigned long)(millis() - t0));
  return true;
}

bool bookIsOpen() { return s_open; }
String bookPath() { return s_path; }
String bookTitle() { return s_title; }
String bookError() { return s_err; }
String bookInfo() { return s_info; }
uint32_t bookSize() { return s_m.len; }
const std::vector<Chapter> &bookChapters() { return s_m.ch; }

const char *bookSpan(uint32_t off, uint32_t len) {
  const TextSpan &s = curSpan();
  if (!s.p || !s.covers(off, off + len)) return nullptr;
  return s.p + (off - s.base);
}

const char *bookTextAt(uint32_t off, uint32_t len) {
  if (!s_open || off >= s_m.len || len == 0) return nullptr;
  if (len > s_m.len - off) len = s_m.len - off;
  // 多要一点：找句子要往后看标点，排版的最后一行也要看行尾后面那个字
  if (!ensureWindow(off, off + len + 256)) return nullptr;
  const TextSpan &s = curSpan();
  if (!s.covers(off, off + len)) return nullptr;
  return s.p + (off - s.base);
}

int bookChapterAt(uint32_t off) {
  auto it = std::upper_bound(s_m.ch.begin(), s_m.ch.end(), off, [](uint32_t o, const Chapter &c) { return o < c.off; });
  return (int)(it - s_m.ch.begin()) - 1;
}

String bookChapterTitle(int i) {
  if (i < 0 || i >= (int)s_m.ch.size()) return "";
  String s;
  s.concat(s_m.titles.data() + s_m.ch[i].tOff, s_m.ch[i].tLen);
  return s;
}

// =====================================================================
// 分页
// =====================================================================
void bookPaginate(const Layout &L, uint32_t keepOffset) {
  s_L = L;
  s_pages.clear();
  s_paged = false;
  if (!s_open) return;
  uint32_t t0 = millis();
  bool fromCache = false;
  if (!s_builtin) {
    SdSession sd;
    fromCache = sd.ok && loadPages(L);
  }
  if (!fromCache) {
    CpuBoost boost;
    ProgressCtx pc{"正在排版", millis(), -1};
    bool whole = s_builtin || s_tw.whole();
    bool ok;
    if (whole) {
      ok = paginate(s_m, L, &curSpan(), nullptr, 0, s_pages, onProgress, &pc);
    } else {
      // 大书：从卡上一段一段读着排。先把窗口放掉，腾出内存给分页缓冲，排完再按需要取回来
      s_tw.release();
      SdSession sd;
      File f = sd.ok ? SD_MMC.open(s_base + ".txt") : File();
      FileText rd(f);
      ok = f && paginate(s_m, L, nullptr, &rd, 256 * 1024, s_pages, onProgress, &pc);
      if (f) f.close();
    }
    if (!ok || s_pages.empty()) {
      Serial.println("[书] 分页失败");
      s_pages.assign(1, 0);
    } else if (!s_builtin) {
      SdSession sd;
      if (sd.ok) savePages(L);
    }
  }
  s_paged = true;
  s_page = bookPageOf(keepOffset);
  Serial.printf("[书] 《%s》%d 页（%s），%lu ms，当前第 %d 页\n", s_title.c_str(), (int)s_pages.size(),
                fromCache ? "分页表缓存" : "重新排版", (unsigned long)(millis() - t0), s_page + 1);
}

bool bookPaginatedWith(const Layout &L) { return s_paged && s_L == L; }
int bookPageCount() { return s_pages.size(); }
int bookPage() { return s_page; }

void bookSetPage(int p) {
  if (s_pages.empty()) { s_page = 0; return; }
  s_page = constrain(p, 0, (int)s_pages.size() - 1);
}

uint32_t bookPageStart(int p) { return (p >= 0 && p < (int)s_pages.size()) ? s_pages[p] : 0; }

int bookPageOf(uint32_t off) {
  auto it = std::upper_bound(s_pages.begin(), s_pages.end(), off);
  int p = (int)(it - s_pages.begin()) - 1;
  return p < 0 ? 0 : p;
}

uint32_t bookOffset() { return bookPageStart(s_page); }

int bookPermille() {
  if (s_pages.size() <= 1 || s_page >= (int)s_pages.size() - 1) return s_pages.empty() ? 0 : 1000;
  return (int)((uint64_t)bookOffset() * 1000 / (s_m.len ? s_m.len : 1));
}

int bookPageLines(int p, TextLine *out, int maxLines) {
  if (p < 0 || p >= (int)s_pages.size()) return 0;
  // 这一页（到下一页起点）再往后 4KB 要在窗口里：排版要看到行尾后面那个字
  uint32_t start = s_pages[p], end = p + 1 < (int)s_pages.size() ? s_pages[p + 1] : s_m.len;
  if (!ensureWindow(start > 0 ? start - 1 : 0, end + 4096)) return 0;
  int cnt = 0;
  layoutPage(curSpan(), s_m, s_L, start, out, maxLines, &cnt);
  return cnt;
}
