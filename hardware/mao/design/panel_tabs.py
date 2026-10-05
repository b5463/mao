"""Breakaway-tab spots on the rim (KiCad python, read-only). ODD JOBS 144, 145.

A panelised order needs two tabs on the round edge. For every whole degree round the rim this measures the room
from the rim point to the nearest copper (pads, tracks, vias, touch arcs), part courtyard and fastener, on any
layer, skipping the arcs where a tab can never go (touch electrodes at 3 and 9 o'clock, the tail slot, USB-C and
the IR LEDs at 12, the antenna notch at 6). The two roomiest spots at least 120 degrees apart are written to
outputs/PANEL-TABS.json, with the text fab.py puts in FAB-NOTES.txt.
"""
import json
import math
import os

import pcbnew as pcb

import mechanical as m
from board import ROOT, TARGET, courtyard_boxes

NEVER = [(0, 30), (330, 360), (60, 120), (150, 210), (240, 300)]   # 12 (USB-C, IR), 3, 6 (antenna), 9 o'clock


def seg_dist(p, a, b):
    ax, ay = a; bx, by = b; dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    u = 0 if L2 == 0 else max(0, min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / L2))
    return math.hypot(p[0] - ax - u * dx, p[1] - ay - u * dy)


def box_dist(p, q):
    return math.hypot(max(0, q[0] - p[0], p[0] - q[2]), max(0, q[1] - p[1], p[1] - q[3]))


def main():
    b = pcb.LoadBoard(str(TARGET))
    mm = lambda v: pcb.ToMM(v) - 50
    boxes, segs, circles = [], [], []
    for f in b.GetFootprints():
        if f.GetReference().startswith('H'):
            continue
        boxes += courtyard_boxes(f)
        for p in f.Pads():
            r = p.GetBoundingBox()
            boxes.append((mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom())))
    for t in b.GetTracks():
        if isinstance(t, pcb.PCB_VIA):
            circles.append(((mm(t.GetPosition().x), mm(t.GetPosition().y)), pcb.ToMM(t.GetWidth(pcb.F_Cu)) / 2))
        else:
            segs.append(((mm(t.GetStart().x), mm(t.GetStart().y)), (mm(t.GetEnd().x), mm(t.GetEnd().y)),
                         pcb.ToMM(t.GetWidth()) / 2))
    for a in m.SCREW_ANGLES + (m.PEG_ANGLE,):
        circles.append((m.polar(m.MOUNT_R, a), m.MOUNT_KEEPOUT_D / 2))
    room = {}
    for deg in range(360):
        if any(lo <= deg < hi for lo, hi in NEVER):
            continue
        p = m.polar(m.PCB_R, deg)
        d = min([box_dist(p, q) for q in boxes] + [seg_dist(p, a, c) - w for a, c, w in segs] +
                [math.hypot(p[0] - c[0], p[1] - c[1]) - r for c, r in circles])
        room[deg] = round(d, 2)
    best = sorted(room, key=lambda k: -room[k])
    a = best[0]
    b2 = next(k for k in best if min(abs(k - a), 360 - abs(k - a)) >= 120)
    clock = lambda deg: '%d o\'clock' % (round(deg / 30) % 12 or 12)
    pair = sorted((a, b2))
    text = ' and '.join('about %s (%d deg clockwise from the USB-C, %.1f mm clear)' % (clock(k), k, room[k]) for k in pair)
    (ROOT / 'outputs' / 'PANEL-TABS.json').write_text(json.dumps({'tabs_deg': pair, 'room_mm': {str(k): room[k] for k in pair},
                                                                  'text': text, 'room_by_degree': room}, indent=1) + '\n')
    print('panel tabs:', text, flush=True)


if __name__ == '__main__':
    main()
    os._exit(0)
