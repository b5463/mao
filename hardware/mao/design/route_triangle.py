"""Face-press switches to an even triangle (owner decision 2026-10-07), applied to the routed board (KiCad python).

    <KiCad python> route_triangle.py place   SW301..303 to mechanical.PRESS_TRIPOD; the TAIL WELL rule area to
                                             mechanical.TAIL_WELL (shortened so the 84 deg switch clears it)
    <KiCad python> route_triangle.py rip     the copper the switches now sit on: any F.Cu track or via in a switch's
                                             two keep-outs (they take no track, via or pour), and any F.Cu track or
                                             via of another net within 0.2 mm of a switch pad; writes the list to
                                             .cache/mao-routing/triangle-rip.json
then: drc, prune (until nothing dangles; the old legs' PRESS_N / GND stubs go too), gridroute, drc.

Where the triangle went (searched by a sweep over rotation 0..120 deg, radius 9..16.6 mm and the switch's turn
relative to the centre, against every F courtyard, the tail well and slot clears, the fiducial clears and the boss
keep-outs): 85 / 205 / 325 deg clockwise from 12 o'clock, r 13.5 / 14.25 / 13.5 (0.18 mm to the nearest courtyard, U503); SW302
turned 90 deg on its centre and 0.75 mm further out (see mechanical.PRESS_TRIPOD). No other turn of the switch fits
any rotation; the copper it displaced is listed in the rip file.
"""
import json
import os
import sys

import pcbnew as pcb

import mechanical as m
from board import TARGET, pt
from netrules import CACHE

SW = ('SW301', 'SW302', 'SW303')
NUDGE = {}                       # parts moved out of the switches' way (none needed)
CLR = 0.2


def place():
    b = pcb.LoadBoard(str(TARGET))
    out = []
    for f in b.GetFootprints():
        ref = f.GetReference()
        if ref in m.PRESS_TRIPOD:
            a, r, rot = m.PRESS_TRIPOD[ref]
            x, y = m.polar(r, a)
            f.SetPosition(pt(50 + x, 50 + y))
            f.SetOrientationDegrees(rot)
            out.append('%s at (%.3f, %.3f) %g' % (ref, x, y, rot))
        if ref in NUDGE:
            dx, dy = NUDGE[ref]
            q = f.GetPosition()
            f.SetPosition(pcb.VECTOR2I(q.x + pcb.FromMM(dx), q.y + pcb.FromMM(dy)))
            out.append('%s nudged (%+.1f, %+.1f)' % (ref, dx, dy))
    for z in b.Zones():
        if z.GetIsRuleArea() and z.GetZoneName() == 'TAIL WELL':
            x0, y0, x1, y1 = m.TAIL_WELL
            o = z.Outline()
            o.RemoveAllContours()
            o.NewOutline()
            for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
                o.Append(pt(50 + x, 50 + y))
            out.append('TAIL WELL x %.1f..%.1f' % (x0, x1))
    pcb.SaveBoard(str(TARGET), b)
    print('triangle place: ' + '; '.join(out), flush=True)
    os._exit(0)


def rip():
    b = pcb.LoadBoard(str(TARGET))
    mm = lambda v: pcb.ToMM(v) - 50
    keepouts, pads, moved = [], [], []
    for f in b.GetFootprints():
        if f.GetReference() in SW:
            keepouts += [z.Outline() for z in f.Zones() if z.GetIsRuleArea()]
            pads += [p for p in f.Pads() if p.IsOnLayer(pcb.F_Cu)]
        if f.GetReference() in NUDGE:
            moved += list(f.Pads())
    clr = pcb.FromMM(CLR)
    doomed, why = [], []
    for t in b.GetTracks():
        via = isinstance(t, pcb.PCB_VIA)
        if not via and t.GetLayer() != pcb.F_Cu:
            continue
        half = (t.GetWidth(pcb.F_Cu) if via else t.GetWidth()) // 2
        if via:
            hit_ko = any(k.Collide(t.GetPosition(), half) for k in keepouts)
        else:
            seg = pcb.SEG(t.GetStart(), t.GetEnd())
            hit_ko = any(k.Collide(seg, half) for k in keepouts)
        shape = t.GetEffectiveShape(pcb.F_Cu)
        hit_pad = [p for p in pads if p.GetNetCode() != t.GetNetCode() and
                   p.GetEffectiveShape(pcb.F_Cu).Collide(shape, clr)]
        if not hit_pad and t.GetNetname() in ('+3V3', 'IMU_INT1') and not via:   # R404's two pad stubs
            hit_pad = [p for p in moved if p.GetNetCode() == t.GetNetCode() and
                       p.GetEffectiveShape(pcb.F_Cu).Collide(shape, pcb.FromMM(0.4))]
        if hit_ko or hit_pad:
            doomed.append(t)
            ends = [mm(t.GetPosition().x), mm(t.GetPosition().y)] if via else \
                [[mm(t.GetStart().x), mm(t.GetStart().y)], [mm(t.GetEnd().x), mm(t.GetEnd().y)]]
            why.append({'net': t.GetNetname(), 'kind': 'via' if via else 'F', 'at': ends,
                        'reason': 'keep-out' if hit_ko else 'pad ' + ','.join(sorted({p.GetParentFootprint().GetReference()
                                                                                   for p in hit_pad}))})
    (CACHE / 'triangle-rip.json').write_text(json.dumps(why, indent=1))
    n = len(doomed)
    for t in doomed:
        b.Remove(t)
    pcb.SaveBoard(str(TARGET), b)
    nets = sorted({w['net'] for w in why})
    print('triangle rip: %d items (%s)' % (n, ', '.join(nets)), flush=True)
    os._exit(0)


