#include "epub.h"

#include "text_types.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <unordered_map>

namespace {

// =====================================================================
// ZIP（只支持 stored 和 deflate，不支持 ZIP64 和加密）
// =====================================================================
uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

struct ZipEntry {
  std::string name;
  uint16_t method;
  uint32_t csize, usize, off;  // off：本地文件头位置
};

class Zip {
 public:
  explicit Zip(EpubSource &s) : src_(s) {}

  bool open(std::string &err) {
    uint32_t size = src_.size();
    if (size < 22) { err = "文件太小，不是 EPUB"; return false; }
    uint32_t tail = size < 65557 ? size : 65557;
    std::vector<uint8_t> t(tail);
    if (!src_.read(size - tail, t.data(), tail)) { err = "读文件出错"; return false; }
    int eocd = -1;
    for (int i = (int)tail - 22; i >= 0; i--)
      if (rd32(&t[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { err = "不是 zip 格式（EPUB 应该是 zip）"; return false; }
    uint16_t count = rd16(&t[eocd + 10]);
    uint32_t cdSize = rd32(&t[eocd + 12]), cdOff = rd32(&t[eocd + 16]);
    if (cdOff == 0xFFFFFFFF || (uint64_t)cdOff + cdSize > size) { err = "zip 目录损坏（或是 ZIP64）"; return false; }
    std::vector<uint8_t> cd(cdSize);
    if (!src_.read(cdOff, cd.data(), cdSize)) { err = "读文件出错"; return false; }
    uint32_t p = 0;
    for (int i = 0; i < count && p + 46 <= cdSize; i++) {
      const uint8_t *e = &cd[p];
      if (rd32(e) != 0x02014b50) break;
      uint16_t nl = rd16(e + 28), xl = rd16(e + 30), cl = rd16(e + 32);
      if (p + 46 + nl > cdSize) break;
      ZipEntry z;
      z.name.assign((const char *)e + 46, nl);
      z.method = rd16(e + 10);
      z.csize = rd32(e + 20);
      z.usize = rd32(e + 24);
      z.off = rd32(e + 42);
      if (rd16(e + 8) & 1) z.method = 0xFFFF;  // 加密的不认
      entries_.push_back(z);
      p += 46 + nl + xl + cl;
    }
    for (size_t i = 0; i < entries_.size(); i++) index_[entries_[i].name] = i;
    return !entries_.empty();
  }

  const ZipEntry *find(const std::string &name) const {
    auto it = index_.find(name);
    if (it != index_.end()) return &entries_[it->second];
    for (auto &e : entries_)  // 有的书里大小写对不上
      if (strcasecmp(e.name.c_str(), name.c_str()) == 0) return &e;
    return nullptr;
  }

  bool readEntry(const ZipEntry &z, std::string &out) {
    uint8_t lh[30];
    if (!src_.read(z.off, lh, 30) || rd32(lh) != 0x04034b50) return false;
    uint32_t data = z.off + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (z.method == 0) {
      out.resize(z.usize);
      return z.usize == 0 || src_.read(data, &out[0], z.usize);
    }
    if (z.method != 8) return false;
    uint8_t *in = (uint8_t *)malloc(z.csize ? z.csize : 1);
    if (!in) return false;
    bool ok = src_.read(data, in, z.csize);
    if (ok) {
      out.resize(z.usize);
      ok = z.usize == 0 || epubInflate(in, z.csize, (uint8_t *)&out[0], z.usize);
    }
    free(in);
    return ok;
  }

  bool read(const std::string &name, std::string &out) {
    const ZipEntry *z = find(name);
    return z && readEntry(*z, out);
  }

  uint32_t usize(const std::string &name) const {
    const ZipEntry *z = find(name);
    return z ? z->usize : 0;
  }

 private:
  EpubSource &src_;
  std::vector<ZipEntry> entries_;
  std::unordered_map<std::string, size_t> index_;
};

// =====================================================================
// 路径、XML 小工具
// =====================================================================
std::string dirOf(const std::string &p) {
  size_t s = p.rfind('/');
  return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// base 目录 + 相对 href → zip 里的完整路径（处理 ./ ../ 和 %20）；frag 返回 # 后面的部分
std::string resolve(const std::string &base, const std::string &href, std::string *frag = nullptr) {
  std::string h = href, f;
  size_t hash = h.find('#');
  if (hash != std::string::npos) { f = h.substr(hash + 1); h = h.substr(0, hash); }
  if (frag) *frag = f;
  std::string dec;
  for (size_t i = 0; i < h.size(); i++) {
    if (h[i] == '%' && i + 2 < h.size() && hexv(h[i + 1]) >= 0 && hexv(h[i + 2]) >= 0) {
      dec += (char)(hexv(h[i + 1]) * 16 + hexv(h[i + 2]));
      i += 2;
    } else {
      dec += h[i];
    }
  }
  std::string full = (!dec.empty() && dec[0] == '/') ? dec.substr(1) : base + dec;
  std::vector<std::string> parts;
  size_t s = 0;
  while (s <= full.size()) {
    size_t e = full.find('/', s);
    if (e == std::string::npos) e = full.size();
    std::string part = full.substr(s, e - s);
    if (part == "..") { if (!parts.empty()) parts.pop_back(); }
    else if (!part.empty() && part != ".") parts.push_back(part);
    s = e + 1;
  }
  std::string r;
  for (size_t i = 0; i < parts.size(); i++) r += (i ? "/" : "") + parts[i];
  return r;
}

struct Tag {
  std::string name;  // 小写，去掉命名空间前缀
  bool close = false, selfClose = false;
  std::vector<std::pair<std::string, std::string>> attrs;
  const std::string *attr(const char *k) const {
    for (auto &a : attrs)
      if (a.first == k) return &a.second;
    return nullptr;
  }
};

// s[pos] == '<'：解析一个标签，pos 移到 '>' 之后。注释、<!...>、<?...?> 返回 false（pos 也会跳过）
bool parseTag(const std::string &s, size_t &pos, Tag &t) {
  size_t n = s.size();
  if (s.compare(pos, 4, "<!--") == 0) {
    size_t e = s.find("-->", pos + 4);
    pos = e == std::string::npos ? n : e + 3;
    return false;
  }
  if (pos + 1 < n && (s[pos + 1] == '!' || s[pos + 1] == '?')) {
    size_t e = s.find('>', pos);
    pos = e == std::string::npos ? n : e + 1;
    return false;
  }
  size_t i = pos + 1;
  t = Tag();
  if (i < n && s[i] == '/') { t.close = true; i++; }
  size_t ns = i;
  while (i < n && (isalnum((unsigned char)s[i]) || s[i] == ':' || s[i] == '_' || s[i] == '-' || s[i] == '.')) i++;
  std::string name = s.substr(ns, i - ns);
  size_t colon = name.rfind(':');
  if (colon != std::string::npos) name = name.substr(colon + 1);
  for (auto &c : name) c = tolower(c);
  t.name = name;
  // 属性
  while (i < n && s[i] != '>') {
    if (s[i] == '/' && i + 1 < n && s[i + 1] == '>') { t.selfClose = true; i++; continue; }
    if (isspace((unsigned char)s[i])) { i++; continue; }
    size_t ks = i;
    while (i < n && !isspace((unsigned char)s[i]) && s[i] != '=' && s[i] != '>' && s[i] != '/') i++;
    std::string key = s.substr(ks, i - ks);
    size_t kc = key.rfind(':');
    if (kc != std::string::npos && key.compare(0, kc, "xmlns") != 0) key = key.substr(kc + 1);  // epub:type → type
    for (auto &c : key) c = tolower(c);
    while (i < n && isspace((unsigned char)s[i])) i++;
    std::string val;
    if (i < n && s[i] == '=') {
      i++;
      while (i < n && isspace((unsigned char)s[i])) i++;
      if (i < n && (s[i] == '"' || s[i] == '\'')) {
        char q = s[i++];
        size_t vs = i;
        while (i < n && s[i] != q) i++;
        val = s.substr(vs, i - vs);
        if (i < n) i++;
      } else {
        size_t vs = i;
        while (i < n && !isspace((unsigned char)s[i]) && s[i] != '>') i++;
        val = s.substr(vs, i - vs);
      }
    }
    if (!key.empty()) t.attrs.push_back({key, val});
    else if (i < n && s[i] != '>') i++;  // 防止卡住
  }
  pos = i < n ? i + 1 : n;
  return !t.name.empty();
}

void putUtf8(std::string &o, uint32_t cp) {
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) {
    o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
  } else {
    o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
    o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
  }
}

// s[pos] == '&'：解一个实体，成功返回码位并把 pos 移到 ';' 之后；不认识返回 0
uint32_t entity(const std::string &s, size_t &pos) {
  size_t e = s.find(';', pos);
  if (e == std::string::npos || e - pos > 12) return 0;
  std::string k = s.substr(pos + 1, e - pos - 1);
  uint32_t cp = 0;
  if (!k.empty() && k[0] == '#') {
    cp = (k.size() > 1 && (k[1] == 'x' || k[1] == 'X')) ? strtoul(k.c_str() + 2, nullptr, 16) : strtoul(k.c_str() + 1, nullptr, 10);
    if (cp == 0 || cp > 0x10FFFF) return 0;
  } else {
    static const struct { const char *n; uint32_t cp; } E[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
        {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009}, {"mdash", 0x2014}, {"ndash", 0x2013},
        {"hellip", 0x2026}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
        {"middot", 0xB7}, {"bull", 0x2022}, {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122},
        {"times", 0xD7}, {"divide", 0xF7}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"deg", 0xB0}, {"plusmn", 0xB1},
        {"zwj", 0x200D}, {"zwnj", 0x200C}, {"shy", 0xAD},
    };
    for (auto &x : E)
      if (k == x.n) { cp = x.cp; break; }
    if (!cp) return 0;
  }
  pos = e + 1;
  return cp;
}

// 一段标签外的文字：解实体、合并空白
std::string plainText(const std::string &s, size_t from, size_t to) {
  std::string o;
  bool sp = false;
  for (size_t i = from; i < to;) {
    char c = s[i];
    if (c == '<') {
      Tag t;
      parseTag(s, i, t);
      continue;
    }
    if (isspace((unsigned char)c)) { sp = true; i++; continue; }
    if (sp && !o.empty()) o += ' ';
    sp = false;
    if (c == '&') {
      size_t j = i;
      uint32_t cp = entity(s, j);
      if (cp) { putUtf8(o, cp == 0xA0 ? ' ' : cp); i = j; continue; }
    }
    o += c;
    i++;
  }
  return o;
}

bool isCjk(uint32_t cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
         (cp >= 0x20000 && cp <= 0x3FFFF);
}

uint32_t hashStr(const std::string &s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) h = (h ^ c) * 16777619u;
  return h;
}

// =====================================================================
// XHTML → 文字
// =====================================================================
// 正文边写边记两种位置：
//   settle：登记以后"第一个不是空白的字"的位置（目录指到的段落开头）
//   capture：从那个位置起到行尾的文字（不到 120 字节才有用：判断是不是标题、拿标题文字）
struct Spot {
  uint32_t off = UINT32_MAX;
  bool capture = false, nl = false, tooLong = false;
  std::string text;
};

class Emitter {
 public:
  explicit Emitter(TextSink &sink) : sink_(sink) { buf_.reserve(BUF); }
  uint32_t len() const { return len_; }
  bool full() const { return err_; }  // 写不进去了（卡满、出错）

