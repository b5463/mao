# Ported from b5463/kino-d4 hardware/pcb/kino-d4-carrier-a0/design/grid_router.py @ 68aba75 (ODD JOBS PCB toolchain).
"""Grid maze router for the connections Freerouting could not finish (system python + numpy/scipy/Pillow).

Input : .cache/mao-routing/grid-dump.json (grid_dump.py) and outputs/DRC.json (unconnected list).
Output: .cache/mao-routing/grid-routes.json, applied by apply_routes.py.

Model: 0.05 mm cells on F.Cu, In2.Cu (L3: slow nets only, outside the power-region cores) and B.Cu (In1 is
the solid ground plane: vias only). Copper is rasterised
per net; a per-connection Euclidean distance field gives, for a track of width w, the cells whose
centre keeps w/2 + 0.15 mm + 0.036 mm (raster allowance; obstacles mark every cell they touch) from foreign copper. Vias need the same on
all six layers. A* runs over (cell, layer, direction) with 45-degree moves, a bend penalty, a via
cost and a mild In2 penalty. Each finished route is rasterised before the next connection.
Pad entry (ODD JOBS 87/88): inside a 0.35 mm margin a track runs only on the pad's centre lines (and, for
plated through-holes, its 45-degree diagonals) and ends at the pad centre; an arm already carrying a track is taken. No 135-degree corners. A join onto an existing
track is square to it and at least 0.8 mm from any pad. No via in or beside an own pad.
The output is a draft for DRC and manual review (ODD JOBS 191), not an approved layout.
"""
import json, math, os, re, sys
from pathlib import Path
import numpy as np
DEBUG = os.environ.get('GR_DEBUG')      # directory for per-attempt clear-cell mask images (diagnosis only)
from scipy.ndimage import distance_transform_edt
from grid_kernel import _astar, _DX, _DY   # compiled search (numba)
from grid_smooth import smooth_run
from PIL import Image, ImageDraw

from netrules import ROOT, CACHE, DRC_JSON, POWER, SWITCH, width_for, priority, ORIGIN, PLANE_NETS, INNER_OK, INNER_FAST
RES, CLR = 0.05, 0.15
RASTER = RES * 0.7072          # obstacles mark every cell they touch: copper lies within this of a marked centre
VIA_D, VIA_COST, BEND = 0.6, float(os.environ.get('GR_VIACOST', 90.0)), (0.0, 3.0, 8.0, 20.0, 200.0)  # in cells: via = 2.2 mm of track; bend cost per 45-degree step
from netrules import COPPER, PLANE_LAYERS, SLOW_LAYER
LAYERS = list(COPPER); NLAY = len(LAYERS)
SL = LAYERS.index(SLOW_LAYER); BL = LAYERS.index("B")
ROUTE = tuple(i for i, n in enumerate(LAYERS) if n not in PLANE_LAYERS)
L3COST = float(os.environ.get('GR_L3COST', 2.0))
LCOST = {i: (L3COST if i == SL else 1.0) for i in ROUTE}
# MAO 4 layers: In1 the solid GND plane (vias only). In2 (L3) carries power regions and takes only slow nets
# (netrules.INNER_OK), at a cost that keeps it for crossings, never inside a power-region core (l3_forbid).
DIRS = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]

dump = json.loads((CACHE / 'grid-dump.json').read_text())
x0, y0, x1, y1 = dump['edge']
W, H = int(math.ceil((x1 - x0) / RES)), int(math.ceil((y1 - y0) / RES))
nets = {'': -2}
def nid(n):
    if n not in nets: nets[n] = len(nets)
    return nets[n]
GND_ID = nid('GND')
lab = np.zeros((NLAY, H, W), np.int32)      # inflated: every cell copper touches (clearance)
exact = np.zeros((NLAY, H, W), np.int32)    # shrunk: cells whose centre is 0.03 mm inside copper (connectivity)
via_forbid = np.zeros((H, W), bool)
ko = np.zeros((NLAY, H, W), bool)          # track keep-outs, kept apart: own-net copper under them must not open them
cx = x0 + (np.arange(W) + 0.5) * RES; cy = y0 + (np.arange(H) + 0.5) * RES

def ix(x): return int(round((x - x0) / RES - 0.5))
def iy(y): return int(round((y - y0) / RES - 0.5))
def window(a, b, c, d, pad=0.0):
    return max(0, ix(a - pad) - 1), max(0, iy(b - pad) - 1), min(W, ix(c + pad) + 2), min(H, iy(d + pad) + 2)

def fill_box(layers, box, n, r=None, cx0=None, cy0=None, connect=True):
    e = RES / 2                               # a cell touches the box iff its centre is within RES/2 per axis
    i0, j0, i1, j1 = window(*box, e)
    X, Y = np.meshgrid(cx[i0:i1], cy[j0:j1])
    if r:
        m = (X - cx0) ** 2 + (Y - cy0) ** 2 <= (r + RASTER) ** 2
    else:
        m = (X >= box[0] - e) & (X <= box[2] + e) & (Y >= box[1] - e) & (Y <= box[3] + e)
    for l in layers: lab[l, j0:j1, i0:i1][m] = n
    if not connect: return (i0, j0, m)
    s_ = 0.03
    mi = ((X - cx0) ** 2 + (Y - cy0) ** 2 <= max(r - s_, 0.01) ** 2) if r else          ((X >= box[0] + s_) & (X <= box[2] - s_) & (Y >= box[1] + s_) & (Y <= box[3] - s_))
    for l in layers: exact[l, j0:j1, i0:i1][mi] = n
    return (i0, j0, m)

