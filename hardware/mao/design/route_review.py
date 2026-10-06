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
}

if __name__ == '__main__':
    chosen = [int(a) for a in sys.argv[2:]] or sorted(FINDINGS)
    RIP = [item for n in chosen for item in FINDINGS[n][0]]
    ADD = [item for n in chosen for item in FINDINGS[n][1]]
    run('review %s' % ','.join(map(str, chosen)), RIP, ADD)
