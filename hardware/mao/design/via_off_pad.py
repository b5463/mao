"""No via in an SMD pad (ODD JOBS 82), as a guarantee after routing (KiCad 10 python).

The router may land a via on the edge of a pad of its own net (it joins the pad there). An unfilled via
in or touching an SMD pad wicks solder away from the joint. Every via whose ring touches an SMD pad, of
any net, is slid straight away from that pad in 0.05 mm steps (then 45 degrees either side) to the first
spot where its ring clears every SMD pad and keeps 0.15 mm from copper of other nets on every layer it
crosses, and where the ends of the tracks that met it, moved with it, keep the same clearance on their
layers and stay out of rule areas. Connectivity is unchanged: only the via and the track ends that meet
it move. Exposed-pad thermal vias belong to their footprint and are not board vias, so they are not
touched. Run DRC afterwards.

    python via_off_pad.py [--dry]
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
mm = lambda v: pcb.ToMM(v) - 50


def on_smd(c, r):
    shape = pcb.SHAPE_CIRCLE(c, r)
    for p in smd:
        for ly in (pcb.F_Cu, pcb.B_Cu):
            if p.IsOnLayer(ly) and p.GetEffectiveShape(ly).Collide(shape, 0):
                return p
    return None


def foreign(shape, layer, net, skip):
    for t in segs + vias:
        if t is skip or t.GetNetCode() == net or not t.IsOnLayer(layer):
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
        if not z.IsOnLayer(layer):
            continue
        if (via and z.GetDoNotAllowVias()) or (not via and z.GetDoNotAllowTracks()):
            if z.Outline().Collide(shape, 0):
                return True
    return False


moved, stuck = [], []
for v in vias:
    c, r, net = v.GetPosition(), v.GetWidth(pcb.F_Cu) // 2, v.GetNetCode()
    pad = on_smd(c, r)
    if not pad:
        continue
    ends = [(t, 'start' if t.GetStart() == c else 'end') for t in segs if t.GetNetCode() == net
            and (t.GetStart() == c or t.GetEnd() == c)]
    dx, dy = c.x - pad.GetPosition().x, c.y - pad.GetPosition().y
    base = math.atan2(dy, dx) if (dx or dy) else 0.0
    done = None
    for turn in (0.0, math.pi / 4, -math.pi / 4):
        a = base + turn
        for k in range(1, 21):
            d = pcb.FromMM(0.05 * k)
            q = pcb.VECTOR2I(int(c.x + d * math.cos(a)), int(c.y + d * math.sin(a)))
            ring = pcb.SHAPE_CIRCLE(q, r)
            if on_smd(q, r) or any(foreign(ring, ly, net, v) or in_area(ring, ly, True) for ly in LAYERS):
                continue
            ok = True
            for t, which in ends:              # the track ends follow the via
                s0, s1 = (q, t.GetEnd()) if which == 'start' else (t.GetStart(), q)
                seg = pcb.SHAPE_SEGMENT(s0, s1, t.GetWidth())
                if foreign(seg, t.GetLayer(), net, t) or in_area(seg, t.GetLayer(), False):
                    ok = False
                    break
            if ok:
                done = q
                break
        if done:
            break
    if not done:
        stuck.append((v.GetNetname(), round(mm(c.x), 2), round(mm(c.y), 2), pad.GetParentFootprint().GetReference()))
        continue
    moved.append((v.GetNetname(), round(mm(c.x), 2), round(mm(c.y), 2), round(mm(done.x), 2), round(mm(done.y), 2),
                  pad.GetParentFootprint().GetReference() + '.' + pad.GetNumber()))
    if not DRY:
        v.SetPosition(done)
        for t, which in ends:
            (t.SetStart if which == 'start' else t.SetEnd)(done)
if not DRY and moved:
    pcb.SaveBoard(str(TARGET), b)
print('via off pad: moved %d %s; stuck %d %s%s' % (len(moved), moved, len(stuck), stuck, ' (dry)' if DRY else ''),
      flush=True)
os._exit(1 if stuck else 0)