def capsule(ax, ay, bx, by, w):
    r = w / 2
    i0, j0, i1, j1 = window(min(ax, bx), min(ay, by), max(ax, bx), max(ay, by), r)
    X, Y = np.meshgrid(cx[i0:i1], cy[j0:j1])
    dx, dy = bx - ax, by - ay; L2 = dx * dx + dy * dy
    t = np.clip(((X - ax) * dx + (Y - ay) * dy) / L2, 0, 1) if L2 > 0 else 0
    return i0, j0, (X - ax - t * dx) ** 2 + (Y - ay - t * dy) ** 2 <= r * r

def draw_track(l, ax, ay, bx, by, w, n):
    i0, j0, m = capsule(ax, ay, bx, by, w + 2 * RASTER); lab[l, j0:j0 + m.shape[0], i0:i0 + m.shape[1]][m] = n
    i0, j0, m = capsule(ax, ay, bx, by, max(w - 0.06, 0.02)); exact[l, j0:j0 + m.shape[0], i0:i0 + m.shape[1]][m] = n

def draw_via(x, y, d, n):
    for l in range(NLAY): fill_box([l], (x - d / 2, y - d / 2, x + d / 2, y + d / 2), n, d / 2, x, y)

L = {n: i for i, n in enumerate(LAYERS)}
HOLE_CLR = 0.25                          # board rule min_hole_clearance (copper to a non-plated hole)
from scipy.ndimage import binary_dilation, binary_erosion

def fill_polys(layers, polys, n):
    """A pad by its real outline: every cell it touches (one cell of dilation) is copper for clearance, its
    eroded interior for connectivity."""
    xs = [x for o in polys for x, _ in o]; ys = [y for o in polys for _, y in o]
    i0, j0, i1, j1 = window(min(xs), min(ys), max(xs), max(ys), 0.2)
    img = Image.new('1', (i1 - i0, j1 - j0), 0)
    for o in polys:
        ImageDraw.Draw(img).polygon([((x - x0) / RES - 0.5 - i0, (y - y0) / RES - 0.5 - j0) for x, y in o], fill=1, outline=1)
    m = np.array(img, bool)
    for l in layers:
        lab[l, j0:j1, i0:i1][binary_dilation(m)] = n
        exact[l, j0:j1, i0:i1][binary_erosion(m)] = n

for p in dump['pads']:
    ls = [L[l] for l in p['layers']]
    if p.get('polys') and p['kind'] == 'SMD':
        fill_polys(ls, p['polys'], nid(p['net']))
        continue
    extra = max(p.get('clr', 0.0), HOLE_CLR if p['kind'] == 'NPTH' else 0.0) - CLR   # rules stricter than 0.15 mm
    if extra > 0:                         # inflate the obstacle only; connectivity (exact) stays the real pad
        b_ = p['box']; r_ = p.get('r')
        fill_box(ls, (b_[0] - extra, b_[1] - extra, b_[2] + extra, b_[3] + extra), -2, r_ + extra if r_ else None,
                 p['x'], p['y'], connect=False)
    fill_box(ls, p['box'], nid(p['net']) if p['kind'] != 'NPTH' else -2, p.get('r'), p['x'], p['y'])
SPOKE_M = 0.45                           # through-hole GND pads keep room for two thermal spokes on F and B
for p in dump['pads']:
    if p['kind'] == 'PTH' and p['net'] == 'GND':
        bx = p['box']; i0, j0, i1, j1 = window(bx[0], bx[1], bx[2], bx[3], SPOKE_M)
        X, Y = np.meshgrid(cx[i0:i1], cy[j0:j1])
        m = (X >= bx[0] - SPOKE_M) & (X <= bx[2] + SPOKE_M) & (Y >= bx[1] - SPOKE_M) & (Y <= bx[3] + SPOKE_M)
        for l in (L['F'], L['B']):
            sl = lab[l, j0:j1, i0:i1]; sl[m & (sl == 0)] = nid('GND')
for t in dump['tracks']: draw_track(L[t['layer']], t['x1'], t['y1'], t['x2'], t['y2'], t['w'], nid(t['net']))
for v in dump['vias']: draw_via(v['x'], v['y'], v['d'], nid(v['net']))
for k in dump['keepouts']:
    img = Image.new('1', (W, H), 0)
    ImageDraw.Draw(img).polygon([((x - x0) / RES - 0.5, (y - y0) / RES - 0.5) for x, y in k['pts']], fill=1, outline=1)
    m = np.array(img, bool)
    if k['tracks']:
        for l in (L[n] for n in k['layers'] if n not in PLANE_LAYERS):   # planes carry no tracks; vias must pass them
            sl = lab[l]; sl[m & (sl == 0)] = -1; ko[l] |= m
    if k['vias']: via_forbid |= distance_transform_edt(~m) * RES < VIA_D / 2 + 0.02   # the ring stays out, not just the centre
HOLE_GAP = 0.25                          # board rule min_hole_to_hole
holes = [(v['x'], v['y'], 0.15) for v in dump['vias']] +         [(p['x'], p['y'], p['drill'] / 2) for p in dump['pads'] if p['kind'] != 'SMD']
drill = np.zeros((H, W), bool)          # cells where copper joins all layers (vias, plated holes)
hole_block = np.zeros((H, W), bool)     # new via drill centre not allowed here
def mark_hole(x, y, r, conductive=True):
    i0, j0, i1, j1 = window(x - 1.5, y - 1.5, x + 1.5, y + 1.5)
    X, Y = np.meshgrid(cx[i0:i1], cy[j0:j1]); d2 = (X - x) ** 2 + (Y - y) ** 2
    hole_block[j0:j1, i0:i1] |= d2 <= (r + 0.15 + HOLE_GAP + RASTER) ** 2
    if conductive: drill[j0:j1, i0:i1] |= d2 <= max(r, 0.15) ** 2
