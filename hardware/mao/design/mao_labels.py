# Ported from b5463/kino-d4 hardware/pcb/kino-d4-carrier-a0/design/assembly_labels.py @ 68aba75 (ODD JOBS PCB toolchain).
"""One readable assembly reference per component, beside its own part (KiCad 10 python).

MAO: the board is a disc with an antenna notch (edge test below); fasteners from mechanical.py;
fiducials, mounting holes and test pads carry no reference on silk (test pads get their function
name from silk.py, ODD JOBS 103); the KINO camera groups and explicit banks are gone.

ODD JOBS 91, 94, 95, 177. Designator policy. Rule 177 keeps references on prototypes for service; the
master prompt asks for no generic reference text everywhere, hiding designators only where service does
not need them. So a reference is on silk wherever a technician has to find the part on the bare board:
every IC (U), connector and spring (J), transistor (Q), diode and LED (D), the switch (SW), the microphone
(MK), the inductor (L), the touch electrodes (E), and every resistor or capacitor that the bring-up or
factory-test procedure names (read from docs/hardware/mao-bringup.md and mao-factory-test.md, so the
procedures and the board cannot drift apart). The other resistors and capacitors carry their reference on
Fab only, 0.5 mm at the part centre, in the assembly drawing that fab.py writes (outputs/fab/ASSEMBLY-*.pdf).
Each silk reference is placed beside its own part body: candidates sit
along each side of the part outline, 0.25 to 3 mm away, sliding in 0.25 mm steps, horizontal or
vertical. A candidate must keep
  - 0.15 mm from every part outline on that side (the outline includes the courtyard, so pads stay
    at least 0.4 mm clear), 0.2 mm from the through-holes of parts on the other side, 0.05 mm from every
    via (a tented via breaks the print), clear of the fasteners,
  - 0.5 mm from every other silkscreen text or marking, so neighbouring references read as
    separate words,
  - 0.5 mm inside the board edge.
Of those, the nearest wins, with penalties for a label not at least 0.3 mm nearer its own part than
any other part, for text across the part's long axis, and for sliding off the middle of
the part's side (each label lines up with its own part: centred on it where the spot is clear, and
centred spots are tried first); a spot not at least 0.1 mm nearer its own part than any other is never used: such references go to Fab. Horizontal text reads left to
right; vertical text reads bottom to top from the side it is on (front 90 degrees, back 270 degrees
mirrored, keep-upright off). 1.0 mm text first; 0.8 mm (the board minimum) where 1.0 mm finds no
spot or only an ambiguous one. Parts with the fewest clear spots are labelled first. The four camera
circuits share offsets where that leaves every one of them unambiguous. The XIAO socket references
sit outside the sockets, where the fitted modules do not hide them. Values stay in the BOM and properties. Documentation geometry only, never copper.
check_silk_text.py verifies the result.
"""
import json, math, os, re, sys
from collections import defaultdict
import pcbnew as pcb
from board import courtyard_boxes, ROOT, TARGET, pt
import mechanical as m

def pos(obj): return (obj.GetPosition().x / 1e6 - 50, obj.GetPosition().y / 1e6 - 50)
def rect(fp, pad=.12):
    r = fp.GetBoundingBox(False, False)
    return (r.GetX() / 1e6 - 50 - pad, r.GetY() / 1e6 - 50 - pad, r.GetRight() / 1e6 - 50 + pad, r.GetBottom() / 1e6 - 50 + pad)
def intersects(a, b): return a[0] < b[2] - 1e-5 and b[0] < a[2] - 1e-5 and a[1] < b[3] - 1e-5 and b[1] < a[3] - 1e-5
INSERT_CENTRES = [m.polar(m.MOUNT_R, a) for a in m.SCREW_ANGLES + (m.PEG_ANGLE,)]
def on_board(q, edge):
    """Box q wholly inside the disc and clear of the antenna notch, by edge mm."""
    for x, y in ((q[0], q[1]), (q[2], q[1]), (q[0], q[3]), (q[2], q[3])):
        if x * x + y * y > (m.PCB_R - edge) ** 2: return False
    return not (q[2] > -m.NOTCH_W / 2 - edge and q[0] < m.NOTCH_W / 2 + edge and q[3] > m.NOTCH_Y - edge)