# SW302's west keep-out covers the module's pins 12-14 (LCD_BL, AMP_SD, LCD_CS) and the vias they dropped through;
# their escape on B is boxed in by the neighbouring pins' fan-out, so that whole fan-out is re-routed together
CLUSTER = (tuple(os.environ.get('TRI_NETS', 'LCD_RST_N,LCD_PWR_EN,HALL_FAST,IMU_INT1,LCD_MOSI').split(',')),
           tuple(float(v) for v in os.environ.get('TRI_BOX', '-13.2,8.6,-5.0,15.6').split(',')))


def rip_cluster():
    b = pcb.LoadBoard(str(TARGET))
    mm = lambda v: pcb.ToMM(v) - 50
    nets, (x0, y0, x1, y1) = CLUSTER
    inside = lambda p: x0 <= mm(p.x) <= x1 and y0 <= mm(p.y) <= y1
    doomed = [t for t in b.GetTracks() if t.GetNetname() in nets and
              any(inside(e) for e in ((t.GetPosition(),) if isinstance(t, pcb.PCB_VIA) else (t.GetStart(), t.GetEnd())))]
    n = len(doomed)
    for t in doomed:
        b.Remove(t)
    pcb.SaveBoard(str(TARGET), b)
    print('triangle cluster rip: %d items of %s' % (n, ', '.join(nets)), flush=True)
    os._exit(0)


# Hand-drawn copper the grid router could not find (its pad-entry rules; checked with a free-space search and DRC):
# PRESS_N from SW302's north leg straight up its centre line and 45 deg onto the module pin 18 trunk via.
ADD = [
    ('PRESS_N', 'F', 0.2, [(-3.04, 10.89), (-3.04, 8.6), (-4.25, 7.39), (-4.25, 7.25)]),
]
# LCD_TE, lifted so AMP_SD could leave module pin 13 north-west: J301.5 east on B, L3 round the module's top-left
# corner, F across the centre under the panel, L3 to 3 o'clock, B into TP19 (module pin 31's test pad)
ADD_TE = [
    ('LCD_TE', 'B', 0.2, [(-11.15, 2.25), (-8.75, 2.25), (-8.4, 2.6)]), ('LCD_TE', 'V', None, [(-8.4, 2.6)]),
    ('LCD_TE', 'I2', 0.2, [(-8.4, 2.6), (-8.1, 2.6), (-7.2, 3.5), (-6.2, 3.5), (-5.5, 2.8), (-5.5, 2.1), (-6.3, 1.3),
                           (-6.3, -1.6)]), ('LCD_TE', 'V', None, [(-6.3, -1.6)]),
    ('LCD_TE', 'F', 0.2, [(-6.3, -1.6), (1.9, -1.6), (3.6, 0.1)]), ('LCD_TE', 'V', None, [(3.6, 0.1)]),
    ('LCD_TE', 'I2', 0.2, [(3.6, 0.1), (3.6, 0.3), (7.3, 4.0)]), ('LCD_TE', 'V', None, [(7.3, 4.0)]),
    ('LCD_TE', 'B', 0.2, [(7.3, 4.0), (7.3, 5.6), (9.2, 7.5)]),
    # the +3V3 line feeding the panel rail switch (U105.A2, C110) lost its L3 plane: its via sat in a pocket of the
    # plane that LCD_TE and the re-routed lines now seal off. It drops on F and runs on B to a via in the main plane
    ('+3V3', 'F', 0.25, [(-6.93, 3.85), (-6.93, 2.6)]), ('+3V3', 'V', None, [(-6.93, 2.6)]),
    ('+3V3', 'B', 0.25, [(-6.93, 2.6), (-6.93, 1.45), (-8.43, -0.05)]), ('+3V3', 'V', None, [(-8.43, -0.05)]),
]
RIP_TE = [('+3V3', 'V', (-6.93, 2.9), None), ('+3V3', 'F', (-6.93, 3.85), (-6.93, 2.9))]
# the S/N field (silk.py) lost its old spot to SW301; the clear 6 x 6 mm field at (7.2, -2.6) has one GND stitching via on
# its east edge (silk over a tented via prints badly), which goes
RIP_SN = [('GND', 'V', (10.0, -1.0), None)]


