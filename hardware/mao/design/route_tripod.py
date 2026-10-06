"""Face-press tripod (owner decision 2026-10-06) applied to the routed MAO_MAIN A1 board (KiCad python), in place.

    <KiCad python> sync_fields.py --add       SW302 / SW303 onto the board at their placement.py spots, SW301's fields
    <KiCad python> route_tripod.py place      SW301 moves from the centre to its tripod spot (mechanical.PRESS_TRIPOD)
    <KiCad python> route_tripod.py rip        the single switch's leg joins and the copper the three switches displace
    <KiCad python> route_tripod.py add        the re-drawn copper (locked, 0/45 degrees)

The grid router is not deterministic, so the re-drawn copper was routed once on the committed board (grid_router.py
on the connections the rip left open, then by hand where it took detours) and is written out here, the way
route_review.py records the design review: a re-run of these three phases on the board as committed before the
tripod (d471249) reproduces it. The RIP list names that board's copper and matches only it.

Where the switches went (mechanical.PRESS_TRIPOD; the SKQG footprint's two F.Cu keep-outs under its body take no
track, via or pour):
  SW301  70 deg, r 17.6: the pocket between the tail well, the IR receiver's courtyard and the charger's BAT
         capacitor; the I2C pair, which crossed it diagonally, and one GND stitching via move
  SW302 190 deg, r 14.5: the I2S clock / data lanes pass between its keep-outs; IMU_INT1 and HALL_FAST move
  SW303 310 deg, r 14.5: an empty field between the panel rail and AMP_SD; one GND stitching via moves
The old switch's PRESS_N bar (y -4.35) and GND bar (y -0.65) go; its two GND vias stay as stitching. PRESS_N
reaches the three switches from the existing network (L3 trunk at x -4.25, the pull-up R318 / C308 on F).
"""
import sys

import mechanical as m
from handroute import run

V = 'V'

PLACE = {'SW301': (m.polar(m.PRESS_TRIPOD['SW301'][1], m.PRESS_TRIPOD['SW301'][0]), m.PRESS_TRIPOD['SW301'][2], 'F')}

RIP = [
    # the single switch's leg joins (route_local, A0 copper)
    ('PRESS_N', 'F', (-3.1, -4.35), (3.1, -4.35)), ('PRESS_N', 'F', (-4.6, -4.35), (-3.1, -4.35)),
    ('GND', 'F', (-3.1, -0.65), (3.1, -0.65)), ('GND', 'F', (-3.1, -0.65), (-3.1, 0.85)),
    ('GND', 'F', (3.1, -0.65), (3.1, 0.85)),
    # SW301 (70 deg): the I2C pair's diagonals across its body, one GND stitching via beside its PRESS_N leg
    ('I2C_SDA', 'F', (17.25, -10.65), (8.2, -1.6)), ('I2C_SCL', 'F', (8.6, -0.9), (17.8, -10.1)),
    ('GND', V, (21.2, -5.715), None),
    # SW302 (190 deg): IMU_INT1 across its north keep-out, HALL_FAST over its south legs
    ('IMU_INT1', 'F', (-3.35, 12.36), (-0.39, 9.4)), ('IMU_INT1', 'F', (-3.35, 12.65), (-3.35, 12.36)),
    ('IMU_INT1', 'F', (-3.75, 13.05), (-3.35, 12.65)), ('IMU_INT1', 'F', (-8.4, 13.05), (-3.75, 13.05)),
    ('IMU_INT1', 'F', (-9.0, 13.65), (-8.4, 13.05)),
    ('HALL_FAST', 'F', (7.95, 17.45), (-8.15, 17.45)),
    ('AMP_BCLK', 'F', (-2.4, 14.3), (-1.7, 13.6)), ('AMP_BCLK', 'F', (-1.7, 13.6), (9.0, 13.6)),
    # SW303 (310 deg): one GND stitching via in its south keep-out
    ('GND', V, (-9.9375, -7.6), None),
]
RIP_NETS = []