prototype = True                 # MAO A0 is a prototype: references stay on silk (ODD JOBS 177)
SIZES = [(.8, .15)]             # type scale: identity 1.5 > connectors 1.2 > test pads 0.9 > references 0.8 (0.15 mm stroke)
BODY_GAP, TEXT_GAP, EDGE, AMBIGUITY = .15, .25, .5, .3
ALIGN = float(__import__('os').environ.get('LBL_ALIGN', .6))                     # score per mm a label sits off the middle of its part's side
GAPS = [.25 * i for i in range(1, 17)]
VERT = {'F': 90, 'B': 270}
INSIDE_OK = set()                # nothing inside a part outline: the XIAO socket references between the pin rows were hidden by the fitted modules

LEADERS = 'assembly reference leaders'
if '--placed' not in sys.argv:          # this script's leaders from an earlier run go first (they would block), in a
    b = pcb.LoadBoard(str(TARGET))      # pass of their own: Remove() leaves the other proxies unusable (SWIG)
    old = [g_ for g_ in b.Groups() if g_.GetName() == LEADERS]
    if old:
        for g_ in old:
            for it_ in list(g_.GetItems()):
                b.Remove(it_)
            b.Remove(g_)
        pcb.SaveBoard(str(TARGET), b)
    sys.stdout.flush()
    os.execv(sys.executable, [sys.executable, __file__, '--placed'])
b = pcb.LoadBoard(str(TARGET)); fs = {f.GetReference(): f for f in b.GetFootprints()}
MARKS = {r for r in fs if r.startswith(('FID', 'H', 'TP'))}   # fiducials, holes, test pads: obstacles, never labelled here
SILK_REF = ('U', 'J', 'Q', 'D', 'SW', 'MK', 'L', 'E')
SERVICE_DOCS = [ROOT.parents[1] / 'docs' / 'hardware' / n for n in ('mao-bringup.md', 'mao-factory-test.md')]
SERVICE = {m for d in SERVICE_DOCS if d.exists() for m in re.findall(r'\b[RC]\d{3}\b', d.read_text(encoding='utf-8'))}
POLICY_FAB = {r for r in fs if r not in MARKS and r not in SERVICE
              and ''.join(c for c in r if c.isalpha()) not in SILK_REF}   # R, C not named in a procedure
FAB = {}
side_of = lambda f: 'B' if f.IsFlipped() else 'F'
grow = lambda q, m: (q[0] - m, q[1] - m, q[2] + m, q[3] + m)
def box(r, m=0.): return (r.GetLeft() / 1e6 - 50 - m, r.GetTop() / 1e6 - 50 - m, r.GetRight() / 1e6 - 50 + m, r.GetBottom() / 1e6 - 50 + m)
def tbox(t, m=0.): return box(t.GetEffectiveTextShape().BBox(), m)
def dist(a, c): return math.hypot(max(0, a[0] - c[2], c[0] - a[2]), max(0, a[1] - c[3], c[1] - a[3]))

class Bins:
    """Boxes in 5 mm cells."""
    def __init__(s): s.d = defaultdict(list)
    def keys(s, q): return [(x, y) for x in range(math.floor(q[0] / 5), math.floor(q[2] / 5) + 1) for y in range(math.floor(q[1] / 5), math.floor(q[3] / 5) + 1)]
    def add(s, name, q):
        for k in s.keys(q): s.d[k].append((name, q))
    def hit(s, q, skip=None):
        return next((n for k in s.keys(q) for n, o in s.d[k] if n != skip and intersects(q, o)), None)
    def near(s, q):
        return {n: o for k in s.keys(q) for n, o in s.d[k]}
    def hits(s, q):
        return {n for k in s.keys(q) for n, o in s.d[k] if intersects(q, o)}
    def remove(s, name):
        out = None
        for k in s.d:
            keep = [(n, o) for n, o in s.d[k] if n != name]
            if len(keep) != len(s.d[k]): out = next(o for n, o in s.d[k] if n == name)
            s.d[k] = keep
        return out