for v in dump['vias']: mark_hole(v['x'], v['y'], 0.15)
for p in dump['pads']:
    if p['kind'] != 'SMD': mark_hole(p['x'], p['y'], p['drill'] / 2, p['kind'] == 'PTH')
edge = 0.55
band = np.zeros((H, W), bool); e = int(math.ceil(edge / RES))
band[:e] = band[-e:] = True; band[:, :e] = band[:, -e:] = True
# MAO: the board is a disc with an antenna notch, not its bounding box. Block every cell outside
# the outline minus the edge band (copper-to-edge rule 0.3 mm + track half-width margin).
import mechanical as _m
_X, _Y = np.meshgrid(cx, cy)
outside = (np.hypot(_X, _Y) > _m.PCB_R - edge) | ((np.abs(_X) < _m.NOTCH_W / 2 + edge) & (_Y > _m.NOTCH_Y - edge))
band |= outside
for l in range(NLAY): lab[l][band & (lab[l] == 0)] = -1
via_forbid |= outside
_vedge = 1.3                             # a via ring keeps >= 1.0 mm from the milled edge (ODD JOBS 144)
via_forbid |= (np.hypot(_X, _Y) > _m.PCB_R - _vedge) | ((np.abs(_X) < _m.NOTCH_W / 2 + _vedge) & (_Y > _m.NOTCH_Y - _vedge))

l3_forbid = np.zeros((H, W), bool)       # L3 power-region cores (power_regions.py): signals stay out
import power_regions as _pr
for poly in _pr.cores():
    img = Image.new('1', (W, H), 0)
    ImageDraw.Draw(img).polygon([((x - x0) / RES - 0.5, (y - y0) / RES - 0.5) for x, y in poly], fill=1, outline=1)
    l3_forbid |= np.array(img, bool)

NO_L3 = set()                            # nets whose L3 route would cut a plane piece off: F and B only

def layer_ok(l, net, okl, j0, j1, i0, i1):
    if l != SL: return okl
    if not INNER_OK.match(net) or net in NO_L3: return np.zeros_like(okl)
    return okl & ~l3_forbid[j0:j1, i0:i1]

# ---- L3 plane continuity (brief: slow lines may cross a region but never cut a piece off) ----
# Each L3 plane (the +3V3 default fill and every power region) is modelled as its zone would fill: inside its
# outline (minus higher-priority regions and their 0.2 mm clearance), 0.2 mm from foreign copper, at least
# 0.2 mm wide. Centre cells of that fill are labelled; a plane's pieces are the labels its own vias touch.
# A plane's own outline is eroded by one extra grid cell (MARGIN): where two regions meet, the rasterised
# boundary can land a cell early, and a neck along it that is exactly at the limit fills thinner than the
# minimum width in KiCad and drops out, so the model must not count it as a joint.
# A route that raises any plane's piece count is undone and retried on F and B (plane_check.py has the
# final word on the filled board).
ZCLR, ZMIN = 0.2, 0.2
MARGIN = RES
def _poly(poly):
    img = Image.new('1', (W, H), 0)
    ImageDraw.Draw(img).polygon([((x - x0) / RES - 0.5, (y - y0) / RES - 0.5) for x, y in poly], fill=1, outline=1)
    return np.array(img, bool)
_inside = ~((np.hypot(_X, _Y) > _m.PCB_R - _m.POUR_EDGE) |
            ((np.abs(_X) < _m.NOTCH_W / 2 + _m.POUR_EDGE) & (_Y > _m.NOTCH_Y - _m.POUR_EDGE)))
PLANES, _taken = [], np.zeros((H, W), bool)
for _net, _prio, _outline, _ in sorted(_pr.zones(), key=lambda r: -r[1]):
    _a = _poly(_outline) & _inside & (distance_transform_edt(~_taken) * RES >= ZCLR)
    PLANES.append((_net, _a)); _taken |= _a
PLANES.append(('+3V3', _inside & (distance_transform_edt(~_taken) * RES >= ZCLR)))
PLANES = [(n_, a_ & (distance_transform_edt(a_) * RES >= ZMIN / 2 + MARGIN)) for n_, a_ in PLANES]
from scipy.ndimage import label as _label

def plane_pieces(net, area):
    n = nets.get(net, -99)
    foreign = (lab[SL] != 0) & (lab[SL] != n)
    centre = area & (distance_transform_edt(~foreign) * RES >= ZCLR + ZMIN / 2)
    own = [v for v in all_vias if v['net'] == net]
    for v in own:                                    # a via ring joins every piece of fill that touches it
        i0, j0, m = capsule(v['x'], v['y'], v['x'], v['y'], 2 * (v['d'] / 2 + ZMIN / 2 + RES))
        centre[j0:j0 + m.shape[0], i0:i0 + m.shape[1]] |= m
    lbl, _ = _label(centre)
    return {int(lbl[iy(v['y']), ix(v['x'])]) for v in own} - {0}

def plane_counts():
    return {n_: len(plane_pieces(n_, a_)) for n_, a_ in PLANES}

def stranded(net_):
    """Own vias of the plane `net_` outside its largest piece (diagnosis for an undone route)."""
    area = dict(PLANES)[net_]
    n = nets.get(net_, -99)
    foreign = (lab[SL] != 0) & (lab[SL] != n)
    centre = area & (distance_transform_edt(~foreign) * RES >= ZCLR + ZMIN / 2)
    own = [v for v in all_vias if v['net'] == net_]
    for v in own:
        i0, j0, m = capsule(v['x'], v['y'], v['x'], v['y'], 2 * (v['d'] / 2 + ZMIN / 2 + RES))
        centre[j0:j0 + m.shape[0], i0:i0 + m.shape[1]] |= m
    lbl, _ = _label(centre)
    tags = [int(lbl[iy(v['y']), ix(v['x'])]) for v in own]
    main_ = max(set(tags), key=tags.count)
    return [(round(v['x'], 2), round(v['y'], 2)) for v, t in zip(own, tags) if t != main_]

