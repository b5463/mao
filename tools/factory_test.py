#!/usr/bin/env python3
"""MAO factory / board self-test, host side (fixture station).

Runs the firmware's self-test over the USB console, relays operator prompts,
and reports a verdict plus a JSON record per board.

    python tools/factory_test.py COM13                    # start "mao selftest", operator at the bench
    python tools/factory_test.py COM13 --auto             # automatic steps only (no operator)
    python tools/factory_test.py COM13 --reset --wait-boot   # factory build: reset, the test runs at boot
    python tools/factory_test.py /dev/ttyACM0 --serial MAO-A1-0042 --record results/
    python tools/factory_test.py COM13 --dmm "python fixture/dmm.py --pad {pad}"   # DMM on the probe pads

Firmware: any dev build ("mao selftest" on the console) or the factory
profile (sdkconfig.factory: the test starts by itself two seconds after
boot). Procedure, fixture and limits: docs/hardware/mao-factory-test.md.

Probe pads (A1: the display logic rail and the IR receiver supply, provisionally
TP13 / TP15 as on the A0 until the A1 board names them): the firmware
switches each rail off and on and asks for a reading ("SELFTEST MEASURE").
With --dmm the given command is run per reading ({pad}, {net}, {state} are
filled in; it must print the voltage in volts) and the result is sent back;
without it the operator measures to GND (TP1/TP2) and types the volts.
--no-pads skips them (the verdict is then INCOMPLETE).

Exit status: 0 PASS, 1 FAIL or INCOMPLETE, 2 no result (port, timeout).
Requires pyserial.
"""
import argparse
import datetime
import json
import os
import queue
import re
import sys
import threading
import time

import serial

STEP_RE = re.compile(r"SELFTEST STEP (\d+)/(\d+) (\S+) (PASS|FAIL|SKIP) ?(.*)$")
PROMPT_RE = re.compile(r"SELFTEST PROMPT (\S+) (confirm|action) (.*)$")
MEASURE_RE = re.compile(r"SELFTEST MEASURE (\S+) (\S+) (\S+) (on|off) (\d+) (\d+) ?(.*)$")
FLOAT_RE = re.compile(r"[-+]?\d+(?:\.\d+)?")
VERDICT_RE = re.compile(r"SELFTEST (PASS|FAIL|INCOMPLETE) (\d+)/(\d+)(.*)$")
BEGIN_RE = re.compile(r"SELFTEST BEGIN (.*)$")
JSON_PREFIX = "SELFTEST_JSON "
ERROR_RE = re.compile(r"SELFTEST ERROR (.*)$")
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


class Console:
    """Line reader on a background thread; writes from the caller."""

    def __init__(self, port, log_path=None):
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.1
        self.ser.write_timeout = 2.0
        self.ser.dtr = False      # USB-Serial/JTAG: keep the chip out of reset
        self.ser.rts = False
        self.ser.open()
        self.lines = queue.Queue()
        self.log = open(log_path, "a", encoding="utf-8") if log_path else None
        self._stop = False
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self):
        buf = b""
        while not self._stop:
            try:
                data = self.ser.read(1024)
            except Exception:                 # port gone, or closed under us at exit
                if not self._stop:
                    self.lines.put(None)
                return
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = ANSI_RE.sub("", raw.decode(errors="replace")).rstrip("\r")
                if self.log:
                    self.log.write(line + "\n")
                    self.log.flush()
                self.lines.put(line)

    def send(self, text):
        self.ser.write((text + "\n").encode())
        self.ser.flush()

    def reset(self):
        # USB-Serial/JTAG reset as esptool does it: RTS pulse with DTR low.
        # VERIFY AT BRING-UP on the A1 (GPIO0 = BOOT pad only, so the press can no longer start the ROM loader).
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False

    def close(self):
        self._stop = True
        try:
            self.ser.close()
        finally:
            if self.log:
                self.log.close()


def ask(question):
    """Operator answer: y / n / s (skip). Empty input repeats the question."""
    while True:
        try:
            ans = input(f"    >>> {question} [y/n/s] ").strip().lower()
        except EOFError:
            return "no"
        if ans in ("y", "yes"):
            return "yes"
        if ans in ("n", "no"):
            return "no"
        if ans in ("s", "skip"):
            return "skip"


def read_dmm(command, pad, net, state):
    """Run the fixture's DMM command; it prints the voltage in volts."""
    import shlex
    import subprocess
    cmd = command.format(pad=pad, net=net, state=state)
    try:
        out = subprocess.run(shlex.split(cmd), capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError) as exc:
        print(f"    DMM command failed: {exc}")
        return None
    m = FLOAT_RE.search(out)
    return float(m.group(0)) if m else None


