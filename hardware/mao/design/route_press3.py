"""P3 whole-top press (owner decision 2026-10-07), applied to the routed board (KiCad python), in place.

    <KiCad python> build_sch.py first (circuit.py: SW302 / SW303 gone, SW301 = SKQGADE010), then:
    <KiCad python> route_press3.py place     SW301 to B at mechanical.PRESS_SWITCH; SW302 / SW303 removed; the debug
                                             pad TP19 steps 0.45 mm onto its own track junction, clear of SW301
    <KiCad python> route_press3.py well      the TAIL WELL rule area back to mechanical.TAIL_WELL (x 6..12: the
                                             face no longer has a switch at 3 o'clock)
    <KiCad python> route_press3.py rip       the copper SW301 now sits on: any track or via in its keep-outs (B.Cu
                                             once flipped), and any copper of another net within 0.2 mm of its pads
    <KiCad python> sync_fields.py            SW301's value / LCSC / MPN
then: drc, route_triangle.py drop (exact dangling), gridroute, drc, and the hand-drawn copper below (add).

Why the switch is where it is: on B the cell fills the board's centre, so a base-fixed steel finger (mechanical.FINGER)
reaches over the cell to the switch; (8.0, 3.8) is the nearest B spot to the centre that clears every courtyard and
the service field's names, once TP19 (LCD_TE's debug pad, no fixture position) moves.
"""
import os
import sys

import pcbnew as pcb

import mechanical as m
from board import TARGET, pt
from handroute import _add, _place

TP19_AT = (9.7, 7.85)
CLR = 0.2


def place():
    b = pcb.LoadBoard(str(TARGET))
    pl = {ref: (xy, rot, side) for ref, (xy, rot, side) in m.PRESS_SWITCH.items()}
    pl['TP19'] = (TP19_AT, 0.0, 'B')
    msg = _place(b, pl)
    gone = [f for f in b.GetFootprints() if f.GetReference() in ('SW302', 'SW303')]
    assert len(gone) == 2, 'SW302 / SW303 not on the board'
    for f in gone:
        b.Remove(f)
    pcb.SaveBoard(str(TARGET), b)
    print('press3 place: %s; removed SW302, SW303' % msg, flush=True)
    os._exit(0)


def well():
    b = pcb.LoadBoard(str(TARGET))
    for z in b.Zones():
        if z.GetIsRuleArea() and z.GetZoneName() == 'TAIL WELL':
            x0, y0, x1, y1 = m.TAIL_WELL
            o = z.Outline()
            o.RemoveAllContours()
            o.NewOutline()
            for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
                o.Append(pt(50 + x, 50 + y))
    pcb.SaveBoard(str(TARGET), b)
    print('press3 well: TAIL WELL x %.1f..%.1f' % (m.TAIL_WELL[0], m.TAIL_WELL[2]), flush=True)
    os._exit(0)


def rip():
    b = pcb.LoadBoard(str(TARGET))
    keepouts, pads = [], []
    for f in b.GetFootprints():
        if f.GetReference() in list(m.PRESS_SWITCH) + ['TP19']:
            keepouts += [(z.Outline(), set(z.GetLayerSet().Seq())) for z in f.Zones() if z.GetIsRuleArea()]
            pads += list(f.Pads())
    clr = pcb.FromMM(CLR)
    doomed = []
    for t in b.GetTracks():
        via = isinstance(t, pcb.PCB_VIA)
        half = (t.GetWidth(pcb.B_Cu) if via else t.GetWidth()) // 2
        lay = None if via else t.GetLayer()
        hit = False
        for poly, layers in keepouts:
            if via or lay in layers:
                if via and poly.Collide(t.GetPosition(), half):
                    hit = True
                elif not via and poly.Collide(pcb.SEG(t.GetStart(), t.GetEnd()), half):
                    hit = True
        for p in pads:
            if p.GetNetCode() == t.GetNetCode():
                continue
            for l_ in (pcb.F_Cu, pcb.B_Cu):
                if p.IsOnLayer(l_) and (via or lay == l_) and p.GetEffectiveShape(l_).Collide(t.GetEffectiveShape(l_), clr):
                    hit = True
        if hit:
            doomed.append(t)
    n = len(doomed)
    nets = sorted({t.GetNetname() for t in doomed})
    for t in doomed:
        b.Remove(t)
    pcb.SaveBoard(str(TARGET), b)
    print('press3 rip: %d items (%s)' % (n, ', '.join(nets)), flush=True)
    os._exit(0)