POWER_IDS = np.array([i for k, i in nets.items() if POWER.match(k)] or [0])
SWITCH_IDS = np.array([i for k, i in nets.items() if SWITCH.match(k)] or [0])
POWER_GAP, SWITCH_GAP = 0.15, 0.5       # added to the 0.15 mm rule for logic tracks


# ---- connections from the DRC report ----
drc = json.loads(DRC_JSON.read_text(encoding='utf-8'))
pads = {(p['ref'], p['num']): p for p in dump['pads']}
def item(it):
    d = it['description']; x, y = it['pos']['x'] - ORIGIN, it['pos']['y'] - ORIGIN
    m = re.match(r'(?:PTH )?[Pp]ad (\S+) \[(.*?)\] of (\S+)', d)
    if m:
        p = pads[(m.group(3), m.group(1))]
        return {'kind': 'pad', 'net': p['net'], 'ref': p['ref'], 'layers': [L[l] for l in p['layers'] if L[l] in ROUTE],
                'box': p['box'], 'r': p.get('r'), 'x': p['x'], 'y': p['y']}
    m = re.match(r'Track \[(.*?)\] on (\S+)', d)
    if m:
        net, layer = m.group(1), m.group(2)[0]
        best = min((t for t in dump['tracks'] if t['net'] == net and t['layer'] == ('F' if layer == 'F' else 'B' if layer == 'B' else 'I' + m.group(2)[2])),
                   key=lambda t: seg_dist(x, y, t))
        return {'kind': 'track', 'net': net, 'layers': [L[best['layer']]], 't': best,
                'box': [min(best['x1'], best['x2']), min(best['y1'], best['y2']), max(best['x1'], best['x2']), max(best['y1'], best['y2'])]}
    m = re.match(r'Via \[(.*?)\]', d)
    if m:
        v = min((v for v in dump['vias'] if v['net'] == m.group(1)), key=lambda v: (v['x'] - x) ** 2 + (v['y'] - y) ** 2)
        return {'kind': 'via', 'net': v['net'], 'layers': list(ROUTE), 'box': [v['x'] - .3, v['y'] - .3, v['x'] + .3, v['y'] + .3],
                'x': v['x'], 'y': v['y'], 'r': .3}
    return None

def seg_dist(x, y, t):
    ax, ay, bx, by = t['x1'], t['y1'], t['x2'], t['y2']; dx, dy = bx - ax, by - ay; L2 = dx * dx + dy * dy
    u = 0 if L2 == 0 else max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / L2))
    return math.hypot(x - ax - u * dx, y - ay - u * dy)

def cells_of(it):
    out = set()
    if it['kind'] == 'track':
        t = it['t']; i0, j0, m = capsule(t['x1'], t['y1'], t['x2'], t['y2'], max(t['w'] - 0.1, 0.1))
        js, is_ = np.nonzero(m)
        for l in it['layers']: out |= {(l, j0 + j, i0 + i) for j, i in zip(js, is_)}
    else:
        b = it['box']; r = it.get('r')
        shrink = [b[0] + .05, b[1] + .05, b[2] - .05, b[3] - .05]
        i0, j0, i1, j1 = window(*shrink)
        X, Y = np.meshgrid(cx[i0:i1], cy[j0:j1])
        m = ((X - it['x']) ** 2 + (Y - it['y']) ** 2 <= (r - .05) ** 2) if r else \
            ((X >= shrink[0]) & (X <= shrink[2]) & (Y >= shrink[1]) & (Y <= shrink[3]))
        js, is_ = np.nonzero(m)
        for l in it['layers']: out |= {(l, j0 + j, i0 + i) for j, i in zip(js, is_)}
    return out

def component(seed, n, i0, j0, i1, j1):
    """Own-net copper connected to seed inside the window; layers join at vias and plated holes."""
    seen = {c for c in seed if j0 <= c[1] < j1 and i0 <= c[2] < i1}
    stack = list(seen)
    while stack:
        l, y, x = stack.pop()
        nb = [(l, y + 1, x), (l, y - 1, x), (l, y, x + 1), (l, y, x - 1)]
        if drill[y, x]: nb += [(l2, y, x) for l2 in ROUTE if l2 != l]
        for c in nb:
            if c in seen or not (j0 <= c[1] < j1 and i0 <= c[2] < i1): continue
            if exact[c[0], c[1], c[2]] != n: continue
            seen.add(c); stack.append(c)
    return seen

ENTRY_M = 0.35          # pad margin: inside it a track runs only on the pad's own centre lines
JOIN_M = 0.8            # a join onto an existing track keeps this far from any pad of the net
VIA_M = 0.45            # via centre to own pad edge: the 0.6 mm via ring stays 0.15 mm off the pad
TRACK_JOIN = 6.0        # cost (cells) of ending on a track instead of a pad or via centre
OWN_PEN = 2.0           # cost per cell of running over existing same-net copper (no overlapping tracks)
NEAR_PEN, NEAR_BAND = 1.5, 3   # soft cost per cell within 0.15 mm beyond the clearance (no hugging)
BROAD_PEN = 6.0         # cost per cell of running on L3 under, or on B over, another net's copper on the other of
                        # the two (L3-L4 is the thin 1080 prepreg: stacked lines couple; ODD JOBS 124). A right-angle
                        # crossing costs a few cells, a 5 mm parallel run about four vias