def ask_volts(text):
    """Operator reading in volts; None = skip."""
    while True:
        try:
            ans = input(f"    >>> {text}\n        volts (or s to skip): ").strip().lower()
        except EOFError:
            return None
        if ans in ("s", "skip"):
            return None
        try:
            return float(ans.replace(",", "."))
        except ValueError:
            continue


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port of the MAO USB console (VID:PID 303A:1001)")
    ap.add_argument("--auto", action="store_true", help="automatic steps only, no operator")
    ap.add_argument("--reset", action="store_true", help="reset the board first")
    ap.add_argument("--wait-boot", action="store_true",
                    help="do not send the command: wait for the factory build to start the test at boot")
    ap.add_argument("--yes", action="store_true", help="answer every confirm prompt 'yes' (unattended runs)")
    ap.add_argument("--dmm", default="", help="command that prints the voltage (V) at a probe pad; "
                    "{pad}, {net}, {state} are substituted")
    ap.add_argument("--no-pads", action="store_true", help="skip the probe-pad measurements (switched rails)")
    ap.add_argument("--timeout", type=float, default=600.0, help="overall limit in seconds (default 600)")
    ap.add_argument("--serial", default="", help="board serial / label to put in the record")
    ap.add_argument("--record", default="", help="directory for a JSON record per board")
    ap.add_argument("--log", default="", help="append the raw console output to this file")
    args = ap.parse_args()

    try:
        con = Console(args.port, args.log or None)
    except serial.SerialException as exc:
        print(f"cannot open {args.port}: {exc}")
        return 2

    steps = []
    verdict = None
    device_json = None
    answered = set()
    pending_confirm = None
    pending_measure = None
    measurements = []
    deadline = time.time() + args.timeout
    try:
        if args.reset:
            print("resetting the board ...")
            con.reset()
            time.sleep(1.5)
        no_pads = args.no_pads or (args.auto and not args.dmm)
        if not args.wait_boot:
            time.sleep(0.3)
            con.send("mao selftest " + ("auto" if args.auto else "run") + (" nopads" if no_pads else ""))
        print(f"MAO self-test on {args.port}{' (automatic steps only)' if args.auto else ''}")

        while time.time() < deadline:
            try:
                line = con.lines.get(timeout=0.2)
            except queue.Empty:
                line = ""
            if line is None:
                print("serial port lost")
                return 2

            if pending_confirm and line == "":
                # Ask the operator only while nothing else is arriving.
                step_id, text = pending_confirm
                pending_confirm = None
                if step_id in answered:
                    continue
                ans = "yes" if args.yes else ask(text)
                if step_id not in answered:          # a face press may have answered already
                    con.send("mao selftest " + ans)
                continue
            if pending_measure and line == "":
                step_id, pad, net, state, lo, hi, text = pending_measure
                pending_measure = None
                if step_id in answered:
                    continue
                if args.dmm:
                    volts = read_dmm(args.dmm, pad, net, state)
                elif no_pads or args.yes:
                    volts = None
                else:
                    volts = ask_volts(text)
                if volts is None:
                    con.send("mao selftest skip")
                    continue
                mv = int(round(volts * 1000))
                verdict_pad = "ok" if lo <= mv <= hi else "OUT OF LIMITS"
                print(f"    {pad} {net} {state}: {mv} mV ({lo}..{hi}) {verdict_pad}")
                measurements.append({"step": step_id, "pad": pad, "net": net, "state": state, "mv": mv,
                                     "min": lo, "max": hi})
                con.send(f"mao selftest mv {mv}")
                continue

            if not line:
                continue
            m = BEGIN_RE.search(line)
            if m:
                print(f"  begin: {m.group(1)}")
                continue
            m = STEP_RE.search(line)
            if m:
                n, total, sid, res, detail = m.groups()
                steps.append({"n": int(n), "id": sid, "r": res, "d": detail})
                answered.add(sid)
                mark = {"PASS": "ok  ", "FAIL": "FAIL", "SKIP": "skip"}[res]
                print(f"  [{mark}] {int(n):2d}/{total} {sid:<15} {detail}")
                continue
            m = MEASURE_RE.search(line)
            if m:
                sid, pad, net, state, lo, hi, text = m.groups()
                answered.discard(sid)
                print(f"    ~   {text}")
                pending_measure = (sid, pad, net, state, int(lo), int(hi), text)
                continue
            m = PROMPT_RE.search(line)
            if m:
                sid, kind, text = m.groups()
                answered.discard(sid)
                if kind == "confirm":
                    print(f"    ?   {text}")
                    pending_confirm = (sid, text)
                else:
                    print(f"    >>> {text}")
                continue
            if line.startswith(JSON_PREFIX) or JSON_PREFIX in line:
                try:
                    device_json = json.loads(line.split(JSON_PREFIX, 1)[1])
                except (ValueError, IndexError):
                    device_json = None
                if verdict:
                    break
                continue
            m = VERDICT_RE.search(line)
            if m:
                verdict = m.group(1)
                print(f"\n  RESULT: {verdict} {m.group(2)}/{m.group(3)}{m.group(4)}")
                continue
            m = ERROR_RE.search(line)
            if m:
                print(f"self-test could not start: {m.group(1)}")
                return 2
            if "BOOTCHECK" in line or "[!!]" in line:
                print(f"  {line.strip()}")
        else:
            print("timed out waiting for the self-test")
            if not verdict:
                return 2

        # The JSON line follows the verdict; give it a moment.
        end = time.time() + 2.0
        while device_json is None and time.time() < end:
            try:
                line = con.lines.get(timeout=0.2)
            except queue.Empty:
                continue
            if line and JSON_PREFIX in line:
                try:
                    device_json = json.loads(line.split(JSON_PREFIX, 1)[1])
                except ValueError:
                    pass
    finally:
        con.close()

    record = {
        "time": datetime.datetime.now().isoformat(timespec="seconds"),
        "port": args.port,
        "serial": args.serial,
        "verdict": verdict,
        "steps": steps,
        "pads": measurements,
        "device": device_json,
    }
    if args.record:
        os.makedirs(args.record, exist_ok=True)
        mac = (device_json or {}).get("mac", "unknown").replace(":", "")
        name = f"{args.serial or mac}-{datetime.datetime.now():%Y%m%d-%H%M%S}.json"
        path = os.path.join(args.record, name)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(record, f, indent=2)
        print(f"  record: {path}")

    failed = [s["id"] for s in steps if s["r"] == "FAIL"]
    skipped = [s["id"] for s in steps if s["r"] == "SKIP"]
    if failed:
        print(f"  failed: {', '.join(failed)}")
    if skipped:
        print(f"  skipped: {', '.join(skipped)}")
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