  void newline() {
    pendingSpace_ = false;
    if (len_ && last_ != '\n') putRaw('\n');
  }
  // 源码里的空白。soft = 含换行（排版时随手折的行）：两个汉字之间的不算空格
  void space(bool soft = false) {
    if (!pendingSpace_) softSpace_ = soft;
    else if (!soft) softSpace_ = false;
    pendingSpace_ = true;
  }
  void markHead() { pendingHead_ = true; }
  void cancelHead() { pendingHead_ = false; }
  void ch(uint32_t cp, std::vector<uint32_t> &heads, std::vector<int> *headSpots) {
    if (cp == 0xA0 || cp == 0x2002 || cp == 0x2003 || cp == 0x2009) { space(); return; }
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xAD || cp == 0xFEFF) return;
    if (pendingSpace_) {
      if (len_ && last_ != '\n' && !(softSpace_ && isCjk(last_) && isCjk(cp))) putRaw(' ');
      pendingSpace_ = false;
    }
    if (pendingHead_) {
      heads.push_back(len_);
      if (headSpots) headSpots->push_back(captureNow());
      pendingHead_ = false;
    }
    putRaw(cp);
  }

  // 位置登记，返回编号
  int settle(bool capture) {
    spots_.push_back(Spot());
    spots_.back().capture = capture;
    pending_.push_back(spots_.size() - 1);
    return spots_.size() - 1;
  }
  int captureNow() {
    spots_.push_back(Spot());
    Spot &s = spots_.back();
    s.capture = true;
    s.off = len_;
    active_.push_back(spots_.size() - 1);
    return spots_.size() - 1;
  }
  const Spot &spot(int id) const { return spots_[id]; }
  // 写完：还没落定的位置都在末尾；缓冲写出去
  bool finish() {
    for (int id : pending_) spots_[id].off = len_;
    pending_.clear();
    active_.clear();
    flush();
    return !err_;
  }

