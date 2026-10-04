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
FIELD = {'TP1', 'TP2', 'TP3', 'TP4', 'TP5', 'TP8', 'TP9', 'TP10', 'TP11', 'TP12', 'TP16'}
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
    placed = {'F': [], 'B': []}

    def why(bx, sd):
        for q in pad_boxes[sd]:
            if overlap(bx, q, 0.2): return 'pad %s' % (tuple(round(v, 2) for v in q),)
        for q in bodies[sd]:
            if overlap(bx, q, 0.0): return 'body %s' % (tuple(round(v, 2) for v in q),)
        for q in graphics[sd]:
            if overlap(bx, q, 0.05): return 'silk %s' % (tuple(round(v, 2) for v in q),)
        for q in placed[sd]:
            if overlap(bx, q, 0.5): return 'text %s' % (tuple(round(v, 2) for v in q),)
        if not all(math.hypot(x, y) < m.PCB_R - 0.5 for x in (bx[0], bx[2]) for y in (bx[1], bx[3])):
            return 'edge'
        return None

    def clear(bx, sd):
        return why(bx, sd) is None

    def label(s_, cands, sd, size=0.8, bold=False, angle=0, gname=None):
        """First clear candidate (centre positions, optionally (x, y, angle)); the first, flagged, if none."""
        cands = [c if len(c) == 3 else (c[0], c[1], angle) for c in cands]
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

    # ---- identity, face side (F): maker's mark over MAO / MAIN A0 / date --------------------------------
    w = 5.0
    h = mark(BRAND_CENTRE, w, 'F')
    bx, by = BRAND_CENTRE
    placed['F'].append((bx - w / 2, by - h / 2, bx + w / 2, by + h / 2))
    for s_, dy, size, bold in (('MAO', 1.3, 1.3, True), ('MAIN A0', 3.0, 0.9, False), (DATE, 4.35, 0.8, False)):
        t = text(s_, bx, by + h / 2 + dy, 'F', size, bold=bold, gname='ODD JOBS maker mark')
        placed['F'].append(tbox(t))

    # ---- face side function labels --------------------------------------------------------------------
    jx, jy = centre('J301')
    label('LCD', [(jx - 2.9, jy), (jx - 3.3, jy)], 'F', 1.0, angle=90)            # beside the FPC land
    tx, ty = centre('J302')
    label('TOP', [(tx, ty + 2.3), (tx - 0.4, ty - 2.2), (tx + 0.8, ty + 2.6), (tx - 2.6, ty + 1.6)], 'F')
    label('ANTENNA  KEEP CLEAR', [(ANTENNA_TEXT[0], ANTENNA_TEXT[1])], 'F')

    # ---- back: maker's mark, product line and S/N box in the corner that stays visible with the cell in --
    bh = mark(B_MARK, 2.8, 'B')
    placed['B'].append((B_MARK[0] - 1.4, B_MARK[1] - bh / 2, B_MARK[0] + 1.4, B_MARK[1] + bh / 2))
    px, py = PRODUCT_B
    label('MAO MAIN A0', [(px, py)], 'B', 1.0, bold=True)
    label(DATE, [(px - 2.3, py + 1.55), (px - 2.3, py + 1.6), (px - 2.0, py + 1.55)], 'B', 0.8)      # the mark beside it says ODD JOBS
    sx, sy = SN_BOX
    rect(sx - 3.0, sy - 0.85, sx + 3.0, sy + 0.85)
    placed['B'].append((sx - 3.0, sy - 0.85, sx + 3.0, sy + 0.85))
    label('S/N', [(sx - 4.7, sy), (sx - 4.9, sy)], 'B')

    # ---- back: connectors with their pin cues -----------------------------------------------------------
    label('USB', [(-4.4, -19.8), (-5.0, -18.2)], 'B', 1.0, bold=True)
    # battery plug: pin cues and name on the plug side, one aligned block (R108's link sits on the other)
    x2, _ = pad('J102', '2')
    t = text('-  T  +\nBAT', x2, -0.39 + 1.4, 'B', 0.8, bold=True)    # J102: 1 BAT-, 2 NTC, 3 BAT+ (mirrored view)
    placed['B'].append(tbox(t))
    for ref, s_ in (('J501', 'SPK+'), ('J502', 'SPK-')):
        x, y = centre(ref)
        label(s_, [(x + 3.3, y), (x + 2.4, y + 2.2 if s_ == 'SPK-' else y - 2.2), (x + 3.0, y)], 'B')
    lx, ly = centre('J503')
    label('LRA', [(lx + 2.9, ly + 3.0), (lx + 3.1, ly + 3.2), (lx, ly - 2.4)], 'B')
    rx, ry = centre('J303')
    label('REAR', [(rx, ry - 2.0), (rx, ry + 2.0), (rx + 3.6, ry), (rx, ry - 2.4)], 'B')
    gx, gy = centre('J201')
    label('SERVICE', [(gx + 1.4, gy + 2.75), (gx + 1.8, gy + 2.75)], 'B')
    kx, ky = centre('R108')
    label('BAT\nLINK', [(kx + 2.65, ky - 0.4), (kx + 2.8, ky - 0.4)], 'B')

    # ---- back: test pads by function (ODD JOBS 103), each name in the first clear spot round its pad --------
    for ref in sorted((r for r in fps if r.startswith('TP')), key=lambda r: int(r[2:])):
        f = fps[ref]
        x, y = centre(ref)
        r = 0.6 if f.GetValue() in ('GND', '3V3', 'SYS', 'BAT', 'VBUS') else 0.5
        wd = 0.6 * len(f.GetValue()) + 0.2
        side_c = [(x + r + 0.68, y), (x - (r + 0.68), y)]          # vertical, beside the pad
        flat_c = [(x, y + r + 0.7), (x, y - (r + 0.7)), (x + r + 0.45 + wd / 2, y), (x - (r + 0.45 + wd / 2), y)]
        if ref in FIELD:                   # service field: names beside their pads, reading upwards; row 2's on
            row2 = abs(y - (-2.9)) < 0.5   # the right, rows 1 and 3 on the left, so stacked names never touch
            label(f.GetValue(), side_c if row2 else side_c[::-1], 'B', angle=270)
        else:
            label(f.GetValue(), flat_c + [(cx_, cy_, 270) for cx_, cy_ in side_c], 'B')

    tb = b.GetTitleBlock()                 # drawings plotted from the board (assembly PDFs) carry the identity too
    tb.SetTitle('MAO_MAIN A0')
    tb.SetRevision('A0')
    tb.SetDate(DATE)
    tb.SetCompany('ODD JOBS')
    tb.SetComment(0, 'Main board, 6 layers, 1.6 mm, ENIG, black mask')
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


BRAND_CENTRE = (5.5, 13.6)       # F, under the module's right half: no parts, only a few inner-layer runs
ANTENNA_TEXT = (-4.6, 21.5)      # F, along the notch, clear of the maker's mark column
B_MARK = (12.1, 12.6)            # B, 4 o'clock corner outside the cell: what you see when the base is off
PRODUCT_B = (19.4, 12.6)
SN_BOX = (17.4, 15.95)           # label / Data Matrix area (ODD JOBS 101)

if __name__ == '__main__':
    if '--build' not in sys.argv:
        clean()
        sys.stdout.flush()
        os.execv(sys.executable, [sys.executable, __file__, '--build'])
    main()
    sys.stdout.flush()
    os._exit(0)
