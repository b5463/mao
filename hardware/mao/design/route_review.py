"""Design-review fixes of 2026-10-06 applied to the routed MAO_MAIN A1 board (KiCad python), in place.

    <KiCad python> sync_fields.py --add          new parts (Q102, R123, TP26) and changed pad nets onto the board
    <KiCad python> route_review.py rip [N ...]   removes the copper the fixes replace (exact items, asserted)
    <KiCad python> route_review.py add [N ...]   adds the designed copper (locked, 0/45 degrees)

N selects findings (default: all, in order, on the board as committed before the review, fc57470); they were
applied one at a time in that order. The grid router is not deterministic, so the fixes are drawn on the committed
board rather than by a re-route. Designed copper that belongs to the board's topology lives in route_local.py
(USB_PAIR, USB_PRESENT), so a full pipeline re-run draws it before the router; the RIP lists here name the committed
board's router-made copper and only match that board.

Finding 1 (USB wake): VBUS_SENSE no longer ends at GPIO39; its last router segment (F, x 8.2) is extended east to
the gate of Q102, the old escape via and stub of pin 35 go, and the GPIO2 stub to TP24 changes net to USB_PRESENT_N.

Finding 3 (USB pair): the router's D+ (F, round the face switch's east side) and D- (B, x 0.2, across the L3
VBUS / +3V3 split under the receptacle) go; route_local.USB_PAIR draws both on F as one pair. The ESD array's two
identical channels swap (circuit.py: pin 3 D-, pin 5 D+), so each line passes through its channel in line. To fit the
pair between the I2C clock's via (3.4, -5.45) and the I2C data line's corner, the data line turns 0.35 mm earlier
((3.95, -7.0) -> (4.75, -6.2)); the I2C clock's B -> L3 via north of the 22R moves from (0.1, 3.45) to (0.5, 2.6),
out of the pair's way into R216 / R215.

Finding 4 (VBUS feed to the charger): the 0.4 mm L3 run from the VBUS strip to the charger's IN via becomes 0.8 mm,
re-drawn 0.3-1.7 mm further east so it clears the vias beside it; the router's VBUS_SENSE run beside it on L3 moves
0.35 mm east under the buck (x 6.6) to leave room for the buck's GND via; the F link from the via to C101 / U102.10
widens to 0.4-0.5 mm (0.3 mm for the last 0.5 mm into the 0.2 mm pin).

Finding 5 (BQ25185 exposed pad): two 0.5 / 0.2 mm GND vias in the pad's south half (TI's layout example puts vias in
the thermal pad); the north half sits over LINK_REG R122 on B, so the pad keeps its third via just outside, as before.

Finding 6 (TPS62840 grounds and the module's 3V3): one 0.5 / 0.2 mm GND via 0.6 mm from GND pin 1, on F between
R103 / R104's ground pads and on B joined to the ground web of pins 1, 3 and 6 and C105's ground (1.8 mm of 0.3 mm
track); no other site exists round the part (REG_IN, REG_SW, VSET and the R118 / IR_TX copper box it in). A second
+3V3 plane via sits on the module's 3V3 track between C202 and C201 (0.8 mm from C201.1).
"""
import sys

from handroute import run
from route_local import USB_PAIR, USB_PRESENT

V = 'V'

