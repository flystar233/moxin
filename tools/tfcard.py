"""通过 USB 串口管理墨水屏板上 TF 卡里的文件（没有读卡器时用）。板子要插着 USB、跑着 app 固件。

用法（在 app 目录下）：
  python tools/tfcard.py put  桌面上的文件或文件夹  [卡上目录，默认 /]
  python tools/tfcard.py ls   [卡上目录，默认 /]
  python tools/tfcard.py rm   卡上路径
  python tools/tfcard.py df
可选 --port COM6（默认 COM6）

例：python tools/tfcard.py put "C:/Users/me/Desktop/LXGWWenKai-Regular.ttf" /fonts/
    python tools/tfcard.py put "C:/Users/me/Desktop/书" /books/      （整个文件夹）

需要 pyserial：PlatformIO 自带的 Python 里就有：
    %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe tools/tfcard.py ...
"""
import argparse
import os
import sys
import time
import zlib

import serial

CHUNK = 16384   # 要和固件 xfer.cpp 的 CHUNK 一样
ACK = b"\x06"


def open_port(port):
    t0 = time.time()
    while True:
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 0.2
            s.dtr = False  # 别动 DTR/RTS：USB 串口会把它们当成复位 / 下载模式
            s.rts = False
            s.open()
            break
        except serial.SerialException as e:
            # 电脑上别的程序（比如 keybrick 状态脚本）可能刚好占着串口，等一下再试
            if time.time() - t0 > 20:
                sys.exit(f"打不开 {port}：{e}")
            time.sleep(0.3)
    s.write(b"\r\n")       # 冲掉板子那边半截的输入
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def read_line(s, timeout):
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        c = s.read(1)
        if not c:
            continue
        if c == b"\n":
            return buf.decode("utf-8", "replace").rstrip("\r")
        buf += c
    return None


def wait_for(s, prefixes, timeout):
    """读行，直到某一行以 prefixes 之一开头；中间别的日志行原样打出来"""
    end = time.time() + timeout
    while time.time() < end:
        line = read_line(s, end - time.time())
        if line is None:
            break
        if line.startswith(prefixes):
            return line
        if line.strip():
            print("  [板子] " + line)
    return None


def cmd_lines(s, cmd, until="END", timeout=15):
    s.write((cmd + "\r\n").encode("utf-8"))
    out = []
    end = time.time() + timeout
    while time.time() < end:
        line = read_line(s, end - time.time())
        if line is None:
            break
        if line == until or line.startswith("ERR"):
            if line.startswith("ERR"):
                out.append(line)
            break
        out.append(line)
    return out


def put_file(s, local, remote):
    size = os.path.getsize(local)
    crc = 0
    with open(local, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            crc = zlib.crc32(b, crc)
    print(f"→ {remote}  {size / 1048576:.2f} MB")
    s.write(f"fput {remote} {size} {crc:08x}\r\n".encode("utf-8"))
    r = wait_for(s, ("READY", "ERR"), 15)
    if r is None or r.startswith("ERR"):
        print(f"  失败：{r or '板子没有回应（固件是不是 app？插着 USB 吗？）'}")
        return False
    t0 = time.time()
    sent = 0
    with open(local, "rb") as f:
        while sent < size:
            chunk = f.read(CHUNK)
            s.write(chunk)
            # 等这一块的确认；中间夹着的日志字节忽略
            end = time.time() + 10
            while True:
                c = s.read(1)
                if c == ACK:
                    break
                if time.time() > end:
                    print("\n  失败：等确认超时")
                    return False
            sent += len(chunk)
            if sent % (64 * CHUNK) == 0 or sent == size:
                dt = time.time() - t0
                print(f"\r  {sent * 100 // max(size, 1):3d}%  {sent / 1048576:.1f} MB  {sent / 1024 / max(dt, 1e-3):.0f} KB/s",
                      end="", flush=True)
    r = wait_for(s, ("OK", "ERR"), 30)
    print()
    if r and r.startswith("OK"):
        print(f"  完成，CRC 校验通过（{time.time() - t0:.1f} 秒）")
        return True
    print(f"  失败：{r or '没有收到结果'}")
    return False


def main():
    ap = argparse.ArgumentParser(description="通过 USB 串口管理墨水屏板 TF 卡上的文件")
    ap.add_argument("--port", default="COM6")
    sub = ap.add_subparsers(dest="op", required=True)
    p = sub.add_parser("put"); p.add_argument("local"); p.add_argument("dest", nargs="?", default="/")
    p = sub.add_parser("ls"); p.add_argument("dir", nargs="?", default="/")
    p = sub.add_parser("rm"); p.add_argument("path")
    sub.add_parser("df")
    a = ap.parse_args()

    s = open_port(a.port)
    if a.op == "ls":
        for line in cmd_lines(s, f"fls {a.dir}"):
            parts = line.split("\t")
            if len(parts) == 3:
                kind, name, size = parts
                print(f"{'[目录]' if kind == 'D' else '      '} {name:40s} {'' if kind == 'D' else f'{int(size) / 1024:10.1f} KB'}")
            else:
                print(line)
    elif a.op == "rm":
        print(cmd_lines(s, f"frm {a.path}", until="OK", timeout=10) or "OK")
    elif a.op == "df":
        s.write(b"fdf\r\n")
        print(wait_for(s, ("OK", "ERR"), 10))
    elif a.op == "put":
        dest = a.dest.replace("\\", "/")
        if not dest.startswith("/"):
            dest = "/" + dest
        files = []
        if os.path.isdir(a.local):
            base = os.path.basename(os.path.normpath(a.local))
            for root, _, names in os.walk(a.local):
                for n in names:
                    full = os.path.join(root, n)
                    rel = os.path.relpath(full, a.local).replace("\\", "/")
                    files.append((full, dest.rstrip("/") + "/" + base + "/" + rel))
        else:
            name = os.path.basename(a.local)
            files.append((a.local, dest + name if dest.endswith("/") else dest))
        ok = 0
        for local, remote in files:
            for attempt in range(3):  # 偶尔会卡住（板子正好在刷屏等），整个文件重传
                if put_file(s, local, remote):
                    ok += 1
                    break
                print(f"  第 {attempt + 2} 次重试…" if attempt < 2 else "  放弃")
                time.sleep(2)
                s.reset_input_buffer()
                s.write(b"\r\n")  # 让板子那边超时结束上一次接收
                time.sleep(6)
                s.reset_input_buffer()
        print(f"共 {len(files)} 个文件，成功 {ok} 个")
    s.close()


if __name__ == "__main__":
    main()