hard = {'F': Bins(), 'B': Bins()}       # part bodies, pads, through-holes, fasteners
text = {'F': Bins(), 'B': Bins()}       # silkscreen texts and markings, each grown by TEXT_GAP
bodies = {'F': Bins(), 'B': Bins()}     # bare part outlines, for the ambiguity test
own_rect = {r: rect(f, 0) for r, f in fs.items()}
own_boxes = {r: [grow(q, .12) for q in courtyard_boxes(f)] for r, f in fs.items()}
for r, f in fs.items():
    for g in list(f.GraphicalItems()):   # stock library centre references duplicate the reference field
        if isinstance(g, pcb.PCB_TEXT) and g.GetText() in ('${REFERENCE}', '%R'): g.SetText(''); g.SetVisible(False)
    f.Value().SetVisible(False)
    s = side_of(f)
    for q in courtyard_boxes(f): q = grow(q, .12); hard[s].add(r, q); bodies[s].add(r, q)
    for p in f.Pads():
        if p.GetAttribute() in (pcb.PAD_ATTRIB_PTH, pcb.PAD_ATTRIB_NPTH): hard['F' if s == 'B' else 'B'].add(r + ' hole', box(p.GetBoundingBox(), .05))
        if r in INSIDE_OK: hard[s].add(r + ' pad', box(p.GetBoundingBox(), .05))
for d in b.GetDrawings():
    if d.GetLayer() in (pcb.F_SilkS, pcb.B_SilkS):
        text['B' if d.GetLayer() == pcb.B_SilkS else 'F'].add('board marking', tbox(d, TEXT_GAP) if isinstance(d, pcb.PCB_TEXT) else box(d.GetBoundingBox(), TEXT_GAP))
for x, y in INSERT_CENTRES:
    for s in 'FB': hard[s].add('fastener', (x - 3.5, y - 3.5, x + 3.5, y + 3.5))
sl_ = m.TAIL_SLOT                       # the display-tail slot (Edge.Cuts inside the board): 0.5 mm clear
for s in 'FB': hard[s].add('slot', (sl_[0] - .5, sl_[1] - .5, sl_[2] + .5, sl_[3] + .5))
for t_ in b.GetTracks():                # tented vias print through: a reference keeps 0.05 mm off them (the box is
    if isinstance(t_, pcb.PCB_VIA):     # shrunk by BODY_GAP - 0.05 because candidates are grown by BODY_GAP)
        x, y = pos(t_); r_ = pcb.ToMM(t_.GetWidth(pcb.F_Cu)) / 2 - (BODY_GAP - .05)
        for s in 'FB': hard[s].add('via', (x - r_, y - r_, x + r_, y + r_))
for r in MARKS:                         # fiducials: no silk within 1 mm of the 2 mm mask opening
    if r.startswith('FID'):
        x, y = pos(fs[r]); hard[side_of(fs[r])].add(r + ' clear', (x - 2.05, y - 2.05, x + 2.05, y + 2.05))
for r in MARKS:                         # nothing of theirs on silk: reference to Fab at the part centre
    f = fs[r]; t = f.Reference(); t.SetLayer(pcb.B_Fab if f.IsFlipped() else pcb.F_Fab); t.SetPosition(f.GetPosition())
    t.SetVisible(True); t.SetMirrored(f.IsFlipped()); t.SetKeepUpright(False); t.SetTextAngle(pcb.EDA_ANGLE(0, pcb.DEGREES_T))
    t.SetTextSize(pt(.6, .6)); t.SetTextThickness(pcb.FromMM(.09))
    t.SetHorizJustify(pcb.GR_TEXT_H_ALIGN_CENTER); t.SetVertJustify(pcb.GR_TEXT_V_ALIGN_CENTER)
for r in sorted(POLICY_FAB):            # passives: assembly-drawing reference, small, at the part centre, along the part
    f = fs[r]; t = f.Reference(); s_ = 'B' if f.IsFlipped() else 'F'
    t.SetVisible(True); t.SetLayer(pcb.B_Fab if s_ == 'B' else pcb.F_Fab); t.SetMirrored(s_ == 'B'); t.SetKeepUpright(False)
    t.SetTextSize(pt(.5, .5)); t.SetTextThickness(pcb.FromMM(.08))
    t.SetHorizJustify(pcb.GR_TEXT_H_ALIGN_CENTER); t.SetVertJustify(pcb.GR_TEXT_V_ALIGN_CENTER)
    o = rect(f, 0); t.SetTextAngle(pcb.EDA_ANGLE(VERT[s_] if o[3] - o[1] > o[2] - o[0] else 0, pcb.DEGREES_T))
    t.SetPosition(f.GetPosition())

