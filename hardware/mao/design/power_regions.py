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

# VSYS: one band from the charger output (1-2 o'clock) up the east side of the USB-C input, along the
# 12 o'clock edge (IR LED anodes) and down the 9 o'clock rim to the amplifier and the haptic driver. The
# band hugs the rim, so the +3V3 copper inside it stays one piece. Vias: route_power.py.
VSYS_BAND = [(4.7, -6.7), (12.4, -6.7), (12.4, -26.0), (-25.0, -26.0), (-25.0, 12.6), (-18.0, 12.6),
             (-18.0, 9.7), (-16.3, 9.7), (-16.3, 8.1), (-18.0, 8.1),     # notch: the haptic driver's via
             (-18.0, -21.6), (10.6, -21.6), (10.6, -13.8), (4.7, -13.8)]
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
    [(-9.1, 8.1), (-9.8, 8.1), (-14.55, 3.35), (-16.0, 3.35), (-16.7, 4.0), (-16.7, 6.6),
     (-11.55, 11.45), (-9.1, 11.45)],                                               # I2S lanes
    [(9.4, 6.75), (21.9, 6.75), (21.9, 8.95), (9.4, 8.95)],                         # PDM pair
]


def zones():
    return REGIONS


def cores():
    out = [core or outline for _, _, outline, core in REGIONS]
    return out + list(V33_CORES)
