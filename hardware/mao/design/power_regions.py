"""L3 (In2.Cu) power copper for MAO_MAIN A1, 4 layers (brief: "power distribution on L3 as deliberate
regions, broad and coherent; selected slow signals may share the layer").

+3V3 is the layer's default fill: one zone over the whole board at the lowest priority. Each other rail
that travels gets its own region at a higher priority, shaped round its source and its loads:
  REGIONS: (net, priority, outline, core)
    outline  the zone polygon on In2.Cu (board mm, centre origin, y towards 6 o'clock)
    core     the part of it that must stay whole: the grid router keeps every L3 signal out of it
             (grid_router.l3_forbid); None = the whole outline is a core
Short local rails (VBUS beyond its strip, VBAT, REG_IN, 3V3_LCD, AUX_3V3, IR_RX_VCC) stay on the outer layers
next to their parts. Filled in from the placement (placement.py); plane_check.py proves each region is one piece.
"""

# VSYS (A1): from the charger's SYS vias (x 11.6, under the charger on F, beside LINK_REG on B) one strip runs north
# to the 12 o'clock edge (IR LED anodes and their reservoir), along the edge and down the 10-11 o'clock rim to the
# amplifier's VDD via, under both speaker lines (class-D outputs referenced to the amplifier's own supply), and a
# 2.2 mm bar runs south under J301 to its VLED+ via (pad 12). The haptic driver is on +3V3 in A1, so the A0 band's
# run down the 9 o'clock rim is gone. Vias: route_power.py.
VSYS_BAND = [(10.6, -8.2), (11.95, -8.2), (11.95, -18.8), (12.6, -18.8), (12.6, -26.0), (-25.0, -26.0), (-25.0, -10.4), (-13.7, -10.4),
             (-13.7, -0.8), (-11.5, -0.8), (-11.5, -12.75), (-18.0, -12.75), (-18.0, -19.6), (-16.0, -21.6),
             (10.6, -21.6)]
# VBUS: joins the receptacle's two VBUS contact pairs (A4/B9 over A9/B4) under the USB pair's escape.
VBUS_STRIP = [(-3.3, -21.0), (5.2, -21.0), (5.2, -16.9), (-3.3, -16.9)]

REGIONS = [
    ('VSYS', 2, VSYS_BAND, None),
    ('VBUS', 3, VBUS_STRIP, None),
]

# +3V3 default fill: cores where the +3V3 copper must stay whole: the buck output (L101 / COUT and their plane
# vias) and the module's supply pin with its 22 uF + 100 nF. The rest of the layer may carry slow signals (A1: the
# display lanes' core of A0 is gone: J301's DC/TE/RST vias sit where it was; stackup_check.py measures what crosses).
V33_CORES = [
    [(-0.6, -15.2), (4.6, -15.2), (4.6, -10.0), (-0.6, -10.0)],
    [(-11.6, 18.4), (-6.4, 18.4), (-6.4, 21.6), (-11.6, 21.6)],
]


def zones():
    return REGIONS


def gnd_via_ok(x, y):
    """A GND via may sit outside every L3 power region (0.6 mm off it) or deep inside one (1.2 mm from every edge
    that lies on the board): there its hole is an island in the rail's fill and pinches nothing. Anywhere between,
    its antipad necks the rail (a C205/U202 via in the VSYS bar's corner left it 0.26 mm, A0 audit 3)."""
    import math
    import mechanical as m
    for _, _, poly, _ in REGIONS:
        inside, edge = False, 99.0
        for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
            if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
                inside = not inside
            dx, dy = x2 - x1, y2 - y1
            t = max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy or 1)))
            px, py = x1 + t * dx, y1 + t * dy
            if math.hypot(px, py) < m.PCB_R - 0.3:        # edges beyond the milled edge do not count
                edge = min(edge, math.hypot(x - px, y - py))
        if not (edge >= 1.2 if inside else edge >= 0.6):
            return False
    return True


def cores():
    out = [core or outline for _, _, outline, core in REGIONS]
    return out + list(V33_CORES)
