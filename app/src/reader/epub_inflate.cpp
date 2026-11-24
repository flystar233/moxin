// 设备上解 deflate：用 ESP32-S3 ROM 里的 miniz（tinfl），不占 flash。
// 解压器状态约 11KB，放堆上（主循环任务的栈只有 8KB，不能用 tinfl_decompress_mem_to_mem）。
#include <stdlib.h>

#include "epub.h"
#include "esp32s3/rom/miniz.h"

bool epubInflate(const uint8_t *in, uint32_t inLen, uint8_t *out, uint32_t outLen) {
  tinfl_decompressor *d = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
  if (!d) return false;
  tinfl_init(d);
  size_t inBytes = inLen, outBytes = outLen;
  tinfl_status st = tinfl_decompress(d, in, &inBytes, out, out, &outBytes, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(d);
  return st == TINFL_STATUS_DONE && outBytes == outLen;
}