def style(f, size):
    t = f.Reference(); s = side_of(f)
    t.SetVisible(True); t.SetMirrored(s == 'B'); t.SetKeepUpright(False)
    t.SetLayer((pcb.B_SilkS if s == 'B' else pcb.F_SilkS) if prototype else (pcb.B_Fab if s == 'B' else pcb.F_Fab))
    t.SetTextSize(pt(size[0], size[0])); t.SetTextThickness(pcb.FromMM(size[1]))
    t.SetHorizJustify(pcb.GR_TEXT_H_ALIGN_CENTER); t.SetVertJustify(pcb.GR_TEXT_V_ALIGN_CENTER)
def shape(f, vertical):
    """Text box width, height and box-centre offset from the text position."""
    t = f.Reference(); x, y = pos(f)
    t.SetTextAngle(pcb.EDA_ANGLE(VERT[side_of(f)] if vertical else 0, pcb.DEGREES_T)); t.SetPosition(pt(50 + x, 50 + y))
    q = tbox(t); return q[2] - q[0], q[3] - q[1], (q[0] + q[2]) / 2 - x, (q[1] + q[3]) / 2 - y
def candidates(r, w, h):
    """(key, box centre, least distance to own outline), nearest first; keys match across identical parts."""
    o = own_rect[r]; cx, cy = (o[0] + o[2]) / 2, (o[1] + o[3]) / 2
    if r in INSIDE_OK:
        for kx in range(-24, 25):
            for ky in range(-24, 25):
                c = (cx + kx * .25, cy + ky * .25)
                if o[0] <= c[0] - w / 2 and c[0] + w / 2 <= o[2] and o[1] <= c[1] - h / 2 and c[1] + h / 2 <= o[3]: yield ('in', kx, ky), c, 0.
    kx, ky = int(((o[2] - o[0]) / 2 + w / 2) / .25), int(((o[3] - o[1]) / 2 + h / 2) / .25)
    centred = lambda n: sorted(range(-n, n + 1), key=abs)    # 0, -1, 1, -2, 2 ...: aligned spots first
    for g in GAPS:
        for k in centred(kx):
            yield ('S', g, k), (cx + k * .25, o[3] + g + h / 2), g
            yield ('N', g, k), (cx + k * .25, o[1] - g - h / 2), g
        for k in centred(ky):
            yield ('W', g, k), (o[0] - g - w / 2, cy + k * .25), g
            yield ('E', g, k), (o[2] + g + w / 2, cy + k * .25), g
def judge(r, s, w, h, c, vertical):
    """None if the box is blocked, else (score, ambiguity, distance to own part)."""
    q = (c[0] - w / 2, c[1] - h / 2, c[0] + w / 2, c[1] + h / 2)
    if not on_board(q, EDGE): return None
    if hard[s].hit(grow(q, BODY_GAP), r if r in INSIDE_OK else None) or text[s].hit(grow(q, TEXT_GAP)): return None
    o = own_rect[r]; d_own = min(dist(q, p) for p in own_boxes[r])   # the real outline (courtyard strips)
    d_other = min((dist(q, p) for k in bodies[s].keys(grow(q, d_own + AMBIGUITY + .5)) for n, p in bodies[s].d[k] if n != r), default=99.)
    amb = max(0., d_own + AMBIGUITY - d_other)
    if amb > AMBIGUITY - .1: return None                   # never as near another part as its own (0.1 mm margin)
    pw, ph = o[2] - o[0], o[3] - o[1]
    want = True if ph > 1.3 * pw else False if pw > 1.3 * ph else None
    pen = (.15 if vertical else 0.) if want is None else (0. if want == vertical else .4)
    return d_own + 4 * amb + pen + ALIGN * slide(o, q), amb, d_own
