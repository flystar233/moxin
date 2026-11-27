#pragma once
// TF 卡：只在用的时候上电挂载，用完卸载并关卡电源（省电，文档 CARD1）。
// 挂载有引用计数，嵌套调用没问题；推荐用 SdSession 自动配对。
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

#include <vector>

bool sdMount();
void sdUnmount();
bool sdLastMountOk();  // 最近一次挂载是否成功（不重新挂载）

struct SdSession {
  bool ok;
  SdSession() : ok(sdMount()) {}
  ~SdSession() { if (ok) sdUnmount(); }
  SdSession(const SdSession &) = delete;
  SdSession &operator=(const SdSession &) = delete;
};

struct DirEntry {
  String path;    // 完整路径
  String name;
  uint32_t size;
  bool dir;
};
// 列目录（调用方要先挂载）
std::vector<DirEntry> sdList(const char *dir, size_t maxEntries = 500);
// 整个文件读进 PSRAM（调用方 free）；超过 maxBytes 返回 nullptr
uint8_t *sdReadAll(const String &path, uint32_t &len, uint32_t maxBytes);
// 读进调用方给的缓冲（调用方要先挂载）；文件比 cap 大返回 false
bool sdReadInto(const String &path, uint8_t *buf, uint32_t cap, uint32_t &len);
