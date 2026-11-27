"""墨芯 · USB 传书：本地网页，拖拽把书通过 USB 串口写进设备 TF 卡（不用 Wi-Fi）。

用法（在仓库根目录）：
    %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe tools/usbput.py
    ... --port COM17          # 自动找 ESP32 串口失败时手动指定
    ... --http-port 8765      # 网页端口
    ... --no-browser          # 不自动开浏览器

它在本机起一个只监听 127.0.0.1 的小服务，你在网页里拖 TXT/EPUB，文件走
USB 串口按固件的 fput 协议写进 TF 卡，和 tools/tfcard.py 用的是同一套协议。
"""
import argparse
import json
import os
import re
import sys
import tempfile
import threading
import time
import uuid
import webbrowser
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

try:
    import serial
except ImportError:
    serial = None

list_ports = None
if serial is not None:
    try:
        from serial.tools import list_ports
    except ImportError:  # 有的环境里装的是假的 serial 包，或者 pyserial 太老
        list_ports = None

if serial is None or not hasattr(serial, "Serial"):
    sys.exit(
        "没有可用的 pyserial。\n"
        "  1) 用 PlatformIO 自带的 Python（推荐，已经装好）：\n"
        "     %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe tools\\usbput.py\n"
        "  2) 或者在你现在这个 Python 里装：python -m pip install pyserial\n"
        "     （如果提示 serial 已存在但不对：先 python -m pip uninstall serial，再装 pyserial）"
    )

CHUNK = 16384  # 和固件 src/core/xfer.cpp 的 CHUNK 一样
ACK = b"\x06"
BOOK_EXTS = (".txt", ".epub")
FONT_EXTS = (".efnt",)

# ---------------------------------------------------------------- 串口设备


def _windows_com_ports():
    """list_ports 用不了时，从注册表拿 COM 口列表（Windows）。"""
    ports = []
    if os.name != "nt":
        return ports
    try:
        import winreg

        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM")
        i = 0
        while True:
            try:
                _, val, _ = winreg.EnumValue(key, i)
            except OSError:
                break
            ports.append(str(val))
            i += 1
    except OSError:
        pass
    return ports


def _probe_device(port):
    """打开串口问一句 fdf，能回 OK 就说明是墨芯（用来在多个 COM 口里认设备）。"""
    try:
        s = serial.Serial()
        s.port, s.baudrate, s.timeout, s.write_timeout = port, 115200, 0.1, 0.5
        s.dtr = False
        s.rts = False
        s.open()
        try:
            s.write(b"\r\nfdf\r\n")
            end = time.time() + 1.5
            buf = b""
            while time.time() < end:
                buf += s.read(64)
                if b"OK " in buf or b"ERR " in buf:
                    return True
        finally:
            s.close()
    except Exception:  # noqa: BLE001
        return False
    return False


def find_port(prefer=None):
    if prefer:
        return prefer
    comps = []
    if list_ports is not None:
        try:
            comps = list(list_ports.comports())
        except Exception:  # noqa: BLE001
            comps = []
    esp = [p.device for p in comps if getattr(p, "vid", None) == 0x303A]  # Espressif
    if esp:
        return esp[0]
    devs = [p.device for p in comps]
    if not devs:
        devs = _windows_com_ports()
    if len(devs) == 1:
        return devs[0]
    for d in devs:
        if _probe_device(d):
            return d
    return None


def open_serial(port):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False  # 动 DTR/RTS 会让芯片复位
    s.rts = False
    s.open()
    s.write(b"\r\n")  # 叫醒串口控制台、冲掉半截输入
    time.sleep(0.2)
    s.write(b"\r\n")
    time.sleep(0.2)
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
    end = time.time() + timeout
    while time.time() < end:
        line = read_line(s, end - time.time())
        if line is None:
            break
        if line.startswith(prefixes):
            return line
    return None


