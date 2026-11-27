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
import sys
import tempfile
import threading
import time
import uuid
import webbrowser
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

import serial
from serial.tools import list_ports

CHUNK = 16384  # 和固件 src/core/xfer.cpp 的 CHUNK 一样
ACK = b"\x06"
BOOK_EXTS = (".txt", ".epub")
FONT_EXTS = (".efnt",)

# ---------------------------------------------------------------- 串口设备


def find_port(prefer=None):
    if prefer:
        return prefer
    cands = [p.device for p in list_ports.comports() if p.vid == 0x303A]  # Espressif
    return cands[0] if cands else None


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
            raise RuntimeError("没找到设备串口：插着 USB 吗？（也可以用 --port 指定）")
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
    return """<!doctype html><html lang="zh"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>墨芯 · USB 传书</title>
<style>
:root{--fg:#1b1b1b;--muted:#6b6b6b;--line:#ddd;--accent:#2f6f4f;--bg:#faf9f6}
*{box-sizing:border-box}
body{margin:0;font:15px/1.6 -apple-system,"Segoe UI","Microsoft YaHei",sans-serif;color:var(--fg);background:var(--bg)}
header{display:flex;align-items:baseline;gap:14px;padding:22px 28px 14px;border-bottom:1px solid var(--line)}
h1{font-size:19px;margin:0;letter-spacing:.5px}
#info{color:var(--muted);font-size:13px;margin-left:auto;text-align:right}
main{max-width:820px;margin:0 auto;padding:22px 20px 60px}
.drop{border:2px dashed #c9c4ba;border-radius:14px;padding:34px 20px;text-align:center;background:#fff;transition:.15s}
.drop.on{border-color:var(--accent);background:#f2f8f4}
.drop p{margin:6px 0}
.sub{color:var(--muted);font-size:13px}
.btn{display:inline-block;padding:7px 14px;border:1px solid var(--accent);color:var(--accent);border-radius:9px;cursor:pointer;background:#fff}
h2{font-size:14px;color:var(--muted);font-weight:600;margin:28px 0 8px;display:flex;align-items:center;gap:10px}
ul{list-style:none;margin:0;padding:0}
li{display:flex;align-items:center;gap:12px;padding:10px 12px;background:#fff;border:1px solid var(--line);border-radius:10px;margin-bottom:8px}
li .n{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
li .s{color:var(--muted);font-size:12px;white-space:nowrap}
.bar{height:6px;background:#eee;border-radius:3px;overflow:hidden;width:150px}
.bar i{display:block;height:100%;width:0;background:var(--accent);transition:width .2s}
.ok{color:var(--accent)} .bad{color:#b03a2e}
button.del{border:1px solid var(--line);background:#fff;border-radius:7px;padding:3px 9px;cursor:pointer;color:var(--muted)}
button.del:hover{color:#b03a2e;border-color:#e5b4ae}
#refresh{border:1px solid var(--line);background:#fff;border-radius:8px;padding:3px 10px;cursor:pointer;color:var(--muted);font-size:12px}
</style>
<header><h1>墨芯 · USB 传书</h1><span id="info">正在读取设备…</span></header>
<main>
  <div class="drop" id="drop">
    <p><b>把 TXT / EPUB 拖到这里</b></p>
    <p class="sub">或者 <label class="btn">选择文件<input id="file" type="file" multiple accept=".txt,.epub" hidden></label></p>
    <p class="sub">走 USB 串口直接写进 TF 卡，不用 Wi-Fi。传的时候别拔线。</p>
  </div>
  <h2>队列</h2>
  <ul id="queue"><li class="sub" style="border:none;background:none;padding-left:0">还没有文件</li></ul>
  <h2>设备上的书 <button id="refresh">刷新</button></h2>
  <ul id="books"><li class="sub" style="border:none;background:none;padding-left:0">…</li></ul>
</main>
<script>
const $ = id => document.getElementById(id);
const fmt = b => b >= 1048576 ? (b/1048576).toFixed(1)+' MB' : Math.max(1, Math.round(b/1024))+' KB';

async function info(){
  try{
    const j = await (await fetch('/api/info')).json();
    $('info').textContent = '串口 ' + (j.port || '未连接') + (j.df ? ' · ' + j.df : '') + (j.busy ? ' · 设备忙' : '');
  }catch(e){ $('info').textContent = '设备没连上：' + e; }
}
async function books(){
  const ul = $('books'); ul.innerHTML = '<li class="sub" style="border:none;background:none;padding-left:0">…</li>';
  try{
    const j = await (await fetch('/api/books')).json();
    if(!j.items.length){ ul.innerHTML = '<li class="sub" style="border:none;background:none;padding-left:0">（空）</li>'; return; }
    ul.innerHTML = '';
    for(const it of j.items){
      const li = document.createElement('li');
      const n = document.createElement('span'); n.className='n'; n.textContent = it.name;
      const s = document.createElement('span'); s.className='s'; s.textContent = fmt(it.size);
      const b = document.createElement('button'); b.className='del'; b.textContent='删除';
      b.onclick = async () => {
        if(!confirm('从设备删除 ' + it.name + '？')) return;
        await fetch('/api/delete?path=' + encodeURIComponent('/books/' + it.name), {method:'POST'});
        books(); info();
      };
      li.append(n, s, b); ul.append(li);
    }
  }catch(e){ ul.innerHTML = '<li class="sub">读取失败：' + e + '</li>'; }
}
function row(name){
  const li = document.createElement('li');
  li.innerHTML = '<span class="n"></span><span class="bar"><i></i></span><span class="s">等待</span>';
  li.querySelector('.n').textContent = name;
  $('queue').prepend(li);
  return li;
}
function setRow(li, pct, text, cls){
  li.querySelector('.bar i').style.width = Math.max(0, Math.min(100, pct)) + '%';
  const s = li.querySelector('.s'); s.textContent = text; s.className = 's ' + (cls || '');
}
function upload(file){
  const li = row(file.name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/upload?name=' + encodeURIComponent(file.name) + '&dir=/books');
  xhr.upload.onprogress = e => { if(e.lengthComputable) setRow(li, 100*e.loaded/e.total*0.15, '发给电脑端 ' + Math.round(100*e.loaded/e.total) + '%'); };
  xhr.onload = () => {
    let j; try{ j = JSON.parse(xhr.responseText); }catch(e){ setRow(li, 100, '出错', 'bad'); return; }
    if(!j.id){ setRow(li, 100, j.msg || '出错', 'bad'); return; }
    poll(li, j.id);
  };
  xhr.onerror = () => setRow(li, 100, '本机服务断了', 'bad');
  xhr.send(file);
}
async function poll(li, id){
  while(true){
    await new Promise(r => setTimeout(r, 250));
    let j; try{ j = await (await fetch('/api/progress?id=' + id)).json(); }catch(e){ setRow(li, 100, '轮询失败', 'bad'); return; }
    if(j.state === 'sending'){ setRow(li, 15 + 85*(j.sent/Math.max(j.size,1)), '写入设备 ' + Math.round(100*j.sent/Math.max(j.size,1)) + '%'); continue; }
    if(j.state === 'done'){ setRow(li, 100, j.msg, 'ok'); info(); books(); return; }
    setRow(li, 100, j.msg || '失败', 'bad'); return;
  }
}
let chain = Promise.resolve();
function enqueue(files){ for(const f of files) chain = chain.then(() => upload(f)); }

const drop = $('drop');
drop.ondragover = e => { e.preventDefault(); drop.classList.add('on'); };
drop.ondragleave = () => drop.classList.remove('on');
drop.ondrop = e => { e.preventDefault(); drop.classList.remove('on'); enqueue([...e.dataTransfer.files].filter(f => /\\.(txt|epub)$/i.test(f.name))); };
$('file').onchange = e => { enqueue([...e.target.files]); e.target.value = ''; };
$('refresh').onclick = () => { books(); info(); };
info(); books();
</script></html>"""


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
                return self._json({"port": port, "df": df, "busy": busy})
            except Exception as e:  # noqa: BLE001
                return self._json({"port": DEV.port, "df": "", "busy": False, "error": str(e)})
        if u.path == "/api/books":
            try:
                items = [x for x in DEV.list_dir("/books") if not x["dir"]]
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
            ext = os.path.splitext(name)[1].lower()
            if not name or ext not in BOOK_EXTS:
                return self._json({"msg": "只支持 TXT / EPUB"}, 400)
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
            threading.Thread(target=worker, args=(tid, tmp, "/books/" + name), daemon=True).start()
            return self._json({"id": tid}, 202)
        if u.path == "/api/delete":
            path = unquote(parse_qs(u.query).get("path", [""])[0] or "")
            if not path.startswith("/books/") or ".." in path:
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
