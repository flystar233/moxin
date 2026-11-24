#include "xfer.h"

#include <FS.h>
#include <SD_MMC.h>

#include "book.h"
#include "config.h"

// 每收满这么多回一个确认（ACK = 0x06），电脑再发下一块。要小于串口接收缓冲（main.cpp 里 setRxBufferSize）。
// 11-10 实测 4KB 一块只有 162KB/s，主要是每块一来一回的延迟，改 16KB
static const size_t CHUNK = 16384;
static const uint8_t ACK = 0x06;

static uint32_t s_crcTab[256];
static void crcInit() {
  static bool done = false;
  if (done) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    s_crcTab[i] = c;
  }
  done = true;
}
// 和 Python 的 zlib.crc32(data, crc) 一致
static uint32_t crcUpdate(uint32_t crc, const uint8_t *p, size_t n) {
  crc = ~crc;
  while (n--) crc = s_crcTab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

static void mkdirs(const String &path) {
  for (unsigned i = 1; i < path.length(); i++)
    if (path[i] == '/') {
      String d = path.substring(0, i);
      if (!SD_MMC.exists(d)) SD_MMC.mkdir(d);
    }
}

static bool readExact(uint8_t *buf, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    int a = Serial.available();
    if (a > 0) {
      got += Serial.readBytes(buf + got, min((size_t)a, n - got));
      t0 = millis();
    } else if (millis() - t0 > timeoutMs) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

// fput <路径> <字节数> <crc32>：路径里可以有空格，所以从后往前取后两个字段
static void doPut(String args) {
  args.trim();
  int sp2 = args.lastIndexOf(' ');
  int sp1 = sp2 > 0 ? args.lastIndexOf(' ', sp2 - 1) : -1;
  if (sp1 <= 0) { Serial.println("ERR 用法：fput <路径> <字节数> <crc32>"); return; }
  String path = args.substring(0, sp1);
  path.trim();
  uint32_t size = strtoul(args.substring(sp1 + 1, sp2).c_str(), nullptr, 10);
  uint32_t want = strtoul(args.substring(sp2 + 1).c_str(), nullptr, 16);
  if (!path.startsWith("/")) path = "/" + path;

  if (!storageMount()) { Serial.println("ERR 没有 TF 卡或挂载失败"); return; }
  uint64_t freeB = SD_MMC.totalBytes() - SD_MMC.usedBytes();
  if (size + 65536 > freeB) { Serial.println("ERR 卡上空间不够"); storageUnmount(); return; }
  mkdirs(path);
  String tmp = path + ".part";  // 先写临时文件，校验通过再改名，传一半断了不会留下坏文件
  if (SD_MMC.exists(tmp)) SD_MMC.remove(tmp);
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) { Serial.println("ERR 建不了文件（路径或文件名不支持？）"); storageUnmount(); return; }

  crcInit();
  setCpuFrequencyMhz(240);
  static uint8_t buf[CHUNK];
  uint32_t left = size, crc = 0, t0 = millis();
  bool ok = true;
  const char *why = "";
  // 命令行末尾的 \r\n 里，\r 结束了命令，\n 还留在缓冲里：不清掉会被当成文件第一个字节（11-10 CRC 错就是它）。
  // 电脑要等到 READY 才发数据，所以这之前缓冲里的都是残留
  delay(20);
  while (Serial.available()) Serial.read();
  Serial.println("READY");
  Serial.flush();
  while (left) {
    size_t n = min<uint32_t>(CHUNK, left);
    if (!readExact(buf, n, 5000)) { ok = false; why = "接收超时"; break; }
    crc = crcUpdate(crc, buf, n);
    if (f.write(buf, n) != n) { ok = false; why = "写卡失败（卡满了或卡坏了）"; break; }
    left -= n;
    Serial.write(ACK);
  }
  f.close();
  setCpuFrequencyMhz(CPU_MHZ);
  if (ok && crc != want) { ok = false; why = "CRC 不对"; }
  if (ok) {
    if (SD_MMC.exists(path)) SD_MMC.remove(path);
    ok = SD_MMC.rename(tmp, path);
    if (!ok) why = "改名失败";
  }
  if (!ok && SD_MMC.exists(tmp)) SD_MMC.remove(tmp);
  uint32_t ms = millis() - t0;
  storageUnmount();
  if (ok) Serial.printf("OK %08lx %lu ms\n", (unsigned long)crc, (unsigned long)ms);
  else Serial.printf("ERR %s\n", why);
}

static void doList(String dir) {
  dir.trim();
  if (!dir.length()) dir = "/";
  if (!storageMount()) { Serial.println("ERR 没有 TF 卡或挂载失败"); Serial.println("END"); return; }
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) {
    Serial.println("ERR 不是目录");
  } else {
    for (File f = d.openNextFile(); f; f = d.openNextFile())
      Serial.printf("%s\t%s\t%lu\n", f.isDirectory() ? "D" : "F", f.name(), (unsigned long)f.size());
  }
  storageUnmount();
  Serial.println("END");
}

static void doRemove(String path) {
  path.trim();
  if (!path.startsWith("/")) path = "/" + path;
  if (!storageMount()) { Serial.println("ERR 没有 TF 卡或挂载失败"); return; }
  bool ok = false;
  if (SD_MMC.exists(path)) {
    File f = SD_MMC.open(path);
    bool dir = f && f.isDirectory();
    f.close();
    ok = dir ? SD_MMC.rmdir(path) : SD_MMC.remove(path);
  }
  storageUnmount();
  Serial.println(ok ? "OK" : "ERR 删不掉（不存在，或是非空目录）");
}

static void doFree() {
  if (!storageMount()) { Serial.println("ERR 没有 TF 卡或挂载失败"); return; }
  Serial.printf("OK 容量 %llu MB，已用 %llu MB，剩余 %llu MB\n", SD_MMC.totalBytes() >> 20, SD_MMC.usedBytes() >> 20,
                (SD_MMC.totalBytes() - SD_MMC.usedBytes()) >> 20);
  storageUnmount();
}

bool xferCommand(const String &line) {
  if (line.startsWith("fput ")) { doPut(line.substring(5)); return true; }
  if (line == "fls" || line.startsWith("fls ")) { doList(line.length() > 3 ? line.substring(4) : String("/")); return true; }
  if (line.startsWith("frm ")) { doRemove(line.substring(4)); return true; }
  if (line == "fdf") { doFree(); return true; }
  return false;
}