# Hand-drawn where the grid router's pad-entry rules found nothing (each route from a free-space search, checked by
# DRC): the charger status lines, I2C_SCL and HALL_A used to run north on B across where SW301 now sits.
ADD = {
    # I2C_SCL: its original F run to the fuel gauge (U103), joined to the re-routed trunk at x 6.25
    'I2C_SCL': [('I2C_SCL', 'F', 0.2, [(6.25, -0.9), (8.6, -0.9), (9.3, -1.6), (16.72, -1.6), (16.72, -9.02),
                                        (17.8, -10.1), (17.8, -13.1), (16.1, -14.8)]),
                ('I2C_SCL', 'V', None, [(16.1, -14.8)]), ('I2C_SCL', 'B', 0.2, [(16.1, -14.8), (15.6, -14.3)])],
    # the three escapes through the strip between the module's pad row and SW301 (0.5 mm fan-out vias)
    'ESCAPE': [('CHG_STAT1', 'B', 0.15, [(4.25, 8.3), (4.25, 7.8), (5.15, 6.9)]), ('CHG_STAT1', 'V', 0.5, [(5.15, 6.9)]),
               ('CHG_STAT2', 'B', 0.15, [(5.1, 8.3), (5.1, 8.0), (5.7, 7.4), (5.7, 7.1), (6.7, 6.1)]),
               ('CHG_STAT2', 'V', 0.5, [(6.7, 6.1)]),
               # I2C_SCL down SW301's channel (between its two keep-outs) to a via below it, onto its F run
               ('I2C_SCL', 'B', 0.15, [(5.95, 8.3), (5.95, 8.0), (6.5, 7.45), (7.1, 7.45), (7.5, 7.05), (7.5, 1.6)]),
               ('I2C_SCL', 'V', 0.5, [(7.5, 1.6)]), ('I2C_SCL', 'F', 0.2, [(7.5, 1.6), (7.5, 0.0), (8.4, -0.9), (8.6, -0.9)])],
    # CHG_STAT1 from its escape via: L3 west of SW301, F past its west pads, B past the service field, F into R118
    'CHG_STAT1': [('CHG_STAT1', 'I2', 0.2, [(5.15, 6.9), (5.15, 5.35), (3.65, 3.85)]), ('CHG_STAT1', 'V', 0.5, [(3.65, 3.85)]),
                  ('CHG_STAT1', 'F', 0.2, [(3.65, 3.85), (3.65, 2.8), (5.15, 1.3), (5.15, -1.75)]),
                  ('CHG_STAT1', 'V', 0.5, [(5.15, -1.75)]),
                  ('CHG_STAT1', 'B', 0.2, [(5.15, -1.75), (5.15, -2.0), (4.95, -2.2), (4.95, -7.65)]),
                  ('CHG_STAT1', 'V', 0.5, [(4.95, -7.65)]),
                  ('CHG_STAT1', 'F', 0.2, [(4.95, -7.65), (6.6, -9.3), (6.8, -9.5)])],
    # HALL_A from TP21: B round SW301's west pads to a via, L3 below LCD_TE's diagonal (clear of the gap that links the
    # IMU's patch of the +3V3 plane to the rest), F out to U301
    'HALL_A': [('HALL_A', 'B', 0.2, [(1.4, 3.05), (2.6, 3.05), (3.1, 2.55), (3.1, 1.7), (2.45, 1.05), (2.45, 0.4), (2.75, 0.1)]),
               ('HALL_A', 'V', 0.5, [(2.75, 0.1)]),
               ('HALL_A', 'I2', 0.2, [(2.75, 0.1), (3.4, -0.55), (4.45, -0.55), (6.8, 1.8), (6.8, 1.85)]),
               ('HALL_A', 'V', 0.5, [(6.8, 1.85)]),
               ('HALL_A', 'F', 0.2, [(6.8, 1.85), (6.95, 1.85), (7.3, 2.2), (8.3, 2.2), (10.4, 0.1), (10.85, 0.1), (14.1, 3.35),
                                     (14.1, 6.45), (20.4, 12.75), (21.75, 12.75), (21.98, 12.98)])],
    # LCD_TE from TP19: B down beside SW301's east pads, a via under its body, L3 diagonally to its west part
    'LCD_TE': [('LCD_TE', 'B', 0.2, [(9.7, 7.85), (9.7, 6.2), (8.9, 5.4)]), ('LCD_TE', 'V', 0.5, [(8.9, 5.4)]),
               ('LCD_TE', 'I2', 0.2, [(8.9, 5.4), (3.8, 0.3), (3.6, 0.3)])],
    # CHG_STAT2 from its escape via: F diagonally under the panel, B down past the service field, F into its old line
    'CHG_STAT2': [('CHG_STAT2', 'F', 0.2, [(6.7, 6.1), (7.7, 5.1), (7.7, 3.65), (10.65, 0.7)]),
                  ('CHG_STAT2', 'V', 0.5, [(10.65, 0.7)]),
                  ('CHG_STAT2', 'B', 0.2, [(10.65, 0.7), (10.65, -4.8), (11.0, -5.15)]), ('CHG_STAT2', 'V', 0.5, [(11.0, -5.15)]),
                  ('CHG_STAT2', 'F', 0.2, [(11.0, -5.15), (14.15, -8.3), (14.15, -11.77)])],
}