FINDINGS = {
    1: ([
        # pin 35 escape (VBUS_SENSE -> GPIO39 spare) and the GPIO2 stub (now USB_PRESENT_N)
        ('VBUS_SENSE', 'B', (7.0, 12.75), (8.2, 12.75)), ('VBUS_SENSE', V, (8.2, 12.75), None),
        ('USB_PRESENT_N', 'B', (-7.0, 17.0), (-10.2, 17.0)),    # (GPIO2's stub: KiCad renames it when sync_fields
                                                                # --add moves pad 6 and TP24 off the GPIO2 net)
    ], USB_PRESENT + [
        # the router's VBUS_SENSE line (ends at x 8.2, y 12.75 on F) on to the inverter's gate
        ('VBUS_SENSE', 'F', 0.2, [(8.2, 12.75), (9.5, 12.75), (10.05, 13.3), (10.05, 14.237)]),
    ]),
    3: ([
        # the router's USB lines, A1's connector-side links into the old ESD pins, the I2C bits in the way
        ('USB_C_DP', 'F', (-0.5, -18.712), (-1.1, -18.113)), ('USB_C_DP', 'F', (-1.1, -18.113), (-1.1, -10.9)),
        ('USB_C_DP', 'F', (-1.1, -10.9), (4.45, -5.35)), ('USB_C_DP', 'F', (4.45, -5.35), (4.45, 1.45)),
        ('USB_C_DP', 'F', (4.45, 1.45), (1.21, 4.69)), ('USB_C_DP', 'F', (1.21, 4.69), (0.65, 4.69)),
        ('USB_C_DP', 'F', (-0.25, -20.75), (-0.5, -20.5)), ('USB_C_DP', 'F', (-0.5, -20.5), (-0.5, -18.712)),
        ('USB_C_DN', 'B', (0.25, -19.9), (0.75, -19.4)), ('USB_C_DN', 'B', (0.75, -19.4), (1.2, -19.4)),
        ('USB_C_DN', V, (1.2, -19.4), None), ('USB_C_DN', 'F', (1.2, -19.4), (1.2, -17.7)),
        ('USB_C_DN', 'F', (1.2, -17.7), (0.79, -17.29)), ('USB_C_DN', 'F', (0.79, -17.29), (0.5, -17.29)),
        ('USB_C_DN', 'B', (0.2, -0.7), (0.2, -19.9)), ('USB_C_DN', 'B', (-0.55, 0.05), (0.2, -0.7)),
        ('USB_C_DN', V, (-0.55, 0.05), None), ('USB_C_DN', 'F', (-0.55, 5.09), (-0.55, 0.05)),
        ('I2C_SDA', 'F', (3.95, -18.95), (3.95, -6.65)), ('I2C_SDA', 'F', (3.95, -6.65), (4.4, -6.2)),
        ('I2C_SDA', 'F', (4.4, -6.2), (6.1, -6.2)),
        ('I2C_SCL', V, (0.1, 3.45), None), ('I2C_SCL', 'B', (0.1, 3.45), (-0.75, 2.6)),
        ('I2C_SCL', 'I2', (0.1, 5.15), (0.1, 3.45)), ('I2C_SCL', 'I2', (3.2, 5.15), (0.1, 5.15)),
    ], USB_PAIR + [
        # the I2C data line's corner 0.35 mm tighter, the I2C clock's B -> L3 via out of the pair's way
        ('I2C_SDA', 'F', 0.2, [(3.95, -18.95), (3.95, -7.0), (4.75, -6.2), (6.1, -6.2)]),
        ('I2C_SCL', 'B', 0.2, [(0.5, 2.6), (-0.75, 2.6)]), ('I2C_SCL', V, None, [(0.5, 2.6)]),
        ('I2C_SCL', 'I2', 0.2, [(0.5, 2.6), (0.15, 2.95), (0.15, 5.15), (3.2, 5.15)]),   # west of C206's GND via
    ]),
    4: ([
        ('VBUS', 'I2', (9.0, -7.9), (9.0, -12.0)), ('VBUS', 'I2', (5.0, -16.0), (5.0, -17.3)),
        ('VBUS', 'I2', (9.0, -12.0), (5.0, -16.0)),
        ('VBUS', 'F', (9.0, -9.78), (9.0, -7.9)), ('VBUS', 'F', (8.31, -9.78), (9.0, -9.78)),
        ('VBUS', 'F', (8.31, -10.975), (8.31, -9.78)),
        ('VBUS_SENSE', 'I2', (6.25, -13.1), (5.0, -13.1)), ('VBUS_SENSE', 'I2', (6.25, -8.4), (6.25, -13.1)),
    ], [
        ('VBUS', 'I2', 0.8, [(4.9, -17.1), (7.3, -14.7), (7.3, -12.2), (9.0, -10.5), (9.0, -7.9)]),
        ('VBUS', 'F', 0.4, [(9.0, -7.9), (9.0, -9.78)]),                      # 0.4 beside C101's ground pad
        ('VBUS', 'F', 0.5, [(9.0, -9.78), (8.31, -9.78), (8.31, -10.5)]),
        ('VBUS', 'F', 0.3, [(8.31, -10.5), (8.31, -10.975)]),
        ('VBUS_SENSE', 'I2', 0.2, [(5.0, -13.1), (6.6, -13.1), (6.6, -9.35), (6.25, -9.0), (6.25, -8.4)]),
    ]),
    5: ([], [
        ('GND', V, 0.5, [(9.185, -12.2)]), ('GND', V, 0.5, [(9.635, -12.2)]),
    ]),
    6: ([], [
        ('GND', V, 0.5, [(6.05, -12.35)]), ('GND', 'B', 0.3, [(6.05, -12.35), (6.4, -12.0)]),
        ('+3V3', V, None, [(-9.3, 19.55)]),
    ]),
}

if __name__ == '__main__':
    chosen = [int(a) for a in sys.argv[2:]] or sorted(FINDINGS)
    RIP = [item for n in chosen for item in FINDINGS[n][0]]
    ADD = [item for n in chosen for item in FINDINGS[n][1]]
    run('review %s' % ','.join(map(str, chosen)), RIP, ADD)
