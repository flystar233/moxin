#pragma once
// 书库：TF 卡 /books 和根目录里的 .txt、.epub，加上内置的一篇文章；每本书的阅读进度存在 NVS。
// 进度按"在 UTF-8 正文里的字节位置"存，换字号、换屏以后也能接着读。
#include <Arduino.h>

#include <vector>

#define BUILTIN_PATH "<内置>"

struct BookEntry {
  String path;     // BUILTIN_PATH = 内置
  String title;
  uint32_t bytes;  // 文件大小
  int permille;    // 读到千分之几，-1 = 没读过
};

void libraryBegin();
bool libraryScan();                               // 挂载卡、重新列书；没有卡返回 false（书单里只有内置）
const std::vector<BookEntry> &libraryBooks();     // 最近读过的在前
String libraryLastPath();                         // 上次读的书（没有 = 内置）
void librarySetLast(const String &path);
// 上次读的书读到哪一章（章名），桌面上显示；换书时清空
String libraryLastChapter();
void librarySetChapter(const String &title);
uint32_t progressLoad(const String &path);
uint32_t libraryRecentSeq(const String &path);    // 最近打开的序号（越大越近，0 = 没打开过）；清阅读缓存时用
void progressSave(const String &path, uint32_t offset, int permille);
int progressPermille(const String &path);
String titleFromPath(const String &path);
