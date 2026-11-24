#pragma once
// 当前打开的书。
//
// 第一次打开一本书时"导入"：TXT（UTF-8 / GBK / UTF-16）、EPUB 都转成统一的 UTF-8 正文，存进卡上的缓存
// （/.reader/<编号>.txt），同时扫出目录信息（章节、标题段）存成 .bkm。以后再打开直接读缓存，书改了才重新导入。
// 分页表按版面（字体、字号、行距、边距）存成 .pg，换回同样的版面不用再排。
//
// 内存里只放阅读位置附近的一段正文（"窗口"）：整本放得下（≤ BOOK_WINDOW_MAX）就整本，
// 放不下就放全书的 20%（最少 1MB，最多 BOOK_WINDOW_MAX），翻出窗口再从卡上补。书多大都能读。
#include <Arduino.h>

#include <vector>

#include "text_types.h"

bool bookOpen(const String &path);  // BUILTIN_PATH = 内置文章；失败时原来的书不动
bool bookIsOpen();
String bookPath();
String bookTitle();
String bookError();                 // 最近一次打开失败的原因
String bookInfo();                  // 编码、大小，给"关于"和串口用
uint32_t bookSize();                // 正文总字节数

// 正文里 [off, off + len) 这一段（只在 bookPageLines 刚排过的那一页范围内保证有效）
const char *bookSpan(uint32_t off, uint32_t len);
// 正文里 [off, off + len) 这一段，需要时把阅读窗口移过去（主线程调用，语音朗读取句子用）。
// 返回的指针在下一次取窗口之前有效；off 越界或内存不够返回 nullptr
const char *bookTextAt(uint32_t off, uint32_t len);

const std::vector<Chapter> &bookChapters();
int bookChapterAt(uint32_t off);    // off 所在的章，-1 = 第一章之前
String bookChapterTitle(int i);

// 分页：keepOffset 所在的那一页作为当前页（有缓存直接读，没有就排一遍全书并存起来）
void bookPaginate(const Layout &L, uint32_t keepOffset);
bool bookPaginatedWith(const Layout &L);
int bookPageCount();
int bookPage();
void bookSetPage(int p);            // 越界截到首页 / 末页
uint32_t bookPageStart(int p);
int bookPageOf(uint32_t off);
uint32_t bookOffset();              // 当前页起点
int bookPermille();                 // 读到千分之几
int bookPageLines(int p, TextLine *out, int maxLines);

// 导入、排版这种要几秒的事，过程中报进度（界面画个提示框）。what = "正在导入" / "正在排版"
typedef void (*BookProgressFn)(const char *what, int percent);
void bookSetProgress(BookProgressFn fn);

// 删掉卡上的阅读缓存（全部重新导入）；返回删了几个文件
int bookCacheClear();
