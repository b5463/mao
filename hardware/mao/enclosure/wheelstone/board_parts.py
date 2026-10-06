"""Dump the A1 board's part bodies for the wheel-stone clash check (KiCad python, read-only).

Each part: side, body boxes in board mm (centre at the dial axis, +y towards 6 o'clock) and its height above its
board face, both from mech_check.py (Fab outline / courtyard, 3D-model height). Parts without a body (pads, holes,
fiducials, test pads) are skipped, as in mech_check. Writes board_parts.json next to this file.

    "C:/Program Files/KiCad/10.0/bin/python.exe" board_parts.py
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'design'))

import pcbnew as pcb  # noqa: E402

import mech_check as mc  # noqa: E402
from board import TARGET  # noqa: E402


def main():
    b = pcb.LoadBoard(str(TARGET))
    parts = []
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith(mc.NO_BODY):
            continue
        h = mc.height(fp)
        if h is None:
            continue
        parts.append({'ref': ref, 'side': 'B' if fp.IsFlipped() else 'F', 'height': round(h, 3),
                      'boxes': [[round(v, 3) for v in bx] for bx in mc.body_box(fp)]})
    json.dump({'board': os.path.basename(str(TARGET)), 'parts': parts}, open(os.path.join(HERE, 'board_parts.json'), 'w'),
              indent=1)
    print(len(parts), 'parts')


if __name__ == '__main__':
    main()
