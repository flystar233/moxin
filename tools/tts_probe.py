#!/usr/bin/env python3
"""在电脑上直接试 qwen3-tts-flash：确认 API Key、音色和返回格式对不对。

用法：
    python tools/tts_probe.py sk-xxxxxxxx
    python tools/tts_probe.py sk-xxxxxxxx "要念的一句话" Cherry

不给 Key 就读环境变量 DASHSCOPE_API_KEY。用标准库，不用装 requests。
它会打印：首字节延迟、收到几段、每段多少字节、总共多少、估计时长，
并把音频存成 tts_probe.wav（24kHz 16bit 单声道；服务端给的是裸 PCM 时加头，
本来就是 WAV 就原样保存）。

固件假设：流式返回的 audio.data 是 24kHz、16bit、单声道的裸 PCM（base64 编码）。
这个脚本顺便把前 16 个字节打出来——如果开头是 RIFF，说明服务端给的是 WAV，
固件也能处理（会自动跳过文件头），但采样率要看打印出来的 fmt。
"""
import base64
import json
import os
import struct
import sys
import time
import urllib.error
import urllib.request

URL = "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation"


def wav_header(pcm_len: int, rate: int = 24000, channels: int = 1, bits: int = 16) -> bytes:
    byte_rate = rate * channels * bits // 8
    block = channels * bits // 8
    return (
        b"RIFF"
        + struct.pack("<I", 36 + pcm_len)
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 1, channels, rate, byte_rate, block, bits)
        + b"data"
        + struct.pack("<I", pcm_len)
    )


def parse_wav(d: bytes):
    """返回 (rate, channels, bits, pcm)；服务端给的 data 长度可能是假的，按实际字节算。"""
    if len(d) < 44 or d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        return None
    pos, rate, ch, bits, pcm = 12, 0, 0, 0, b""
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        sz = struct.unpack("<I", d[pos + 4:pos + 8])[0]
        if cid == b"fmt " and sz >= 16:
            _, ch, rate, _, _, bits = struct.unpack("<HHIIHH", d[pos + 8:pos + 24])
        elif cid == b"data":
            pcm = d[pos + 8:]
            break
        pos += 8 + sz + (sz & 1)
    return (rate, ch, bits, pcm) if pcm else None


def main() -> int:
    key = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("DASHSCOPE_API_KEY", "")
    if not key:
        print("用法：python tools/tts_probe.py sk-xxxx [文本] [音色]")
        print("（也可以先设环境变量 DASHSCOPE_API_KEY）")
        return 2
    text = sys.argv[2] if len(sys.argv) > 2 else "你好，这是语音朗读的试听。这以后每句话都会边下载边播放。"
    voice = sys.argv[3] if len(sys.argv) > 3 else "Cherry"

    body = json.dumps(
        {
            "model": "qwen3-tts-flash",
            "input": {"text": text, "voice": voice, "language_type": "Chinese"},
        },
        ensure_ascii=False,
    ).encode("utf-8")
    req = urllib.request.Request(
        URL,
        data=body,
        headers={
            "Authorization": "Bearer " + key,
            "Content-Type": "application/json",
            "X-DashScope-SSE": "enable",
        },
        method="POST",
    )
    print(f"模型 qwen3-tts-flash，音色 {voice}，文本 {len(text)} 字")
    t0 = time.time()
    try:
        resp = urllib.request.urlopen(req, timeout=60)
    except urllib.error.HTTPError as e:
        print("HTTP", e.code)
        print(e.read().decode("utf-8", "replace")[:2000])
        return 1
    except Exception as e:  # noqa: BLE001
        print("连不上：", e)
        return 1

    ttfb = None
    chunks = []
    raw = b""
    url = ""
    with resp:
        print("HTTP", resp.status, resp.headers.get("content-type", ""))
        for line in resp:
            raw += line[:4096]
            if not line.startswith(b"data:"):
                continue
            payload = line[5:].strip()
            try:
                j = json.loads(payload)
            except Exception:  # noqa: BLE001
                continue
            a = (j.get("output") or {}).get("audio") or {}
            d = a.get("data") or ""
            if d:
                pcm = base64.b64decode(d)
                if ttfb is None:
                    ttfb = time.time() - t0
                    print(f"首段音频：{ttfb:.2f} 秒，前 16 字节 {pcm[:16].hex()}")
                chunks.append(pcm)
            u = a.get("url") or ""
            if u:
                url = u
            if (j.get("output") or {}).get("finish_reason") == "stop":
                break

    pcm = b"".join(chunks)
    if not pcm and url:
        print("服务端返回的是音频网址，下载：", url)
        with urllib.request.urlopen(url, timeout=60) as r:
            pcm = r.read()
        print("下载", len(pcm), "字节")

    total_s = time.time() - t0
    print(f"共 {len(chunks)} 段，{len(pcm)} 字节，总耗时 {total_s:.2f} 秒")
    if not pcm:
        print("没收到音频。检查 API Key、余额、模型是否开通。")
        return 1
    if pcm[:4] == b"RIFF":
        w = parse_wav(pcm)
        if not w:
            print("是 WAV，但找不到 fmt / data 块")
            return 1
        rate, ch, bits, payload = w
        sec = len(payload) / (rate * ch * bits / 8)
        print(f"返回的是 WAV：{rate} Hz，{ch} 声道，{bits} bit，PCM {len(payload)} 字节（约 {sec:.2f} 秒）")
        print("注意：服务端 WAV 头里的 data 长度是假的（流式不知道总长），这里按实际字节重写头。")
        # 重写一个长度正确的头，不然有些播放器会拖到最后
        open("tts_probe.wav", "wb").write(wav_header(len(payload), rate, ch, bits) + payload)
    else:
        print(f"按 24kHz 16bit 单声道算，时长约 {len(pcm) / 48000:.2f} 秒")
        open("tts_probe.wav", "wb").write(wav_header(len(pcm)) + pcm)
    print("存到 tts_probe.wav，电脑上可以直接播来听。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
