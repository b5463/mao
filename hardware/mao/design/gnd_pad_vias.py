# Ported from b5463/kino-d4 hardware/pcb/kino-d4-carrier-a0/design/gnd_pad_vias.py @ 68aba75 (ODD JOBS PCB toolchain).
"""A GND via beside each named GND pad that reaches no ground (KiCad 10 python). ODD JOBS 13, 152.

gnd_islands.py lists GND pads whose pour fragment has no via (the fragment is too small for
gnd_stitch.py). Each named pad gets a 0.6 / 0.3 mm via 0.9 to 3.0 mm from its centre in one of the
eight 45-degree directions, joined by a 0.3 mm track on the pad's layer that leaves the pad on a
centre line, or a diagonal where no centre line is clear (ODD JOBS 87/88). The nearest candidate wins whose via keeps 0.15 mm from
copper of other nets on every layer it crosses (In1 is the GND plane), 0.25 mm hole to hole, and
whose track keeps 0.15 mm on its own layer. Run DRC with --refill-zones afterwards.

    python gnd_pad_vias.py [--dry] U900.4 U900.8 U701.3 ...
    python gnd_pad_vias.py U1201.1>U1201.9      # pad to a grounded pad of the same part instead:
                                                # a straight 0.25 mm track along the shared axis
    python gnd_pad_vias.py --all                # MAO: every SMD GND pad without its own via yet,
                                                # outward from the part first (ODD JOBS 16)
"""
import math, sys
import pcbnew as pcb
from board import TARGET, pt

b = pcb.LoadBoard(str(TARGET))
tracks = list(b.GetTracks())                     # SWIG order trap: read before pad lists
fps = {f.GetReference(): f for f in b.GetFootprints()}
pads = [p for f in b.GetFootprints() for p in f.Pads()]
NET = sys.argv[sys.argv.index('--net') + 1] if '--net' in sys.argv else 'GND'
code = b.FindNet(NET).GetNetCode()
mm = lambda v: pcb.ToMM(v) - 50
CLR = pcb.FromMM(0.15)
COPPER = (pcb.F_Cu, pcb.In2_Cu, pcb.In3_Cu, pcb.B_Cu)
added = []

def foreign(shape, layer):
    bb = shape.BBox(); bb.Inflate(pcb.FromMM(1.5))
    for t in tracks + added:
        if t.GetNetCode() == code or not t.IsOnLayer(layer) or not bb.Intersects(t.GetBoundingBox()): continue
        if t.GetEffectiveShape(layer).Collide(shape, CLR - 1): return True
    for p in pads:
        if p.GetNetCode() == code and p.GetNetCode() != 0 or not p.IsOnLayer(layer): continue
        if not bb.Intersects(p.GetBoundingBox()): continue
        if p.GetEffectiveShape(layer).Collide(shape, CLR - 1): return True
    return False
def hole_ok(at):
    for t in tracks + added:
        if isinstance(t, pcb.PCB_VIA) and (t.GetPosition() - at).EuclideanNorm() < pcb.FromMM(0.55): return False
    for p in pads:
        if p.GetAttribute() in (pcb.PAD_ATTRIB_PTH, pcb.PAD_ATTRIB_NPTH) and \
           (p.GetPosition() - at).EuclideanNorm() < pcb.FromMM(0.15 + 0.25) + p.GetDrillSize().x // 2: return False
    return True

