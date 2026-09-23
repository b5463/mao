#!/usr/bin/env python3
"""Scripted MAO interaction smoke test over the development console.

Injects synthetic input events (the same events the knob produces) to walk
every M1 view, gesture and dial speed class several times, records the serial
log, and prints a summary of views, character states, dial classes, frame
performance and any errors.

    python tools/mao_smoke.py COM13 --cycles 5 --log smoke.log

Requires pyserial and firmware built with CONFIG_MAO_DEV_CONSOLE. The board
is not reset (DTR/RTS held inactive).
"""
import argparse
import re
import sys
import time

import serial

ANSI = re.compile(rb"\x1b\[[0-9;]*m")


class Mao:
    def __init__(self, port: str):
        self.port = serial.Serial()
        self.port.port = port
        self.port.baudrate = 115200
        self.port.timeout = 0.02
        self.port.write_timeout = 2.0
        self.port.dtr = False
        self.port.rts = False
        self.port.open()
        self.log = bytearray()

    def pump(self, seconds: float) -> None:
        end = time.time() + seconds
        while True:
            self.log += self.port.read(4096)
            if time.time() >= end:
                break

    def cmd(self, text: str, wait: float = 0.0) -> None:
        self.port.write(("mao " + text + "\n").encode())
        self.pump(wait)

    def key(self, name: str, count: int = 1, wait: float = 0.0) -> None:
        self.cmd(f"key {name} {count}", wait)


# The dev console is polled every 50 ms; keep >= 80 ms between commands and
# express dial speed through the detent count per event instead of rate.
GAP = 0.08


def cycle(m: Mao) -> None:
    # HOME gestures
    m.key("press", wait=0.12); m.key("release", wait=GAP); m.key("click", wait=0.8)
    m.key("press", wait=0.62); m.key("long", wait=0.1); m.key("release", wait=1.2)
    # HOME dial classes (detents/s): slow ~3, normal ~12, fast ~37, very fast ~75, reversing
    for _ in range(5):
        m.key("cw", 1, wait=0.30)
    for _ in range(12):
        m.key("cw", 1, wait=GAP)
    for _ in range(12):
        m.key("ccw", 3, wait=GAP)
    for _ in range(12):
        m.key("cw", 6, wait=GAP)
    for i in range(10):
        m.key("cw" if i % 2 else "ccw", 1, wait=GAP)
    m.pump(2.2)
    # Menu shell
    m.key("click", wait=GAP); m.key("double", wait=0.7)
    for _ in range(3):
        m.key("cw", 1, wait=0.18)
    m.key("cw", 4, wait=0.4)               # push past the end
    for _ in range(3):
        m.key("ccw", 1, wait=GAP)          # fast back
    m.key("ccw", 3, wait=0.4)              # past the start
    m.key("cw", 2, wait=0.3)
    m.key("click", wait=0.7)               # open placeholder
    m.key("click", wait=0.6)               # back to menu
    m.key("cw", 1, wait=0.3)
    m.key("click", wait=0.7)
    m.key("long", wait=1.0)                # home from placeholder
    m.key("double", wait=0.7)
    m.key("long", wait=1.0)                # home from menu


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--cycles", type=int, default=3)
    ap.add_argument("--log", default=None)
    args = ap.parse_args()
    sys.stdout.reconfigure(errors="replace")

    m = Mao(args.port)
    try:
        m.cmd("status", 1.0)
        for i in range(args.cycles):
            cycle(m)
        m.pump(11.0)                       # let the last perf window close
        m.cmd("status", 1.0)
    finally:
        m.port.close()

    text = ANSI.sub(b"", bytes(m.log)).decode(errors="replace").replace("\r", "")
    if args.log:
        with open(args.log, "w", encoding="utf-8") as f:
            f.write(text)

    def count(pattern: str) -> dict:
        out: dict = {}
        for hit in re.findall(pattern, text):
            out[hit] = out.get(hit, 0) + 1
        return out

    print("views:     ", count(r"view (\w+ -> \w+)"))
    print("opened:    ", count(r"MAO_APP: open (\w+)"))
    print("character: ", count(r"MAO_CHARACTER: \w+ -> (\w+)"))
    print("dial:      ", count(r"dial (\w+(?: \+REVERSING)?)"))
    for line in re.findall(r"MAO_DISPLAY: (perf .*)", text):
        print("  ", line)
    for line in re.findall(r"MAO_APP: (status: .*)", text):
        print("  ", line)
    lost = [l for l in text.splitlines() if "dev: unknown command" in l]
    problems = [l for l in text.splitlines()
                if (re.match(r"^[EW] \(", l) and "dev: unknown command" not in l)
                or re.search(r"Guru|panic|abort\(|assert|rst:0x", l)]
    print("dev commands garbled in transit:", len(lost))
    print("problems:  ", len(problems))
    for p in problems[:20]:
        print("  ", p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