ADD = [
    # I2C pair through SW301's channel (between its two keep-outs, 0.35 mm pitch), then west below its PRESS_N leg
    # and the IR receiver to the old junctions
    ('I2C_SDA', 'F', 0.2, [(17.25, -10.65), (16.37, -9.77), (16.37, -1.95), (8.55, -1.95), (8.2, -1.6)]),
    ('I2C_SCL', 'F', 0.2, [(17.8, -10.1), (16.72, -9.02), (16.72, -1.6), (9.3, -1.6), (8.6, -0.9)]),
    # PRESS_N to SW301 (its south leg): from the old switch's trunk via east through the freed centre, under the USB
    # pair and the I2C lines on L3, up beside the tail well into the leg
    ('PRESS_N', 'F', 0.2, [(-4.6, -4.35), (0.95, -4.35), (3.45, -1.85)]), ('PRESS_N', V, None, [(3.45, -1.85)]),
    ('PRESS_N', 'I2', 0.2, [(3.45, -1.85), (5.5, -3.9), (10.55, -3.9), (10.85, -4.2)]), ('PRESS_N', V, None, [(10.85, -4.2)]),
    ('PRESS_N', 'F', 0.2, [(10.85, -4.2), (11.86, -3.19), (14.16, -3.19)]),
    # PRESS_N to SW302 (its north leg): straight down from the module pin's via
    ('PRESS_N', 'F', 0.2, [(-4.25, 7.25), (-4.25, 10.6)]),
    # PRESS_N to SW303 (its east leg): along y -5.95 into the pull-up's line
    ('PRESS_N', 'F', 0.2, [(-9.82, -5.95), (-6.4, -5.95), (-5.85, -6.5), (-5.55, -6.5)]),
    # IMU_INT1 round SW302: east of its north GND leg's via, west through its channel (north of the I2S clock), back to
    # the old line at y 13.05 and its via
    ('IMU_INT1', 'F', 0.2, [(-0.39, 9.4), (0.6, 10.39), (0.6, 13.3), (0.15, 13.75), (-5.5, 13.75), (-6.2, 13.05),
                            (-8.4, 13.05), (-9.0, 13.65)]),
    # the I2S clock keeps y 14.3 through SW302's channel and steps up to its old y 13.6 east of the switch
    ('AMP_BCLK', 'F', 0.2, [(-2.4, 14.3), (0.9, 14.3), (1.6, 13.6), (9.0, 13.6)]),
    # SW302's ground: its north GND leg (boxed in by IMU_INT1 and its keep-out) gets its own via; the F pour between
    # the I2S data line and HALL_FAST (SW302's south legs) two stitching vias
    ('GND', V, None, [(-0.118, 12.55)]), ('GND', 'F', 0.3, [(-0.668, 11.18), (-0.668, 12.0), (-0.118, 12.55)]),
    ('GND', V, None, [(4.0, 16.0)]), ('GND', V, None, [(-5.6, 15.85)]),
    # HALL_FAST south of SW302's legs (y 18.75), back to its old line east of the fiducial
    ('HALL_FAST', 'F', 0.2, [(-8.15, 17.45), (-7.85, 17.15), (-7.25, 17.15), (-5.65, 18.75), (6.05, 18.75), (7.35, 17.45),
                             (7.95, 17.45)]),
]

def place():
    """SW301 to its tripod spot; every SKQG's legs jumpered (build_pcb.JUMPERED_LEGS), as a fresh placement makes them."""
    import os
    import pcbnew as pcb
    from board import TARGET, pt
    from build_pcb import JUMPERED_LEGS
    b = pcb.LoadBoard(str(TARGET))
    out = []
    for f in b.GetFootprints():
        if str(f.GetFPID().GetUniStringLibId()) in JUMPERED_LEGS:
            f.SetDuplicatePadNumbersAreJumpers(True)
            out.append(f.GetReference() + ' legs jumpered')
        if f.GetReference() in PLACE:
            (x, y), rot, side = PLACE[f.GetReference()]
            f.SetPosition(pt(50 + x, 50 + y))
            f.SetOrientationDegrees(rot)
            out.append('%s at (%.3f, %.3f) %g' % (f.GetReference(), x, y, rot))
    pcb.SaveBoard(str(TARGET), b)
    print('tripod place: ' + '; '.join(out), flush=True)
    os._exit(0)


if __name__ == '__main__':
    if sys.argv[1:2] == ['place']:
        place()
    run('tripod', RIP, ADD, rip_nets=RIP_NETS)