# the three module pins between the pad row and SW301 (28 CHG_STAT1, 29 CHG_STAT2, 30 I2C_SCL) share one 1.7 mm strip
# under the IMU: I2C_SCL's router-drawn escape is lifted so the three are drawn together
RIP_ESCAPE = [('I2C_SCL', 'B', (5.95, 8.3), (5.95, 7.1)), ('I2C_SCL', 'B', (5.95, 7.1), (6.4, 6.65)),
              ('I2C_SCL', 'B', (6.4, 6.65), (6.4, 5.65)), ('I2C_SCL', 'V', (6.4, 5.65), None)]


# LCD_TE's router-drawn B run down the same channel, lifted (re-drawn after the three escapes)
RIP_TE_CHANNEL = [('LCD_TE', 'B', (9.7, 7.85), (8.85, 7.85)), ('LCD_TE', 'B', (8.85, 7.85), (7.4, 6.4)),
                  ('LCD_TE', 'B', (7.4, 6.4), (7.4, 2.0)), ('LCD_TE', 'B', (7.4, 2.0), (6.35, 0.95)),
                  ('LCD_TE', 'B', (6.35, 0.95), (3.9, 0.95))]


def rip_escape():
    from handroute import _rip
    b = pcb.LoadBoard(str(TARGET))
    n = _rip(b, RIP_TE_CHANNEL if 'te' in sys.argv[2:] else RIP_ESCAPE)
    pcb.SaveBoard(str(TARGET), b)
    print('press3 rip escape: %d items' % n, flush=True)
    os._exit(0)


def add():
    b = pcb.LoadBoard(str(TARGET))
    n = _add(b, sum((ADD[k] for k in sys.argv[2:]), []))
    pcb.SaveBoard(str(TARGET), b)
    print('press3 add: %d items' % n, flush=True)
    os._exit(0)


if __name__ == '__main__':
    {'place': place, 'well': well, 'rip': rip, 'add': add, 'escape': rip_escape}[sys.argv[1]]()
