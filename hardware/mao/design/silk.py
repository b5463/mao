# Brand placement adapted from b5463/kino-d4 hardware/pcb/kino-d4-carrier-a0/design/apply_brand.py @ 68aba75.
"""MAO_MAIN A0 silkscreen: identity, function labels, service marks (KiCad python). Idempotent.

ODD JOBS 41-43, 92-103, 172-177:
  F.SilkS  ODD JOBS maker's mark (approved artwork from brand/odd-jobs-symbol.json, traced from the
           supplied PNG, never redrawn) above MAO / MAIN A0 / date, on the calm lower-left of the face
           side; LCD, TOP and the antenna keep-clear note
  B.SilkS  maker's mark, product line and S/N box in the corner visible with the cell fitted; connector
           functions with pin cues (USB, BAT - T +, SPK+/SPK-, LRA, REAR, TAG-CONNECT), test-pad
           function names (never bare TPn), BAT LINK at the 0R link
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
GROUPS = ('MAO silk F', 'MAO silk B', 'ODD JOBS maker mark')
STROKE = 0.15


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

    def rect(x0, y0, x1, y1, side='B'):
        s = pcb.PCB_SHAPE(b)
        s.SetShape(pcb.SHAPE_T_RECT)
        s.SetStart(at(x0, y0))
        s.SetEnd(at(x1, y1))
        s.SetWidth(pcb.FromMM(STROKE))
        s.SetLayer(pcb.B_SilkS if side == 'B' else pcb.F_SilkS)
        b.Add(s)
        group('MAO silk ' + side).AddItem(s)

    def line(x0, y0, x1, y1, side='B'):
        s = pcb.PCB_SHAPE(b)
        s.SetShape(pcb.SHAPE_T_SEGMENT)
        s.SetStart(at(x0, y0))
        s.SetEnd(at(x1, y1))
        s.SetWidth(pcb.FromMM(STROKE))
        s.SetLayer(pcb.B_SilkS if side == 'B' else pcb.F_SilkS)
        b.Add(s)
        group('MAO silk ' + side).AddItem(s)

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
    graphics = {'F': [], 'B': []}
    for f in fps.values():
        for p in f.Pads():
            r = p.GetBoundingBox()
            for sd, lay in (('F', pcb.F_Mask), ('B', pcb.B_Mask)):
                if p.IsOnLayer(lay):
                    pad_boxes[sd].append((mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom())))
        if not f.GetReference().startswith('TP'):
            bodies[side_of(f)] += courtyard_boxes(f)
        if f.GetReference().startswith('FID'):
            x, y = centre(f.GetReference())
            bodies[side_of(f)].append((x - 2.05, y - 2.05, x + 2.05, y + 2.05))
        for g in f.GraphicalItems():
            if g.GetLayer() in (pcb.F_SilkS, pcb.B_SilkS) and not isinstance(g, pcb.PCB_TEXT):
                r = g.GetBoundingBox()
                graphics['B' if g.GetLayer() == pcb.B_SilkS else 'F'].append(
                    (mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom())))
    vias = {'F': [], 'B': []}               # silk never prints over a via (tented bumps read as noise)
    for t in b.GetTracks():
        if isinstance(t, pcb.PCB_VIA):
            x, y, r = mm(t.GetPosition().x), mm(t.GetPosition().y), pcb.ToMM(t.GetWidth(pcb.F_Cu)) / 2
            for sd in ('F', 'B'):
                vias[sd].append((x - r, y - r, x + r, y + r))
    placed = {'F': [], 'B': []}

    def why(bx, sd, text_gap=0.5):
        for q in pad_boxes[sd]:
            if overlap(bx, q, 0.2): return 'pad %s' % (tuple(round(v, 2) for v in q),)
        for q in bodies[sd]:
            if overlap(bx, q, 0.0): return 'body %s' % (tuple(round(v, 2) for v in q),)
        for q in graphics[sd]:
            if overlap(bx, q, 0.05): return 'silk %s' % (tuple(round(v, 2) for v in q),)
        for q in vias[sd]:
            if overlap(bx, q, 0.05): return 'via %s' % (tuple(round(v, 2) for v in q),)
        for q in placed[sd]:
            if overlap(bx, q, text_gap): return 'text %s' % (tuple(round(v, 2) for v in q),)
        if not all(math.hypot(x, y) < m.PCB_R - 0.5 for x in (bx[0], bx[2]) for y in (bx[1], bx[3])):
            return 'edge'
        return None

    def clear(bx, sd):
        return why(bx, sd) is None

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

    def label(s_, cands, sd, size=0.8, bold=False, angle=0, gname=None, anchor=None):
        """First clear candidate (centre positions, optionally (x, y, angle)), then the nearest clear spot round
        `anchor` (x, y, r); the first candidate, flagged, if none."""
        cands = [c if len(c) == 3 else (c[0], c[1], angle) for c in cands]
        if anchor:
            cands = cands + ring(*anchor)
        t = text(s_, cands[0][0], cands[0][1], sd, size, angle=angle, bold=bold, gname=gname)
        for x, y, a_ in cands:
            t.SetPosition(at(x, y))
            t.SetTextAngle(pcb.EDA_ANGLE(a_, pcb.DEGREES_T))
            if clear(tbox(t), sd):
                break
        else:
            reasons = []
            for x, y, a_ in cands:
                t.SetPosition(at(x, y))
                t.SetTextAngle(pcb.EDA_ANGLE(a_, pcb.DEGREES_T))
                reasons.append('(%.1f,%.1f,%d) %s' % (x, y, a_, why(tbox(t), sd)))
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
    for s_, dy, size, bold in (('MAO', 1.3, 1.3, True), ('MAIN A0', 3.0, 0.9, False), (DATE, 4.35, 0.8, False)):
        t = text(s_, bx, by + h / 2 + dy, 'F', size, bold=bold, gname='ODD JOBS maker mark')
        placed['F'].append(tbox(t))
    sy = by + h / 2 + 6.2                  # S/N field (ODD JOBS 101): the name, then a box for a sticker or laser mark
    t = text('S/N', bx - 1.85, sy, 'F', 0.8)
    placed['F'].append(tbox(t))
    rect(bx - 0.2, sy - 0.8, bx + 3.05, sy + 0.8, 'F')
    placed['F'].append((bx - 0.2, sy - 0.8, bx + 3.05, sy + 0.8))

    # ---- face side function labels --------------------------------------------------------------------
    jx, jy = centre('J301')                # beside the FPC connector, on the side the tail comes from
    label('LCD', [(jx - 4.0, jy, 90), (jx - 4.4, jy, 90), (jx - 1.6, jy - 7.0, 0), (jx - 1.6, jy + 7.0, 0)], 'F', 1.0)
    tx, ty = centre('J302')
    label('TOP', [(tx, ty + 2.3), (tx - 0.4, ty - 2.2), (tx + 0.8, ty + 2.6), (tx - 2.6, ty + 1.6)], 'F')
    label('ANTENNA  KEEP CLEAR', [(ANTENNA_TEXT[0], ANTENNA_TEXT[1])], 'F')

    # ---- back: maker's mark and product line beside the module, outside the cell (seen with the base off) --
    bw = 2.0
    bh = mark(B_MARK, bw, 'B')
    placed['B'].append((B_MARK[0] - bw / 2, B_MARK[1] - bh / 2, B_MARK[0] + bw / 2, B_MARK[1] + bh / 2))
    edge_x = B_MARK[0] - bw / 2 - 0.35     # the text block ends here (it reads from the back: mark first)
    for s_, y_, size, bold in (('MAO MAIN A0', B_MARK[1] - 0.6, 0.8, True), (DATE, B_MARK[1] + 0.75, 0.8, False)):
        t = text(s_, edge_x, y_, 'B', size, bold=bold)
        x0, _, x1, _ = tbox(t)
        t.SetPosition(at(edge_x - (x1 - x0) / 2, y_))
        placed['B'].append(tbox(t))

    # ---- back: connectors with their pin cues -----------------------------------------------------------
    label('USB', [(-4.4, -19.8), (-5.0, -18.2)], 'B', 1.0, bold=True)
    # battery plug: pin cues and name on the plug side, one aligned block (R108's link sits on the other)
    x2, _ = pad('J102', '2')
    t = text('- T +\nBAT', x2, 0.0, 'B', 0.8, bold=True)    # J102: 1 BAT- at the larger x, read first from the back
    _, y0_, _, _ = tbox(t)
    t.SetPosition(at(x2, 0.0 + (-0.39 + 0.25) - y0_))       # top of the block 0.25 mm below the plug's body
    placed['B'].append(tbox(t))
    for ref, s_ in (('J502', 'SPK-'), ('J501', 'SPK+')):    # beside each spring, level with it
        x, y = centre(ref)
        label(s_, [(x + 3.3, y), (x + 2.7, y), (x + 3.0, y - 0.3), (x + 3.0, y + 0.3)], 'B')
    lx, ly = centre('J503')
    label('LRA', [(lx - 2.05, ly + 2.3), (lx - 2.5, ly - 2.4), (lx + 1.25, ly + 3.5)], 'B')
    rx, ry = centre('J303')
    label('REAR', [(rx, ry - 2.0), (rx, ry + 2.0), (rx + 3.6, ry), (rx, ry - 2.4)], 'B')
    gx, gy = centre('J201')                # Tag-Connect: name along its module-side column
    label('SERVICE', [(gx - 2.6, gy, 270), (gx - 0.3, gy + 3.65, 0), (gx + 2.5, gy, 270)], 'B')
    kx, ky = centre('R108')
    label('BAT\nLINK', [(kx + 2.65, ky - 0.4), (kx + 2.8, ky - 0.4)], 'B')

    # ---- back: test pads by function (ODD JOBS 103) --------------------------------------------------------
    # The service field is labelled as one set: every name upright beside its pad or level above / below it,
    # chosen together so no two names crowd each other (0.4 mm between field names) and none sits on a pad,
    # a body or a via; the preferred side of each pad (FIELD_SPOT) wins where the field allows it.
    tps = sorted((r for r in fps if r.startswith('TP')), key=lambda r: int(r[2:]))
    def box_of(s_, x, y, a_, sd='B'):
        return measure(s_, x, y, sd, 0.8, a_)
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
                    if why(bx_, 'B') is None:
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
        """Most-constrained pad first; every choice prunes the neighbours' spots it crowds (forward check)."""
        budget[0] -= 1
        if budget[0] < 0 or (best[0] is not None and cost >= best[0]):
            return
        if not domains:
            best[0], best[1] = cost, dict(chosen); return
        ref = min(domains, key=lambda r_: len(domains[r_]))
        for c_, cand, bx_ in domains[ref]:
            rest = {r_: [o for o in d if not overlap(bx_, o[2], FIELD_GAP)] for r_, d in domains.items() if r_ != ref}
            if all(rest.values()):
                chosen[ref] = (cand, bx_)
                search(rest, chosen, cost + c_)
                del chosen[ref]
    search({r_: options[r_] for r_ in field}, {}, 0.0)
    if os.environ.get('SILK_DEBUG'):
        print('silk: field search budget left', budget[0], 'cost', best[0])
    if best[1] is None:
        print('silk: service field labels: no complete arrangement')
    for ref, (cand, bx_) in (best[1] or {}).items():
        t = text(fps[ref].GetValue(), cand[0], cand[1], 'B', 0.8, angle=cand[2])
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
        label(f.GetValue(), flat_c + [(cx_, cy_, 270) for cx_, cy_ in side_c], 'B', anchor=(x, y, r))

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
                          {'layer': 'B.SilkS (mirrored to read from the back)', 'width_mm': 3.2, 'centre_mm': list(B_MARK)}],
           'artwork_licence': data['license']}
    (ROOT / 'outputs' / 'BRAND-PLACEMENT.json').write_text(json.dumps(rec, indent=2) + '\n')
    print('silk: %d items' % sum(len(list(g.GetItems())) for g in groups.values()))


# service field: preferred spot per pad, ('side', +1 right / -1 left) upright, ('flat', -1 above / +1 below)
FIELD_GAP = 0.5                    # between two service-field names (as everywhere: closer reads as one word)
FIELD_SHARED = [('TP1', 'TP16')]   # the row-2 ground pair: one GND name between them
FIELD_SPOT = {ref: [('side', 1)] for ref in ('TP1', 'TP2', 'TP3', 'TP4', 'TP5', 'TP7', 'TP8', 'TP9', 'TP10', 'TP11', 'TP16')}
BRAND_CENTRE = (-6.6, 8.6)       # F, under the module's left half: the calmest free field on the face side
ANTENNA_TEXT = (3.4, 21.5)       # F, along the notch, beside the identity column
B_MARK = (19.4, 12.0)            # B, beside the module's right column, below the cell: seen when the base is off

if __name__ == '__main__':
    if '--build' not in sys.argv:
        clean()
        sys.stdout.flush()
        os.execv(sys.executable, [sys.executable, __file__, '--build'])
    main()
    sys.stdout.flush()
    os._exit(0)
