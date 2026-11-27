"""从设备截图：发 shot 命令，收 PBM，存成 PNG（不需要 PIL）。
用法：python tools/shot.py COM6 out.png [先发的命令;用分号隔开] [每条命令后等几秒]
例：  python tools/shot.py COM6 menu.png "m" 3
"""
import struct
import sys
import time
import zlib

import serial


def save_png(path, w, h, rows, scale=1):
    # 1 位灰度 PNG：1 = 白。PBM 里 1 = 黑，所以取反
    raw = bytearray()
    for r in rows:
        bits = [(r[x >> 3] >> (7 - (x & 7))) & 1 for x in range(w)]
        bits = [b for b in bits for _ in range(scale)]
        line = bytearray((len(bits) + 7) // 8)
        for x, b in enumerate(bits):
            if not b:
                line[x >> 3] |= 0x80 >> (x & 7)
        for _ in range(scale):
            raw += b"\x00" + line

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 1, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    port, out = sys.argv[1], sys.argv[2]
    cmds = [c for c in (sys.argv[3].split(";") if len(sys.argv) > 3 else []) if c]
    wait = float(sys.argv[4]) if len(sys.argv) > 4 else 3
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False
    s.rts = False
    end = time.time() + 20
    while True:  # 别的程序可能正短暂占着串口
        try:
            s.open()
            break
        except serial.SerialException:
            if time.time() > end:
                raise
            time.sleep(0.3)
    log = sys.stdout.buffer

    def pump(sec):
        e = time.time() + sec
        while time.time() < e:
            d = s.read(4096)
            if d:
                log.write(d)
                log.flush()

    pump(0.3)
    for c in cmds:
        s.write((c + "\r\n").encode())
        pump(wait)
    s.reset_input_buffer()
    s.write(b"shot\r\n")
    buf = b""
    end = time.time() + 15
    while b"P4\n" not in buf and time.time() < end:
        buf += s.read(4096)
    buf = buf[buf.index(b"P4\n") + 3:]
    while buf.count(b"\n") < 1:
        buf += s.read(4096)
    dims, buf = buf.split(b"\n", 1)
    w, h = map(int, dims.split())
    need = (w + 7) // 8 * h
    while len(buf) < need and time.time() < end:
        buf += s.read(need - len(buf))
    stride = (w + 7) // 8
    rows = [buf[i * stride:(i + 1) * stride] for i in range(h)]
    save_png(out, w, h, rows, scale=2)
    print(f"\n截图 {w}x{h} → {out}")


if __name__ == "__main__":
    main()
