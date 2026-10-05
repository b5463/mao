# Brand placement adapted from b5463/kino-d4 hardware/pcb/kino-d4-carrier-a0/design/apply_brand.py @ 68aba75.
"""MAO_MAIN A0 silkscreen: identity, function labels, service marks (KiCad python). Idempotent.

ODD JOBS 41-43, 92-103, 172-177:
  F.SilkS  ODD JOBS maker's mark (approved artwork from brand/odd-jobs-symbol.json, traced from the
           supplied PNG, never redrawn) above MAO / MAIN A0 / date, on the calm lower-left of the face
           side; LCD, TOP and the antenna keep-clear note
  B.SilkS  maker's mark and product line in the corner visible with the cell fitted; connector
           functions with pin cues (USB, BAT - T +, SPK, LRA, REAR, LCD, TAG), test-pad function names
           (never bare TPn), BAT LINK at the 0R link
Type scale (ODD JOBS 95, 98): identity 1.5 mm > connector names 1.2 > test-pad names 0.9 > references 0.8.
Easter eggs (MAO is Mandarin for cat): a cat asleep under the face with a paw-print trail, 'boop' at the face
switch, 'meow' under the speaker, '9 lives' at the reverse-polarity FET, and ODD JOBS' own hidden line 'MADE FOR
BAD IDEAS' (standard rule 175) under the panel. Each is placed by the same clearance test as every label (off pads, vias, bodies
and other text) and is skipped, with a note, where nothing clear is found.
Every item goes into one group per side, removed and rebuilt on each run. Reference designators are
placed by mao_labels.py afterwards (it treats everything here as fixed). Positions are derived from
the footprints, so a placement change moves the labels with their parts.
"""
import json
import math
import os
import sys

import pcbnew as pcb
from board import ROOT, TARGET, at, courtyard_boxes
import mechanical as m

DATE = '2026-10'
FIELD = {'TP1', 'TP2', 'TP3', 'TP4', 'TP5', 'TP7', 'TP8', 'TP9', 'TP10', 'TP11', 'TP16'}
GROUPS = ('MAO silk F', 'MAO silk B', 'ODD JOBS maker mark', 'MAO easter eggs')
IDENT, CONN, DEBUG = 1.5, 1.2, m.FIELD_TEXT  # type scale: identity, connectors, test pads (references 0.8)
STROKE = 0.15
ART_STROKE = 0.16                  # easter-egg line art (JLC silk minimum 0.153 mm)


def mm(v):
    return pcb.ToMM(v) - 50


def tbox(t):
    r = t.GetEffectiveTextShape().BBox()
    return (mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom()))


def overlap(a, c, gap):
    return a[0] - gap < c[2] and c[0] - gap < a[2] and a[1] - gap < c[3] and c[1] - gap < a[3]


def clean():
    """Remove the previous run's silk (every board-level silkscreen drawing is this script's) and save.
    Runs in its own process: this SWIG build's object lists are unusable after a Remove()."""
    b = pcb.LoadBoard(str(TARGET))
    old = [g for g in b.Groups() if g.GetName() in GROUPS]
    for g in old:
        g.RemoveAll()
    for d in list(b.GetDrawings()):
        if d.GetLayer() in (pcb.F_SilkS, pcb.B_SilkS):
            b.Remove(d)
    for g in old:
        b.Remove(g)
    pcb.SaveBoard(str(TARGET), b)


