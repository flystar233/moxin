// 在电脑上测阅读器的导入和分页（src/reader 的 import / chapters / layout / epub，和设备上同一份代码）。
// 导入一本书 → 正文写成文件、目录信息；然后分页两次：整本在内存里一次、用小缓冲一段一段读一次，
// 两次的分页表、每一页排出来的行必须完全一样（设备上大书就是一段一段读的）。
//
// 编译（MSYS2 ucrt64，在 app 目录下）：
//   g++ -O2 -std=c++17 -Isrc -Itools/readertest tools/readertest/main.cpp src/reader/import.cpp src/reader/chapters.cpp
//       src/reader/layout.cpp src/reader/window.cpp src/reader/epub.cpp src/text/decode.cpp -lz -o readertest
// 用法：readertest 书.txt|书.epub [输出前缀]   （给了前缀就写 前缀.txt / 前缀.meta，方便和别的版本对比）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <chrono>
#include <string>
#include <vector>

#include "reader/import.h"
#include "reader/layout.h"
#include "reader/window.h"

bool epubInflate(const uint8_t *in, uint32_t inLen, uint8_t *out, uint32_t outLen) {
  z_stream z{};
  if (inflateInit2(&z, -15) != Z_OK) return false;
  z.next_in = (Bytef *)in;
  z.avail_in = inLen;
  z.next_out = out;
  z.avail_out = outLen;
  int r = inflate(&z, Z_FINISH);
  inflateEnd(&z);
  return r == Z_STREAM_END && z.total_out == outLen;
}

struct FileEpub : EpubSource {
  FILE *f;
  uint32_t n;
  explicit FileEpub(FILE *fp) : f(fp) { fseek(f, 0, SEEK_END); n = ftell(f); }
  uint32_t size() override { return n; }
  bool read(uint32_t off, void *buf, uint32_t len) override { return fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, len, f) == len; }
};
struct FileBytes : ByteSource {
  FILE *f;
  uint32_t n;
  explicit FileBytes(FILE *fp) : f(fp) { fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); }
  uint32_t size() override { return n; }
  int read(uint8_t *buf, uint32_t len) override { return (int)fread(buf, 1, len, f); }
};
// 正文：写进内存（顺便当"卡上的缓存文件"用）
struct MemSink : TextSink {
  std::string s;
  int writes = 0;
  bool write(const char *p, uint32_t n) override { s.append(p, n); writes++; return true; }
};
struct MemReader : TextReader {
  const std::string &s;
  int reads = 0;
  explicit MemReader(const std::string &x) : s(x) {}
  bool read(uint32_t off, char *buf, uint32_t n) override {
    if (off + n > s.size()) return false;
    memcpy(buf, s.data() + off, n);
    reads++;
    return true;
  }
};

// 假字宽（和旧版参考程序一样）
int adv(uint32_t cp) {
  if (cp < 0x80) return 8 + cp % 7;
  if (cp >= 0x2E80) return 24;
  return 14;
}

