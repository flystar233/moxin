#pragma once
// 按"第十二章 xxx""Chapter 3""序章""番外"这类规则认章节（TXT，以及没有像样目录的 EPUB）。
// 边导入边认：正文一段一段喂进来，不用整本书在内存里。
#include "text_types.h"

class ChapterScanner {
 public:
  explicit ChapterScanner(uint32_t maxChapters) : max_(maxChapters) {}
  void feed(const char *p, uint32_t n);  // 按顺序喂正文
  // 喂完了：把认出的章节写进 m.ch / m.titles / m.heads（标题行同时当标题段）。
  // 平均一章不到 400 字节，多半是把正文认成了标题，就不算
  void finish(BookMeta &m);
  size_t found() const { return ch_.size(); }

 private:
  void endLine();
  uint32_t max_, pos_ = 0, lineStart_ = 0;
  std::string line_;         // 当前这一行（只留前 4KB，标题不会那么长）
  bool lineLong_ = false;
  BookMeta tmp_;             // 认出来的章节先放这里
  std::vector<Chapter> &ch_ = tmp_.ch;
};

bool isChapterHeading(const char *p, uint32_t n);
