"""Design-review fixes of 2026-10-06 applied to the routed MAO_MAIN A1 board (KiCad python), in place.

    <KiCad python> sync_fields.py --add       new parts (Q102, R123, TP26) and the changed pad nets onto the board
    <KiCad python> route_review.py rip        removes the copper the fixes replace (exact items, asserted)
    <KiCad python> route_review.py add        adds the designed copper (locked, 0/45 degrees)

The grid router is not deterministic, so the fixes are drawn on the committed board rather than by a re-route.
The designed copper that belongs to the board's topology lives in route_local.py (USB_PRESENT), so a full
pipeline re-run draws it before the router; the RIP list here names the committed board's router-made copper
and only matches that board.

Finding 1 (USB wake): VBUS_SENSE no longer ends at GPIO39; its last router segment (F, x 8.2) is extended east to
the gate of Q102, the old escape via and stub of pin 35 go, and the GPIO2 stub to TP24 changes net to USB_PRESENT_N.
"""
from handroute import run
from route_local import USB_PRESENT

V = 'V'

RIP = [
    # finding 1: pin 35 escape (VBUS_SENSE -> GPIO39 spare) and the GPIO2 stub (now USB_PRESENT_N)
    ('VBUS_SENSE', 'B', (7.0, 12.75), (8.2, 12.75)), ('VBUS_SENSE', V, (8.2, 12.75), None),
    ('USB_PRESENT_N', 'B', (-7.0, 17.0), (-10.2, 17.0)),    # (GPIO2's stub: KiCad renames it when sync_fields --add
                                                            # moves pad 6 and TP24 off the GPIO2 net)
]

ADD = USB_PRESENT + [
    # finding 1: the router's VBUS_SENSE line (ends at x 8.2, y 12.75 on F) on to the inverter's gate
    ('VBUS_SENSE', 'F', 0.2, [(8.2, 12.75), (9.5, 12.75), (10.05, 13.3), (10.05, 14.237)]),
]

if __name__ == '__main__':
    run('review', RIP, ADD)
