#pragma once
// Edge 朗读的 Sec-MS-GEC v2 令牌。
//
// 老版本就是「时间刻度 + 固定串」做一次 SHA-256；2026 年的接口改成了 v2：
// 令牌形如 "<16 位十六进制>-<64 位十六进制>"，前一段是 CS64 摘要、后一段是
// 改了初始状态的 SHA-256 摘要（和标准 SHA-256 结果不一样，必须照抄算法）。
// 时间按 5 分钟一档取，所以设备时钟必须准。
#include <Arduino.h>

namespace tts {
// unixSec 换成某一档的令牌；不传就用当前时间
String edgeSecMsGec(uint64_t unixSec);
}  // namespace tts
