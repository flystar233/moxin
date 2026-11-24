"""串口升级固件：把 firmware.bin 发给正在运行的固件，它写进另一个程序区、校验通过后软件重启。
不用按电源键（拆了 R33 以后，普通烧录要一直按住电源键，因为烧录器的硬复位会让 PWR_HOLD 掉电）。

用法（在 app 目录下）：
  python tools/fwupdate.py                       # 默认 .pio/build/pcb1/firmware.bin，COM6
  python tools/fwupdate.py --port COM6 路径/firmware.bin
也可以：pio run -e pcb1_serial -t upload   （platformio.ini 里配好了，编译完自动用这个脚本上传）

中途断了会自动重试（最多 3 次）；失败时板子上还是原来的固件。
板子里的固件跑不起来（比如烧坏了）时这个办法用不了，只能按住电源键用普通方式烧：pio run -e pcb1 -t upload
"""
import argparse
import os
import sys
import time
import zlib

import serial

CHUNK = 16384   # 和固件 xfer.cpp 的 CHUNK 一样
ACK = b"\x06"


def open_port(port, timeout=20):
    end = time.time() + timeout
    while True:
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 0.2
            s.dtr = False  # 不要动 DTR/RTS：会让芯片硬复位（断电）
            s.rts = False
            s.open()
            return s
        except serial.SerialException:
            if time.time() > end:
                raise
            time.sleep(0.3)


def wait_line(s, prefixes, timeout):
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        buf += s.read(256)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            if text.startswith(prefixes):
                return text
    return None


def send(port, data, crc):
    """发一次，成功返回 True"""
    s = open_port(port)
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write(f"fwup {len(data)} {crc:08x}\r\n".encode())
    r = wait_line(s, ("READY", "ERR"), 15)
    if r is None or r.startswith("ERR"):
        print(f"  失败：{r or '板子没有回应（板子开着吗？跑的是 app 固件吗？）'}")
        s.close()
        return False
    t0 = time.time()
    sent = 0
    while sent < len(data):
        chunk = data[sent:sent + CHUNK]
        s.write(chunk)
        end = time.time() + 10
        while True:  # 等这一块的确认；中间夹着的日志字节忽略
            c = s.read(1)
            if c == ACK:
                break
            if time.time() > end:
                print("\n  等确认超时")
                s.close()
                return False
        sent += len(chunk)
        dt = max(time.time() - t0, 1e-3)
        print(f"\r  {sent * 100 // len(data):3d}%  {sent / 1048576:.2f} MB  {sent / 1024 / dt:.0f} KB/s", end="", flush=True)
    print()
    r = wait_line(s, ("OK", "ERR"), 60)  # 写完以后还要校验镜像（SHA256），要几秒
    s.close()
    if not r or not r.startswith("OK"):
        print(f"  失败：{r or '没等到结果'}")
        return False
    print(f"  {r}")
    return True


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="串口升级墨水屏板的固件（不用按电源键）")
    ap.add_argument("bin", nargs="?", default=os.path.join(here, "..", ".pio", "build", "pcb1", "firmware.bin"))
    ap.add_argument("--port", default="COM6")
    a = ap.parse_args()

    data = open(a.bin, "rb").read()
    if data[:1] != b"\xE9":
        sys.exit(f"{a.bin} 不像 ESP32 固件（开头不是 0xE9）")
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"固件 {a.bin}：{len(data) / 1048576:.2f} MB，CRC {crc:08x}")

    for attempt in range(3):
        if send(a.port, data, crc):
            break
        if attempt < 2:
            print(f"  第 {attempt + 2} 次重试（板子上还是原来的固件）…")
            time.sleep(7)  # 等板子那边接收超时、放弃上一次
    else:
        sys.exit("失败了 3 次（板子上还是原来的固件）")

    # 等它重启回来，确认跑的是新固件
    print("  等板子重启…")
    time.sleep(2)
    s = open_port(a.port, timeout=30)
    s.write(b"info\r\n")
    r = wait_line(s, ("固件",), 8)
    print(f"  {r}" if r else "  （没读到版本信息，看一下板子是否正常开机）")
    s.close()


if __name__ == "__main__":
    main()
