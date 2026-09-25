#!/usr/bin/env python3
"""LAMP 01 live viewer: a local web page showing the real test lamp.

Bridges the lamp's USB console (COM14) to a browser: a 3D lamp that glows
with the real LEVEL, radio packets flying in as MAO's commands arrive, the
ODD BUS counters, and buttons that drive the lamp's own dev console.

    python tools/lamp_view.py COM14
    -> http://localhost:8123

Requires pyserial. DTR/RTS are held inactive so the board is not reset.
Close any other serial monitor on the port first (one open handle only).
The lamp is polled with "lamp status" every 2 s; changes stream to the
page over SSE. Buttons send ordinary "lamp ..." console commands, so what
you click is a *local* change on the device - MAO gets the NOTIFY, exactly
like a person flipping a real lamp.
"""
import argparse
import json
import queue
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import serial

HTML = Path(__file__).with_name("lamp_view.html")
POLL_S = 2.0

RE_STATE = re.compile(r"LAMP 01 power=(ON|OFF) level=(\d+) \(([^)]*)\)")
RE_STATUS = re.compile(
    r"status: (ONLINE|OFFLINE) power=(ON|OFF) level=(\d+) applied=(\d+) dup=(\d+) stale=(\d+)"
    r" odd_rx=(\d+) odd_tx=(\d+) set_rx=(\d+) discover_rx=(\d+)"
    r" acks_dropped=(\d+) drop_pending=(\d+) ack_delay=(\d+)ms")
RE_NOTE = re.compile(r"LAMP01: (test: .*|controller .* sequence restarted.*|simulating power loss.*|"
                     r"back: announcing|sent 4 invalid frames.*|LAMP 01 ready .*)")
ANSI = re.compile(r"\x1b\[[0-9;]*m")

s_clients: list[queue.Queue] = []
s_lock = threading.Lock()
s_port: serial.Serial | None = None
s_state = {"type": "status", "online": None, "power": None, "level": None, "counters": {}, "serial": False}


def broadcast(ev: dict) -> None:
    with s_lock:
        for q in list(s_clients):
            try:
                q.put_nowait(ev)
            except queue.Full:
                s_clients.remove(q)


def send_cmd(text: str) -> bool:
    try:
        if s_port and s_port.is_open:
            s_port.write((text.strip() + "\n").encode())
            return True
    except serial.SerialException:
        pass
    return False


def reader(portname: str) -> None:
    """Own the serial port: read lines, poll status, survive unplugs."""
    global s_port
    buf = b""
    next_poll = 0.0
    while True:
        if s_port is None or not s_port.is_open:
            try:
                p = serial.Serial()
                p.port = portname
                p.baudrate = 115200
                p.timeout = 0.1
                p.write_timeout = 1.0
                p.dtr = False   # never reset the board
                p.rts = False
                p.open()
                s_port = p
                s_state["serial"] = True
                broadcast({"type": "note", "text": f"serial {portname} open"})
            except serial.SerialException as e:
                if s_state["serial"] is not False:
                    s_state["serial"] = False
                    broadcast({"type": "note", "text": f"serial lost: {e}"})
                time.sleep(1.0)
                continue
        try:
            if time.time() >= next_poll:
                next_poll = time.time() + POLL_S
                send_cmd("lamp status")
            data = s_port.read(4096)
        except serial.SerialException:
            s_state["serial"] = False
            broadcast({"type": "note", "text": "serial lost (unplugged?)"})
            try:
                s_port.close()
            except serial.SerialException:
                pass
            time.sleep(1.0)
            continue
        buf += data
        while b"\n" in buf:
            raw, _, buf = buf.partition(b"\n")
            line = ANSI.sub("", raw.decode(errors="replace")).strip()
            if not line:
                continue
            m = RE_STATE.search(line)
            if m:
                ev = {"type": "state", "power": m.group(1) == "ON", "level": int(m.group(2)), "why": m.group(3)}
                s_state["power"], s_state["level"] = ev["power"], ev["level"]
                broadcast(ev)
                continue
            m = RE_STATUS.search(line)
            if m:
                keys = ("applied", "dup", "stale", "odd_rx", "odd_tx", "set_rx", "discover_rx",
                        "acks_dropped", "drop_pending", "ack_delay_ms")
                s_state.update(online=m.group(1) == "ONLINE", power=m.group(2) == "ON", level=int(m.group(3)),
                               counters={k: int(m.group(i + 4)) for i, k in enumerate(keys)})
                broadcast(dict(s_state))
                continue
            m = RE_NOTE.search(line)
            if m:
                broadcast({"type": "note", "text": m.group(1)})


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):   # quiet
        pass

    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._send(200, HTML.read_bytes(), "text/html; charset=utf-8")
        elif self.path == "/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            q: queue.Queue = queue.Queue(maxsize=256)
            with s_lock:
                s_clients.append(q)
            q.put(dict(s_state))   # snapshot first
            try:
                while True:
                    try:
                        ev = q.get(timeout=15.0)
                        self.wfile.write(f"data: {json.dumps(ev)}\n\n".encode())
                    except queue.Empty:
                        self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionError, OSError):
                pass
            finally:
                with s_lock:
                    if q in s_clients:
                        s_clients.remove(q)
        else:
            self._send(404, b"not found", "text/plain")

    def do_POST(self):
        if self.path != "/cmd":
            self._send(404, b"not found", "text/plain")
            return
        n = int(self.headers.get("Content-Length", 0))
        cmd = self.rfile.read(n).decode(errors="replace").strip()
        # Only the lamp's own console vocabulary leaves this machine.
        ok = bool(re.fullmatch(
            r"lamp (status|offline|online|power [01]|level \d{1,3}|junk|reboot|"
            r"drop_ack \d{1,2}|delay_ack \d{1,4}|flood \d{1,3} \d{1,3}( (state|announce))?)", cmd))
        if ok:
            ok = send_cmd(cmd)
            broadcast({"type": "sent", "text": cmd})
        self._send(200 if ok else 400, b"ok" if ok else b"rejected", "text/plain")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", default="COM14", help="lamp serial port (default COM14)")
    ap.add_argument("--http", type=int, default=8123, help="web port (default 8123)")
    args = ap.parse_args()

    threading.Thread(target=reader, args=(args.port,), daemon=True).start()
    srv = ThreadingHTTPServer(("127.0.0.1", args.http), Handler)
    print(f"LAMP 01 viewer: http://localhost:{args.http}  (lamp on {args.port}, Ctrl+C to stop)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