# the dangling-stub cleanup removed the LCD_DC link at its F junction with its 0.1 mm stub (same start point)
ADD_FIX = [('LCD_DC', 'F', 0.2, [(-10.45, 12.6), (-10.0, 12.15)])]


def drop_exact():
    """Remove exactly the tracks / vias DRC reports dangling, matched by net, layer, length and an end at the marker
    (drop_dangling.py matches by position only, which takes a real track sharing the stub's start)."""
    import json, re
    from board import DRC_JSON
    drc = json.loads(DRC_JSON.read_text(encoding='utf-8'))
    want = []
    for v in drc['violations']:
        if v['type'] not in ('track_dangling', 'via_dangling'):
            continue
        for it in v['items']:
            m = re.match(r'(Track|Via) \[(.*?)\](?: on ([^,\s]+))?(?:.*length ([0-9.]+) mm)?', it['description'])
            if m:
                want.append((m.group(1), m.group(2), m.group(3), float(m.group(4) or 0), it['pos']['x'] - 50, it['pos']['y'] - 50))
    b = pcb.LoadBoard(str(TARGET))
    mm = lambda v: pcb.ToMM(v) - 50
    doomed = []
    for kind, net, layer, length, x, y in want:
        for t in b.GetTracks():
            if t.GetNetname() != net or any(t is d for d in doomed):
                continue
            if kind == 'Via' and isinstance(t, pcb.PCB_VIA) and abs(mm(t.GetPosition().x) - x) < 0.02 and abs(mm(t.GetPosition().y) - y) < 0.02:
                doomed.append(t); break
            if kind == 'Track' and not isinstance(t, pcb.PCB_VIA) and b.GetLayerName(t.GetLayer()) == layer and                     abs(pcb.ToMM(t.GetLength()) - length) < 0.002 and                     any(abs(mm(e.x) - x) < 0.02 and abs(mm(e.y) - y) < 0.02 for e in (t.GetStart(), t.GetEnd())):
                doomed.append(t); break
    n = len(doomed)
    for t in doomed:
        b.Remove(t)
    pcb.SaveBoard(str(TARGET), b)
    print('triangle drop: %d of %d dangling items' % (n, len(want)), flush=True)
    os._exit(0)


# the +3V3 drop's first via sits in a 1 mm2 pocket of the L3 plane that LCD_TE and I2C_SCL enclose; the pocket is
# kept from filling (no pour there), so the via passes through and the plane stays one piece (plane_check.py)
POCKET = (-7.9, 2.2, -6.2, 3.3)


def pocket():
    from build_pcb import keepout
    b = pcb.LoadBoard(str(TARGET))
    x0, y0, x1, y1 = POCKET
    keepout(b, [pcb.In2_Cu], [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], 'L3 POCKET',
            tracks=False, vias=False, pads=False, pours=True)
    pcb.SaveBoard(str(TARGET), b)
    print('triangle pocket: L3 no-pour area', POCKET, flush=True)
    os._exit(0)


def add():
    from handroute import _add, _rip
    b = pcb.LoadBoard(str(TARGET))
    if 'te-rip' in sys.argv[2:] or 'sn-rip' in sys.argv[2:]:   # own process: Remove() leaves the bindings unusable
        n = _rip(b, RIP_SN if 'sn-rip' in sys.argv[2:] else RIP_TE)
        pcb.SaveBoard(str(TARGET), b)
        print('triangle rip (te): %d items' % n, flush=True)
        os._exit(0)
    n = _add(b, ADD_FIX if 'fix' in sys.argv[2:] else (ADD_TE if 'te' in sys.argv[2:] else ADD))
    pcb.SaveBoard(str(TARGET), b)
    print('triangle add: %d items' % n, flush=True)
    os._exit(0)


if __name__ == '__main__':
    {'place': place, 'rip': rip, 'cluster': rip_cluster, 'add': add, 'drop': drop_exact, 'pocket': pocket}[sys.argv[1]]()