def main():
    b = pcb.LoadBoard(str(TARGET))
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    groups = {}

    def group(name):
        if name not in groups:
            g = pcb.PCB_GROUP(b)
            g.SetName(name)
            b.Add(g)
            groups[name] = g
        return groups[name]

    def text(s, x, y, side='B', size=0.8, angle=0, bold=False, halign='center', gname=None):
        t = pcb.PCB_TEXT(b)
        t.SetText(s)
        t.SetPosition(at(x, y))
        t.SetTextSize(pcb.VECTOR2I(pcb.FromMM(size), pcb.FromMM(size)))
        t.SetTextThickness(pcb.FromMM(max(STROKE, size * (0.2 if bold else 0.15))))
        t.SetLayer(pcb.B_SilkS if side == 'B' else pcb.F_SilkS)
        t.SetMirrored(side == 'B')
        t.SetHorizJustify({'center': pcb.GR_TEXT_H_ALIGN_CENTER, 'left': pcb.GR_TEXT_H_ALIGN_LEFT,
                           'right': pcb.GR_TEXT_H_ALIGN_RIGHT}[halign])
        t.SetVertJustify(pcb.GR_TEXT_V_ALIGN_CENTER)
        if angle:
            t.SetTextAngle(pcb.EDA_ANGLE(angle, pcb.DEGREES_T))
        b.Add(t)
        group(gname or ('MAO silk ' + side)).AddItem(t)
        return t

    def rect(x0, y0, x1, y1, side='B', gname=None):
        s = pcb.PCB_SHAPE(b)
        s.SetShape(pcb.SHAPE_T_RECT)
        s.SetStart(at(x0, y0))
        s.SetEnd(at(x1, y1))
        s.SetWidth(pcb.FromMM(STROKE))
        s.SetLayer(pcb.B_SilkS if side == 'B' else pcb.F_SilkS)
        b.Add(s)
        group(gname or ('MAO silk ' + side)).AddItem(s)

    def line(x0, y0, x1, y1, side='B', width=STROKE, gname=None):
        s = pcb.PCB_SHAPE(b)
        s.SetShape(pcb.SHAPE_T_SEGMENT)
        s.SetStart(at(x0, y0))
        s.SetEnd(at(x1, y1))
        s.SetWidth(pcb.FromMM(width))
        s.SetLayer(pcb.B_SilkS if side == 'B' else pcb.F_SilkS)
        b.Add(s)
        group(gname or ('MAO silk ' + side)).AddItem(s)

    def dot(x, y, r, side='B', gname=None):
        s = pcb.PCB_SHAPE(b)
        s.SetShape(pcb.SHAPE_T_CIRCLE)
        s.SetCenter(at(x, y))
        s.SetEnd(at(x + r - 0.05, y))
        s.SetWidth(pcb.FromMM(0.1))
        s.SetFilled(True)
        s.SetLayer(pcb.B_SilkS if side == 'B' else pcb.F_SilkS)
        b.Add(s)
        group(gname or ('MAO silk ' + side)).AddItem(s)

    def pad(ref, num):
        p = next(p for p in fps[ref].Pads() if p.GetNumber() == num)
        return mm(p.GetPosition().x), mm(p.GetPosition().y)

    def centre(ref):
        p = fps[ref].GetPosition()
        return mm(p.x), mm(p.y)

    # ---- identity, face side (F) -------------------------------------------------------------------
    data = json.loads((ROOT / 'brand' / 'odd-jobs-symbol.json').read_text())

    def mark(centre, w, side):
        """The approved artwork, traced polygon as supplied; mirrored on the back so it reads correctly."""
        scale = w / (data['pixel_bounds'][2] - 1)
        hh = (data['pixel_bounds'][3] - 1) * scale
        cx, cy = centre
        x0, y0 = cx - w / 2, cy - hh / 2
        poly = pcb.SHAPE_POLY_SET()
        poly.NewOutline()
        for a, c in data['points']:
            xa = x0 + a * scale if side == 'F' else x0 + w - a * scale
            poly.Append(pcb.FromMM(50 + xa), pcb.FromMM(50 + y0 + c * scale))
        shape = pcb.PCB_SHAPE(b)
        shape.SetShape(pcb.SHAPE_T_POLY)
        shape.SetPolyShape(poly)
        shape.SetFilled(True)
        shape.SetWidth(0)
        shape.SetLayer(pcb.F_SilkS if side == 'F' else pcb.B_SilkS)
        b.Add(shape)
        group('ODD JOBS maker mark').AddItem(shape)
        return hh

    # ---- obstacles for every label: mask openings, part bodies (real courtyard outline), part silk -------
    side_of = lambda f: 'B' if f.IsFlipped() else 'F'
    for f in fps.values():                 # a labelled probe pad needs no silk ring: its name is the cue
        if f.GetReference().startswith('TP'):
            for g in f.GraphicalItems():
                if g.GetLayer() in (pcb.F_SilkS, pcb.B_SilkS):
                    g.SetLayer(pcb.B_Fab if f.IsFlipped() else pcb.F_Fab)
    pad_boxes = {'F': [], 'B': []}
    bodies = {'F': [], 'B': []}
    outlines = {'F': [], 'B': []}         # the part's own body (Fab outline): the service-field names may use a
    graphics = {'F': [], 'B': []}         # neighbour's courtyard margin, never its body or pads
    for f in fps.values():
        for p in f.Pads():
            r = p.GetBoundingBox()
            for sd, lay in (('F', pcb.F_Mask), ('B', pcb.B_Mask)):
                if p.IsOnLayer(lay):
                    pad_boxes[sd].append((mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom())))
        if not f.GetReference().startswith('TP'):
            bodies[side_of(f)] += [(q, f.GetReference()) for q in courtyard_boxes(f)]
            fab = [g.GetBoundingBox() for g in f.GraphicalItems()
                   if g.GetLayer() in (pcb.F_Fab, pcb.B_Fab) and not isinstance(g, pcb.PCB_TEXT)]
            if fab:
                outlines[side_of(f)].append(((min(mm(r.GetLeft()) for r in fab), min(mm(r.GetTop()) for r in fab),
                                              max(mm(r.GetRight()) for r in fab), max(mm(r.GetBottom()) for r in fab)),
                                             f.GetReference()))
            else:
                outlines[side_of(f)] += [(q, f.GetReference()) for q in courtyard_boxes(f)]
        if f.GetReference().startswith('FID'):
            x, y = centre(f.GetReference())
            bodies[side_of(f)].append(((x - 2.05, y - 2.05, x + 2.05, y + 2.05), f.GetReference()))
            outlines[side_of(f)].append(((x - 2.05, y - 2.05, x + 2.05, y + 2.05), f.GetReference()))
        for g in f.GraphicalItems():
            if g.GetLayer() in (pcb.F_SilkS, pcb.B_SilkS) and not isinstance(g, pcb.PCB_TEXT):
                r = g.GetBoundingBox()
                graphics['B' if g.GetLayer() == pcb.B_SilkS else 'F'].append(
                    (mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom())))
    sl = m.TAIL_SLOT                        # the display-tail slot: no silk within 0.5 mm of the cut
    for sd in ('F', 'B'):
        bodies[sd].append(((sl[0] - 0.5, sl[1] - 0.5, sl[2] + 0.5, sl[3] + 0.5), 'slot'))
        outlines[sd].append(((sl[0] - 0.5, sl[1] - 0.5, sl[2] + 0.5, sl[3] + 0.5), 'slot'))
    vias = {'F': [], 'B': []}               # silk never prints over a via (tented bumps read as noise)
    for t in b.GetTracks():
        if isinstance(t, pcb.PCB_VIA):
            x, y, r = mm(t.GetPosition().x), mm(t.GetPosition().y), pcb.ToMM(t.GetWidth(pcb.F_Cu)) / 2
            for sd in ('F', 'B'):
                vias[sd].append((x - r, y - r, x + r, y + r))
    placed = {'F': [], 'B': []}
    electrodes = {'F': [], 'B': []}         # touch-electrode copper, under mask: a name printed there labels the wrong thing
    for f in fps.values():
        if f.GetReference().startswith('E'):
            for p in f.Pads():
                for sd, lay in (('F', pcb.F_Cu), ('B', pcb.B_Cu)):
                    if p.IsOnLayer(lay):
                        poly = p.GetEffectivePolygon(lay); r = poly.BBox()
                        electrodes[sd].append((poly, (mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom()))))

    def on_electrode(bx, sd, gap=0.1):
        for poly, pb in electrodes[sd]:
            if not overlap(bx, pb, gap): continue
            nx, ny = max(2, int((bx[2] - bx[0]) / 0.15) + 2), max(2, int((bx[3] - bx[1]) / 0.15) + 2)
            for i in range(nx):
                for j in range(ny):
                    if poly.Collide(at(bx[0] + (bx[2] - bx[0]) * i / (nx - 1), bx[1] + (bx[3] - bx[1]) * j / (ny - 1)),
                                    pcb.FromMM(gap)):
                        return True
        return False

    def why(bx, sd, text_gap=0.5, inside=(), outline=False):
        for q in pad_boxes[sd]:
            if overlap(bx, q, 0.2): return 'pad %s' % (tuple(round(v, 2) for v in q),)
        if on_electrode(bx, sd): return 'electrode copper'
        for q, owner in (outlines if outline else bodies)[sd]:
            if owner not in inside and overlap(bx, q, 0.0): return 'body %s %s' % (owner, tuple(round(v, 2) for v in q))
        for q in graphics[sd]:              # the board rule min_silk_clearance
            if overlap(bx, q, 0.15): return 'silk %s' % (tuple(round(v, 2) for v in q),)
        for q in vias[sd]:
            if overlap(bx, q, 0.05): return 'via %s' % (tuple(round(v, 2) for v in q),)
        for q in placed[sd]:
            if overlap(bx, q, text_gap): return 'text %s' % (tuple(round(v, 2) for v in q),)
        if not all(math.hypot(x, y) < m.PCB_R - 0.5 for x in (bx[0], bx[2]) for y in (bx[1], bx[3])):
            return 'edge'
        return None

    def clear(bx, sd, inside=()):
        return why(bx, sd, inside=inside) is None

    def measure(s_, x, y, sd, size=0.8, a_=0):
        """Box a text would take, from a text object that is never added to the board."""
        t = pcb.PCB_TEXT(b)
        t.SetText(s_)
        t.SetTextSize(pcb.VECTOR2I(pcb.FromMM(size), pcb.FromMM(size)))
        t.SetTextThickness(pcb.FromMM(max(STROKE, size * 0.15)))
        t.SetLayer(pcb.B_SilkS if sd == 'B' else pcb.F_SilkS)
        t.SetMirrored(sd == 'B')
        t.SetHorizJustify(pcb.GR_TEXT_H_ALIGN_CENTER)
        t.SetVertJustify(pcb.GR_TEXT_V_ALIGN_CENTER)
        t.SetPosition(at(x, y))
        t.SetTextAngle(pcb.EDA_ANGLE(a_, pcb.DEGREES_T))
        return tbox(t)

    def ring(x, y, r):
        """Fallback spots round an anchor: nearest first, upright or reading upwards."""
        out = []
        for k in range(17):
            d = r + 0.6 + 0.15 * k
            for a in range(0, 360, 15):
                out.append((d, x + d * math.cos(math.radians(a)), y + d * math.sin(math.radians(a))))
        return [(cx_, cy_, a_) for _, cx_, cy_ in sorted(out) for a_ in (0, 270)]

    def label(s_, cands, sd, size=0.8, bold=False, angle=0, gname=None, anchor=None, inside=(), quiet=False):
        """First clear candidate (centre positions, optionally (x, y, angle)), then the nearest clear spot round
        `anchor` (x, y, r); the first candidate, flagged, if none."""
        cands = [c if len(c) == 3 else (c[0], c[1], angle) for c in cands]
        if anchor:
            cands = cands + ring(*anchor)
        if quiet:                          # an easter egg: the first clear spot, or no egg at all
            for x, y, a_ in cands:
                if clear(measure(s_, x, y, sd, size, a_), sd, inside):
                    t = text(s_, x, y, sd, size, angle=a_, bold=bold, gname=gname)
                    placed[sd].append(tbox(t))
                    return t
            print('silk: easter egg %r skipped (no clear spot)' % s_)
            return None
        t = text(s_, cands[0][0], cands[0][1], sd, size, angle=angle, bold=bold, gname=gname)
        for x, y, a_ in cands:
            t.SetPosition(at(x, y))
            t.SetTextAngle(pcb.EDA_ANGLE(a_, pcb.DEGREES_T))
            if clear(tbox(t), sd, inside):
                break
        else:
            reasons = []
            for x, y, a_ in cands:
                t.SetPosition(at(x, y))
                t.SetTextAngle(pcb.EDA_ANGLE(a_, pcb.DEGREES_T))
                reasons.append('(%.1f,%.1f,%d) %s' % (x, y, a_, why(tbox(t), sd, inside=inside)))
            t.SetPosition(at(cands[0][0], cands[0][1]))
            t.SetTextAngle(pcb.EDA_ANGLE(cands[0][2], pcb.DEGREES_T))
            print('silk: no clear spot for', repr(s_), '|', '; '.join(reasons))
        placed[sd].append(tbox(t))
        return t

    # ---- identity, face side (F): maker's mark over MAO / MAIN A0 / date, the S/N field below ---------------
    w = 4.6
    h = mark(BRAND_CENTRE, w, 'F')
    bx, by = BRAND_CENTRE
    placed['F'].append((bx - w / 2, by - h / 2, bx + w / 2, by + h / 2))
    for s_, dy, size, bold in (('MAO', 1.45, IDENT, True), ('MAIN A0', 3.4, 1.0, False), (DATE, 4.85, 0.8, False)):
        t = text(s_, bx, by + h / 2 + dy, 'F', size, bold=bold, gname='ODD JOBS maker mark')
        placed['F'].append(tbox(t))
    # S/N field (ODD JOBS 101): a 6 x 6 mm box for a DataMatrix sticker or laser mark, its name above it, and
    # JLC's order-number placeholder below (the fab prints its number there and nowhere else)
    def sn_layout(sx_, sy_):
        """Field, its name above, and the order-number placeholder below it (slid sideways round FID2)."""
        e_ = STROKE / 2                    # the field's four edges (its inside may lie over tented vias)
        field_ = [(sx_ - 3.0 - e_, sy_ - 3.0 - e_, sx_ + 3.0 + e_, sy_ - 3.0 + e_),
                  (sx_ - 3.0 - e_, sy_ + 3.0 - e_, sx_ + 3.0 + e_, sy_ + 3.0 + e_),
                  (sx_ - 3.0 - e_, sy_ - 3.0 - e_, sx_ - 3.0 + e_, sy_ + 3.0 + e_),
                  (sx_ + 3.0 - e_, sy_ - 3.0 - e_, sx_ + 3.0 + e_, sy_ + 3.0 + e_),
                  measure('S/N', sx_ - 2.2, sy_ - 3.95, 'F')]
        for jx in (0.0, -0.5, -1.0, -1.5, 0.5, 1.0, -2.0, 1.5):
            for jy in (3.8, 4.2):
                jq = measure('JLCJLCJLCJLC', sx_ + jx, sy_ + jy, 'F')
                if clear(jq, 'F'):
                    return field_ + [jq], (sx_ + jx, sy_ + jy)
        return field_ + [measure('JLCJLCJLCJLC', sx_, sy_ + 3.8, 'F')], (sx_, sy_ + 3.8)
    for sx_, sy_ in SN_SPOTS:
        boxes_, jxy = sn_layout(sx_, sy_)
        if all(clear(q, 'F') for q in boxes_):
            break
    else:
        sx_, sy_ = SN_SPOTS[0]
        boxes_, jxy = sn_layout(sx_, sy_)
        print('silk: S/N field: no clear 6 x 6 spot |', [why(q, 'F') for q in boxes_])
    rect(sx_ - 3.0, sy_ - 3.0, sx_ + 3.0, sy_ + 3.0, 'F')
    placed['F'].append((sx_ - 3.0, sy_ - 3.0, sx_ + 3.0, sy_ + 3.0))
    for s_, x_, y_ in (('S/N', sx_ - 2.2, sy_ - 3.95), ('JLCJLCJLCJLC', jxy[0], jxy[1])):
        placed['F'].append(tbox(text(s_, x_, y_, 'F', 0.8)))

    # ---- face side function labels --------------------------------------------------------------------
    tx, ty = centre('J302')
    label('TOP', [(tx, ty + 2.6), (tx, ty - 2.6), (tx + 2.6, ty, 90), (tx - 2.6, ty, 90)], 'F', CONN,
          anchor=(tx, ty, 1.6))
    ny = m.NOTCH_Y
    one = [(0.0, ny - 0.9), (0.0, ny - 0.8), (0.0, ny - 0.7), (0.0, ny - 1.2), (0.0, ny - 1.5)] + \
          [(dx_, ny - 1.8 - 0.3 * k) for k in range(5) for dx_ in (0.0, -0.5, 0.5, -1.0, 1.0)]
    if any(clear(measure('ANTENNA KEEP CLEAR', x_, y_, 'F'), 'F') for x_, y_ in one):
        label('ANTENNA KEEP CLEAR', one, 'F')
    else:                                  # two lines between the vias of the antenna-edge stitching row
        label('ANTENNA\nKEEP CLEAR', [(x_ / 10, ny - 1.3 - 0.1 * k) for k in range(6)
                                       for x_ in sorted(range(-60, 41, 5), key=lambda v_: abs(v_ + 10))], 'F')

    # ---- back: maker's mark and product line beside the module, outside the cell (seen with the base off) --
    bw = B_MARK_W                          # the back mark (4.6 mm: its finest strokes stay >= 0.15 mm), the product
    bh = bw * (data['pixel_bounds'][3] - 1) / (data['pixel_bounds'][2] - 1)   # line centred under it
    def block(cx_, cy_, ident=1.5):        # identity 1.5 mm (1.3 / 1.2 where the back has no field for it)
        lines_ = [('MAO A0', cy_ + bh / 2 + 0.45 + ident / 2, ident, True), (DATE, cy_ + bh / 2 + ident + 1.45, 0.8, False)]
        boxes_ = [(cx_ - bw / 2, cy_ - bh / 2, cx_ + bw / 2, cy_ + bh / 2)]
        boxes_ += [measure(s_, cx_, y_, 'B', size) for s_, y_, size, _ in lines_]
        return lines_, boxes_
    cx0, cy0 = m.BATTERY_CENTRE            # anywhere on the back, best outside the cell (visible with the base off)
    cell = (cx0 - m.BATTERY_ENVELOPE[0] / 2, cy0 - m.BATTERY_ENVELOPE[1] / 2,
            cx0 + m.BATTERY_ENVELOPE[0] / 2, cy0 + m.BATTERY_ENVELOPE[1] / 2)
    def under_cell(cx_, cy_):
        q = (cx_ - bw / 2, cy_ - bh / 2, cx_ + bw / 2, cy_ + bh / 2 + 3.4)
        ix = max(0.0, min(q[2], cell[2]) - max(q[0], cell[0])) * max(0.0, min(q[3], cell[3]) - max(q[1], cell[1]))
        return ix / ((q[2] - q[0]) * (q[3] - q[1]))
    spots = sorted(((x_ / 5, y_ / 5) for x_ in range(-130, 131) for y_ in range(-130, 131)
                    if math.hypot(x_ / 5, y_ / 5) < m.PCB_R - 4.0),
                   key=lambda p_: 8.0 * under_cell(*p_) + 0.1 * math.hypot(p_[0] - B_MARK_AT[0], p_[1] - B_MARK_AT[1]))
    for probe in filter(None, os.environ.get('SILK_MARK_PROBE', '').split(';')):   # debugging: why a spot fails
        x_, y_ = map(float, probe.split(','))
        print('silk: back mark probe (%.1f, %.1f):' % (x_, y_), [why(q, 'B') for q in block(x_, y_, 1.3)[1]])
    found = None
    for ident in (1.5, 1.3, 1.2):
        for cx_, cy_ in spots:
            if not clear((cx_ - bw / 2, cy_ - bh / 2, cx_ + bw / 2, cy_ + bh / 2), 'B'):
                continue                   # the mark first: cheap, and most spots fail here
            lines_, boxes_ = block(cx_, cy_, ident)
            if all(clear(q, 'B') for q in boxes_):
                found = (cx_, cy_, ident)
                break
        if found:
            break
    if found:
        cx_, cy_, ident = found
        lines_, boxes_ = block(cx_, cy_, ident)
        print('silk: back mark at (%.1f, %.1f), identity %.1f mm, %.0f %% over the cell outline' % (
            cx_, cy_, ident, 100 * under_cell(cx_, cy_)))
    else:
        cx_, cy_ = B_MARK_AT
        lines_, boxes_ = block(cx_, cy_)
        print('silk: back mark: no clear spot | best-ranked spots:',
              ['(%.1f, %.1f) %s' % (x_, y_, next((w_ for w_ in (why(q, 'B') for q in block(x_, y_, 1.2)[1]) if w_), None))
               for x_, y_ in spots[:4]])
    b_mark = (cx_, cy_)
    mark(b_mark, bw, 'B')
    placed['B'].append(boxes_[0])
    for s_, y_, size, bold in lines_:
        placed['B'].append(tbox(text(s_, cx_, y_, 'B', size, bold=bold, gname='ODD JOBS maker mark')))

    # ---- back: connectors with their pin cues -----------------------------------------------------------
    label('USB', [(-4.4, -19.8), (-5.0, -18.2), (4.6, -19.8)], 'B', CONN, bold=True, anchor=(0.0, -21.0, 4.0))
    # battery plug: pin cues and name on the plug side, one aligned block (R108's link sits on the other)
    x2, _ = pad('J102', '2')
    t = text('- T +\nBAT', x2, 0.0, 'B', 0.8, bold=True)    # J102: 1 BAT- at the larger x, read first from the back
    _, y0_, _, _ = tbox(t)
    base_y = 0.0 + (-0.39 + 0.25) - y0_                      # top of the block 0.25 mm below the plug's body
    for dy_, dx_ in [(dy_ / 20, dx_ / 20) for dx_ in (0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6)   # a little lower
                     for dy_ in range(0, 21)]:                                    # (or a hair sideways, still under each
        t.SetPosition(at(x2 + dx_, base_y + dy_))                                 # pin) where a via sits there
        if clear(tbox(t), 'B'):
            break
    else:
        t.SetPosition(at(x2, base_y))
        print('silk: battery pin cues: no clear spot |', why(tbox(t), 'B'), tuple(round(v, 2) for v in tbox(t)))
        if os.environ.get('SILK_DEBUG_BAT'):
            for dy_, dx_ in [(dy_, dx_) for dx_ in (0.0, 0.3, -0.3) for dy_ in (0.0, 0.45, 1.0)]:
                t.SetPosition(at(x2 + dx_, base_y + dy_)); print('   ', dx_, dy_, why(tbox(t), 'B'))
            t.SetPosition(at(x2, base_y))
    placed['B'].append(tbox(t))
    sx, sy = centre('LS501')                # speaker: its name inside its outline, read before the part goes in
    label('SPK', [(sx + dx_, sy + dy_, 270) for dx_ in (2.2, 1.6, 2.8) for dy_ in (-3.0, 3.0, -1.5, 1.5)], 'B', CONN,
          inside=('LS501',), anchor=(sx, sy, 7.0))
    lx, ly = centre('J501')
    label('LRA', [(lx - 2.05, ly + 2.3), (lx - 2.5, ly - 2.4), (lx + 1.25, ly + 3.5)], 'B', CONN, anchor=(lx, ly, 1.5))
    rx, ry = centre('J303')
    label('REAR', [(rx, ry - 3.8), (rx - 0.6, ry - 3.8), (rx, ry - 4.2), (rx + 3.6, ry)], 'B', CONN, anchor=(rx, ry, 1.5))
    jx, jy = centre('J301')                # display FPC on B: the name on the side the tail comes in from
    label('LCD', [(jx - 2.9, jy, 270), (jx - 3.3, jy, 270), (jx + 2.6, jy, 270), (jx, jy - 6.6), (jx, jy + 6.6)],
          'B', CONN, anchor=(jx, jy, 3.0))
    gx, gy = centre('J201')                # Tag-Connect: name along its module-side column
    label('TAG', [(gx - 2.6, gy, 270), (gx - 0.3, gy + 3.65, 0), (gx + 2.5, gy, 270)], 'B', CONN, anchor=(gx, gy, 2.6))
    kx, ky = centre('R108')
    label('BAT\nLINK', [(kx + 2.65, ky - 0.4), (kx + 2.8, ky - 0.4)] +
          [(kx + dx_, ky + dy_) for dx_ in (2.65, 2.4, 2.9, -2.65, -2.9) for dy_ in (0.0, -0.8, 0.8, -1.2, 1.2)], 'B')

    # ---- back: test pads by function (ODD JOBS 103) --------------------------------------------------------
    # The service field is labelled as one set: every name upright beside its pad or level above / below it,
    # chosen together so no two names crowd each other (0.4 mm between field names) and none sits on a pad,
    # a part's body or a via (a neighbour's courtyard margin is allowed: silk there stays visible and clear of its
    # pads); the preferred side of each pad (FIELD_SPOT) wins where the field allows it.
    tps = sorted((r for r in fps if r.startswith('TP')), key=lambda r: int(r[2:]))
    def box_of(s_, x, y, a_, sd='B'):
        return measure(s_, x, y, sd, DEBUG, a_)
    options = {}
    shared = {r_: pair for pair in FIELD_SHARED for r_ in pair}
    for ref in [r for r in tps if r in FIELD and (r not in shared or r == shared[r][0])]:
        f = fps[ref]; x, y = centre(ref); s_ = f.GetValue()
        r = 0.6 if s_ in ('GND', '3V3', 'SYS', 'BAT', 'VBUS') else 0.5
        pref = FIELD_SPOT.get(ref, [])
        order = pref + [k for k in (('side', 1), ('side', -1), ('flat', 1), ('flat', -1)) if k not in pref]
        if ref in shared:                  # one name for an adjacent pair of like pads, centred between them
            x = (x + centre(shared[ref][1])[0]) / 2
            order = [('flat', 1), ('flat', -1)]
        opts = []
        for rank, (kind, sgn) in enumerate(order):
            for off in (0.0, 0.2, -0.2, 0.3, -0.3, 0.4, -0.4, 0.6, -0.6, 0.8, -0.8):
                for out in (0.0, 0.15, 0.3, 0.45, 0.6):   # a little further out where a via sits beside the pad
                    d_ = r + 0.72 + out
                    c = ((x + sgn * d_, y + off, 270) if kind == 'side' else (x + off, y + sgn * d_, 0))
                    bx_ = box_of(s_, *c)
                    if why(bx_, 'B', outline=True) is None:
                        opts.append((rank * 10 + abs(off) * 5 + out * 8, c, bx_))
        options[ref] = sorted(opts, key=lambda o: o[0])[:int(os.environ.get('SILK_CANDS', 60))]   # best spots per pad
    field = list(options)
    if os.environ.get('SILK_DEBUG'):
        print('silk: field options', {r_: len(o) for r_, o in options.items()})
        json.dump({r_: [[c_, list(cand), list(bx_)] for c_, cand, bx_ in o] for r_, o in options.items()},
                  open(os.environ['SILK_DEBUG'], 'w'))
    best = [None, None]
    budget = [300000]                      # search steps: bounded whatever the board
    def search(domains, chosen, cost):
        """Most-constrained pad first; every choice prunes the neighbours' spots it crowds (forward check), and a
        branch stops once its cost plus the cheapest spot left for every other pad cannot beat the best so far."""
        budget[0] -= 1
        if budget[0] < 0 or (best[0] is not None and cost >= best[0]):
            return
        if not domains:
            best[0], best[1] = cost, dict(chosen); return
        ref = min(domains, key=lambda r_: len(domains[r_]))
        for c_, cand, bx_ in domains[ref]:
            rest = {r_: [o for o in d if not overlap(bx_, o[2], FIELD_GAP)] for r_, d in domains.items() if r_ != ref}
            if all(rest.values()):
                if best[0] is not None and cost + c_ + sum(min(o[0] for o in d) for d in rest.values()) >= best[0]:
                    continue
                chosen[ref] = (cand, bx_)
                search(rest, chosen, cost + c_)
                del chosen[ref]
    search({r_: options[r_] for r_ in field}, {}, 0.0)
    if os.environ.get('SILK_DEBUG'):
        print('silk: field search budget left', budget[0], 'cost', best[0])
    if best[1] is None:
        print('silk: service field labels: no complete arrangement')
    for ref, (cand, bx_) in (best[1] or {}).items():
        t = text(fps[ref].GetValue(), cand[0], cand[1], 'B', DEBUG, angle=cand[2])
        placed['B'].append(tbox(t))
    for ref in tps:
        if ref in FIELD and best[1] and (ref in best[1] or (ref in shared and shared[ref][0] in best[1])):
            continue
        f = fps[ref]
        x, y = centre(ref)
        r = 0.6 if f.GetValue() in ('GND', '3V3', 'SYS', 'BAT', 'VBUS') else 0.5
        wd = 0.6 * len(f.GetValue()) + 0.2
        side_c = [(x + r + 0.68, y), (x - (r + 0.68), y)]          # vertical, beside the pad
        flat_c = [(x, y + r + 0.7), (x, y - (r + 0.7)), (x + r + 0.45 + wd / 2, y), (x - (r + 0.45 + wd / 2), y)]
        label(f.GetValue(), [(cx_, cy_, 270) for cx_, cy_ in side_c] + flat_c, 'B', DEBUG, anchor=(x, y, r))

    # ---- face side: IMU axes (ODD JOBS 156), from ST AN5192 Fig. 1: pin 1 top left, +X towards pins 8-11,
    # +Y towards pins 12-14, +Z out of the top. U401 sits on F at 0 deg: +X = board +x, +Y = towards 12 o'clock.
    ix, iy = centre('U401')
    spots = [(ix + dx, iy + dy) for dx in (3.2, 3.6, 4.0, -4.6) for dy in (1.5, 1.1, 0.7, 1.9)]
    for ox, oy in spots:
        glyph = (ox - 0.2, oy - 1.7, ox + 1.75, oy + 0.2)
        if all(clear(q, 'F') for q in (measure('+X', ox + 2.55, oy, 'F'), measure('+Y', ox, oy - 2.45, 'F'), glyph)):
            break
    else:
        ox, oy = spots[0]
        print('silk: IMU axes: no clear spot')
    tx_ = text('+X', ox + 2.55, oy, 'F', 0.8)
    ty_ = text('+Y', ox, oy - 2.45, 'F', 0.8)
    placed['F'] += [tbox(tx_), tbox(ty_), (ox - 0.2, oy - 1.7, ox + 1.75, oy + 0.2)]
    for (x0, y0, x1, y1) in ((ox, oy, ox + 1.5, oy), (ox, oy, ox, oy - 1.5)):      # shafts from the origin
        line(x0, y0, x1, y1, 'F')
    for (x0, y0, x1, y1) in ((ox + 1.5, oy, ox + 1.15, oy - 0.3), (ox + 1.5, oy, ox + 1.15, oy + 0.3),
                             (ox, oy - 1.5, ox - 0.3, oy - 1.15), (ox, oy - 1.5, ox + 0.3, oy - 1.15)):
        line(x0, y0, x1, y1, 'F')                                                   # arrow heads

    # ---- easter eggs (module docstring): after every functional label, so they only take space left over -------
    # The references mao_labels.py puts on silk come after this script, so their room is held back here: no egg
    # within 1.4 mm of a part whose reference the silk policy wants (ICs, connectors, semiconductors, the switch,
    # mic, inductor, electrodes and every R/C a procedure names)
    import eggart, re
    EGG = 'MAO easter eggs'
    named = {r for d in ('mao-bringup.md', 'mao-factory-test.md')
             for r in re.findall(r'\b[RC]\d{3}\b', (ROOT.parents[1] / 'docs' / 'hardware' / d).read_text(encoding='utf-8'))}
    held = {'F': [], 'B': []}
    for r_, f_ in fps.items():
        if (''.join(c_ for c_ in r_ if c_.isalpha()) in ('U', 'J', 'Q', 'D', 'SW', 'MK', 'L', 'E') or r_ in named) \
                and r_ != 'SW301':         # the switch has room all round: 'boop' is its nose
            held[side_of(f_)] += [(q[0] - 1.4, q[1] - 1.4, q[2] + 1.4, q[3] + 1.4) for q in courtyard_boxes(f_)]
    for sd_ in ('F', 'B'):
        placed[sd_] += held[sd_]

    def art_pts(lines_, dots_, ox, oy, sd):
        mir = -1 if sd == 'B' else 1       # the back reads mirrored: flip x so the art faces the right way
        L_ = [[(ox + mir * x_, oy + y_) for x_, y_ in pl] for pl in lines_]
        D_ = [(ox + mir * x_, oy + y_, r_) for x_, y_, r_ in dots_]
        return L_, D_

    def art_box(L_, D_):
        xs = [x_ for pl in L_ for x_, _ in pl] + [x_ + k_ * r_ for x_, _, r_ in D_ for k_ in (-1, 1)]
        ys = [y_ for pl in L_ for _, y_ in pl] + [y_ + k_ * r_ for _, y_, r_ in D_ for k_ in (-1, 1)]
        e = ART_STROKE / 2
        return (min(xs) - e, min(ys) - e, max(xs) + e, max(ys) + e)

    def draw_art(L_, D_, sd):
        for pl in L_:
            for (xa, ya), (xb, yb) in zip(pl, pl[1:]):
                line(xa, ya, xb, yb, sd, ART_STROKE, EGG)
        for x_, y_, r_ in D_:
            dot(x_, y_, r_, sd, EGG)
        placed[sd].append(art_box(L_, D_))

    def grid(cx_, cy_, box_, step=0.4):
        pts = [(box_[0] + i * step, box_[1] + j * step) for i in range(int((box_[2] - box_[0]) / step) + 1)
               for j in range(int((box_[3] - box_[1]) / step) + 1)]
        return sorted(pts, key=lambda p_: math.hypot(p_[0] - cx_, p_[1] - cy_))

    face = (-14.0, -14.0, 14.0, 14.5)      # under the panel (outline r 17.8): seen only with the face lifted
    cat_l, cat_d = eggart.sleeping_cat()
    cat_l = cat_l + eggart.zzz()           # with its three z drifting up from the head, as line art
    body_l = cat_l[:-3]                    # the cat without its z
    def scaled(lines_, k):
        return [[(x_ * k, y_ * k) for x_, y_ in pl] for pl in lines_], [(x_ * k, y_ * k, r_ * k) for x_, y_, r_ in cat_d]
    spots = [(lines_, k, ox, oy) for lines_, k in ((cat_l, 1.0), (cat_l, 0.9), (body_l, 1.0), (body_l, 0.85))
             for ox, oy in grid(*CAT_AT, face)]
    for lines_, k, ox, oy in spots:
        L_, D_ = art_pts(*scaled(lines_, k), ox, oy, 'F')
        q = art_box(L_, D_)
        if clear(q, 'F') and math.hypot(max(abs(q[0]), abs(q[2])), max(abs(q[1]), abs(q[3]))) < 17.0:
            draw_art(L_, D_, 'F')
            trail = 0                      # paw prints walking away from the tail while there is room
            for px_, py_ in ((4.7, 1.25), (5.8, 0.25), (6.9, 1.05), (8.0, 0.05), (9.1, 0.85)):
                pl_, pd_ = eggart.paw(px_ * k, py_ * k)
                P_ = art_pts(pl_, pd_, ox, oy, 'F')
                if not clear(art_box(*P_), 'F'):
                    break
                draw_art(*P_, 'F'); trail += 1
            print('silk: easter egg: cat asleep under the face at (%.1f, %.1f), scale %.2f, %d paw prints' % (ox, oy, k, trail))
            break
    else:
        print('silk: easter egg: sleeping cat skipped (no clear spot under the face)')
    wx, wy = centre('SW301')               # face side, under the panel: the face switch is MAO's nose
    label('boop', [], 'F', 0.8, gname=EGG, anchor=(wx, wy, 2.2), quiet=True)
    label('meow', [(sx + dx_, sy + dy_, 270) for dx_ in (2.2, 1.6, 2.8, 0.8) for dy_ in (4.6, -4.6, 3.6, -3.6, 5.4, -5.4)],
          'B', 0.8, gname=EGG, inside=('LS501',), quiet=True)          # under the speaker
    qx, qy = centre('Q101')                # the reverse-polarity FET: a cell put in backwards is survived
    jx_, jy_ = centre('J102')              # ... or at the cell's own plug
    label('9 lives', ring(qx, qy, 1.6) + ring(jx_, jy_, 2.6) +
          [(jx_ + dx_ / 5, jy_ + dy_ / 5, a_) for dx_ in range(-40, 41, 2) for dy_ in range(-40, 41, 2) for a_ in (0, 270)],
          'B', 0.8, gname=EGG, quiet=True)
    label('MADE FOR\nBAD IDEAS', [(x_, y_) for x_, y_ in grid(*BAD_IDEAS_AT, face, 0.4)][:1500], 'F', 0.8, gname=EGG,
          quiet=True)

    for sd_ in ('F', 'B'):                 # the held room is for the references, not a placed item
        placed[sd_] = [q for q in placed[sd_] if q not in held[sd_]]
    tb = b.GetTitleBlock()                 # drawings plotted from the board (assembly PDFs) carry the identity too
    tb.SetTitle('MAO_MAIN A0')
    tb.SetRevision('A0')
    tb.SetDate(DATE)
    tb.SetCompany('ODD JOBS')
    tb.SetComment(0, 'Main board, 4 layers (JLC04161H-1080), 1.6 mm, ENIG, black mask')
    tb.SetComment(1, 'docs/hardware/mao-rev-a0-review.md')
    pcb.SaveBoard(str(TARGET), b)
    import project
    project.write()
    rec = {'source': data['source'], 'source_sha256': data['source_sha256'], 'colour': 'white',
           'placements': [{'layer': 'F.SilkS', 'width_mm': w, 'height_mm': round(h, 3), 'centre_mm': list(BRAND_CENTRE)},
                          {'layer': 'B.SilkS (mirrored to read from the back)', 'width_mm': bw, 'centre_mm': list(b_mark)}],
           'artwork_licence': data['license']}
    (ROOT / 'outputs' / 'BRAND-PLACEMENT.json').write_text(json.dumps(rec, indent=2) + '\n')
    print('silk: %d items' % sum(len(list(g.GetItems())) for g in groups.values()))


