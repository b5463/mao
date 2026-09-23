#!/usr/bin/env python3
"""MAO M2 end-to-end ODD BUS test: MAO + LAMP 01, both on USB.

Drives MAO with synthetic knob events and LAMP 01 with its dev console, then
checks each M2 behaviour from both devices' logs:

  discover -> capabilities -> state -> set LEVEL -> ACK -> fast spin
  (coalescing) -> POWER toggle -> lamp-side change notification -> invalid
  frames rejected -> lamp offline -> lamp back (no duplicate) -> latency

    python tools/odd_m2_test.py --mao COM13 --lamp COM14

Requires pyserial and dev-console builds on both boards. Boards are not reset.
"""
import argparse
import re
import sys
import threading
import time

import serial

ANSI = re.compile(r"\x1b\[[0-9;]*m")


class Port:
    def __init__(self, name: str, prefix: str):
        self.prefix = prefix
        self.s = serial.Serial()
        self.s.port = name
        self.s.baudrate = 115200
        self.s.timeout = 0.05
        self.s.write_timeout = 2.0
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        self.lines: list[str] = []
        self.lock = threading.Lock()
        self.buf = ""
        self.alive = True
        self.t = threading.Thread(target=self._reader, daemon=True)
        self.t.start()

    def _reader(self):
        while self.alive:
            data = self.s.read(4096)
            if not data:
                continue
            self.buf += ANSI.sub("", data.decode(errors="replace")).replace("\r", "")
            *done, self.buf = self.buf.split("\n")
            with self.lock:
                self.lines.extend(done)

    def mark(self) -> int:
        with self.lock:
            return len(self.lines)

    def since(self, mark: int) -> list[str]:
        with self.lock:
            return self.lines[mark:]

    def send(self, text: str, wait: float = 0.1):
        self.s.write((self.prefix + text + "\n").encode())
        time.sleep(wait)

    def wait_for(self, pattern: str, mark: int, timeout: float):
        rx = re.compile(pattern)
        end = time.time() + timeout
        while time.time() < end:
            for line in self.since(mark):
                m = rx.search(line)
                if m:
                    return m
            time.sleep(0.05)
        return None

    def close(self):
        self.alive = False
        self.t.join(timeout=1)
        self.s.close()


results: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail else ""))
    return ok


def lamp_level(lamp: Port, mark: int):
    hits = [re.search(r"LAMP 01 power=(ON|OFF) level=(\d+)", l) for l in lamp.since(mark)]
    hits = [h for h in hits if h]
    return (hits[-1].group(1), int(hits[-1].group(2))) if hits else (None, None)