def slide(o, q):
    """How far the label's centre sits off the middle of the part side it is on (mm); a label that
    also leaves the part's span costs double for the part beyond it."""
    cx, cy = (o[0] + o[2]) / 2, (o[1] + o[3]) / 2; qx, qy = (q[0] + q[2]) / 2, (q[1] + q[3]) / 2
    if q[3] <= o[1] or q[1] >= o[3]: d, half = abs(qx - cx), (o[2] - o[0]) / 2     # above or below the part
    else: d, half = abs(qy - cy), (o[3] - o[1]) / 2                                 # beside it
    return d + max(0., d - half)

placed, unplaced, records = set(), [], []
def commit(f, vertical, c, offs, j, how, size):
    t = f.Reference(); s = side_of(f)
    t.SetTextAngle(pcb.EDA_ANGLE(VERT[s] if vertical else 0, pcb.DEGREES_T))
    t.SetPosition(pt(50 + c[0] - offs[0], 50 + c[1] - offs[1]))
    q = tbox(t); text[s].add(f.GetReference() + ' label', grow(q, TEXT_GAP)); placed.add(f.GetReference())
    if f.GetReference() in unplaced: unplaced.remove(f.GetReference())
    x, y = pos(f); tx, ty = pos(t)
    records.append({'reference': f.GetReference(), 'side': s, 'text_xy_mm': [round(tx, 3), round(ty, 3)],
                    'offset_mm': [round(tx - x, 3), round(ty - y, 3)], 'angle_deg': VERT[s] if vertical else 0,
                    'text_height_mm': size[0], 'bounding_box_mm': [round(v, 3) for v in q],
                    'distance_to_own_part_mm': round(j[2], 2), 'ambiguity_mm': round(j[1], 2), 'placement': how})
def place(refs, size, how='search', strict=False):
    ff = [fs[r] for r in refs]
    for f in ff: style(f, size)
    best = None
    for vertical in (False, True):
        shp = [shape(f, vertical) for f in ff]
        cands = [list(candidates(f.GetReference(), w, h)) for f, (w, h, _, _) in zip(ff, shp)]
        others = [dict((k, c) for k, c, _ in cd) for cd in cands[1:]]
        for key, c0, g in cands[0]:
            if best and g * len(ff) > best[0]: break          # nearest first: nothing further can win
            cs = [c0] + [o.get(key) for o in others]
            if None in cs: continue
            js = [judge(f.GetReference(), side_of(f), w, h, c, vertical) for f, (w, h, _, _), c in zip(ff, shp, cs)]
            if None in js or (strict and any(j[1] > 0 for j in js)): continue
            sc = sum(j[0] for j in js)
            if best is None or sc < best[0]: best = (sc, vertical, shp, cs, js)
    if best is None:
        if not strict: unplaced.extend(r for r in refs if r not in unplaced and r not in placed)
        return False
    _, vertical, shp, cs, js = best
    for f, s_, c, j in zip(ff, shp, cs, js): commit(f, vertical, c, s_[2:], j, how, size)
    return True

grouped, fab_only = set(), set(FAB)
unplaced.extend(sorted(FAB))
def options(r, size):
    f = fs[r]; style(f, size); n = 0
    for vertical in (False, True):
        w, h, _, _ = shape(f, vertical)
        n += sum(1 for _, c, _ in candidates(r, w, h) if judge(r, side_of(f), w, h, c, vertical))
    return n
for strict, size in [(True, s) for s in SIZES] + [(False, s) for s in SIZES]:   # unambiguous first, at either size
    todo = [r for r in fs if r not in placed and r not in fab_only and r not in MARKS and r not in FAB and r not in POLICY_FAB]
    count = {r: options(r, size) for r in todo}
    for r in sorted(todo, key=lambda r: (count[r], pos(fs[r])[1], pos(fs[r])[0])):
        if r not in placed: place([r], size, strict=strict)
