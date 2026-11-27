#pragma once
// 联网升级：只有在菜单里点"检查新版本"才联网查询，不定时查、不自动装。
// 服务器上放两个文件（必须 HTTPS），OTA_URL（config.h）指向 version.json：
//   {"version": "0.3.0", "url": "firmware-0.3.0.bin", "notes": "修了翻页卡顿"}
// url 可以写完整网址，也可以写相对 version.json 所在目录的文件名。两个文件由 tools/release.py 生成。
// 两个函数都要先连上 Wi-Fi（net::connect），会阻塞，在主循环里调用。
#include <Arduino.h>

#include <functional>

namespace net {

struct FwInfo {
  String version, url, notes;
};

// 1 = 有新版本（info 里是新版本信息），0 = 已是最新，-1 = 出错（why 里是原因）
int fwCheck(FwInfo &info, String *why);
// 下载并写进另一个程序区，镜像校验通过后切换启动分区；不重启。progress(0～100) 下载过程中反复调用
bool fwInstall(const FwInfo &info, std::function<void(int)> progress, String *why);

}  // namespace net