# service field: preferred spot per pad, ('side', +1 right / -1 left) upright, ('flat', -1 above / +1 below)
FIELD_GAP = 0.4                    # between two service-field names: half the 0.8 mm name height (closer reads as
                                   # one word); 0.5 has no arrangement in the 2.8 mm grid
FIELD_SHARED = []                  # (every probe pad carries its own name)
FIELD_SPOT = {ref: [('side', 1) if spec[1] == 'right' else ('flat', -1)] for ref, spec in m.FIELD_NAMES.items()}
BRAND_CENTRE = (-6.6, 8.6)       # F, under the module's left half: the calmest free field on the face side
SN_SPOTS = [(x_ / 10, y_ / 10) for y_ in (140, 142, 138, 144, 136, 146) for x_ in (0, 2, -2, 4, -4, 6, -6, 8, -8)]
                                 # F: centre of the 6 x 6 mm S/N field (the back has no clear 6 x 6 field)
B_MARK_W = 4.6                   # B mark width: the artwork's finest strokes (0.07 mm at 2 mm) reach 0.16 mm
B_MARK_AT = m.B_IDENT_AT          # B: the reserved via-free spot (mechanical.B_IDENT_KEEPOUT); a clear field outside the cell wins
CAT_AT = (-4.3, -7.7)            # F, under the panel: preferred centre of the sleeping cat
BAD_IDEAS_AT = (6.0, 6.0)        # F, under the panel: preferred spot of the ODD JOBS line (rule 175), two lines

if __name__ == '__main__':
    if '--build' not in sys.argv:
        clean()
        sys.stdout.flush()
        os.execv(sys.executable, [sys.executable, __file__, '--build'])
    main()
    sys.stdout.flush()
    os._exit(0)