# Repair: a reference with no clear spot may take an unambiguous one blocked by a single other label,
# if that label has another spot no more ambiguous than its own (at either size). Repeats until
# nothing more moves.
def best_spot(r, size, max_amb=0.):
    f = fs[r]; style(f, size); best = None
    for vertical in (False, True):
        shp = shape(f, vertical)
        for key, c, g in candidates(r, shp[0], shp[1]):
            if best and g > best[0]: break
            j = judge(r, side_of(f), shp[0], shp[1], c, vertical)
            if j and j[1] <= max_amb and (best is None or j[0] < best[0]): best = (j[0], vertical, shp, c, j, size)
    return best
def save(f):                             # copies: the bindings hand back live references to the text's own fields
    t = f.Reference(); q, z = t.GetPosition(), t.GetTextSize()
    return (pcb.VECTOR2I(q.x, q.y), t.GetTextAngle().AsDegrees(), pcb.VECTOR2I(z.x, z.y), int(t.GetTextThickness()), int(t.GetLayer()))
def restore(f, st):
    t = f.Reference(); t.SetPosition(st[0]); t.SetTextAngle(pcb.EDA_ANGLE(st[1], pcb.DEGREES_T))
    t.SetTextSize(st[2]); t.SetTextThickness(st[3]); t.SetLayer(st[4])
moved = True
while moved:
    moved = False
    for r in list(unplaced):
        if r in fab_only: continue
        f = fs[r]; s = side_of(f); done = False
        for size in SIZES:
            for vertical in (False, True):
                style(f, size); w, h, ox, oy = shape(f, vertical)
                for key, c, g in candidates(r, w, h):
                    q = (c[0] - w / 2, c[1] - h / 2, c[0] + w / 2, c[1] + h / 2)
                    blk = text[s].hits(grow(q, TEXT_GAP))
                    if len(blk) != 1 or not next(iter(blk)).endswith(' label'): continue
                    x = next(iter(blk))[:-6]
                    if x in grouped: continue                     # a camera group moves together or not at all
                    fx = fs[x]; st = save(fx)
                    old = text[s].remove(x + ' label')
                    j = judge(r, s, w, h, c, vertical)
                    if j is None or j[1] > 0: text[s].add(x + ' label', old); continue     # repaired labels are unambiguous
                    text[s].add(r + ' label', grow(q, TEXT_GAP))
                    x_amb = next((q_['ambiguity_mm'] for q_ in records if q_['reference'] == x), 0.)
                    alt = next((a for sz in SIZES for a in [best_spot(x, sz, x_amb)] if a), None)   # and the moved one no worse
                    text[s].remove(r + ' label')
                    if alt is None:
                        restore(fx, st); text[s].add(x + ' label', old); style(f, size); shape(f, vertical); continue
                    style(f, size); shape(f, vertical)
                    commit(f, vertical, c, (ox, oy), j, 'repair', size)
                    records[:] = [q_ for q_ in records if q_['reference'] != x]
                    _, xv, xs, xc, xj, xsz = alt
                    style(fx, xsz); shape(fx, xv)
                    commit(fx, xv, xc, xs[2:], xj, 'moved for ' + r, xsz)
                    done = moved = True; break
                if done: break
            if done: break
# Leaders (ODD JOBS 177, the silk policy): a part with no clear spot beside it gets its reference up to 6 mm away
# with a straight 0.15 mm silk leader from just off the text to 0.25 mm short of its own outline. The leader keeps
# 0.125 mm from every other part's outline, hole, via, fastener and silkscreen text and from other leaders, and
# the silk clearance from every footprint's silk graphics; the
# reference box keeps the usual clearances. Shortest leader wins; vertical text costs a little.
LEADER_W, LEADER_CLR = .15, .05
SILK_CLR = .15 + .03                     # the board's min_silk_clearance, edge to edge, plus margin
leader_grp = None
vias_c = [(pos(t_), pcb.ToMM(t_.GetWidth(pcb.F_Cu)) / 2) for t_ in b.GetTracks() if isinstance(t_, pcb.PCB_VIA)]
sgfx = {'F': Bins(), 'B': Bins()}       # every footprint's silk graphics (outlines, pin-1 marks)
for r_, f_ in fs.items():
    for g_ in f_.GraphicalItems():
        if g_.GetLayer() in (pcb.F_SilkS, pcb.B_SilkS) and not isinstance(g_, pcb.PCB_TEXT):
            sgfx['B' if g_.GetLayer() == pcb.B_SilkS else 'F'].add(r_ + ' silk', box(g_.GetBoundingBox()))