import mechanical as m
from board import ORIGIN
areas = [z for z in b.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
areas += [z for f in b.GetFootprints() for z in f.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
track_areas = [z for z in b.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowTracks()]
track_areas += [z for f in b.GetFootprints() for z in f.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowTracks()]
fiducials = [f.GetPosition() for f in b.GetFootprints() if f.GetReference().startswith('FID')]
def inside_ok(at):
    x, y = pcb.ToMM(at.x) - ORIGIN, pcb.ToMM(at.y) - ORIGIN
    if math.hypot(x, y) > m.PCB_R - 0.8: return False
    if abs(x) < m.NOTCH_W / 2 + 0.8 and y > m.NOTCH_Y - 0.8: return False
    for z in areas:
        if z.Outline().Collide(at, pcb.FromMM(0.3)): return False
    if any((at - c).EuclideanNorm() < pcb.FromMM(2.2) for c in fiducials): return False   # 2 mm mask ring + gap
    return True
def stub_ok(c, at, layer):
    seg = pcb.SHAPE_SEGMENT(c, at, pcb.FromMM(0.3))
    for z in track_areas:
        if z.IsOnLayer(layer) and z.Outline().Collide(seg, 0): return False
    return True

log = []
DRY = '--dry' in sys.argv
def link(name):
    a_, b_ = name.split('>'); ra, na = a_.split('.'); rb, nb = b_.split('.')
    pa = next(p for p in fps[ra].Pads() if p.GetNumber() == na)
    pb = max((p for p in fps[rb].Pads() if p.GetNumber() == nb), key=lambda p: p.GetBoundingBox().GetArea())
    layer = pcb.B_Cu if fps[ra].IsFlipped() else pcb.F_Cu
    c, bb = pa.GetPosition(), pb.GetBoundingBox(); inset = pcb.FromMM(0.25)
    if bb.GetTop() <= c.y <= bb.GetBottom(): end = pcb.VECTOR2I(bb.GetLeft() + inset if c.x < bb.GetLeft() else bb.GetRight() - inset, c.y)
    elif bb.GetLeft() <= c.x <= bb.GetRight(): end = pcb.VECTOR2I(c.x, bb.GetTop() + inset if c.y < bb.GetTop() else bb.GetBottom() - inset)
    else: return f'{name}: pads not aligned'
    if foreign(pcb.SHAPE_SEGMENT(c, end, pcb.FromMM(0.25)), layer): return f'{name}: link blocked'
    t = pcb.PCB_TRACK(b); t.SetStart(c); t.SetEnd(end); t.SetWidth(pcb.FromMM(0.25)); t.SetLayer(layer)
    t.SetNetCode(code); t.SetLocked(True); b.Add(t); added.append(t)
    return f'{name}: {pcb.ToMM((end - c).EuclideanNorm()):.2f} mm link'
names = [a for a in sys.argv[1:] if not a.startswith('--') and a != NET]
if '--all' in sys.argv:
    skip = {'FID', 'H', 'E'}                       # no part / electrodes
    for f in b.GetFootprints():
        ref = f.GetReference()
        if ''.join(ch for ch in ref if ch.isalpha()) in skip:
            continue
        has_vias = any(p.GetAttribute() == pcb.PAD_ATTRIB_PTH and p.GetNetCode() == code for p in f.Pads())
        for p in f.Pads():
            if p.GetNetCode() != code or p.GetAttribute() != pcb.PAD_ATTRIB_SMD:
                continue
            lay = pcb.B_Cu if f.IsFlipped() else pcb.F_Cu     # pad.GetLayer() reports F.Cu for B parts
            if any(t.IsLocked() and t.GetNetCode() == code and t.IsOnLayer(lay) and
                   t.GetEffectiveShape(lay).Collide(p.GetEffectiveShape(lay), 0) for t in tracks):
                continue                           # MAO: designed copper (route_power.py) already serves it
            if has_vias and p.GetSize(pcb.F_Cu).x > pcb.FromMM(1.0):
                continue                           # exposed pad that already carries thermal vias
            names.append('%s.%s' % (ref, p.GetNumber()))
    names = sorted(set(names), key=lambda n: (n.split('.')[0], n))
done_pads = set()
for name in names:
    if '>' in name: log.append(link(name)); continue
    ref, num = name.split('.')
    cands = [p for p in fps[ref].Pads() if p.GetNumber() == num and p.GetAttribute() == pcb.PAD_ATTRIB_SMD]
    for pad in cands:
      assert pad.GetNetCode() == code, (name, 'not ' + NET)
      key = (ref, num, pad.GetPosition().x, pad.GetPosition().y)
      if key in done_pads:
          continue
      done_pads.add(key)
      layer = pcb.B_Cu if fps[ref].IsFlipped() else pcb.F_Cu
      c = pad.GetPosition(); best = None
      fc = fps[ref].GetPosition()
      out = math.atan2(c.y - fc.y, c.x - fc.x) if (c - fc).EuclideanNorm() > pcb.FromMM(0.2) else 0.0
      order = sorted(range(8), key=lambda k: (k % 2, abs(math.remainder(math.pi / 4 * k - out, 2 * math.pi))))
      edge = max(pad.GetSize(pcb.F_Cu).x, pad.GetSize(pcb.F_Cu).y) / 2e6
      for r10 in range(int((edge + 0.6) * 10), 31):
          r = r10 / 10
          for k in order:                                  # centre lines first, outward first
              a = math.pi / 4 * k
              at = c + pcb.VECTOR2I(pcb.FromMM(r * math.cos(a)), pcb.FromMM(r * math.sin(a)))
              via = pcb.SHAPE_CIRCLE(at, pcb.FromMM(0.3))
              if not hole_ok(at) or any(foreign(via, l) for l in COPPER): continue
              stub = pcb.SHAPE_SEGMENT(c, at, pcb.FromMM(0.3))
              if foreign(stub, layer): continue
              if not inside_ok(at) or not stub_ok(c, at, layer): continue
              best = (r, at); break
          if best: break
      if not best: log.append(f'{name}: NO SITE within 3 mm'); continue
      v = pcb.PCB_VIA(b); v.SetPosition(best[1]); v.SetWidth(pcb.FromMM(.6)); v.SetDrill(pcb.FromMM(.3))
      v.SetViaType(pcb.VIATYPE_THROUGH); v.SetLayerPair(pcb.F_Cu, pcb.B_Cu); v.SetNetCode(code); v.SetLocked(True); b.Add(v)
      t = pcb.PCB_TRACK(b); t.SetStart(c); t.SetEnd(best[1]); t.SetWidth(pcb.FromMM(0.3)); t.SetLayer(layer)
      t.SetNetCode(code); t.SetLocked(True); b.Add(t)
      added += [v, t]
      log.append(f'{name}: {best[0]:.1f} mm')
    continue
    pad = None
    layer = pcb.B_Cu if fps[ref].IsFlipped() else pcb.F_Cu
    c = pad.GetPosition(); best = None
    for r10 in range(9, 31):
        r = r10 / 10
        for k in (0, 2, 4, 6, 1, 3, 5, 7):                 # centre lines before diagonals
            a = math.pi / 4 * k
            at = c + pcb.VECTOR2I(pcb.FromMM(r * math.cos(a)), pcb.FromMM(r * math.sin(a)))
            via = pcb.SHAPE_CIRCLE(at, pcb.FromMM(0.3))
            if not hole_ok(at) or any(foreign(via, l) for l in COPPER): continue
            stub = pcb.SHAPE_SEGMENT(c, at, pcb.FromMM(0.3))
            if foreign(stub, layer): continue
            best = (r, at); break
        if best: break
    if not best: log.append(f'{name}: NO SITE within 3 mm'); continue
    v = pcb.PCB_VIA(b); v.SetPosition(best[1]); v.SetWidth(pcb.FromMM(.6)); v.SetDrill(pcb.FromMM(.3))
    v.SetViaType(pcb.VIATYPE_THROUGH); v.SetLayerPair(pcb.F_Cu, pcb.B_Cu); v.SetNetCode(code); v.SetLocked(True); b.Add(v)
    t = pcb.PCB_TRACK(b); t.SetStart(c); t.SetEnd(best[1]); t.SetWidth(pcb.FromMM(0.3)); t.SetLayer(layer)
    t.SetNetCode(code); t.SetLocked(True); b.Add(t)
    added += [v, t]
    log.append(f'{name}: via at ({mm(best[1].x):.2f}, {mm(best[1].y):.2f}), {best[0]:.1f} mm')
if not DRY: pcb.SaveBoard(str(TARGET), b)
print(NET + ' pad vias' + (' (dry)' if DRY else '') + ':', '; '.join(log), flush=True)
import os; os._exit(0)