SEARCH_LIMIT, H_WEIGHT = int(os.environ.get('GR_LIMIT', 30_000_000)), 2.0   # GR_LIMIT raises the budget for one long run    # state pops per attempt; weighted A*: bend and via costs keep paths tidy
MIN_RUN = 6             # grid steps between two bends (0.30 mm straight, 0.42 mm diagonal): no micro-jogs
pads_by_net = {}
for p in dump['pads']:
    if p['kind'] != 'NPTH' and p['net']: pads_by_net.setdefault(p['net'], []).append(p)
all_tracks = [dict(t, l=L[t['layer']]) for t in dump['tracks']]     # grows with each route
all_vias = [dict(v) for v in dump['vias']]

def pad_zones(net, l, i0, j0, i1, j1, arms_block):
    """block: cells near own pads off the pad centre lines; near: cells within JOIN_M of own pads."""
    X, Y = np.meshgrid(cx[i0:i1], cy[j0:j1])
    block = np.zeros(X.shape, bool); near = np.zeros(X.shape, bool); vnear = np.zeros(X.shape, bool)
    own = np.zeros(X.shape, bool)
    for t in all_tracks:
        if t['net'] != net or t['l'] != l: continue
        a, b = (t['x1'], t['y1']), (t['x2'], t['y2'])
        if max(a[0], b[0]) < cx[i0] - 1 or min(a[0], b[0]) > cx[i1 - 1] + 1 or \
           max(a[1], b[1]) < cy[j0] - 1 or min(a[1], b[1]) > cy[j1 - 1] + 1: continue
        dx, dy = b[0] - a[0], b[1] - a[1]; L2 = dx * dx + dy * dy
        tt = np.clip(((X - a[0]) * dx + (Y - a[1]) * dy) / L2, 0, 1) if L2 > 0 else 0
        own |= (X - a[0] - tt * dx) ** 2 + (Y - a[1] - tt * dy) ** 2 <= (t['w'] / 2) ** 2
    cols = np.arange(i0, i1)[None, :]; rows = np.arange(j0, j1)[:, None]
    for p in pads_by_net.get(net, []):
        if LAYERS[l] not in p['layers']: continue
        bx = p['box']
        if bx[2] + JOIN_M < cx[i0] or bx[0] - JOIN_M > cx[i1 - 1] or bx[3] + JOIN_M < cy[j0] or bx[1] - JOIN_M > cy[j1 - 1]: continue
        if p.get('r'):
            d2 = (X - p['x']) ** 2 + (Y - p['y']) ** 2
            inside = d2 <= p['r'] ** 2; R = d2 <= (p['r'] + ENTRY_M) ** 2; near |= d2 <= (p['r'] + JOIN_M) ** 2
            vnear |= d2 <= (p['r'] + VIA_M) ** 2
        else:
            def boxm(m): return (X >= bx[0] - m) & (X <= bx[2] + m) & (Y >= bx[1] - m) & (Y <= bx[3] + m)
            inside = boxm(0); R = boxm(ENTRY_M); near |= boxm(JOIN_M); vnear |= boxm(VIA_M)
        row = np.broadcast_to(rows == iy(p['y']), X.shape); col = np.broadcast_to(cols == ix(p['x']), X.shape)
        allowed = (row | col) & R
        if p['kind'] == 'PTH':      # through-hole pins may also leave at 45 degrees: no SMD fillet, and a header's
            di = cols - ix(p['x']); dj = rows - iy(p['y'])      # first row escapes between the second row's pins
            allowed |= np.broadcast_to((di == dj) | (di == -dj), X.shape) & R
        if arms_block:              # an arm that already carries a track is taken: no second track, no Y
            for arm in (row & (X > p['x']), row & (X < p['x']), col & (Y > p['y']), col & (Y < p['y'])):
                if (arm & R & ~inside & own).any(): allowed &= ~arm
        block |= R & ~allowed
    return block, near, own, vnear

def anchors(comp, net, l_ok, near, i0, j0, i1, j1):
    """cell -> (cost, exact end point or None, allowed headings or None) for one connected fragment."""
    out = {}
    for p in pads_by_net.get(net, []):
        c = (iy(p['y']), ix(p['x']))
        for l in ROUTE:
            if LAYERS[l] in p['layers'] and (l,) + c in comp: out[(l,) + c] = (0.0, (p['x'], p['y']), None)
    for v in all_vias:
        if v['net'] != net: continue
        c = (iy(v['y']), ix(v['x']))
        for l in ROUTE:
            if (l,) + c in comp: out[(l,) + c] = (0.0, (v['x'], v['y']), None)
    for t in all_tracks:
        if t['net'] != net or t['l'] not in l_ok: continue
        dx, dy = t['x2'] - t['x1'], t['y2'] - t['y1']; Lt = math.hypot(dx, dy)
        if Lt < 0.1: continue
        perp = {k for k, (a, b) in enumerate(DIRS) if abs(a * dx + b * dy) < 0.08 * Lt * math.hypot(a, b)}
        if not perp: continue                                 # joins stay square to the track
        n_ = int(Lt / (RES / 2)) + 1
        for s in range(n_ + 1):
            x_, y_ = t['x1'] + dx * s / n_, t['y1'] + dy * s / n_
            c = (t['l'], iy(y_), ix(x_))
            if c in out or c not in comp or not (j0 <= c[1] < j1 and i0 <= c[2] < i1): continue
            if near[c[0]][c[1] - j0, c[2] - i0]: continue
            out[c] = (TRACK_JOIN, None, perp)
    return out

