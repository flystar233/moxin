#pragma once
// 书：从 TF 卡读 UTF-8 的 TXT 整本放进 PSRAM，按当前版面分页；没有卡或没有书时用内置的《过秦论》。
// 阅读进度按"字节位置"存在 NVS 里，换字号、换屏以后也能接着读。
#include <Arduino.h>
#include <vector>

struct TextLine {
  uint32_t start, len;
  bool indent;  // 段首，缩进两个字
};

typedef int (*GlyphWidthFn)(uint32_t cp);

struct Layout {
  int maxWidth;      // 一行可用的像素宽
  int linesPerPage;
  int indentWidth;   // 段首缩进
  GlyphWidthFn width;
};

bool bookStorageBegin();            // 接通卡电源、挂载 TF 卡、列出书
bool storageMount();                // TF 卡上电并挂载（传文件用），用完调 storageUnmount
void storageUnmount();
bool bookRescan();                  // 重新扫描 TF 卡（打开菜单时调用；没有插卡检测）
int bookCount();                    // TF 卡上的书（不含内置）
String bookTitleAt(int i);
bool bookOpen(int i);               // i = -1 打开内置
bool bookOpenLast();                // 打开上次读的书，没有就打开第一本或内置
int bookCurrentIndex();             // -1 = 内置，-2 = 卡上的书但卡已拔出（书还在内存里）
String bookTitle();
String bookStatus();                // 最近一次打开的结果（给界面和串口用）

void bookPaginate(const Layout &L);
int bookPageCount();
int bookPage();
void bookGoto(int page);            // 越界会被截到首页 / 末页，并保存进度
// 取某一页的各行，返回行数
int bookPageLines(int page, TextLine *out, int maxLines);
const char *bookText();             // 行里的 start/len 指向这里
uint32_t bookSize();

uint32_t utf8Next(const char *s, uint32_t n, uint32_t &i);  // 解码一个字符，i 前进
