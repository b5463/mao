"""Copper-quality measurements the DRC does not make (KiCad python, read-only). ODD JOBS 5, 16, 18, 124, 129, 144.

  broadside   L3 (In2) signal tracks running parallel on top of B.Cu signal tracks of another net (L3-L4 is the
              thin 1080 prepreg, so stacked parallel lines couple strongly): total and per pair, crossings excluded
  stitching   GND vias near the board perimeter (within 3 mm) and the largest angular gap between them;
              distance from module GND pins 1 / 40 and every IC decoupling-cap GND pad to the nearest GND via
  edge        every via's and every SMD pad's distance to the milled edge (outline and slot)
Writes outputs/STACKUP-CHECK.json and prints a summary.
"""
import json
import math
import os

import pcbnew as pcb

import mechanical as m
from board import ROOT, TARGET

mm = lambda v: pcb.ToMM(v) - 50
b = pcb.LoadBoard(str(TARGET))
tracks = [t for t in b.GetTracks()]
vias = [t for t in tracks if isinstance(t, pcb.PCB_VIA)]
segs = [t for t in tracks if not isinstance(t, pcb.PCB_VIA)]
POWER = ('GND', '+3V3', 'VSYS', 'VBUS', 'VBAT')


def seg(t):
    return (mm(t.GetStart().x), mm(t.GetStart().y)), (mm(t.GetEnd().x), mm(t.GetEnd().y)), pcb.ToMM(t.GetWidth())


def overlap_len(a, b_):
    """Length over which two segments run parallel (< 15 deg) and stacked (centrelines within half widths + 0.15)."""
    (a0, a1, wa), (b0, b1, wb) = a, b_
    ax, ay = a1[0] - a0[0], a1[1] - a0[1]; La = math.hypot(ax, ay)
    bx, by = b1[0] - b0[0], b1[1] - b0[1]; Lb = math.hypot(bx, by)
    if La < 1e-6 or Lb < 1e-6: return 0.0
    cos = abs(ax * bx + ay * by) / (La * Lb)
    if cos < math.cos(math.radians(15)): return 0.0
    ux, uy = ax / La, ay / La
    # lateral offset of b's endpoints from a's line
    off = lambda p: abs((p[0] - a0[0]) * uy - (p[1] - a0[1]) * ux)
    if min(off(b0), off(b1)) > (wa + wb) / 2 + 0.15: return 0.0
    t = lambda p: (p[0] - a0[0]) * ux + (p[1] - a0[1]) * uy
    lo, hi = max(0.0, min(t(b0), t(b1))), min(La, max(t(b0), t(b1)))
    return max(0.0, hi - lo)


l3 = [(t.GetNetname(), seg(t)) for t in segs if t.GetLayer() == pcb.In2_Cu and t.GetNetname() not in POWER]
l4 = [(t.GetNetname(), seg(t)) for t in segs if t.GetLayer() == pcb.B_Cu]
pairs = {}
for n3, s3 in l3:
    for n4, s4 in l4:
        if n3 == n4: continue
        L = overlap_len(s3, s4)
        if L > 0.05:
            pairs[(n3, n4)] = pairs.get((n3, n4), 0.0) + L
broadside = sorted(((round(v, 2), k[0], k[1]) for k, v in pairs.items()), reverse=True)

gnd = [(mm(v.GetPosition().x), mm(v.GetPosition().y)) for v in vias if v.GetNetname() == 'GND']
gnd += [(mm(p.GetPosition().x), mm(p.GetPosition().y)) for f in b.GetFootprints() for p in f.Pads()
        if p.GetNetname() == 'GND' and p.GetAttribute() == pcb.PAD_ATTRIB_PTH]
per = sorted(math.degrees(math.atan2(x, -y)) % 360 for x, y in gnd if math.hypot(x, y) > m.PCB_R - 3.0)
gaps = [((per[(i + 1) % len(per)] - per[i]) % 360, round(per[i]), round(per[(i + 1) % len(per)])) for i in range(len(per))] if per else []
near = lambda x, y: min(math.hypot(x - gx, y - gy) for gx, gy in gnd)
fps = {f.GetReference(): f for f in b.GetFootprints()}
modpins = {}
for n in ('1', '40', '41'):
    for p in fps['U201'].Pads():
        if p.GetNumber() == n:
            x, y = mm(p.GetPosition().x), mm(p.GetPosition().y)
            modpins[n] = round(near(x, y), 2)
caps = {}
for r, f in fps.items():
    if not r.startswith('C'): continue
    for p in f.Pads():
        if p.GetNetname() == 'GND':
            x, y = mm(p.GetPosition().x), mm(p.GetPosition().y)
            caps[r] = round(near(x, y), 2)
far_caps = sorted(((d, r) for r, d in caps.items() if d > 1.6), reverse=True)


def edge_dist(x, y):
    d = m.PCB_R - math.hypot(x, y)
    if abs(x) < m.NOTCH_W / 2 + 2 and y > m.NOTCH_Y - 2:
        d = min(d, m.NOTCH_Y - y if abs(x) < m.NOTCH_W / 2 else math.hypot(abs(x) - m.NOTCH_W / 2, max(0.0, m.NOTCH_Y - y)))
    sl = m.TAIL_SLOT
    dx = max(sl[0] - x, 0.0, x - sl[2]); dy = max(sl[1] + 0.5 - y, 0.0, y - sl[3] + 0.5)
    return min(d, math.hypot(dx, dy) - 0.0)


via_edge = sorted((round(edge_dist(mm(v.GetPosition().x), mm(v.GetPosition().y)) - pcb.ToMM(v.GetWidth(pcb.F_Cu)) / 2, 2),
                   v.GetNetname(), round(mm(v.GetPosition().x), 2), round(mm(v.GetPosition().y), 2)) for v in vias)[:8]
pad_edge = []
for r, f in fps.items():
    if r.startswith(('E', 'H', 'FID', 'J101', 'LS')): continue
    for p in f.Pads():
        if p.GetAttribute() != pcb.PAD_ATTRIB_SMD: continue
        bb = p.GetBoundingBox()
        d = min(edge_dist(mm(x), mm(y)) for x in (bb.GetLeft(), bb.GetRight()) for y in (bb.GetTop(), bb.GetBottom()))
        pad_edge.append((round(d, 2), r + '.' + p.GetNumber()))
pad_edge = sorted(pad_edge)[:8]
out = {'broadside_total_mm': round(sum(v for v, _, _ in broadside), 1), 'broadside_pairs': broadside[:20],
       'perimeter_gnd_vias': len(per), 'largest_perimeter_gaps_deg': sorted(gaps, reverse=True)[:4],
       'module_gnd_pin_to_via_mm': modpins, 'cap_gnd_pads_over_1.6mm_from_via': far_caps[:15],
       'closest_vias_to_edge_mm': via_edge, 'closest_smd_pads_to_edge_mm': pad_edge}
(ROOT / 'outputs' / 'STACKUP-CHECK.json').write_text(json.dumps(out, indent=1) + '\n')
print(json.dumps(out, indent=1), flush=True)
os._exit(0)
