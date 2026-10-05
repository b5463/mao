"""GND fence and coverage vias (KiCad 10 python). ODD JOBS 5, 16, 129, 144.

Run after routing, on a board whose zone fills are current (after a DRC with refill):
  ring      candidate sites every 1.5 degrees on r = PCB_R - 1.4 mm (the via ring stays >= 1.1 mm from the milled
            edge); a site is used when it lies inside an outer GND fill, keeps 0.2 mm from copper of other nets on
            every layer, 0.6 mm hole to hole, clear of via keep-outs (touch arcs, antenna, fasteners), outside the L3 power
            regions or deep inside one (power_regions.py, so no rail is pinched) and 2.2 mm from fiducials, and sits >= SPACING from
            every GND via already near the rim
  coverage  then sites on a 1 mm grid inside the outer GND fills that are more than COVER mm from every GND via,
            farthest first, until none is left or the budget is spent
  planes    last, the inner planes are filled with a 0.5 mm minimum width before and after: a fence via that splits
            an L2/L3 plane or leaves a neck under 0.5 mm wide (the fill drops it, so the piece count rises) is taken
            out again, one at a time, until no plane is worse than before
Budget: all through vias together stay under VIA_CAP (the 6-layer first pass had 230; the 4-layer brief asks
for fewer). The vias this script places are recorded in .cache/mao-routing/gnd-fence.json and replaced on
every run. --dry only reports.
"""
import json
import math
import sys

import pcbnew as pcb

import mechanical as m
import power_regions
from board import TARGET, CACHE

DRY = '--dry' in sys.argv
PRE = '--pre' in sys.argv          # before routing: ring only, sites need no fill yet (the pours will reach them)
OWN = CACHE / ('gnd-fence-ring.json' if '--pre' in sys.argv else 'gnd-fence.json')
SPACING, COVER, VIA_CAP, NECK = 3.5, 6.0, 229, 0.5
R_RING = m.PCB_R - 1.4
CLR = pcb.FromMM(0.2)

b = pcb.LoadBoard(sys.argv[sys.argv.index('--board') + 1] if '--board' in sys.argv else str(TARGET))
tracks = list(b.GetTracks())                         # SWIG order trap: read tracks before pads
mm = lambda v: pcb.ToMM(v) - 50
at = lambda x, y: pcb.VECTOR2I(pcb.FromMM(x + 50), pcb.FromMM(y + 50))
gnd = b.FindNet('GND').GetNetCode()
own = json.loads(OWN.read_text()) if OWN.exists() else []
if not PRE and (CACHE / 'gnd-fence-ring.json').exists():   # a ring via from --pre is not ours to replace, even where
    ring_pre = json.loads((CACHE / 'gnd-fence-ring.json').read_text())   # an earlier run left one at the same spot
    own = [p for p in own if not any(abs(p[0] - x) < .02 and abs(p[1] - y) < .02 for x, y in ring_pre)]