def wait_ack(s, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if s.read(1) == ACK:
            return True
    return False


def crc32_file(path):
    crc = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            crc = zlib.crc32(b, crc)
    return crc & 0xFFFFFFFF


class Device:
    """串口独占 + 懒连接；所有操作都是串行的。"""

    def __init__(self, port=None):
        self.port = port
        self.ser = None
        self.lock = threading.Lock()

    def ensure(self):
        if self.ser and self.ser.is_open:
            return self.ser
        port = find_port(self.port)
        if not port:
            raise RuntimeError("没找到设备串口：插着 USB 吗？（也可以用 --port COM17 指定）")
        self.port = port
        self.ser = open_serial(port)
        return self.ser

    def send_file(self, local, remote, progress):
        """按 fput 协议发送一个文件；progress(sent, size) 用来更新网页。"""
        with self.lock:
            for attempt in range(2):
                try:
                    self._send_file_once(local, remote, progress)
                    return
                except Exception:
                    if attempt:
                        raise
                    # 设备可能睡着了 / 上一次没收干净：等一下重来
                    self.close()
                    time.sleep(1.2)
                    self.ensure()

    def _send_file_once(self, local, remote, progress):
        s = self.ensure()
        size = os.path.getsize(local)
        crc = crc32_file(local)
        s.write(f"fput {remote} {size} {crc:08x}\r\n".encode("utf-8"))
        r = wait_for(s, ("READY", "ERR"), 20)
        if r is None:
            raise RuntimeError("设备没有回应（是不是在睡 / 串口被别的程序占了？）")
        if r.startswith("ERR"):
            raise RuntimeError(r[4:].strip() or "设备拒绝了这次写入")

        sent = 0
        with open(local, "rb") as f:
            while sent < size:
                chunk = f.read(CHUNK)
                if not chunk:
                    break
                s.write(chunk)
                if not wait_ack(s, 15):
                    raise RuntimeError("等设备确认超时（线松了？设备睡了？）")
                sent += len(chunk)
                progress(sent, size)
        r = wait_for(s, ("OK", "ERR"), 40)
        if r is None:
            raise RuntimeError("传完了但没收到校验结果")
        if r.startswith("ERR"):
            raise RuntimeError(r[4:].strip() or "CRC 校验失败")

    def list_dir(self, path="/books"):
        with self.lock:
            s = self.ensure()
            s.write(f"fls {path}\r\n".encode("utf-8"))
            out = []
            end = time.time() + 15
            while time.time() < end:
                line = read_line(s, end - time.time())
                if line is None:
                    break
                if line == "END":
                    break
                if line.startswith("ERR"):
                    raise RuntimeError(line[4:].strip())
                parts = line.split("\t")
                if len(parts) == 3:
                    kind, name, sz = parts
                    out.append({"name": name, "dir": kind == "D", "size": int(sz or 0)})
            return out

    def remove(self, path):
        with self.lock:
            s = self.ensure()
            s.write(f"frm {path}\r\n".encode("utf-8"))
            line = wait_for(s, ("OK", "ERR"), 15)
            if line is None:
                raise RuntimeError("设备没有回应")
            if line.startswith("ERR"):
                raise RuntimeError(line[4:].strip())

    def df(self):
        with self.lock:
            s = self.ensure()
            s.write(b"fdf\r\n")
            line = wait_for(s, ("OK", "ERR"), 10)
            if line is None:
                raise RuntimeError("设备没有回应")
            if line.startswith("ERR"):
                raise RuntimeError(line[4:].strip())
            return line[3:].strip()  # "容量 1234 MB，已用 12 MB，剩余 1222 MB"

    def close(self):
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        self.ser = None


# ---------------------------------------------------------------- 上传任务

TASKS = {}
TASKS_LOCK = threading.Lock()
DEV = Device()


def task_update(tid, **kw):
    with TASKS_LOCK:
        t = TASKS.setdefault(tid, {})
        t.update(kw)


def worker(tid, local, remote):
    try:
        DEV.send_file(local, remote, lambda sent, size: task_update(tid, sent=sent, size=size))
        task_update(tid, state="done", msg="完成，CRC 校验通过")
    except Exception as e:  # noqa: BLE001
        task_update(tid, state="error", msg=str(e))
    finally:
        try:
            os.unlink(local)
        except OSError:
            pass


# ---------------------------------------------------------------- 网页


def page_html():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "usbput_page.html"), "r", encoding="utf-8") as f:
        return f.read()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # 安静一点
        pass

    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/":
            body = page_html().encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if u.path == "/api/info":
            try:
                port = DEV.port or find_port()
                busy = DEV.lock.locked()
                df = "" if busy else DEV.df()
                total = used = free = 0
                m = re.search(r"容量\s*(\d+)\s*MB，已用\s*(\d+)\s*MB，剩余\s*(\d+)\s*MB", df)
                if m:
                    total, used, free = (int(m.group(i)) for i in (1, 2, 3))
                return self._json(
                    {"port": port, "df": df, "busy": busy, "total": total, "used": used, "free": free}
                )
            except Exception as e:  # noqa: BLE001
                return self._json({"port": DEV.port, "df": "", "busy": False, "error": str(e)})
        if u.path == "/api/list":
            d = parse_qs(u.query).get("dir", ["/books"])[0] or "/books"
            if d not in ("/books", "/fonts"):
                return self._json({"items": [], "error": "目录不对"}, 400)
            try:
                items = [x for x in DEV.list_dir(d) if not x["dir"]]
                return self._json({"items": items})
            except Exception as e:  # noqa: BLE001
                return self._json({"items": [], "error": str(e)}, 500)
        if u.path == "/api/progress":
            tid = parse_qs(u.query).get("id", [""])[0]
            with TASKS_LOCK:
                t = dict(TASKS.get(tid, {"state": "error", "msg": "没有这个任务"}))
            return self._json(t)
        return self._json({"msg": "not found"}, 404)

    def do_POST(self):
        u = urlparse(self.path)
        if u.path == "/api/upload":
            q = parse_qs(u.query)
            name = unquote(q.get("name", [""])[0] or "")
            name = os.path.basename(name)  # 只取文件名，防 ../
            d = q.get("dir", ["/books"])[0] or "/books"
            allowed = {"/books": BOOK_EXTS, "/fonts": FONT_EXTS}
            if d not in allowed:
                return self._json({"msg": "目录不对"}, 400)
            ext = os.path.splitext(name)[1].lower()
            if not name or ext not in allowed[d]:
                return self._json({"msg": "这个目录只收 " + " / ".join(allowed[d])}, 400)
            length = int(self.headers.get("Content-Length") or 0)
            if length <= 0:
                return self._json({"msg": "空文件"}, 400)
            fd, tmp = tempfile.mkstemp(suffix=ext)
            try:
                with os.fdopen(fd, "wb") as f:
                    left = length
                    while left > 0:
                        b = self.rfile.read(min(65536, left))
                        if not b:
                            raise RuntimeError("上传中断")
                        f.write(b)
                        left -= len(b)
            except Exception as e:  # noqa: BLE001
                try:
                    os.unlink(tmp)
                except OSError:
                    pass
                return self._json({"msg": str(e)}, 500)
            tid = uuid.uuid4().hex
            task_update(tid, state="sending", sent=0, size=length, msg="")
            threading.Thread(target=worker, args=(tid, tmp, d + "/" + name), daemon=True).start()
            return self._json({"id": tid}, 202)
        if u.path == "/api/delete":
            path = unquote(parse_qs(u.query).get("path", [""])[0] or "")
            if not (path.startswith("/books/") or path.startswith("/fonts/")) or ".." in path:
                return self._json({"msg": "路径不对"}, 400)
            try:
                DEV.remove(path)
                return self._json({"ok": True})
            except Exception as e:  # noqa: BLE001
                return self._json({"msg": str(e)}, 500)
        return self._json({"msg": "not found"}, 404)


def main():
    ap = argparse.ArgumentParser(description="墨芯 USB 传书：本地网页，把书通过 USB 串口写进 TF 卡")
    ap.add_argument("--port", help="设备串口，默认自动找 Espressif USB 串口")
    ap.add_argument("--http-port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()

    DEV.port = find_port(a.port)
    srv = ThreadingHTTPServer(("127.0.0.1", a.http_port), Handler)
    url = f"http://127.0.0.1:{a.http_port}/"
    print(f"墨芯 · USB 传书  {url}")
    print(f"设备串口：{DEV.port or '还没找到（插上 USB 会自动找）'}")
    print("Ctrl+C 退出")
    if not a.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
        DEV.close()


if __name__ == "__main__":
    main()
