#pragma once
// 排版和分页。正文不用整本在内存里：排一页只要这一页附近的一段（TextSpan），
// 分页时一段一段从卡上读（或者整本已经在内存里就直接用）。
#include "text_types.h"

// 内存里的一段正文：p[0] 是正文第 base 个字节，共 len 字节；total 是全书长度
struct TextSpan {
  const char *p = nullptr;
  uint32_t base = 0, len = 0, total = 0;
  uint32_t end() const { return base + len; }
  bool covers(uint32_t a, uint32_t b) const { return a >= base && b <= base + len; }
};

// 从 start 开始排最多 maxLines 行，返回下一页的起点。新的一章总是从新的一页开始，标题后空一行。
// span 要包含 start 前一个字节（判断是不是段首），往后要够一页（不够就排到 span 末尾为止）
uint32_t layoutPage(const TextSpan &span, const BookMeta &m, const Layout &L, uint32_t start, TextLine *out,
                    int maxLines, int *outCount);

// 全书分页，返回每页起点。whole 非空 = 整本已在内存（最快）；否则用 rd 一段一段读（缓冲 bufBytes）。
// progress 可以为空
bool paginate(const BookMeta &m, const Layout &L, const TextSpan *whole, TextReader *rd, uint32_t bufBytes,
              std::vector<uint32_t> &pages, ProgressFn progress = nullptr, void *ctx = nullptr);