def route(a, b, net, w, pad_mm, keep=True):
    min_run = MIN_RUN                    # the relaxed attempt drops keep-away only, never the run length
    n = nets[net]
    bx0 = min(a['box'][0], b['box'][0]); by0 = min(a['box'][1], b['box'][1])
    bx1 = max(a['box'][2], b['box'][2]); by1 = max(a['box'][3], b['box'][3])
    i0, j0, i1, j1 = window(bx0, by0, bx1, by1, pad_mm)
    need = (w / 2 + CLR + RASTER) / RES; vneed = (VIA_D / 2 + CLR + RASTER) / RES + 1   # + one cell: via-to-via measured 0.11 mm short
    ok, pen, near = {}, {}, {}; vok = ~via_forbid[j0:j1, i0:i1] & ~hole_block[j0:j1, i0:i1]
    signal = keep and w <= 0.25 and not POWER.match(net)
    for l in range(NLAY):
        sub = lab[l, j0:j1, i0:i1]
        foreign = (sub != 0) & (sub != n)
        d = distance_transform_edt(~foreign)
        if l in ROUTE:
            ok[l] = d >= need
            if ko[l, j0:j1, i0:i1].any(): ok[l] &= distance_transform_edt(~ko[l, j0:j1, i0:i1]) >= (w / 2 + RASTER) / RES
            if signal:                     # ODD JOBS 8/86: logic keeps clear of power and switch nodes
                pw = np.isin(sub, POWER_IDS) & foreign; sw = np.isin(sub, SWITCH_IDS) & foreign
                if pw.any(): ok[l] &= distance_transform_edt(~pw) >= need + POWER_GAP / RES
                if sw.any(): ok[l] &= distance_transform_edt(~sw) >= need + SWITCH_GAP / RES
            block, near[l], own, vnear = pad_zones(net, l, i0, j0, i1, j1, keep)
            ok[l] &= ~block
            ok[l] = layer_ok(l, net, ok[l], j0, j1, i0, i1)
            vok &= ~block & ~vnear         # no via in or touching an own pad
            pen[l] = np.where(d < need + NEAR_BAND, NEAR_PEN, 0.0) + np.where(own, OWN_PEN, 0.0)
            if l in (SL, BL):              # broadside: the other layer of the L3/L4 pair, other nets, GND excepted
                osub = lab[BL if l == SL else SL, j0:j1, i0:i1]
                oth = (osub > 0) & (osub != n) & (osub != GND_ID)
                if oth.any():
                    pen[l] = pen[l] + np.where(distance_transform_edt(~oth) * RES < w / 2 + 0.1, BROAD_PEN, 0.0)
        vok &= d >= vneed
    global SMOOTH_CTX; SMOOTH_CTX = (ok, i0, j0)    # to_copper smooths the found path on these masks
    S = component(cells_of(a), n, i0, j0, i1, j1)
    G = component(cells_of(b), n, i0, j0, i1, j1) - S
    if DEBUG:                            # GR_DEBUG=<dir>: clear-cell masks per layer, start green, goal red, via sites blue tint
        for l in ROUTE:
            im = np.zeros((j1 - j0, i1 - i0, 3), np.uint8); im[ok[l]] = (255, 255, 255); im[~ok[l]] = (60, 60, 60)
            im[vok & ok[l]] = (210, 225, 255)
            for (ll, y, x) in S:
                if ll == l: im[y - j0, x - i0] = (0, 170, 0)
            for (ll, y, x) in G:
                if ll == l: im[y - j0, x - i0] = (220, 0, 0)
            Image.fromarray(im).resize(((i1 - i0) * 3, (j1 - j0) * 3), Image.NEAREST).save(
                f'{DEBUG}/{net}-{a.get("ref", a["kind"])}-{b.get("ref", b["kind"])}-w{w}-{"keep" if keep else "min"}-p{pad_mm}-{LAYERS[l]}.png')
            print('  debug mask', net, LAYERS[l], 'window', round(x0 + i0 * RES, 2), round(y0 + j0 * RES, 2), round(x0 + i1 * RES, 2), round(y0 + j1 * RES, 2), flush=True)
    if not S or not G:
        why['r'] = f'no start/goal cells in window (S={len(S)}, G={len(G)})'; return None
    Sa = anchors(S, net, ROUTE, near, i0, j0, i1, j1); Ga = anchors(G, net, ROUTE, near, i0, j0, i1, j1)
    if not Sa or not Ga:
        why['r'] = f'no pad, via or track anchor (S={len(Sa)}, G={len(Ga)})'; return None
    gx = [c[2] for c in Ga]; gy = [c[1] for c in Ga]; gx0, gx1, gy0, gy1 = min(gx), max(gx), min(gy), max(gy)
    NL, hh, ww = len(ROUTE), j1 - j0, i1 - i0
    if NL * hh * ww * 9 > 250_000_000:
        why['r'] = 'window too large'; return None
    li = {l: i for i, l in enumerate(ROUTE)}
    scost = np.full((NL, hh, ww), np.inf, np.float32); smask = np.zeros((NL, hh, ww), np.uint8)
    gcost = np.full((NL, hh, ww), np.inf, np.float32); gdir = np.zeros((NL, hh, ww), np.uint8)
    def bits(dirs): return 255 if dirs is None else sum(1 << k for k in dirs)
    for A, cost, mask in ((Sa, scost, smask), (Ga, gcost, gdir)):
        for (l, y, x), (c0, _, dirs) in A.items():
            if not (j0 <= y < j1 and i0 <= x < i1): continue
            q = (li[l], y - j0, x - i0)
            cost[q] = min(cost[q], c0); mask[q] |= bits(dirs)
    out, status = _astar(np.stack([ok[l] for l in ROUTE]), np.stack([pen[l] for l in ROUTE]).astype(np.float32),
                         np.ascontiguousarray(vok), scost, smask, gcost, gdir,
                         np.array([LCOST[l] for l in ROUTE], np.float64), np.array(BEND, np.float64),
                         float(VIA_COST), int(min_run), SEARCH_LIMIT, gx0 - i0, gx1 - i0, gy0 - j0, gy1 - j0, _DX, _DY, H_WEIGHT)
    if status:
        why['r'] = 'search limit' if status == 1 else 'exhausted'; return None
    path = [(ROUTE[l], y + j0, x + i0) for (l, y, x) in out[::-1]]
    return [c + (0,) for c in path], Sa[path[0]][1], Ga[path[-1]][1]