 private:
  static const uint32_t BUF = 16 * 1024;
  void flush() {
    if (buf_.empty() || err_) return;
    if (!sink_.write(buf_.data(), buf_.size())) err_ = true;
    buf_.clear();
  }
  void putRaw(uint32_t cp) {
    if (err_) return;
    char b[4];
    int n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    if (n == 1) b[0] = cp;
    else if (n == 2) { b[0] = 0xC0 | (cp >> 6); b[1] = 0x80 | (cp & 0x3F); }
    else if (n == 3) { b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F); b[2] = 0x80 | (cp & 0x3F); }
    else { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F); b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); }
    // 等着落定的位置：落在第一个不是空白的字上
    bool ws = cp == '\n' || cp == ' ' || cp == '\t' || cp == 0x3000 || cp == 0xA0;
    if (!ws && !pending_.empty()) {
      for (int id : pending_) {
        spots_[id].off = len_;
        if (spots_[id].capture) active_.push_back(id);
      }
      pending_.clear();
    }
    // 正在记文字的位置：记到行尾，超过 120 字节就不记了
    if (!active_.empty()) {
      if (cp == '\n') {
        for (int id : active_) spots_[id].nl = true;
        active_.clear();
      } else {
        size_t k = 0;
        for (int id : active_) {
          Spot &s = spots_[id];
          s.text.append(b, n);
          if (s.text.size() >= 120) s.tooLong = true;
          else active_[k++] = id;
        }
        active_.resize(k);
      }
    }
    buf_.append(b, n);
    len_ += n;
    last_ = cp;
    if (buf_.size() >= BUF) flush();
  }
  TextSink &sink_;
  std::string buf_;
  uint32_t len_ = 0, last_ = 0;
  bool pendingSpace_ = false, softSpace_ = false, pendingHead_ = false, err_ = false;
  std::vector<Spot> spots_;
  std::vector<int> pending_, active_;
};