static double ms(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "用法：readertest 书 [输出前缀]\n"); return 2; }
  std::string path = argv[1];
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) { fprintf(stderr, "打不开 %s\n", path.c_str()); return 1; }
  bool epub = path.size() > 5 && path.substr(path.size() - 5) == ".epub";
  MemSink text;
  BookMeta m;
  std::string err;
  auto t0 = std::chrono::steady_clock::now();
  bool ok;
  if (epub) { FileEpub src(f); ok = importEpub(src, text, m, err, 20000); }
  else { FileBytes src(f); ok = importTxt(src, text, m, err, 20000); }
  fclose(f);
  if (!ok) { printf("导入失败：%s\n", err.c_str()); return 1; }
  if (text.s.size() != m.len) { printf("错：正文长度 %zu 和 meta.len %u 不一致\n", text.s.size(), m.len); return 1; }
  printf("导入 %.0f ms：%s，%u 字节（写了 %d 次），%zu 章%s，%zu 标题段\n", ms(t0), m.info.c_str(), m.len, text.writes,
         m.ch.size(), m.tocFromBook ? "（书里的目录）" : "", m.heads.size());

  Layout L;
  L.maxWidth = 448;
  L.linesPerPage = 25;
  L.indentWidth = 48;
  L.adv = adv;
  TextSpan whole;
  whole.p = text.s.data();
  whole.len = whole.total = m.len;
  std::vector<uint32_t> pa, pb;
  t0 = std::chrono::steady_clock::now();
  paginate(m, L, &whole, nullptr, 0, pa);
  double t1 = ms(t0);
  MemReader rd(text.s);
  t0 = std::chrono::steady_clock::now();
  paginate(m, L, nullptr, &rd, 256 * 1024, pb);  // 设备上大书的读法（缓冲 256KB）
  printf("分页：整本 %zu 页 %.0f ms；分段读 %zu 页 %.0f ms（读了 %d 次）\n", pa.size(), t1, pb.size(), ms(t0), rd.reads);
  int bad = 0;
  if (pa != pb) { printf("错：两种分页结果不一样\n"); bad++; }

  // 每一页：只给这一页附近的一小段（像设备上的阅读窗口），排出来的行要和整本时一样
  TextLine la[64], lb[64];
  for (size_t p = 0; p < pa.size(); p++) {
    int na = 0, nb = 0;
    layoutPage(whole, m, L, pa[p], la, L.linesPerPage, &na);
    // 设备上的规矩：窗口盖住这一页（到下一页起点）再往后多 4KB
    TextSpan s;
    uint32_t next = p + 1 < pa.size() ? pa[p + 1] : m.len;
    uint32_t from = pa[p] > 0 ? pa[p] - 1 : 0, to = next + 4096 < m.len ? next + 4096 : m.len;
    s.p = text.s.data() + from;
    s.base = from;
    s.len = to - from;
    s.total = m.len;
    layoutPage(s, m, L, pa[p], lb, L.linesPerPage, &nb);
    bool same = na == nb;
    for (int k = 0; same && k < na; k++)
      same = la[k].start == lb[k].start && la[k].len == lb[k].len && la[k].kind == lb[k].kind &&
             la[k].justify == lb[k].justify && la[k].width == lb[k].width;
    if (!same && bad++ < 5) printf("错：第 %zu 页用小窗口排出来不一样\n", p + 1);
  }
  // 阅读窗口（设备上的用法）：窗口故意开小（全书 20%，最少 300KB），从头翻到尾、从尾翻回头、再随机跳 3000 次，
  // 每一页排出来都要和整本时一样，窗口里的字节要和正文一样
  {
    uint32_t cap = m.len / 5 < 300 * 1024 ? 300 * 1024 : m.len / 5;
    TextWindow tw(malloc);
    tw.reset(m.len, cap);
    MemReader wr(text.s);
    uint64_t readBytes = 0;
    struct Counting : TextReader {
      MemReader &r;
      uint64_t &n;
      Counting(MemReader &x, uint64_t &c) : r(x), n(c) {}
      bool read(uint32_t off, char *buf, uint32_t len) override { n += len; return r.read(off, buf, len); }
    } crd(wr, readBytes);
    auto check = [&](size_t p) {
      uint32_t start = pa[p], end = p + 1 < pa.size() ? pa[p + 1] : m.len;
      if (!tw.ensure(start > 0 ? start - 1 : 0, end + 4096, crd)) return false;
      const TextSpan &s = tw.span();
      if (memcmp(s.p, text.s.data() + s.base, s.len) != 0) return false;
      int na = 0, nb = 0;
      layoutPage(whole, m, L, pa[p], la, L.linesPerPage, &na);
      layoutPage(s, m, L, pa[p], lb, L.linesPerPage, &nb);
      if (na != nb) return false;
      for (int k = 0; k < na; k++)
        if (la[k].start != lb[k].start || la[k].len != lb[k].len || la[k].justify != lb[k].justify) return false;
      return true;
    };
    int wbad = 0;
    for (size_t p = 0; p < pa.size(); p++) wbad += !check(p);
    uint64_t fwd = readBytes;
    for (size_t p = pa.size(); p-- > 0;) wbad += !check(p);
    srand(12345);
    for (int k = 0; k < 3000; k++) wbad += !check(((size_t)rand() * 7919 + rand()) % pa.size());
    printf("窗口 %u KB：顺读一遍从卡上读了 %.1f MB（全书 %.1f MB），总共读 %.1f MB，%s\n", tw.capacity() / 1024,
           fwd / 1048576.0, m.len / 1048576.0, readBytes / 1048576.0, wbad ? "有错" : "每页一致");
    if (wbad) { printf("错：窗口有 %d 页不一致\n", wbad); bad++; }
  }
  if (argc > 2) {
    std::string out = argv[2];
    FILE *t = fopen((out + ".txt").c_str(), "wb");
    fwrite(text.s.data(), 1, text.s.size(), t);
    fclose(t);
    FILE *mf = fopen((out + ".meta").c_str(), "wb");
    fprintf(mf, "len %u\n", m.len);
    for (auto &c : m.ch) fprintf(mf, "C %u %s\n", c.off, std::string(m.titles.data() + c.tOff, c.tLen).c_str());
    for (auto h : m.heads) fprintf(mf, "H %u\n", h);
    for (auto p : pa) fprintf(mf, "P %u\n", p);
    fclose(mf);
  }
  printf(bad ? "有 %d 处不一致\n" : "全部一致\n", bad);
  return bad ? 1 : 0;
}