def leader_ok(s, r, a, c):
    L = math.hypot(c[0] - a[0], c[1] - a[1]); n = max(2, int(L / .1))
    e = LEADER_W / 2 + LEADER_CLR
    for i in range(n + 1):
        x = a[0] + (c[0] - a[0]) * i / n; y = a[1] + (c[1] - a[1]) * i / n
        q = (x - e, y - e, x + e, y + e)
        if not on_board(q, EDGE): return False
        h = hard[s].hits(q) - {r, r + ' hole'}
        if h or text[s].hit(q): return False
        if any(math.hypot(x - vx, y - vy) < vr + LEADER_W / 2 + .05 for (vx, vy), vr in vias_c): return False
        if sgfx[s].hit((x - LEADER_W / 2 - SILK_CLR, y - LEADER_W / 2 - SILK_CLR, x + LEADER_W / 2 + SILK_CLR, y + LEADER_W / 2 + SILK_CLR)):
            return False
    return True
def nearest(q, p):
    return (min(max(p[0], q[0]), q[2]), min(max(p[1], q[1]), q[3]))
for r in [r for r in list(unplaced) if r not in fab_only]:
    f = fs[r]; s = side_of(f); size = SIZES[0]; style(f, size); best = None
    o = own_rect[r]; cx, cy = (o[0] + o[2]) / 2, (o[1] + o[3]) / 2
    for vertical in (False, True):
        w, h, ox, oy = shape(f, vertical)
        for g in [.5 + .25 * i for i in range(35)]:
            kx, ky = int(((o[2] - o[0]) / 2 + w / 2 + 3) / .25), int(((o[3] - o[1]) / 2 + h / 2 + 3) / .25)
            cands = [(cx + k * .25, o[3] + g + h / 2) for k in range(-kx, kx + 1)] + \
                    [(cx + k * .25, o[1] - g - h / 2) for k in range(-kx, kx + 1)] + \
                    [(o[0] - g - w / 2, cy + k * .25) for k in range(-ky, ky + 1)] + \
                    [(o[2] + g + w / 2, cy + k * .25) for k in range(-ky, ky + 1)]
            for c in cands:
                q = (c[0] - w / 2, c[1] - h / 2, c[0] + w / 2, c[1] + h / 2)
                if not on_board(q, EDGE) or hard[s].hit(grow(q, BODY_GAP)) or text[s].hit(grow(q, TEXT_GAP)):
                    continue
                end = min((nearest(grow(ob, .25), c) for ob in own_boxes[r]), key=lambda p_: math.hypot(p_[0] - c[0], p_[1] - c[1]))
                st = nearest(grow(q, LEADER_W / 2 + SILK_CLR), end)      # silk clearance to its own text
                L = math.hypot(end[0] - st[0], end[1] - st[1])
                if L > 9.0 or (best and L + (.3 if vertical else 0) >= best[0]):
                    continue
                if L < .05 or not leader_ok(s, r, st, end):
                    continue
                best = (L + (.3 if vertical else 0), vertical, c, (ox, oy), st, end)
            if best and g > best[0] + 1: break
    if not best: continue
    _, vertical, c, offs, st, end = best
    style(f, size); shape(f, vertical)
    j = (0., 0., round(math.hypot(end[0] - st[0], end[1] - st[1]), 2))
    commit(f, vertical, c, offs, j, 'leader', size)
    ln = pcb.PCB_SHAPE(b); ln.SetShape(pcb.SHAPE_T_SEGMENT); ln.SetLayer(pcb.B_SilkS if s == 'B' else pcb.F_SilkS)
    ln.SetStart(pt(50 + st[0], 50 + st[1])); ln.SetEnd(pt(50 + end[0], 50 + end[1])); ln.SetWidth(pcb.FromMM(LEADER_W))
    b.Add(ln)
    if leader_grp is None:
        leader_grp = pcb.PCB_GROUP(b); leader_grp.SetName(LEADERS); b.Add(leader_grp)
    leader_grp.AddItem(ln)
    e_ = LEADER_W / 2 + .05
    text[s].add(r + ' leader', (min(st[0], end[0]) - e_, min(st[1], end[1]) - e_, max(st[0], end[0]) + e_, max(st[1], end[1]) + e_))
