#!/usr/bin/env python3
"""Capture MAO's screen over the development console and save it as PNG.

    python tools/mao_snap.py COM13 out.png [--before "anim fast" --delay 0.6] [--scale 2]

--before sends a dev command first (e.g. a view or animation preview) and waits
--delay seconds, so a specific moment can be captured. The area outside the
round 240x240 panel is drawn dark grey to show the physical boundary.

The firmware streams the areas LVGL flushes during one forced full refresh
(no extra RAM on the device), each as run-length-encoded RGB565 in base64:
    MAO_SNAP_BEGIN w h rle565-areas / A x1 y1 x2 y2 / S<b64>... / MAO_SNAP_END
Requires pyserial; firmware built with CONFIG_MAO_DEV_CONSOLE.
"""
import argparse
import base64
import struct
import sys
import time
import zlib

import serial


def request(port: serial.Serial, timeout: float = 5.0) -> bytes:
    port.write(b"mao snap\n")
    buf = b""
    end = time.time() + timeout
    while time.time() < end and b"MAO_SNAP_END" not in buf:
        buf += port.read(65536)
    return buf


def decode(raw: bytes):
    """Return (w, h, rgb565 bytes) or None."""
    text = raw.decode(errors="replace").replace("\r", "")
    if "MAO_SNAP_BEGIN" not in text or "MAO_SNAP_END" not in text:
        return None
    body = text.split("MAO_SNAP_BEGIN", 1)[1]
    header, body = body.split("\n", 1)
    w, h = (int(v) for v in header.split()[:2])
    body = body.split("MAO_SNAP_END", 1)[0]
    fb = bytearray(w * h * 2)
    area, packed = None, b""

    def flush():
        if not area:
            return
        x1, y1, x2, y2 = area
        aw = x2 - x1 + 1
        px = bytearray()
        for i in range(0, len(packed) - 3, 4):
            px += bytes(packed[i + 2:i + 4]) * (packed[i] | packed[i + 1] << 8)
        for k in range(len(px) // 2):
            x, y = x1 + k % aw, y1 + k // aw
            if 0 <= x < w and 0 <= y < h:
                fb[(y * w + x) * 2:(y * w + x) * 2 + 2] = px[k * 2:k * 2 + 2]

    for line in body.split("\n"):
        if line.startswith("A "):
            flush()
            area = tuple(int(v) for v in line.split()[1:5])
            packed = b""
        elif line.startswith("S") and area:
            try:
                packed += base64.b64decode(line[1:])
            except Exception:
                pass   # a log line interleaved; skip
    flush()
    return w, h, bytes(fb)


def to_rgb(w: int, h: int, fb: bytes) -> bytearray:
    cx, cy, r2 = (w - 1) / 2, (h - 1) / 2, (w / 2) ** 2
    rgb = bytearray()
    for y in range(h):
        for x in range(w):
            if (x - cx) ** 2 + (y - cy) ** 2 > r2:
                rgb += b"\x28\x28\x28"          # outside the round panel
                continue
            v = fb[(y * w + x) * 2] | (fb[(y * w + x) * 2 + 1] << 8)
            rgb += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    return rgb


def png_write(path: str, w: int, h: int, rgb: bytes) -> None:
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def open_port(name: str) -> serial.Serial:
    s = serial.Serial()
    s.port, s.baudrate, s.timeout, s.write_timeout = name, 115200, 0.02, 2.0
    s.dtr = False
    s.rts = False
    s.open()
    s.reset_input_buffer()
    return s


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("--before", default=None, help="dev command to send first, e.g. 'view menu'")
    ap.add_argument("--delay", type=float, default=0.5)
    ap.add_argument("--scale", type=int, default=1)
    args = ap.parse_args()

    s = open_port(args.port)
    try:
        if args.before:
            s.write(("mao " + args.before + "\n").encode())
            time.sleep(args.delay)
        snap = decode(request(s))
    finally:
        s.close()
    if not snap:
        print("no snapshot received", file=sys.stderr)
        return 1
    w, h, fb = snap
    rgb = to_rgb(w, h, fb)
    if args.scale > 1:
        k = args.scale
        big = bytearray()
        for y in range(h):
            row = b"".join(bytes(rgb[(y * w + x) * 3:(y * w + x) * 3 + 3]) * k for x in range(w))
            big += row * k
        rgb, w, h = big, w * k, h * k
    png_write(args.out, w, h, bytes(rgb))
    print(f"saved {args.out} ({w}x{h})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