bool isBlock(const std::string &n) {
  static const char *const B[] = {"p", "div", "br", "hr", "li", "ul", "ol", "dl", "dt", "dd", "tr", "table",
                                  "blockquote", "section", "article", "header", "footer", "aside", "nav", "pre",
                                  "figure", "figcaption", "h1", "h2", "h3", "h4", "h5", "h6", "body", "center",
                                  "caption", "address", "main"};
  for (const char *b : B)
    if (n == b) return true;
  return false;
}

bool isSkip(const std::string &n) {
  return n == "head" || n == "script" || n == "style" || n == "svg" || n == "math" || n == "rt" || n == "rp";
}

// 目录要用到的锚点：(文件序号 << 32 | id 哈希) → 位置编号（-1 = 还没遇到）。只记第一次出现的
typedef std::unordered_map<uint64_t, int> AnchorWant;

// 一个 XHTML 文件 → 文字，追加到 em；目录要的 id 登记位置
void htmlToText(const std::string &s, uint32_t file, Emitter &em, std::vector<uint32_t> &heads,
                std::vector<int> *headSpots, AnchorWant &want) {
  int skip = 0;
  size_t i = 0, n = s.size();
  em.newline();
  while (i < n && !em.full()) {
    char c = s[i];
    if (c == '<') {
      if (s.compare(i, 9, "<![CDATA[") == 0) {
        size_t e = s.find("]]>", i + 9);
        if (e == std::string::npos) e = n;
        for (size_t k = i + 9; k < e && !skip; k++) em.ch((unsigned char)s[k], heads, headSpots);  // 极少见，只当 ASCII
        i = e + 3;
        continue;
      }
      Tag t;
      if (!parseTag(s, i, t)) continue;
      if (isSkip(t.name)) {
        if (t.close) { if (skip > 0) skip--; }
        else if (!t.selfClose) skip++;
        continue;
      }
      if (skip) continue;
      bool h = t.name.size() == 2 && t.name[0] == 'h' && t.name[1] >= '1' && t.name[1] <= '6';
      if (isBlock(t.name)) {
        em.newline();
        if (h && !t.close && t.name[1] <= '3') em.markHead();
        if (h && t.close) em.cancelHead();
      }
      if (!t.close && !want.empty()) {
        const std::string *id = t.attr("id");
        if (!id && t.name == "a") id = t.attr("name");
        if (id && !id->empty()) {
          auto it = want.find(((uint64_t)file << 32) | hashStr(*id));
          if (it != want.end() && it->second < 0) it->second = em.settle(true);
        }
      }
      continue;
    }
    if (skip) { i++; continue; }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { em.space(c == '\r' || c == '\n'); i++; continue; }
    if (c == '&') {
      size_t j = i;
      uint32_t cp = entity(s, j);
      if (cp) { em.ch(cp, heads, headSpots); i = j; continue; }
    }
    // 一个 UTF-8 字符
    unsigned char b = c;
    int l = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
    if (i + l > n) l = 1;
    uint32_t cp = l == 1 ? (b < 0x80 ? b : '?') : (b & (0xFF >> (l + 1)));
    for (int k = 1; k < l; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    if (cp >= 0x20 || cp == '\t') em.ch(cp, heads, headSpots);
    i += l;
  }
  em.newline();
}

// =====================================================================
// 目录：EPUB3 nav（<nav epub:type="toc"> 里的 <a href>）或 EPUB2 NCX（navPoint）
// =====================================================================
struct RawToc {
  std::string path, frag, title;
};

void parseNav(const std::string &s, const std::string &base, std::vector<RawToc> &out) {
  size_t i = 0, n = s.size();
  bool inToc = false;
  int navDepth = 0;
  while (i < n) {
    size_t lt = s.find('<', i);
    if (lt == std::string::npos) break;
    i = lt;
    Tag t;
    if (!parseTag(s, i, t)) continue;
    if (t.name == "nav") {
      if (!t.close) {
        const std::string *ty = t.attr("type");
        if (ty && ty->find("toc") != std::string::npos) { inToc = true; navDepth = 1; }
        else if (inToc) navDepth++;
      } else if (inToc && --navDepth == 0) {
        inToc = false;
      }
      continue;
    }
    if (inToc && t.name == "a" && !t.close) {
      const std::string *href = t.attr("href");
      size_t end = s.find("</a", i);
      if (end == std::string::npos) end = n;
      std::string title = plainText(s, i, end);
      if (href && !title.empty()) {
        RawToc r;
        r.path = resolve(base, *href, &r.frag);
        r.title = title;
        out.push_back(r);
      }
      i = end;
    }
  }
}

void parseNcx(const std::string &s, const std::string &base, std::vector<RawToc> &out) {
  size_t i = 0, n = s.size();
  std::string title;
  while (i < n) {
    size_t lt = s.find('<', i);
    if (lt == std::string::npos) break;
    i = lt;
    Tag t;
    if (!parseTag(s, i, t)) continue;
    if (t.name == "text" && !t.close) {
      size_t end = s.find("</", i);
      if (end == std::string::npos) end = n;
      title = plainText(s, i, end);
      i = end;
    } else if (t.name == "content") {
      const std::string *src = t.attr("src");
      if (src && !title.empty()) {
        RawToc r;
        r.path = resolve(base, *src, &r.frag);
        r.title = title;
        out.push_back(r);
      }
      title.clear();
    }
  }
}

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

}  // namespace

