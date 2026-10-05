"""Trim designed fan-out the router did not use (KiCad 10 python). Run DRC first, then this, then DRC.

The fan-out in route_local.py gives every crowded pin a stub and, where its line may change layer, a
via. The router then joins each net wherever it likes: sometimes at the via, sometimes partway along
the stub, sometimes at the pad itself. What it did not use is left dangling. For every item KiCad's
DRC reports as dangling (outputs/DRC.json), locked or not:
  via    connected on one layer only: removed
  track  the end that touches nothing of its own net is pulled back along the segment to the last
         point where same-net copper still touches it (a router join, a pad, a via); if nothing
         touches it beyond the connected end, the segment is removed
Repeat (pipeline step 'prune') until DRC reports nothing dangling. Connectivity never changes: only
copper that leads nowhere is cut back.
"""
import json, math, re, sys
import pcbnew as pcb
from board import TARGET, DRC_JSON

drc = json.loads(DRC_JSON.read_text(encoding='utf-8'))
wanted = []
for v in drc['violations']:
    if v['type'] not in ('track_dangling', 'via_dangling'): continue
    for it in v['items']:
        m = re.match(r'(Track|Via) \[(.*?)\]', it['description'])
        if m: wanted.append((m.group(1), m.group(2), pcb.FromMM(it['pos']['x']), pcb.FromMM(it['pos']['y'])))

b = pcb.LoadBoard(str(TARGET))
tracks = list(b.GetTracks())
pads = [p for f in b.GetFootprints() for p in f.Pads()]
TOL = pcb.FromMM(0.01)


def seg_dist(p, a, c):
    ax, ay, cx, cy = a.x, a.y, c.x, c.y
    dx, dy = cx - ax, cy - ay
    L2 = dx * dx + dy * dy
    u = 0 if L2 == 0 else max(0.0, min(1.0, ((p.x - ax) * dx + (p.y - ay) * dy) / L2))
    return math.hypot(p.x - ax - u * dx, p.y - ay - u * dy)


def touched(net, layer, pt, me):
    """Same-net copper other than `me` covers point pt on `layer`."""
    for t in tracks:
        if t is me or t.GetNetname() != net or t.GetParent() is None: continue
        if isinstance(t, pcb.PCB_VIA):
            if math.hypot(pt.x - t.GetPosition().x, pt.y - t.GetPosition().y) <= t.GetWidth(pcb.F_Cu) / 2 + TOL:
                return True
        elif t.GetLayer() == layer and seg_dist(pt, t.GetStart(), t.GetEnd()) <= t.GetWidth() / 2 + TOL:
            return True
    for p in pads:
        if p.GetNetname() == net and p.IsOnLayer(layer) and p.HitTest(pt):
            return True
    return False


done = []
for kind, net, x, y in wanted:
    for t in tracks:
        if t.GetNetname() != net or t.GetParent() is None: continue
        if kind == 'Via' and isinstance(t, pcb.PCB_VIA):
            if abs(t.GetPosition().x - x) < TOL and abs(t.GetPosition().y - y) < TOL:
                b.Remove(t); done.append('%s via removed' % net); break
        elif kind == 'Track' and not isinstance(t, pcb.PCB_VIA):
            s_, e_ = t.GetStart(), t.GetEnd()
            if min(math.hypot(s_.x - x, s_.y - y), math.hypot(e_.x - x, e_.y - y)) > TOL and seg_dist(pcb.VECTOR2I(x, y), s_, e_) > TOL:
                continue
            ls = touched(net, t.GetLayer(), s_, t)
            le = touched(net, t.GetLayer(), e_, t)
            covered = any(o is not t and not isinstance(o, pcb.PCB_VIA) and o.GetParent() is not None
                          and o.GetNetname() == net and o.GetLayer() == t.GetLayer()
                          and seg_dist(s_, o.GetStart(), o.GetEnd()) <= TOL and seg_dist(e_, o.GetStart(), o.GetEnd()) <= TOL
                          for o in tracks)
            if covered:                               # lies wholly on another track of its net: redundant
                b.Remove(t); done.append('%s track removed (overlapping)' % net); break
            if ls and le: continue
            if not ls and not le:
                b.Remove(t); done.append('%s track removed (no connection)' % net); break
            a, d = (s_, e_) if ls else (e_, s_)        # a connected, d dangling
            n = max(2, int(math.hypot(d.x - a.x, d.y - a.y) / pcb.FromMM(0.01)))
            keep = None
            for i in range(n, -1, -1):                # from the dangling end towards the connected one
                q = pcb.VECTOR2I(int(a.x + (d.x - a.x) * i / n), int(a.y + (d.y - a.y) * i / n))
                if i < n and touched(net, t.GetLayer(), q, t):
                    keep = q; break
            if keep is None or math.hypot(keep.x - a.x, keep.y - a.y) < pcb.FromMM(0.05):
                b.Remove(t); done.append('%s track removed' % net)
            else:
                if ls: t.SetEnd(keep)
                else: t.SetStart(keep)
                done.append('%s track trimmed' % net)
            break
pcb.SaveBoard(str(TARGET), b)
print('pruned %d: %s' % (len(done), '; '.join(done)), flush=True)
import os; os._exit(0)
