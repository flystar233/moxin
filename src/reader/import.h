#pragma once
// 导入：把一本书（TXT 各种编码、EPUB）转成统一的 UTF-8 正文写进 sink，同时扫出目录信息（BookMeta）。
// 一段一段处理，书多大都行，内存里不用放整本。纯 C++，电脑上能测（tools/readertest）。
#include "epub.h"
#include "text_types.h"

// 按顺序读原文件
struct ByteSource {
  virtual ~ByteSource() = default;
  virtual uint32_t size() = 0;
  virtual int read(uint8_t *buf, uint32_t n) = 0;  // 返回读到的字节数，<= 0 = 出错 / 读完
};

// maxChapters：最多认多少章
bool importTxt(ByteSource &src, TextSink &out, BookMeta &m, std::string &err, uint32_t maxChapters,
               ProgressFn progress = nullptr, void *ctx = nullptr);
bool importEpub(EpubSource &src, TextSink &out, BookMeta &m, std::string &err, uint32_t maxChapters,
                ProgressFn progress = nullptr, void *ctx = nullptr);
// 已经在内存里的 UTF-8 正文（内置文章）：只扫目录
void importMemory(const char *text, uint32_t len, BookMeta &m, uint32_t maxChapters);
