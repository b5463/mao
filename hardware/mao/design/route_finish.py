"""Designed finish of MAO_MAIN A1, run right after the grid router (KiCad python): <KiCad python> route_finish.py add

The router joins AMP_LRCLK last and finds the board walled: the I2S clock and data it routed first take F over
the module, and on L3 the panel's 3V3_LCD and LCD_BL runs (y 11.5-12.2) close the west half. The word clock
(48 kHz, a slow line) goes the long way instead: from one via beside the amplifier's pin 14 west on L3 along
y -6.6, down the board's west margin at x -21.2 (outside the display-tail slot), east along y 14.6 north of the
H3 screw hole, then on F (not L3) across the corridor between H3 and the module's west escape column, so the
+3V3 plane keeps its direct path from the buck to the module's 3V3 corner, and back on L3 along y 16.6 under
the module to its escape via east of it (8.2, 14.45). Checked on the routed board: 0.2 mm and more to every
other net, the +3V3 L3 fill stays one piece; DRC and plane_check re-check it.

Three resistor GND pads the router boxed in (starved thermal, no via site after routing) get their ground here:
R313.2 (backlight divider) one via in its pocket, R104.2 (ILIM) a stub to R103.2's ground beside it, and R316.2
(gate pull-down) is freed by re-drawing the router's BL_GATE loop round it as a direct R315 -> R316 run beneath.
"""
from handroute import run

V = 'V'

RIP = [
    ('BL_GATE', 'B', (-6.2, -4.4), (-6.2, -5.85)), ('BL_GATE', 'B', (-6.2, -5.85), (-7.8, -5.85)),
    ('BL_GATE', 'B', (-7.8, -5.85), (-7.8, -5.11)),
]

ADD = [
    ('AMP_LRCLK', 'B', 0.15, [(-14.15, -7.263), (-14.15, -6.75), (-14.5, -6.4)]), ('AMP_LRCLK', V, 0.5, [(-14.5, -6.4)]),
    ('AMP_LRCLK', 'I2', 0.15, [(-14.5, -6.4), (-14.7, -6.6), (-20.6, -6.6), (-21.2, -6.0), (-21.2, 12.9), (-19.5, 14.6),
                               (-15.4, 14.6)]),
    ('AMP_LRCLK', V, 0.5, [(-15.4, 14.6)]),
    ('AMP_LRCLK', 'F', 0.15, [(-15.4, 14.6), (-14.0, 16.0), (-12.3, 16.0)]),
    ('AMP_LRCLK', V, 0.5, [(-12.3, 16.0)]),
    ('AMP_LRCLK', 'I2', 0.15, [(-12.3, 16.0), (-11.7, 16.6), (7.35, 16.6), (7.35, 15.3), (8.2, 14.45)]),
    # boxed-in GND pads
    ('GND', 'B', 0.25, [(-7.88, -10.11), (-7.88, -10.75)]), ('GND', V, 0.5, [(-7.88, -10.75)]),    # R313.2
    ('GND', 'F', 0.25, [(6.04, -12.98), (6.04, -11.78)]),                                           # R104.2 -> R103.2
    ('BL_GATE', 'B', 0.15, [(-7.8, -5.11), (-7.8, -4.6), (-7.0, -4.6), (-7.0, -4.09)]),            # R315.2 -> R316.1
]

if __name__ == '__main__':
    run('finish', RIP, ADD)
