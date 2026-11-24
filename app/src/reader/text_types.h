#pragma once
// 阅读器各模块共用的类型。纯 C++（不依赖 Arduino），导入、认章节、分页都能在电脑上单独编译测试（tools/readertest）。
#include <stdint.h>

#include <string>
#include <vector>

// 正文：每本书导入时统一转成 UTF-8 纯文本（段落之间是 \n），所有位置都是这份正文里的字节位置。
// 阅读进度、章节、分页表都按这个位置存，换字号、换屏都不变。

struct Chapter {
  uint32_t off;    // 这一章在正文里的起点（一段的第一个字）；排版时从新的一页开始
  uint32_t tOff;   // 标题在标题池里的位置（EPUB 的目录标题不一定和正文一样，单独存）
  uint16_t tLen;
};

// 一本书的"目录信息"：导入时扫一遍全书得到，存在卡上，以后打开直接读
struct BookMeta {
  std::string title, author;
  std::string info;                // 编码、大小等，给"关于本机"看
  uint32_t len = 0;                // 正文字节数
  std::vector<Chapter> ch;         // 按位置排好
  std::string titles;              // 章节标题池
  std::vector<uint32_t> heads;     // 标题段的起点（居中加粗），排好序
  bool tocFromBook = false;        // EPUB 自带目录（否则按"第X章"认的）
  void clear() { *this = BookMeta(); }
  void addChapter(uint32_t off, const char *t, size_t n) {
    if (n > 300) n = 300;
    ch.push_back({off, (uint32_t)titles.size(), (uint16_t)n});
    titles.append(t, n);
  }
};

enum LineKind : uint8_t { LINE_TEXT, LINE_PARA, LINE_HEADING, LINE_BLANK };

struct TextLine {
  uint32_t start;  // 正文里的位置
  uint16_t len;
  uint8_t kind;    // LineKind；LINE_PARA = 段首（缩进）
  bool justify;    // 段中间的行：两端对齐
  int16_t width;   // 这一行字加起来的宽度（不含缩进）
};

typedef int (*AdvanceFn)(uint32_t cp);

struct Layout {
  int maxWidth = 0;      // 一行可用的像素宽
  int linesPerPage = 0;
  int indentWidth = 0;   // 段首缩进
  uint32_t fontId = 0;   // 正文字库（文件和字号）的指纹：字号一样、字体不同，字宽也不同
  AdvanceFn adv = nullptr;
  bool operator==(const Layout &o) const {
    return maxWidth == o.maxWidth && linesPerPage == o.linesPerPage && indentWidth == o.indentWidth &&
           fontId == o.fontId && adv == o.adv;
  }
  // 存分页表时用的签名（不含函数指针）
  uint32_t signature() const {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) {
      for (int k = 0; k < 4; k++) h = (h ^ ((v >> (8 * k)) & 0xFF)) * 16777619u;
    };
    mix(maxWidth);
    mix(linesPerPage);
    mix(indentWidth);
    mix(fontId);
    return h;
  }
};

// 正文写到哪里：设备上是卡上的缓存文件，电脑上是文件或内存
struct TextSink {
  virtual ~TextSink() = default;
  virtual bool write(const char *p, uint32_t n) = 0;
};

// 按位置读正文（分页时一段一段读）
struct TextReader {
  virtual ~TextReader() = default;
  virtual bool read(uint32_t off, char *buf, uint32_t n) = 0;
};

// 进度：done / total（字节）；返回 false = 取消
typedef bool (*ProgressFn)(void *ctx, uint32_t done, uint32_t total);