// =====================================================================
bool epubExtract(EpubSource &src, TextSink &sink, EpubResult &res,
                 const std::function<void(uint32_t, uint32_t)> &progress) {
  Zip zip(src);
  if (!zip.open(res.err)) return false;

  // META-INF/container.xml → OPF 的路径
  std::string xml;
  if (!zip.read("META-INF/container.xml", xml)) { res.err = "缺 META-INF/container.xml"; return false; }
  std::string opfPath;
  for (size_t i = 0; i < xml.size();) {
    size_t lt = xml.find('<', i);
    if (lt == std::string::npos) break;
    i = lt;
    Tag t;
    if (parseTag(xml, i, t) && t.name == "rootfile" && t.attr("full-path")) { opfPath = *t.attr("full-path"); break; }
  }
  if (opfPath.empty()) { res.err = "container.xml 里没有 OPF 路径"; return false; }
  std::string opf;
  if (!zip.read(opfPath, opf)) { res.err = "读不了 " + opfPath; return false; }
  const std::string base = dirOf(opfPath);

  // OPF：书名、作者、清单、书脊
  struct Item { std::string href, media, props; };
  std::unordered_map<std::string, Item> manifest;
  std::vector<std::string> spine;
  std::string ncxId;
  for (size_t i = 0; i < opf.size();) {
    size_t lt = opf.find('<', i);
    if (lt == std::string::npos) break;
    i = lt;
    Tag t;
    if (!parseTag(opf, i, t) || t.close) continue;
    if ((t.name == "title" && res.title.empty()) || (t.name == "creator" && res.author.empty())) {
      size_t end = opf.find("</", i);
      if (end == std::string::npos) end = opf.size();
      (t.name == "title" ? res.title : res.author) = trim(plainText(opf, i, end));
      i = end;
    } else if (t.name == "item" && t.attr("id") && t.attr("href")) {
      Item it{resolve(base, *t.attr("href")), t.attr("media-type") ? *t.attr("media-type") : "",
              t.attr("properties") ? *t.attr("properties") : ""};
      manifest[*t.attr("id")] = it;
    } else if (t.name == "spine" && t.attr("toc")) {
      ncxId = *t.attr("toc");
    } else if (t.name == "itemref" && t.attr("idref")) {
      const std::string *lin = t.attr("linear");
      if (!lin || *lin != "no") spine.push_back(*t.attr("idref"));
    }
  }
  std::vector<std::string> files;  // 书脊里的 XHTML，按顺序
  for (auto &id : spine) {
    auto it = manifest.find(id);
    if (it == manifest.end()) continue;
    const std::string &m = it->second.media;
    if (m.find("html") == std::string::npos && m.find("xml") == std::string::npos) continue;
    files.push_back(it->second.href);
  }
  if (files.empty()) { res.err = "书脊（spine）是空的"; return false; }
  std::unordered_map<std::string, uint32_t> fileIndex;
  for (uint32_t k = 0; k < files.size(); k++) fileIndex[files[k]] = k;

  // 目录先读（文件很小）：这样写正文时就知道要记下哪些位置
  std::vector<RawToc> raw;
  std::string navPath, ncxPath;
  for (auto &kv : manifest) {
    if (kv.second.props.find("nav") != std::string::npos) navPath = kv.second.href;
    if (kv.second.media == "application/x-dtbncx+xml" && ncxPath.empty()) ncxPath = kv.second.href;
  }
  if (!ncxId.empty() && manifest.count(ncxId)) ncxPath = manifest[ncxId].href;
  {
    std::string tocDoc;
    if (!navPath.empty() && zip.read(navPath, tocDoc)) parseNav(tocDoc, dirOf(navPath), raw);
    if (raw.empty() && !ncxPath.empty() && zip.read(ncxPath, tocDoc)) parseNcx(tocDoc, dirOf(ncxPath), raw);
  }
  AnchorWant want;
  std::vector<char> fileWanted(files.size(), 0);
  for (auto &r : raw) {
    auto fi = fileIndex.find(r.path);
    if (fi == fileIndex.end()) continue;
    fileWanted[fi->second] = 1;  // 锚点找不到时退回文件开头
    if (!r.frag.empty()) want[((uint64_t)fi->second << 32) | hashStr(r.frag)] = -1;
  }

  // 正文：按书脊顺序一个文件一个文件写
  Emitter em(sink);
  std::vector<int> fileSpot(files.size(), -1), headSpots;
  for (uint32_t k = 0; k < files.size(); k++) {
    if (fileWanted[k]) fileSpot[k] = em.settle(true);
    if (em.full()) break;
    std::string html;
    if (zip.read(files[k], html)) htmlToText(html, k, em, res.heads, &headSpots, want);  // 坏掉的一章跳过
    if (progress) progress(k + 1, files.size());
  }
  if (!em.finish()) { res.err = "正文写不进 TF 卡"; return false; }
  res.len = em.len();

  // 目录条目 → 正文位置（已落到这一段第一个字）
  for (auto &r : raw) {
    auto fi = fileIndex.find(r.path);
    if (fi == fileIndex.end()) continue;
    int sp = fileSpot[fi->second];
    if (!r.frag.empty()) {
      auto a = want.find(((uint64_t)fi->second << 32) | hashStr(r.frag));
      if (a != want.end() && a->second >= 0) sp = a->second;
    }
    const Spot &s = em.spot(sp);
    if (s.off >= res.len) continue;
    std::string title = trim(r.title);
    if (title.empty()) continue;
    res.toc.push_back({s.off, title, s.nl && !s.tooLong});
  }
  std::stable_sort(res.toc.begin(), res.toc.end(), [](const EpubTocEntry &a, const EpubTocEntry &b) { return a.off < b.off; });
  // 同一个位置只留第一个（"第一卷"和它下面的"第一章"指到同一处时）
  std::vector<EpubTocEntry> uniq;
  for (auto &e : res.toc)
    if (uniq.empty() || uniq.back().off != e.off) uniq.push_back(e);
  res.toc.swap(uniq);

  // 书里没有目录：用 h1～h3 标题（不到 120 字节的）
  if (res.toc.empty()) {
    for (size_t k = 0; k < res.heads.size() && k < headSpots.size(); k++) {
      const Spot &s = em.spot(headSpots[k]);
      if (!s.tooLong && !s.text.empty()) res.toc.push_back({res.heads[k], s.text, s.nl});
    }
  }
  return true;
}