def mine(t): return isinstance(t, pcb.PCB_VIA) and any(abs(mm(t.GetPosition().x) - x) < .02 and abs(mm(t.GetPosition().y) - y) < .02 for x, y in own)
stale = [t for t in tracks if mine(t)]
tracks = [t for t in tracks if not mine(t)]
pads = [p for f in b.GetFootprints() for p in f.Pads()]
vias = [t for t in tracks if isinstance(t, pcb.PCB_VIA)]
COPPER = (pcb.F_Cu, pcb.In1_Cu, pcb.In2_Cu, pcb.B_Cu)
keep = [z for z in list(b.Zones()) + [z for f in b.GetFootprints() for z in f.Zones()]
        if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
fid = [(mm(f.GetPosition().x), mm(f.GetPosition().y)) for f in b.GetFootprints() if f.GetReference().startswith('FID')]
fills = []
for z in b.Zones():
    if z.GetNetCode() == gnd and not z.GetIsRuleArea():
        for layer in (pcb.F_Cu, pcb.B_Cu):
            if z.IsOnLayer(layer):
                fills.append((layer, z.GetFilledPolysList(layer)))
added = []
INNER = [z for z in b.Zones() if not z.GetIsRuleArea() and (z.IsOnLayer(pcb.In1_Cu) or z.IsOnLayer(pcb.In2_Cu))]


def inner_pieces():
    """Pieces each inner plane fills when every neck must be NECK wide."""
    saved = [(z, z.GetMinThickness()) for z in INNER]
    for z, _ in saved: z.SetMinThickness(pcb.FromMM(NECK))
    pcb.ZONE_FILLER(b).Fill(INNER)
    out = {z.GetZoneName(): sum(z.GetFilledPolysList(l).OutlineCount() for l in (pcb.In1_Cu, pcb.In2_Cu) if z.IsOnLayer(l))
           for z in INNER}
    for z, w in saved: z.SetMinThickness(w)
    return out


def site_ok(x, y):
    c = at(x, y)
    via = pcb.SHAPE_CIRCLE(c, pcb.FromMM(0.3))
    if not PRE and not any(polys.Contains(c) for _, polys in fills):  # must land in an outer GND fill
        return False
    if any(z.Outline().Collide(c, pcb.FromMM(0.3)) for z in keep): return False
    if any(math.hypot(x - fx, y - fy) < 2.2 for fx, fy in fid): return False
    if not power_regions.gnd_via_ok(x, y): return False
    for t in tracks + added:
        if isinstance(t, pcb.PCB_VIA):
            if (t.GetPosition() - c).EuclideanNorm() < pcb.FromMM(0.6 + 0.25): return False
            if t.GetNetCode() != gnd and (t.GetPosition() - c).EuclideanNorm() < pcb.FromMM(0.6) + CLR: return False
            continue
        if t.GetNetCode() == gnd: continue
        for layer in COPPER:
            if t.IsOnLayer(layer) and t.GetEffectiveShape(layer).Collide(via, CLR): return False
    for p in pads:
        if (p.GetPosition() - c).EuclideanNorm() > pcb.FromMM(4): continue
        if p.GetAttribute() in (pcb.PAD_ATTRIB_PTH, pcb.PAD_ATTRIB_NPTH) and \
           (p.GetPosition() - c).EuclideanNorm() < pcb.FromMM(0.15 + 0.25) + p.GetDrillSize().x // 2: return False
        for layer in COPPER:
            if p.IsOnLayer(layer) and p.GetEffectiveShape(layer).Collide(via, CLR if p.GetNetCode() != gnd else 0):
                return False                                          # no via in any pad, its own net's included
    return True


def gnd_sites():
    return [(mm(t.GetPosition().x), mm(t.GetPosition().y)) for t in tracks + added
            if isinstance(t, pcb.PCB_VIA) and t.GetNetCode() == gnd] + \
           [(mm(p.GetPosition().x), mm(p.GetPosition().y)) for p in pads
            if p.GetNetCode() == gnd and p.GetAttribute() == pcb.PAD_ATTRIB_PTH]


def place(x, y):
    v = pcb.PCB_VIA(b); v.SetPosition(at(x, y)); v.SetWidth(pcb.FromMM(0.6)); v.SetDrill(pcb.FromMM(0.3))
    v.SetViaType(pcb.VIATYPE_THROUGH); v.SetLayerPair(pcb.F_Cu, pcb.B_Cu); v.SetNetCode(gnd); v.SetLocked(True)
    if not DRY: b.Add(v)
    added.append(v)


budget = VIA_CAP - len(vias)
before = None if DRY else inner_pieces()
ring = 0
for k in range(240):
    if budget <= 0: break
    a = math.radians(k * 1.5)
    x, y = R_RING * math.sin(a), -R_RING * math.cos(a)
    near_rim = [(gx, gy) for gx, gy in gnd_sites() if math.hypot(gx, gy) > m.PCB_R - 3.0]
    if any(math.hypot(x - gx, y - gy) < SPACING for gx, gy in near_rim): continue
    if site_ok(x, y):
        place(x, y); budget -= 1; ring += 1
cover = 0
while budget > 0 and not PRE:
    sites = gnd_sites()
    best = None
    for gx in range(-28, 29):
        for gy in range(-28, 29):
            x, y = float(gx), float(gy)
            if math.hypot(x, y) > m.PCB_R - 1.4: continue
            d = min(math.hypot(x - sx, y - sy) for sx, sy in sites)
            if d > COVER and (best is None or d > best[0]) and site_ok(x, y):
                best = (d, x, y)
    if not best: break
    place(best[1], best[2]); budget -= 1; cover += 1
dropped = []
if not DRY:
    for t in stale:                                   # gone before the plane test, so it sees the final copper
        b.Remove(t)
    stale = []
    now = inner_pieces()
    while any(now[k] > before[k] for k in now):
        worse = [k for k in now if now[k] > before[k]]
        for v in list(added):
            b.Remove(v); added.remove(v)
            trial = inner_pieces()
            if all(trial[k] <= now[k] for k in now) and any(trial[k] < now[k] for k in worse):
                dropped.append((round(mm(v.GetPosition().x), 2), round(mm(v.GetPosition().y), 2))); now = trial
                break
            b.Add(v); added.append(v)
        else:
            print('gnd fence: planes worse than before and no single via to blame:', worse, flush=True)
            break
    pcb.ZONE_FILLER(b).Fill(INNER)
OWN.write_text(json.dumps([[round(mm(v.GetPosition().x), 3), round(mm(v.GetPosition().y), 3)] for v in added]))
if not DRY:
    for t in stale:                                   # last: Remove() invalidates the other proxies
        b.Remove(t)
    pcb.SaveBoard(str(TARGET), b)
print('gnd fence: %d ring + %d coverage vias, %d taken out again for plane necks %s (%d through vias in all, cap %d)%s' % (
    ring, cover, len(dropped), dropped, len(vias) + len(added), VIA_CAP, ' [dry]' if DRY else ''), flush=True)
import os
os._exit(0)