for r in unplaced:                       # recorded, not dropped: the assembly drawing keeps it at the part centre
    f = fs[r]; t = f.Reference(); style(f, SIZES[-1]); t.SetLayer(pcb.B_Fab if f.IsFlipped() else pcb.F_Fab)
    t.SetTextSize(pt(.6, .6)); t.SetTextThickness(pcb.FromMM(.09))
    t.SetTextAngle(pcb.EDA_ANGLE(0, pcb.DEGREES_T)); t.SetPosition(f.GetPosition())
unfinished = ROOT / 'outputs/ASSEMBLY-LABELS-UNFINISHED.json'
if unplaced: unfinished.write_text(json.dumps({'unplaced': unplaced, 'reason': 'no clear position beside the part; reference kept on Fab at the part centre'}, indent=2) + '\n')
elif unfinished.exists(): unfinished.unlink()
assert len(placed) + len(unplaced) + len(MARKS) + len(POLICY_FAB) == len(fs), (len(placed), len(unplaced), len(fs))
for r in placed:                        # assembly drawing: a silk-labelled part also shows its reference on Fab, at its
    f = fs[r]; s_ = side_of(f)          # centre, reusing the stock centre text blanked above (no copies pile up on reruns)
    dup = [g for g in f.GraphicalItems() if isinstance(g, pcb.PCB_TEXT) and g.GetLayer() in (pcb.F_Fab, pcb.B_Fab)
           and g.GetText() in ('', '${REFERENCE}', '%R')]
    t = dup[0] if dup else pcb.PCB_TEXT(f)
    if not dup: f.Add(t)
    t.SetText('${REFERENCE}'); t.SetLayer(pcb.B_Fab if s_ == 'B' else pcb.F_Fab); t.SetVisible(True)
    t.SetMirrored(s_ == 'B'); t.SetKeepUpright(False); t.SetTextSize(pt(.6, .6)); t.SetTextThickness(pcb.FromMM(.09))
    t.SetHorizJustify(pcb.GR_TEXT_H_ALIGN_CENTER); t.SetVertJustify(pcb.GR_TEXT_V_ALIGN_CENTER)
    o = rect(f, 0); t.SetTextAngle(pcb.EDA_ANGLE(VERT[s_] if o[3] - o[1] > o[2] - o[0] else 0, pcb.DEGREES_T))
    t.SetPosition(f.GetPosition())
pcb.SaveBoard(str(TARGET), b)
import project; project.write()
small = sum(1 for q in records if q['text_height_mm'] < SIZES[0][0])
amb = [q['reference'] for q in records if q['ambiguity_mm'] > 0]
(ROOT / 'outputs/ASSEMBLY-LABELS.json').write_text(json.dumps({
    'board': TARGET.name, 'text_height_mm': [s[0] for s in SIZES], 'text_stroke_mm': [s[1] for s in SIZES],
    'reference_layers': 'F.SilkS/B.SilkS (plus a Fab copy at the part centre for the assembly drawing)', 'reference_count': len(records),
    'silk_reference_classes': list(SILK_REF), 'service_passives': sorted(SERVICE & set(fs)), 'fab_by_policy': sorted(POLICY_FAB),
    'at_fallback_height': small, 'ambiguous': amb, 'without_silk_spot': sorted(unplaced), 'duplicate_centre_references_removed': True,
    'component_outline_margin_mm': .18 + BODY_GAP, 'text_to_text_gap_mm': 2 * TEXT_GAP,
    'placement': 'beside its own part, clear of bodies, pads, through-holes, fasteners and other silkscreen text',
    'labels': records}, indent=2) + '\n')
print(f'Placed {len(records)} references ({small} at {SIZES[-1][0]} mm, {len(amb)} ambiguous: {amb}); '
      f'{len(POLICY_FAB)} R/C on Fab by policy; {len(unplaced)} without a clear spot: {unplaced}', flush=True)
import os; os._exit(0)
