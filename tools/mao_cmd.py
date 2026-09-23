#!/usr/bin/env python3
"""Send a MAO development-console command without resetting the board.

    python tools/mao_cmd.py COM13 reset-first-boot
    python tools/mao_cmd.py COM13 stress 20 --listen 25

Requires pyserial. DTR/RTS are held inactive so the ESP32-C3 is not reset.
"""
import argparse
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("command", nargs="+")
    ap.add_argument("--listen", type=float, default=2.0, help="seconds to print output afterwards")
    args = ap.parse_args()

    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = 0.1
    port.write_timeout = 2.0   # never hang if the firmware is not reading
    port.dtr = False
    port.rts = False
    port.open()
    try:
        port.write(("mao " + " ".join(args.command) + "\n").encode())
        port.flush()
        end = time.time() + args.listen
        while time.time() < end:
            data = port.read(4096)
            if data:
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()
    finally:
        port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
