#include "library.h"

#include <Preferences.h>

#include <algorithm>

#include "board/storage.h"
#include "config.h"
#include "guoqinlun.h"

static Preferences s_nvs;
static std::vector<BookEntry> s_books;
static uint32_t s_seq = 0;  // 最近打开的顺序号，书单按它排

static String keyFor(char prefix, const String &path) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (size_t i = 0; i < path.length(); i++) h = (h ^ (uint8_t)path[i]) * 16777619u;
  char k[12];
  snprintf(k, sizeof(k), "%c%08lx", prefix, (unsigned long)h);
  return k;
}

void libraryBegin() {
  s_nvs.begin("reader", false);
  s_seq = s_nvs.getUInt("seq", 0);
}

String titleFromPath(const String &path) {
  if (path == BUILTIN_PATH) return GUOQINLUN_TITLE;
  int slash = path.lastIndexOf('/');
  String n = path.substring(slash + 1);
  int dot = n.lastIndexOf('.');
  return dot > 0 ? n.substring(0, dot) : n;
}

int progressPermille(const String &path) {
  String k = keyFor('q', path);
  return s_nvs.isKey(k.c_str()) ? s_nvs.getUShort(k.c_str(), 0) : -1;
}

static uint32_t recentSeq(const String &path) { return s_nvs.getUInt(keyFor('r', path).c_str(), 0); }
uint32_t libraryRecentSeq(const String &path) { return recentSeq(path); }

// 列一个目录里的书；depth > 0 时也进子文件夹（/books/作者/书.epub 这种放法）
static void scanDir(const char *dir, int depth) {
  for (auto &e : sdList(dir, BOOK_MAX_FILES)) {
    if (s_books.size() >= BOOK_MAX_FILES) return;
    if (e.name.startsWith(".") || e.name.startsWith("_")) continue;
    if (e.dir) {
      if (depth > 0) scanDir(e.path.c_str(), depth - 1);
      continue;
    }
    String lower = e.name;
    lower.toLowerCase();
    if (!lower.endsWith(".txt") && !lower.endsWith(".epub")) continue;
    s_books.push_back({e.path, titleFromPath(e.path), e.size, progressPermille(e.path)});
  }
}

bool libraryScan() {
  s_books.clear();
  bool ok;
  {
    SdSession sd;
    ok = sd.ok;
    if (ok) {
      scanDir("/books", 2);
      scanDir("/", 0);
    }
  }
  s_books.push_back({BUILTIN_PATH, GUOQINLUN_TITLE, (uint32_t)strlen(GUOQINLUN_TEXT), progressPermille(BUILTIN_PATH)});
  // 最近读过的在前，没读过的按书名
  std::vector<std::pair<uint32_t, size_t>> order;
  for (size_t i = 0; i < s_books.size(); i++) order.push_back({recentSeq(s_books[i].path), i});
  std::vector<BookEntry> sorted;
  std::stable_sort(order.begin(), order.end(), [&](const std::pair<uint32_t, size_t> &a, const std::pair<uint32_t, size_t> &b) {
    if (a.first != b.first) return a.first > b.first;
    return s_books[a.second].title < s_books[b.second].title;
  });
  for (auto &o : order) sorted.push_back(s_books[o.second]);
  s_books.swap(sorted);
  Serial.printf("[书库] %s，%u 本（含内置）\n", ok ? "TF 卡在" : "没有 TF 卡", (unsigned)s_books.size());
  return ok;
}

const std::vector<BookEntry> &libraryBooks() { return s_books; }

String libraryLastPath() {
  String p = s_nvs.getString("last", "");
  return p.length() ? p : String(BUILTIN_PATH);
}

void librarySetLast(const String &path) {
  if (path != libraryLastPath()) s_nvs.remove("lastch");
  s_nvs.putString("last", path == BUILTIN_PATH ? String("") : path);
  s_nvs.putUInt(keyFor('r', path).c_str(), ++s_seq);
  s_nvs.putUInt("seq", s_seq);
}

String libraryLastChapter() { return s_nvs.getString("lastch", ""); }

void librarySetChapter(const String &title) {
  if (title != libraryLastChapter()) s_nvs.putString("lastch", title);
}

// v0.1 的进度键是 "p" + 路径哈希，内置文章的路径也是 "<内置>"，直接兼容
uint32_t progressLoad(const String &path) { return s_nvs.getUInt(keyFor('p', path).c_str(), 0); }

void progressSave(const String &path, uint32_t offset, int permille) {
  s_nvs.putUInt(keyFor('p', path).c_str(), offset);
  s_nvs.putUShort(keyFor('q', path).c_str(), (uint16_t)constrain(permille, 0, 1000));
}