# ---- smoothing (ODD JOBS 88: no zig-zag) ----
# A weighted search returns the first acceptable path, which can staircase. Each layer run is
# rebuilt greedily: from a vertex, the farthest later vertex reachable by one straight-plus-45-degree
# connection on clear cells replaces everything between. The first and last directions of a route
# stay (pad-entry and square track joins); vias stay; no corner sharper than 90 degrees; no new run
# shorter than MIN_RUN cells.
SMOOTH_MIN = MIN_RUN * RES - 1e-6

def masks(net, w, box, keep, pad_mm=2.0):
    """Clear-cell masks for one net and width around box: (ok per route layer, i0, j0)."""
    n = nets[net]
    i0, j0, i1, j1 = window(box[0], box[1], box[2], box[3], pad_mm)
    need = (w / 2 + CLR + RASTER) / RES
    signal = keep and w <= 0.25 and not POWER.match(net)
    ok = {}
    for l in ROUTE:
        sub = lab[l, j0:j1, i0:i1]
        foreign = (sub != 0) & (sub != n)
        ok[l] = distance_transform_edt(~foreign) >= need
        if ko[l, j0:j1, i0:i1].any(): ok[l] &= distance_transform_edt(~ko[l, j0:j1, i0:i1]) >= (w / 2 + RASTER) / RES
        if signal:
            pw = np.isin(sub, POWER_IDS) & foreign; sw = np.isin(sub, SWITCH_IDS) & foreign
            if pw.any(): ok[l] &= distance_transform_edt(~pw) >= need + POWER_GAP / RES
            if sw.any(): ok[l] &= distance_transform_edt(~sw) >= need + SWITCH_GAP / RES
        ok[l] &= ~pad_zones(net, l, i0, j0, i1, j1, keep)[0]
        ok[l] = layer_ok(l, net, ok[l], j0, j0 + ok[l].shape[0], i0, i0 + ok[l].shape[1])
    return ok, i0, j0

def smooth_polys(polys, ok, i0, j0):
    """polys: [(layer, [[x, y], ...]), ...] in route order; vias sit at run boundaries."""
    out = []
    for k, (l, v) in enumerate(polys):
        out.append((l, [list(p) for p in smooth_run(v, ok[l], i0, j0, k == 0, k == len(polys) - 1,
                                                    lambda x, y: (ix(x), iy(y)), RES, SMOOTH_MIN)]))
    return out

SMOOTH_CTX = None

def snap(poly, T):
    """Move poly[0] onto T; slide the next corner along the following segment so every angle stays."""
    if T is None: return
    if len(poly) >= 3:
        q0, c, nx_ = poly[0], poly[1], poly[2]
        d = (c[0] - q0[0], c[1] - q0[1]); p = (nx_[0] - c[0], nx_[1] - c[1])
        cr = p[0] * d[1] - p[1] * d[0]
        if abs(cr) > 1e-12:
            t = ((T[0] - c[0]) * d[1] - (T[1] - c[1]) * d[0]) / cr
            nc = (c[0] + t * p[0], c[1] + t * p[1])
            nd = (nc[0] - T[0], nc[1] - T[1]); np_ = (nx_[0] - nc[0], nx_[1] - nc[1])
            if nd[0] * d[0] + nd[1] * d[1] > 0 and np_[0] * p[0] + np_[1] * p[1] > 0:
                c[0], c[1] = nc
    poly[0][0], poly[0][1] = T

def end_fix(polys, which, T):
    """Snap a route end onto the exact pad or via centre. A lone segment whose far end is a router via
    moves that via sideways instead of skewing; the run on the other side then slides its corner."""
    if T is None: return
    q = polys[which][1] if which == 0 else polys[which][1][::-1]      # same point objects, end first
    if len(q) == 2 and len(polys) > 1:
        o = q[0]; dv = (q[1][0] - o[0], q[1][1] - o[1]); L2 = dv[0] ** 2 + dv[1] ** 2
        e = (T[0] - o[0], T[1] - o[1]); u = (e[0] * dv[0] + e[1] * dv[1]) / L2 if L2 else 0
        nv = (q[1][0] + e[0] - u * dv[0], q[1][1] + e[1] - u * dv[1])
        nb = polys[1][1] if which == 0 else polys[-2][1][::-1]
        snap(nb, nv); q[1][0], q[1][1] = nv
    snap(q, T)

def sgn(u, v):
    """Grid direction from point u to v as signs, so a jump and single steps form one run."""
    dx, dy = round((v[1] - u[1]) / RES), round((v[2] - u[2]) / RES)
    return ((dx > 0) - (dx < 0), (dy > 0) - (dy < 0))

