"""45-degree discipline for pad-to-via stubs (ODD JOBS 87/88), as a pass after routing (KiCad 10 python).

The plane-via placer and the via-off-pad pass put a via where it fits and join it to its pad with a straight
stub, so a few stubs leave at odd angles. For every track segment that is not a multiple of 45 degrees and
ends on a via that nothing else but this segment (and the planes) joins, the via is slid to the nearest
spot that puts the stub on a 45-degree multiple, keeping the other end where it is. A spot is taken only if
the via ring clears every SMD pad, keeps 0.15 mm from copper of other nets on every layer, stays out of
rule areas, and the moved stub keeps the same clearance on its layer. A via shared with other stubs (the
plane-via sharing) stays put: its oblique stub becomes a two-segment 45-degree dogleg between the same
two points, straight leg first or diagonal leg first, whichever clears. Connectivity is unchanged. Run
DRC and the plane check afterwards.

    python snap45.py [--dry]
"""
import math
import os
import sys

import pcbnew as pcb

from board import TARGET

DRY = '--dry' in sys.argv
CLR = pcb.FromMM(0.15)
b = pcb.LoadBoard(str(TARGET))
tracks = list(b.GetTracks())                     # SWIG order trap: read before the pad lists
vias = [t for t in tracks if isinstance(t, pcb.PCB_VIA)]
segs = [t for t in tracks if not isinstance(t, pcb.PCB_VIA)]
pads = [p for f in b.GetFootprints() for p in f.Pads()]
smd = [p for p in pads if p.GetAttribute() == pcb.PAD_ATTRIB_SMD]
areas = [z for z in b.Zones() if z.GetIsRuleArea()]
LAYERS = (pcb.F_Cu, pcb.In1_Cu, pcb.In2_Cu, pcb.B_Cu)
mm = lambda v: round(pcb.ToMM(v) - 50, 2)


def off45(t):
    a = math.degrees(math.atan2(t.GetEnd().y - t.GetStart().y, t.GetEnd().x - t.GetStart().x)) % 45
    return min(a, 45 - a) > 0.5


def foreign(shape, layer, net, skip):
    for t in segs + vias:
        if any(t is k for k in skip) or t.GetNetCode() == net or not t.IsOnLayer(layer):
            continue
        if t.GetEffectiveShape(layer).Collide(shape, CLR - 1):
            return True
    for p in pads:
        if p.GetNetCode() == net or not p.IsOnLayer(layer):
            continue
        if p.GetEffectiveShape(layer).Collide(shape, CLR - 1):
            return True
    return False


def in_area(shape, layer, via):
    for z in areas:
        if z.IsOnLayer(layer) and ((via and z.GetDoNotAllowVias()) or (not via and z.GetDoNotAllowTracks())):
            if z.Outline().Collide(shape, 0):
                return True
    return False


def on_smd(c, r):
    shape = pcb.SHAPE_CIRCLE(c, r)
    return any(p.IsOnLayer(ly) and p.GetEffectiveShape(ly).Collide(shape, 0)
               for p in smd for ly in (pcb.F_Cu, pcb.B_Cu))


def dogleg(t):
    """Bend point that turns segment t into an orthogonal + a 45-degree leg, clear of other copper."""
    a, e = t.GetStart(), t.GetEnd()
    dx, dy = e.x - a.x, e.y - a.y
    sx, sy = (1 if dx > 0 else -1), (1 if dy > 0 else -1)
    m = min(abs(dx), abs(dy))
    straight_first = pcb.VECTOR2I(a.x + sx * (abs(dx) - m), a.y) if abs(dx) > abs(dy) else \
        pcb.VECTOR2I(a.x, a.y + sy * (abs(dy) - m))
    diagonal_first = pcb.VECTOR2I(a.x + sx * m, a.y + sy * m)
    for bend in (straight_first, diagonal_first):
        legs = [pcb.SHAPE_SEGMENT(a, bend, t.GetWidth()), pcb.SHAPE_SEGMENT(bend, e, t.GetWidth())]
        if not any(foreign(g, t.GetLayer(), t.GetNetCode(), [t]) or in_area(g, t.GetLayer(), False) for g in legs):
            return bend
    return None


moved, bent, kept = [], [], []
for t in [s for s in segs if off45(s)]:
    net = t.GetNetCode()
    for which in ('start', 'end'):
        vpos = t.GetStart() if which == 'start' else t.GetEnd()
        fixed = t.GetEnd() if which == 'start' else t.GetStart()
        v = next((x for x in vias if x.GetPosition() == vpos and x.GetNetCode() == net), None)
        others = [s for s in segs if s is not t and s.GetNetCode() == net and (s.GetStart() == vpos or s.GetEnd() == vpos)]
        if v is not None and not others:
            break
    else:
        bend = dogleg(t)
        if bend is None:
            kept.append((t.GetNetname(), mm(t.GetStart().x), mm(t.GetStart().y), 'no clear dogleg'))
            continue
        bent.append((t.GetNetname(), mm(t.GetStart().x), mm(t.GetStart().y), mm(bend.x), mm(bend.y)))
        if not DRY:
            leg = pcb.PCB_TRACK(b)
            leg.SetLayer(t.GetLayer())
            leg.SetWidth(t.GetWidth())
            leg.SetNetCode(net)
            leg.SetStart(bend)
            leg.SetEnd(t.GetEnd())
            b.Add(leg)
            t.SetEnd(bend)
        continue
    r = v.GetWidth(pcb.F_Cu) // 2
    dx, dy = vpos.x - fixed.x, vpos.y - fixed.y
    length = math.hypot(dx, dy)
    ang = math.degrees(math.atan2(dy, dx))
    cands = []
    for base in (math.floor(ang / 45) * 45, math.ceil(ang / 45) * 45):
        for k in range(-6, 7):
            L = length + pcb.FromMM(0.05 * k)
            if L < pcb.FromMM(0.3):
                continue
            q = pcb.VECTOR2I(int(fixed.x + L * math.cos(math.radians(base))),
                             int(fixed.y + L * math.sin(math.radians(base))))
            cands.append((math.hypot(q.x - vpos.x, q.y - vpos.y), q))
    done = None
    for _, q in sorted(cands, key=lambda c: c[0]):
        ring = pcb.SHAPE_CIRCLE(q, r)
        if on_smd(q, r) or any(foreign(ring, ly, net, [v]) or in_area(ring, ly, True) for ly in LAYERS):
            continue
        seg = pcb.SHAPE_SEGMENT(fixed, q, t.GetWidth())
        if foreign(seg, t.GetLayer(), net, [t]) or in_area(seg, t.GetLayer(), False):
            continue
        done = q
        break
    if not done:
        kept.append((t.GetNetname(), mm(vpos.x), mm(vpos.y), 'no clear 45-degree spot'))
        continue
    moved.append((t.GetNetname(), mm(vpos.x), mm(vpos.y), mm(done.x), mm(done.y)))
    if not DRY:
        v.SetPosition(done)
        (t.SetStart if which == 'start' else t.SetEnd)(done)
if not DRY and (moved or bent):
    pcb.SaveBoard(str(TARGET), b)
print('snap45: moved %d %s; bent %d %s; kept %d %s%s' % (len(moved), moved, len(bent), bent, len(kept), kept,
                                                         ' (dry)' if DRY else ''), flush=True)
os._exit(0)
