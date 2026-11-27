#include "import.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "chapters.h"
#include "text/decode.h"

namespace {

// 写正文的同时喂给认章节的
struct TeeSink : TextSink {
  TextSink &out;
  ChapterScanner &scan;
  uint32_t len = 0;
  TeeSink(TextSink &o, ChapterScanner &s) : out(o), scan(s) {}
  bool write(const char *p, uint32_t n) override {
    scan.feed(p, n);
    len += n;
    return out.write(p, n);
  }
};

std::string mb(uint32_t bytes) {
  char b[24];
  snprintf(b, sizeof(b), "%.1f MB", bytes / 1048576.0);
  return b;
}

}  // namespace

bool importTxt(ByteSource &src, TextSink &out, BookMeta &m, std::string &err, uint32_t maxChapters,
               ProgressFn progress, void *ctx) {
  m.clear();
  const uint32_t raw = src.size();
  if (raw == 0) { err = "是空文件"; return false; }
  const uint32_t CHUNK = 32768, OUT = 65536;
  uint8_t *buf = (uint8_t *)malloc(CHUNK + 8);
  char *obuf = (char *)malloc(OUT);
  if (!buf || !obuf) { free(buf); free(obuf); err = "内存不够"; return false; }
  ChapterScanner scan(maxChapters);
  TeeSink tee(out, scan);

  int r = src.read(buf, raw < CHUNK ? raw : CHUNK);
  uint32_t have = r > 0 ? r : 0, total = have, bom;
  TextEnc enc = detectEncoding(buf, have, bom);
  TextDecoder dec(enc, obuf, OUT);
  uint32_t pos = bom, lastReport = 0;
  bool ok = have > 0;
  if (!ok) err = "读卡出错";
  while (ok) {
    bool last = total >= raw;
    pos += dec.feed(buf + pos, have - pos, last);
    if (dec.full() || last || dec.length() > OUT / 2) {  // 输出攒够了就写出去
      if (dec.length() && !tee.write(obuf, dec.length())) { ok = false; err = "正文写不进 TF 卡"; break; }
      bool wasFull = dec.full();
      dec.rewind();
      if (wasFull) continue;  // 这一段还没解完
    }
    if (last) break;
    uint32_t rest = have - pos;
    memmove(buf, buf + pos, rest);
    r = src.read(buf + rest, raw - total < CHUNK ? raw - total : CHUNK);
    if (r <= 0) { ok = false; err = "读卡出错"; break; }
    total += r;
    have = rest + r;
    pos = 0;
    if (progress && total - lastReport >= 256 * 1024) {
      lastReport = total;
      if (!progress(ctx, total, raw)) { ok = false; err = "取消了"; break; }
    }
  }
  free(buf);
  free(obuf);
  if (!ok) return false;
  m.len = tee.len;
  scan.finish(m);
  m.info = std::string(textEncName(enc)) + "，" + mb(raw);
  if (dec.badChars()) m.info += "，有乱码";
  return true;
}

namespace {
struct EpubProgress {
  ProgressFn fn;
  void *ctx;
};
}  // namespace

bool importEpub(EpubSource &src, TextSink &out, BookMeta &m, std::string &err, uint32_t maxChapters,
                ProgressFn progress, void *ctx) {
  m.clear();
  ChapterScanner scan(maxChapters);
  TeeSink tee(out, scan);
  EpubResult res;
  bool ok = epubExtract(src, tee, res, [&](uint32_t done, uint32_t total) {
    if (progress) progress(ctx, done, total);
  });
  if (!ok) { err = res.err.length() ? res.err : "EPUB 打不开"; return false; }
  if (res.len == 0) { err = "EPUB 里没有文字"; return false; }
  m.len = res.len;
  m.title = res.title;
  m.author = res.author;
  m.info = "EPUB，" + mb(src.size()) + (res.author.length() ? "，" + res.author : "");
  // 书里的目录少于 3 条（常见：整本一个网页、目录里只有版权页）就按"第X回"的规则认
  if (res.toc.size() >= 3) {
    m.tocFromBook = true;
    for (auto &e : res.toc) m.addChapter(e.off, e.title.data(), e.title.size());
    m.heads = res.heads;
    for (auto &e : res.toc)  // 目录指到的那一段如果很短，多半就是标题：也居中加粗
      if (e.shortLine) m.heads.push_back(e.off);
    std::sort(m.heads.begin(), m.heads.end());
    m.heads.erase(std::unique(m.heads.begin(), m.heads.end()), m.heads.end());
  } else {
    scan.finish(m);
  }
  return true;
}

void importMemory(const char *text, uint32_t len, BookMeta &m, uint32_t maxChapters) {
  m.clear();
  ChapterScanner scan(maxChapters);
  scan.feed(text, len);
  m.len = len;
  scan.finish(m);
}