def to_copper(path, net, w, sxy=None, gxy=None):
    pts = [(l, x0 + (x + 0.5) * RES, y0 + (y + 0.5) * RES) for (l, y, x, _) in path]
    runs, cur = [], [pts[0]]
    for p in pts[1:]:
        if p[0] != cur[-1][0]: runs.append(cur); cur = [p]
        else: cur.append(p)
    runs.append(cur)
    polys = []
    for r in runs:
        v = [r[0]]
        for a_, b_, c_ in zip(r, r[1:], r[2:]):
            if sgn(a_, b_) != sgn(b_, c_):
                v.append(b_)
        if len(r) > 1: v.append(r[-1])
        polys.append((r[0][0], [[q[1], q[2]] for q in v]))
    end_fix(polys, 0, sxy); end_fix(polys, -1, gxy)
    if SMOOTH_CTX: polys = smooth_polys(polys, *SMOOTH_CTX)
    segs = [(l, a_[0], a_[1], b_[0], b_[1]) for l, v in polys for a_, b_ in zip(v, v[1:]) if a_ != b_]
    vias = [tuple(v[0]) for l, v in polys[1:]]
    n = nets[net]
    for l, ax, ay, bx, by in segs:
        draw_track(l, ax, ay, bx, by, w, n)
        all_tracks.append({'net': net, 'layer': LAYERS[l], 'l': l, 'x1': ax, 'y1': ay, 'x2': bx, 'y2': by, 'w': w})
    for x, y in vias:
        draw_via(x, y, VIA_D, n); mark_hole(x, y, 0.15); all_vias.append({'net': net, 'x': x, 'y': y, 'd': VIA_D})
    return {'net': net, 'width': w, 'segments': [[LAYERS[l], ax, ay, bx, by] for l, ax, ay, bx, by in segs],
            'vias': [[x, y] for x, y in vias]}

def main():
    global why
    conns = []
    for u in drc['unconnected_items']:
        a, b = item(u['items'][0]), item(u['items'][1])
        if not a or not b or a['net'] in PLANE_NETS: continue      # planes carry these through pad vias
        conns.append((a, b))
    def prio(c):
        net = c[0]['net']
        rank = priority(net)
        return (rank, math.hypot(c[0]['box'][0] - c[1]['box'][0], c[0]['box'][1] - c[1]['box'][1]))
    conns.sort(key=prio)
    pri = [n for n in os.environ.get('GR_PRIORITY', '').split(',') if n]   # MAO: designer's net order first
    def hop(c):                          # short local hops first: they have one natural path, buses go round them
        a_, b_ = c
        gx = max(0.0, max(a_['box'][0], b_['box'][0]) - min(a_['box'][2], b_['box'][2]))
        gy = max(0.0, max(a_['box'][1], b_['box'][1]) - min(a_['box'][3], b_['box'][3]))
        return 0 if math.hypot(gx, gy) < 3.0 else 1
    if pri:
        rank = {n: i for i, n in enumerate(pri)}
        conns.sort(key=lambda c: (hop(c), rank.get(c[0]['net'], len(rank)), prio(c)))
    only = sys.argv[1:] and set(sys.argv[1:])
    if only and os.environ.get('GR_ORDER') == 'argv':   # route the named nets in the order given
        rank = {n: i for i, n in enumerate(sys.argv[1:])}
        conns.sort(key=lambda c: (rank.get(c[0]['net'], len(rank)), prio(c)))
    routes, failed = [], []
    def save(r, f): (CACHE / os.environ.get('GR_OUT', 'grid-routes.json')).write_text(json.dumps({'routes': r, 'failed': f}, indent=1))
    skip = set(filter(None, os.environ.get('GR_SKIP_REF', '').split(',')))   # GR_SKIP_REF=U700: leave that part's pads for designed routes
    counts = plane_counts()
    print('L3 planes before routing (pieces with vias):', counts, flush=True)
    def attempt(a, b, net):
        for w in width_for(net, (a, b)):
            for keep in (True, False):
                for pad_mm in ((4.0, 10.0) if keep else (4.0, 10.0, 30.0)):
                    res = route(a, b, net, w, pad_mm, keep)
                    if res: return res, w, keep
        return None
    for a, b in conns:
        net = a['net']
        if only and net not in only: continue
        if a.get('ref') in skip or b.get('ref') in skip: continue
        done, via_retries = None, 0
        while True:
            got = attempt(a, b, net)
            if not got: break
            res, w, keep = got
            snap_ = (lab.copy(), exact.copy(), drill.copy(), hole_block.copy(), len(all_tracks), len(all_vias))
            done = to_copper(res[0], net, w, res[1], res[2]); done['keepaway'] = keep
            now = plane_counts()
            worse = {k: (counts[k], now[k]) for k in now if now[k] > counts[k]}
            if not worse: counts = now; break
            uses_l3 = any(s[0] == SLOW_LAYER for s in done['segments'])
            if (not uses_l3 or net in NO_L3) and (not done['vias'] or via_retries >= 3):
                print('  plane warning', net, worse, flush=True); counts = now; break     # plane_check reports it
            lab[:], exact[:], drill[:], hole_block[:] = snap_[0], snap_[1], snap_[2], snap_[3]
            del all_tracks[snap_[4]:]; del all_vias[snap_[5]:]
            if os.environ.get('GR_PLANE_DEBUG'):
                print('    stranded', {k: stranded(k) for k in worse}, flush=True)
            if not uses_l3 or net in NO_L3:          # its vias cut a neck of a plane: no via there, try again
                for vx, vy in done['vias']:
                    i0, j0, m = capsule(vx, vy, vx, vy, 1.4)
                    via_forbid[j0:j0 + m.shape[0], i0:i0 + m.shape[1]] |= m
                via_retries += 1
                print('  undone', net, 'vias would split', worse, '- retry without those via sites', flush=True)
            else:
                print('  undone', net, 'L3 route would split', worse, '- retry on F/B', flush=True)
                NO_L3.add(net)
            done = None
        NO_L3.discard(net)
        if done: routes.append(done); save(routes, failed); print('routed', net, done['width'], len(done['segments']), 'seg', len(done['vias']), 'via', '' if done['keepaway'] else 'MIN-CLEARANCE (review)', flush=True)
        else: failed.append(net); print('FAILED', net, a.get('ref', a['kind']), b.get('ref', b['kind']), why.get('r'), flush=True)
    (CACHE / os.environ.get('GR_OUT', 'grid-routes.json')).write_text(json.dumps({'routes': routes, 'failed': failed}, indent=1))
    print(len(routes), 'routed,', len(failed), 'failed')


why = {}
if __name__ == '__main__':
    main()