def mao_status(mao: Port) -> dict:
    m = mao.mark()
    mao.send("status", 1.2)
    text = "\n".join(mao.since(m))
    out = {}
    for key in ("online", "cmd", "ack", "stale", "retry", "timeout", "coalesced", "inbox_drop",
                "bad_ver", "bad_crc", "malformed", "not_odd", "tx", "rx", "tx_fail"):
        mm = re.findall(rf"\b{key}=(\d+)", text)
        if mm:
            out[key] = int(mm[-1])
    lat = re.search(r"rtt avg=([\d.]+)ms min=([\d.]+)ms max=([\d.]+)ms \(n=(\d+)\), input->ack avg=([\d.]+)ms max=([\d.]+)ms", text)
    if lat:
        out["rtt"] = tuple(float(x) for x in lat.groups())
    out["rows"] = re.findall(r"\[(\d)\] '([^']+)' (ONLINE|OFFLINE)[^\n]*", text)
    out["text"] = text
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mao", required=True)
    ap.add_argument("--lamp", required=True)
    args = ap.parse_args()
    sys.stdout.reconfigure(errors="replace")

    mao = Port(args.mao, "mao ")
    lamp = Port(args.lamp, "lamp ")
    try:
        lamp.send("online", 0.3)
        mao.send("odd-reset", 0.3)
        # Known start: HOME, then MENU -> DEVICES (entry 0).
        mao.send("key long", 1.2)
        mao.send("key double", 0.8)
        for _ in range(4):
            mao.send("key ccw 1", 0.12)
        m = mao.mark()
        t0 = time.time()
        mao.send("key click", 0.1)
        hit = mao.wait_for(r"(new device|is back online|device available).*LAMP 01|\[\d\] 'LAMP 01' ONLINE", m, 6)
        if not hit:   # already known and online before the test: confirm via status
            st = mao_status(mao)
            hit = any(n == "LAMP 01" and s == "ONLINE" for _, n, s in st["rows"])
        check("DEVICES discovers LAMP 01", bool(hit), f"{time.time() - t0:.2f}s after opening DEVICES")
        check("capabilities learned", bool(mao.wait_for(r"'LAMP 01': 2 capabilities|cap 2 LEVEL", 0, 3))
              or "LEVEL=" in mao_status(mao)["text"])

        # Open the device view.
        m_l = lamp.mark()
        mao.send("key click", 0.8)
        st = mao_status(mao)
        start_level = int(re.search(r"LEVEL=(\d+)", st["text"]).group(1)) if "LEVEL=" in st["text"] else None
        check("device state read", start_level is not None, f"LEVEL={start_level}")

        # Slow LEVEL control: 10 single detents.
        m_l = lamp.mark()
        for _ in range(10):
            mao.send("key cw 1", 0.12)
        time.sleep(0.5)
        _, lvl = lamp_level(lamp, m_l)
        expected = min(100, (start_level or 0) + 10)
        check("LEVEL +10 applied on lamp", lvl == expected, f"lamp level={lvl}, expected {expected}")
        st = mao_status(mao)
        check("ACKs received", st.get("ack", 0) >= 1 and st.get("timeout", 1) == 0,
              f"cmd={st.get('cmd')} ack={st.get('ack')} retry={st.get('retry')} timeout={st.get('timeout')}")

        # Fast spin: 40 events x 3 detents at ~12 Hz -> coalesced, final value confirmed.
        m_l = lamp.mark()
        before = mao_status(mao)
        for i in range(24):
            mao.send("key ccw 3" if i < 12 else "key cw 3", 0.08)
        time.sleep(0.6)
        after = mao_status(mao)
        _, lvl = lamp_level(lamp, m_l)
        mao_lvl = re.search(r"LEVEL=(\d+)(\*?)", after["text"])
        check("fast spin: final value identical on both", mao_lvl is not None and lvl == int(mao_lvl.group(1))
              and mao_lvl.group(2) == "", f"MAO={mao_lvl.group(0) if mao_lvl else None} lamp={lvl}")
        sent = after.get("cmd", 0) - before.get("cmd", 0)
        check("fast spin: radio not flooded", sent <= 24, f"{sent} SET_VALUE for 24 dial events (72 detents), "
              f"coalesced +{after.get('coalesced', 0) - before.get('coalesced', 0)}")

        # POWER toggle x2.
        m_l = lamp.mark()
        mao.send("key click", 0.6)
        p1, _ = lamp_level(lamp, m_l)
        m_l = lamp.mark()
        mao.send("key click", 0.6)
        p2, _ = lamp_level(lamp, m_l)
        check("POWER toggles", p1 is not None and p2 is not None and p1 != p2, f"{p1} -> {p2}")

        # Lamp-side change is pushed to MAO.
        lamp.send("level 17", 0.6)
        st = mao_status(mao)
        check("lamp notification updates MAO", "LEVEL=17" in st["text"])

        # Invalid frames are rejected, nothing crashes.
        before = mao_status(mao)
        lamp.send("junk", 0.8)
        after = mao_status(mao)
        rej = sum(after.get(k, 0) - before.get(k, 0) for k in ("bad_ver", "bad_crc", "malformed"))
        check("invalid frames rejected", rej == 3, f"v2/bad-CRC/bad-length rejected: {rej} of 3 "
              "(the non-ODD frame is dropped before decoding)")

        # Offline and return.
        m = mao.mark()
        t0 = time.time()
        lamp.send("offline", 0.1)
        hit = mao.wait_for(r"'LAMP 01' offline", m, 9)
        check("offline detected", bool(hit), f"{time.time() - t0:.1f}s")
        mao.send("key cw 3", 0.5)   # dial while offline: must be ignored gracefully
        m = mao.mark()
        t0 = time.time()
        lamp.send("online", 0.1)
        hit = mao.wait_for(r"'LAMP 01' is back online", m, 5)
        check("return detected", bool(hit), f"{time.time() - t0:.2f}s")
        st = mao_status(mao)
        n = sum(1 for _, name, _ in st["rows"] if name == "LAMP 01")
        check("no duplicate after return", n == 1, f"{n} registry entries for LAMP 01")

        # Latency summary.
        if "rtt" in st:
            avg, mn, mx, cnt, ia, imx = st["rtt"]
            print(f"latency: command->ACK avg {avg:.2f} ms, min {mn:.2f}, max {mx:.2f} (n={int(cnt)}); "
                  f"input->ACK avg {ia:.2f} ms, max {imx:.2f}")
        print(f"totals: cmd={st.get('cmd')} ack={st.get('ack')} stale={st.get('stale')} retry={st.get('retry')} "
              f"timeout={st.get('timeout')} tx={st.get('tx')} rx={st.get('rx')} tx_fail={st.get('tx_fail')} "
              f"inbox_drop={st.get('inbox_drop')}")
        mao.send("key long", 0.8)
        mao.send("key long", 0.8)
    finally:
        mao.close()
        lamp.close()

    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
