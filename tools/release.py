"""发版：把编译好的固件整理成联网升级用的两个文件，放进 dist/，再整个传到 OTA_URL 所在的目录。

    pio run                                   # 先改 src/config.h 的 FW_VERSION，再编译
    python tools/release.py --notes "修了翻页卡顿"

生成：
    dist/firmware-<版本>.bin
    dist/version.json   {"version": "<版本>", "url": "firmware-<版本>.bin", "notes": "..."}

先传 bin、后传 version.json：顺序反了的话，中间有设备来查会拿到一个还不存在的文件。
设备只在菜单里点"检查新版本"时才来查，版本号比本机新才提示升级。
"""
import argparse
import json
import os
import re
import shutil
import sys


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..")
    ap = argparse.ArgumentParser(description="生成联网升级用的 version.json 和固件")
    ap.add_argument("--notes", default="", help="更新说明，会显示在设备的确认框里（最多四行，写短一点）")
    ap.add_argument("--bin", default=os.path.join(root, ".pio", "build", "pcb1", "firmware.bin"))
    ap.add_argument("--out", default=os.path.join(root, "dist"))
    a = ap.parse_args()

    cfg = open(os.path.join(root, "src", "config.h"), encoding="utf-8").read()
    m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', cfg)
    if not m:
        sys.exit("src/config.h 里找不到 FW_VERSION")
    ver = m.group(1)

    data = open(a.bin, "rb").read()
    if data[:1] != b"\xE9":
        sys.exit(f"{a.bin} 不像 ESP32 固件（开头不是 0xE9）")
    if ver.encode() not in data:
        sys.exit(f"{a.bin} 里找不到版本号 {ver}：改了 FW_VERSION 以后重新编译了吗？")

    os.makedirs(a.out, exist_ok=True)
    name = f"firmware-{ver}.bin"
    shutil.copyfile(a.bin, os.path.join(a.out, name))
    with open(os.path.join(a.out, "version.json"), "w", encoding="utf-8") as f:
        json.dump({"version": ver, "url": name, "notes": a.notes}, f, ensure_ascii=False, indent=2)
    print(f"版本 {ver}，固件 {len(data) / 1048576:.2f} MB")
    print(f"  {os.path.join(a.out, name)}")
    print(f"  {os.path.join(a.out, 'version.json')}")
    print("先上传 bin，再上传 version.json")


if __name__ == "__main__":
    main()
