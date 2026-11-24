#pragma once
// EPUB → 纯文本（UTF-8），加上目录。纯 C++，不依赖 Arduino，可以在电脑上单独编译测试（tools/readertest）。
//
// 只取文字：按书脊（spine）顺序把每个 XHTML 去掉标签，段落、标题、换行变成 \n，
// 实体（&amp; &#x4E00; ……）解码，HTML 里的空白按网页规则合并（中文之间不留空格）。
// 图片、样式、脚本都不要。目录优先用书里自带的（EPUB3 nav 或 EPUB2 NCX），没有就用 h1～h3 标题。
#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

// 读 EPUB 文件的接口：设备上包 TF 卡的文件，电脑上包 FILE*
struct EpubSource {
  virtual ~EpubSource() = default;
  virtual uint32_t size() = 0;
  virtual bool read(uint32_t off, void *buf, uint32_t n) = 0;
};

struct EpubTocEntry {
  uint32_t off;        // 在输出正文里的位置（已跳到这一段第一个字）
  std::string title;
  bool shortLine;      // 这一段不到 120 字节（多半就是标题本身）
};

struct EpubResult {
  std::string title, author;
  std::vector<EpubTocEntry> toc;   // 按位置排好；没有目录时用 h1～h3 标题
  std::vector<uint32_t> heads;     // h1～h3 段落的起点（排版时居中加粗）
  uint32_t len = 0;                // 正文字节数
  std::string err;                 // 失败原因
};

struct TextSink;
// 正文边解边写进 sink（不用整本在内存里）；目录位置、标题文字在写的过程中顺便记下。
// progress(done, total) 按处理过的 XHTML 文件数报，可以为空
bool epubExtract(EpubSource &src, TextSink &sink, EpubResult &res,
                 const std::function<void(uint32_t, uint32_t)> &progress = nullptr);

// 解 raw deflate（设备上用 ROM 里的 tinfl，电脑上用 zlib）
bool epubInflate(const uint8_t *in, uint32_t inLen, uint8_t *out, uint32_t outLen);
