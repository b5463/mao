"""L3 (In2.Cu) power copper for MAO_MAIN A0, 4 layers (brief: "power distribution on L3 as deliberate
regions, broad and coherent; selected slow signals may share the layer").

+3V3 is the layer's default fill: one zone over the whole board at the lowest priority. Each other rail
that travels gets its own region at a higher priority, shaped round its source and its loads:
  REGIONS: (net, priority, outline, core)
    outline  the zone polygon on In2.Cu (board mm, centre origin, y towards 6 o'clock)
    core     the part of it that must stay whole: the grid router keeps every L3 signal out of it
             (grid_router.l3_forbid); None = the whole outline is a core
Short local rails (VBUS, VBAT, 3V3_LCD, MIC_VDD, IR_RX_VCC) stay on the outer layers next to their parts.
Filled in from the placement (placement.py); every outline is checked against the loads it must reach
by review (one L3 island per rail).
"""

# VSYS: from the charger output vias (x 5.7-7.4, under the charger on F; the UVLO divider and the SYS probe take
# VSYS from those vias on B, so no VSYS copper boxes the +3V3 fill in east of the buck-boost) one strip runs up
# beside the VBUS strip to the
# 12 o'clock edge (IR LED anodes, a tab for their reservoir), along the edge and down the 9 o'clock rim (outboard of
# the tail slot) to the haptic driver. A branch at 10 o'clock carries it under the amplifier (its VDD via) and under
# both speaker lines (class-D outputs referenced to the amplifier's own supply, not to a zone gap), then a
# 2 mm bar runs down to the backlight driver beside J301: one via feeds its input cap, one VLED+ (J301 pad 12). The band hugs the rim, so the +3V3 copper inside it stays one piece. Vias:
# route_power.py.
VSYS_BAND = [(5.7, -11.0), (7.4, -11.0), (7.4, -21.0), (10.4, -21.0), (10.4, -18.8),
             (12.6, -18.8), (12.6, -26.0), (-25.0, -26.0), (-25.0, 12.6), (-18.0, 12.6),
             (-18.0, 8.45), (-16.3, 8.45), (-16.3, 6.85), (-18.0, 6.85),   # notch: the haptic driver's via
             (-18.0, -10.4), (-10.4, -10.4), (-10.4, -1.4), (-8.4, -1.4), (-8.4, -11.6), (-11.6, -11.6),  # 10 o'clock branch,
             (-11.6, -12.75), (-18.0, -12.75),   # deep enough that both speaker lines (B, y -11.55 / -12.15) lie over it
             (-18.0, -19.6), (-16.0, -21.6), (5.7, -21.6)]       # chamfer: the top band meets the rim 2.6 mm wide
# VBUS: joins the receptacle's two VBUS contact pairs (A4/B9 over A9/B4) under the USB pair's escape.
VBUS_STRIP = [(-3.3, -21.0), (5.2, -21.0), (5.2, -16.9), (-3.3, -16.9)]

REGIONS = [
    ('VSYS', 2, VSYS_BAND, None),
    ('VBUS', 3, VBUS_STRIP, None),
]

# +3V3 default fill: cores where the +3V3 copper must stay whole: the buck-boost output and its two
# capacitor vias, the module's supply pin with its 22 uF + 100 nF, and the reference under every fast lane
# on B (display SPI, I2S, PDM pair: brief, references uninterrupted). The rest of the layer may carry slow
# signals.
V33_CORES = [
    [(-2.4, -14.2), (2.2, -14.2), (2.2, -9.0), (-2.4, -9.0)],
    [(-13.0, 17.8), (-7.6, 17.8), (-7.6, 21.6), (-13.0, 21.6)],
    [(-11.4, -0.75), (1.1, -0.75), (1.1, 3.0), (-11.4, 3.0)],                       # display lanes
    [(-9.1, 8.0), (-9.1, 11.45), (-10.0, 11.45), (-14.1, 7.35), (-16.1, 7.35), (-16.1, -7.6), (-13.9, -7.6),
     (-13.9, 5.9), (-12.95, 5.9), (-10.85, 8.0)],                                   # I2S lanes (over J301's end, corridor)
    [(9.4, 6.3), (22.3, 6.3), (22.3, 8.95), (9.4, 8.95)],                           # PDM pair
]


def zones():
    return REGIONS


def gnd_via_ok(x, y):
    """A GND via may sit outside every L3 power region (0.6 mm off it) or deep inside one (1.2 mm from every edge
    that lies on the board): there its hole is an island in the rail's fill and pinches nothing. Anywhere between,
    its antipad necks the rail (a C205/U202 via in the VSYS bar's corner left it 0.26 mm, audit 3)."""
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
