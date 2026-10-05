"""Collision-checked local placement of support parts (KiCad 10 Python).

Adapted from the KINO D4 carrier's routing.prepare() 'near()' search: anchors (ICs, connectors, rim
parts) keep their designed positions; every support part listed in placement.AUTO is put at the
closest free spot to the pads it serves. The score is the distance from the part's pads to the
owner's pads on the same nets, so a decoupling capacitor ends up beside its supply pin with its
ground pad facing away (ODD JOBS 14/16), on a 0.25 mm grid, at 0 or 90 degrees (ODD JOBS 89/90).

Constraints: courtyard (+0.35 mm: a routing channel) clear of every courtyard on the same face, clear of plated and
unplated holes from either face, inside the outline by 0.4 mm, outside fastener and antenna keep-outs.
"""
import math

import pcbnew as pcb

import mechanical as m
from board import ORIGIN

GRID = 0.25
GAP = 0.35          # MAO: courtyard + 0.35 mm keeps a routing channel (one 0.2 mm track) beside every support part
RAILS = {'GND', '+3V3', 'VSYS', 'VBAT', 'VBUS', '3V3_LCD', 'MIC_VDD', 'IR_RX_VCC'}


def mm(v):
    return v / 1e6


class Occupancy:
    def __init__(self, board, keepouts, side_keepouts=None):
        self.boxes = {'F': [], 'B': []}
        self.holes = []
        self.keepouts = keepouts
        self.side_keepouts = side_keepouts or {}
        self.board = board

    def add_fp(self, fp):
        side = 'B' if fp.IsFlipped() else 'F'
        for b in courtyard_boxes(fp):
            self.boxes[side].append((fp.GetReference(), b))
        for p in fp.Pads():
            if p.GetAttribute() in (pcb.PAD_ATTRIB_PTH, pcb.PAD_ATTRIB_NPTH):
                bb = p.GetBoundingBox()
                self.holes.append((mm(bb.GetLeft()) - ORIGIN - 0.25, mm(bb.GetTop()) - ORIGIN - 0.25,
                                   mm(bb.GetRight()) - ORIGIN + 0.25, mm(bb.GetBottom()) - ORIGIN + 0.25))

    def free(self, side, boxes):
        for b in boxes:
            bb = (b[0] - GAP, b[1] - GAP, b[2] + GAP, b[3] + GAP)
            if any(hit(bb, o) for _, o in self.boxes[side]):
                return False
            if any(hit(bb, h) for h in self.holes):
                return False
            for x, y in ((bb[0], bb[1]), (bb[2], bb[1]), (bb[0], bb[3]), (bb[2], bb[3])):
                if not inside_board(x, y, 0.4):
                    return False
            if any(hit(bb, k) for k in self.keepouts + self.side_keepouts.get(side, [])):
                return False
        return True


def hit(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def inside_board(x, y, margin):
    if math.hypot(x, y) > m.PCB_R - margin:
        return False
    if abs(x) < m.NOTCH_W / 2 + margin and y > m.NOTCH_Y - margin:
        return False
    return True


def courtyard_boxes(fp):
    layer = pcb.B_CrtYd if fp.IsFlipped() else pcb.F_CrtYd
    poly = fp.GetCourtyard(layer)
    bb = poly.BBox() if poly.OutlineCount() else fp.GetBoundingBox(False, False)
    box = (mm(bb.GetLeft()) - ORIGIN, mm(bb.GetTop()) - ORIGIN, mm(bb.GetRight()) - ORIGIN, mm(bb.GetBottom()) - ORIGIN)
    if fp.GetReference().startswith('U2') and fp.GetValue().startswith('ESP32'):
        # the module courtyard includes Espressif's whole antenna keep-out; parts may sit beside
        # the module body (the keep-out itself is a board rule area)
        xs = [pad_xy(p)[0] for p in fp.Pads()]
        ys = [pad_xy(p)[1] for p in fp.Pads()]
        box = (min(xs) - 0.9, min(ys) - 0.9, max(xs) + 0.9, m.MODULE_CY + m.MODULE_L / 2)
    return [box]


def pad_xy(p):
    return (mm(p.GetPosition().x) - ORIGIN, mm(p.GetPosition().y) - ORIGIN)


def place(board, fps, auto, occ, ignore_nets=('GND',)):
    """auto: list of (ref, owner_ref, side, angles, radius_mm) in priority order."""
    report = []
    for ref, owner, side, angles, radius in auto:
        fp = fps[ref]
        own = fps[owner]
        nets = {p.GetNetname() for p in fp.Pads()} - set(ignore_nets) - {''}
        # aim at the signal the part serves; a rail pin is only the target when there is no signal
        signal = nets - RAILS
        use = signal or nets
        targets = [(p.GetNetname(), pad_xy(p)) for p in own.Pads() if p.GetNetname() in use]
        if targets:
            tx = sum(t[1][0] for t in targets) / len(targets)
            ty = sum(t[1][1] for t in targets) / len(targets)
        else:
            tx, ty = pad_xy(own.Pads()[0]) if not own.GetPosition() else (mm(own.GetPosition().x) - ORIGIN, mm(own.GetPosition().y) - ORIGIN)
        best = None
        steps = int(radius / GRID)
        offsets = sorted(((i * i + j * j, i, j) for i in range(-steps, steps + 1) for j in range(-steps, steps + 1)))
        for d2, i, j in offsets:
            if best and d2 * GRID * GRID > best[0] + 4.0:
                break
            x, y = round((tx + i * GRID) / GRID) * GRID, round((ty + j * GRID) / GRID) * GRID
            for a in angles:
                fp.SetPosition(pcb.VECTOR2I(round((x + ORIGIN) * 1e6), round((y + ORIGIN) * 1e6)))
                fp.SetOrientationDegrees(a)
                if not occ.free(side, courtyard_boxes(fp)):
                    continue
                score = 0.0
                for p in fp.Pads():
                    for net, (px, py) in targets:
                        if p.GetNetname() == net:
                            qx, qy = pad_xy(p)
                            score += math.hypot(qx - px, qy - py)
                score += 0.15 * math.hypot(x - tx, y - ty)
                if best is None or score < best[0]:
                    best = (score, x, y, a)
        if best is None:
            report.append('NO SPOT: %s near %s' % (ref, owner))
            continue
        _, x, y, a = best
        fp.SetPosition(pcb.VECTOR2I(round((x + ORIGIN) * 1e6), round((y + ORIGIN) * 1e6)))
        fp.SetOrientationDegrees(a)
        occ.add_fp(fp)
    return report
