#pragma once
// 通过 USB 串口管理 TF 卡上的文件（没有读卡器时用）。电脑端脚本：app/tools/tfcard.py
//   fput <路径> <字节数> <crc32十六进制>   → 回 READY，之后每收满 4096 字节回 K，最后回 OK <crc> 或 ERR …
//   fls [目录]                             → 列目录，最后一行 END
//   frm <路径>                             → 删除文件
//   fdf                                    → 卡的容量和剩余空间
//   fwup <字节数> <crc32十六进制>           → 串口升级固件（协议同 fput），成功后自动重启；电脑端 tools/fwupdate.py
#include <Arduino.h>

// 是文件命令就处理并返回 true
bool xferCommand(const String &line);
