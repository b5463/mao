"""Designed signal copper of MAO_MAIN A0, 4 layers (KiCad python): <KiCad python> route_local.py add

Drawn from the real pad coordinates (fp_pads.py) of placement.py and locked, so the grid router works
around it. Only what decides the board's topology is designed here; the router joins everything else.

USB D+/D- (ODD JOBS 23, brief: short, matched, continuous reference, one outer layer). The pair runs on
F over the uninterrupted L2 ground, from the receptacle to the module: the receptacle's interleaved
contacts are untangled at its pads (D+ joins on F over two vias, D- joins on B under them), the ESD
array U101 sits in the pair turned so D+ meets it upper left and D- lower right, the pair then runs
0.6 mm apart (0.2 mm tracks) down the left of the face switch and drops through one via each beside
module pins 14 (D+) and 13 (D-). Lengths match within 0.6 mm.

Display bus (brief: short and direct, no needless layer change). The five lines leave the module's top
row in the connector's own pin order (pinmap.py) as nested lanes on B over the L3 +3V3 region, 22R in
SCLK and MOSI at the pins, straight into J301's pads (the connector sits on B since the A0 revision).

Fan-out (the KINO/6-layer lesson: design the escapes, then route): I2S as three nested lanes over J301's
end and down the tail corridor into the amplifier at 10 o'clock, I2C straight across to the haptic driver, a via for every expander pin and for every slow or
F-bound module pin, so the router can take L3 without paying for new vias in crowded pin rows.
"""
from handroute import run
import mechanical as _m

V = 'V'


def M(*pts):
    """Copper drawn at the module's pre-audit position (antenna end at r 28.7), moved with the module."""
    return [(x, round(y + _m.MODULE_SHIFT, 3)) for x, y in pts]

ADD = [
    # ---- USB at the receptacle (B pads, y -21.905) ---------------------------------------------------
    ('USB_DP', 'B', 0.2, [(-0.25, -21.3), (-0.25, -20.75)]),                          # A6
    ('USB_DP', 'B', 0.2, [(0.75, -21.3), (0.75, -20.75)]),                            # B6
    ('USB_DP', V, 0.5, [(-0.25, -20.75)]),
    ('USB_DP', V, 0.5, [(0.75, -20.75)]),
    ('USB_DP', 'F', 0.2, [(-0.25, -20.75), (0.75, -20.75)]),                          # D+ joins on F
    ('USB_DN', 'B', 0.15, [(0.25, -21.3), (0.25, -19.9)]),                            # A7, between the D+ vias
    ('USB_DN', 'B', 0.2, [(-0.75, -21.3), (-0.75, -19.9)]),                           # B7
    ('USB_DN', 'B', 0.2, [(-0.75, -19.9), (0.25, -19.9), (0.75, -19.4), (1.2, -19.4)]),   # D- joins on B
    ('USB_DN', V, 0.5, [(1.2, -19.4)]),
    # ---- USB on F through the ESD array to the module ------------------------------------------------
    ('USB_DP', 'F', 0.2, [(-0.25, -20.75), (-0.5, -20.5), (-0.5, -18.712)]),          # U101.3
    ('USB_DP', 'F', 0.2, [(-0.5, -18.712), (-1.1, -18.112), (-1.1, -16.1), (-7.6, -9.6)] + M((-7.6, 4.7))),
    ('USB_DN', 'F', 0.2, [(1.2, -19.4), (1.2, -17.7), (0.79, -17.29), (0.5, -17.29)]),    # U101.5
    ('USB_DN', 'F', 0.2, [(0.5, -17.29), (0.5, -16.7), (-0.2, -16.0), (-6.8, -9.4)] +
                         M((-6.8, 5.57), (-7.2, 5.97), (-7.6, 5.97))),
    ('GND', 'F', 0.2, [(-0.5, -17.29), (-0.5, -16.6)]),                               # ESD ground, between the pair
    ('GND', V, 0.5, [(-0.5, -16.6)]),
    ('USB_DP', V, 0.5, M((-7.6, 4.7))),
    ('USB_DN', V, 0.5, M((-7.6, 5.97))),
    ('USB_DP', 'B', 0.2, M((-7.6, 4.7), (-8.75, 4.7))),                              # module pin 14
    ('USB_DN', 'B', 0.2, M((-7.6, 5.97), (-8.75, 5.97))),                            # module pin 13
    # ---- face switch: each pair of legs is one contact inside the part; join them under the body -------
    ('PRESS_N', 'F', 0.3, [(-3.1, -4.35), (3.1, -4.35)]),
    ('GND', 'F', 0.3, [(-3.1, -0.65), (3.1, -0.65)]),          # and its GND legs (the left one has no via site)
    # ---- display bus on B: five nested lanes from the module's top row (pins 17-21) to J301 pads 9-5 ------
    # The module's top row sits 1.55 mm north of J301's DC and TE pads, so the lanes run north of the row and
    # drop 1.8 mm to the pads together, on nested 45-degree lines west of the module's corner pads
    ('LCD_SCLK', 'B', 0.2, M((-0.635, 2.8), (-0.635, 1.9))),                        # pin 20 -> R301
    ('LCD_MOSI', 'B', 0.2, M((-1.905, 2.8), (-1.905, 1.9))),                        # pin 19 -> R302
    ('LCD_CS', 'B', 0.15, M((0.635, 2.8), (0.635, -0.3), (-7.8, -0.3)) + [(-9.6, 0.25), (-11.15, 0.25)]),
    ('LCD_SCLK_P', 'B', 0.15, M((-0.635, 0.73), (-0.635, 0.2), (-7.8, 0.2)) + [(-9.6, 0.75), (-11.15, 0.75)]),
    ('LCD_MOSI_P', 'B', 0.15, M((-1.905, 0.7), (-7.8, 0.7)) + [(-9.6, 1.25), (-11.15, 1.25)]),
    ('LCD_DC', 'B', 0.15, M((-3.175, 2.8), (-3.175, 1.2), (-7.8, 1.2)) + [(-9.6, 1.75), (-11.15, 1.75)]),
    ('LCD_TE', 'B', 0.15, M((-4.445, 2.8), (-4.445, 1.7), (-7.8, 1.7)) + [(-9.6, 2.25), (-11.15, 2.25)]),
    # J301 power pads: VCI (pad 3) through one via under the housing to C301/C302 on F; VLED- (pad 11) to the
    # backlight driver's LED1/LED2 sinks; GND pads 10, 14, 1 and the MP lands drop to the plane
    ('3V3_LCD', 'B', 0.3, [(-11.15, 3.25), (-12.4, 3.25)]), ('3V3_LCD', V, 0.5, [(-12.4, 3.25)]),
    ('3V3_LCD', 'F', 0.3, [(-12.4, 3.25), (-12.4, 1.5)]),
    ('3V3_LCD', 'F', 0.3, [(-12.4, 3.25), (-11.78, 3.25), (-11.78, 3.1)]),
    ('LCD_BL_K', 'B', 0.3, [(-11.15, -0.75), (-9.3, -0.75), (-9.3, -2.2), (-9.05, -2.45), (-9.05, -2.688)]),
    ('LCD_BL_K', 'B', 0.25, [(-9.05, -2.45), (-8.55, -2.45), (-8.55, -2.688)]),
    ('GND', 'B', 0.3, [(-11.15, -0.25), (-12.3, -0.25)]), ('GND', V, 0.5, [(-12.3, -0.25)]),
    ('GND', 'B', 0.3, [(-11.15, -2.25), (-12.3, -2.25)]), ('GND', V, 0.5, [(-12.3, -2.25)]),
    ('GND', 'B', 0.3, [(-11.15, 4.25), (-12.3, 4.25)]), ('GND', V, 0.5, [(-12.3, 4.25)]),
    # backlight driver EN (pad 2): north out of the pad row, west under the VSYS feed to its via
    ('LCD_BL_CTRL', 'B', 0.2, [(-8.55, -4.6125), (-8.55, -5.3), (-9.75, -5.3)]), ('LCD_BL_CTRL', V, 0.5, [(-9.75, -5.3)]),
    # ---- display rail switch U105 (F, SC70-6): each pin a straight line to its part. ON climbs between the pin rows
    # from the expander's via and drops on to its pull-down; VIN runs to C110 and up to its L3 +3V3 via; GND (and
    # C110's) joins the amplifier's F thermal pad (its four vias); pin 4 is NC --------------------------------------
    ('LCD_SW_QOD', 'F', 0.2, [(-10.5625, -9.15), (-9.36, -9.15), (-8.7, -8.49)]),
    ('3V3_LCD', 'F', 0.3, [(-8.7, -9.51), (-8.7, -10.67)]),
    ('3V3_LCD', 'F', 0.3, [(-10.5625, -9.8), (-8.7, -9.8)]),
    ('+3V3', 'F', 0.3, [(-12.2375, -9.8), (-13.92, -9.8)]),                           # C110
    ('+3V3', 'F', 0.3, [(-12.2375, -9.8), (-12.2375, -10.6), (-13.08, -11.4425), (-13.08, -12.85)]),
    ('+3V3', V, None, [(-13.08, -12.85)]),
    ('GND', 'F', 0.3, [(-12.2375, -9.15), (-14.1, -9.15)]),
    ('GND', 'F', 0.3, [(-14.88, -10.1), (-14.88, -9.2)]),                             # C110, into the same pad
    ('GND', 'F', 0.25, [(-15.95, -10.47), (-15.25, -10.47), (-14.88, -10.1)]),        # R208 (SD_MODE pull-down)
    ('GND', 'F', 0.3, [(-8.7, -11.63), (-8.7, -12.6)]),                               # C109 into the expander's F pad
    ('LCD_PWR_EN', 'F', 0.2, [(-11.45, -13.35), (-10.85, -12.75), (-10.85, -11.95), (-11.4, -11.4), (-11.4, -8.5),
                              (-12.2375, -8.5)]),
    ('LCD_PWR_EN', 'F', 0.2, [(-12.2375, -8.5), (-12.2375, -7.51)]),
    # ---- amplifier SD_MODE: pin 4's via feeds the pull-down R208 beside it and climbs on F west of the
    # amplifier's caps to the series resistor R507 and the expander's AMP_SD_N via (no crossing of the speaker
    # pair on B) ------------------------------------------------------------------------------------------------
    ('AMP_SD', 'F', 0.2, [(-16.6, -9.45), (-15.95, -9.45)]),
    ('AMP_SD', 'F', 0.2, [(-16.6, -9.45), (-16.6, -14.4), (-16.1, -14.9), (-14.11, -14.9)]),
    ('AMP_SD_N', 'F', 0.2, [(-13.09, -14.9), (-12.45, -14.9), (-12.15, -14.6), (-12.15, -13.85)]),
    # ---- module corner and left column: USB_PRESENT (pin 15) and BOARD_ID (pin 12) drop to L3 ---------------
    ('USB_PRESENT_N', 'B', 0.2, M((-7.0, 3.45), (-8.25, 3.45))),
    ('USB_PRESENT_N', V, 0.5, M((-8.25, 3.45))),
    # ... and on to the charger's PGOOD: F up the middle beside the face switch, L3 round the buck-boost core and
    # east of the charger's VSYS strip to the PGOOD via (the path the router found before the long designed lines)
    ('USB_PRESENT_N', 'F', 0.2, M((-8.25, 3.45), (-8.25, 6.35), (-7.75, 6.85), (-7.45, 6.85), (-4.35, 3.75)) +
                                [(-4.35, -5.45), (-4.05, -5.75), (2.2, -5.75)]), ('USB_PRESENT_N', V, 0.5, [(2.2, -5.75)]),
    ('USB_PRESENT_N', 'I2', 0.2, [(2.2, -5.75), (3.4, -6.95), (3.4, -7.95), (3.75, -8.3), (8.1, -8.3), (8.1, -13.05),
                                  (8.95, -13.9), (8.95, -15.4), (9.9, -15.4)]),
    ('BOARD_ID', 'B', 0.2, M((-9.4, 7.24), (-10.3, 7.24))),
    ('BOARD_ID', V, 0.5, M((-10.3, 7.24))),
    # the board-ID divider sits on F over this via (C204, R204, R203 top to bottom, BOARD_ID ends east): ~3 mm in all
    ('BOARD_ID', 'F', 0.2, M((-10.3, 7.24), (-11.12, 7.24), (-11.12, 9.64))),
    ('GND', 'F', 0.25, M((-12.08, 7.24), (-12.08, 8.44))),
    # ---- I2S to the amplifier (10 o'clock): three nested lanes up over J301's end, east of the haptic parts,
    # then down the tail corridor (tracks allowed, no parts) into the amplifier's corridor-side pins ---------
    ('AMP_LRCLK', 'B', 0.2, M((-9.4, 8.51), (-12.15, 8.51)) + [(-13.21, 6.2), (-14.8, 6.2), (-14.8, -6.1),
                             (-14.15, -6.75), (-14.15, -7.262)]),
    ('AMP_BCLK', 'B', 0.2, M((-9.4, 9.78), (-11.85, 9.78)) + [(-13.73, 6.65), (-15.25, 6.65), (-15.25, -6.4),
                            (-15.15, -6.5), (-15.15, -7.262)]),
    ('AMP_DIN', 'B', 0.2, M((-9.4, 11.05), (-11.2, 11.05)) + [(-13.9, 7.1), (-15.7, 7.1), (-15.7, -6.6),
                           (-16.4, -7.3), (-16.4, -7.95), (-15.838, -7.95)]),
    # amplifier: SD_MODE drops to L3 for its resistors beside the expander; GND pins into the pad
    ('AMP_SD', 'B', 0.2, [(-15.838, -9.45), (-16.6, -9.45)]), ('AMP_SD', V, 0.5, [(-16.6, -9.45)]),
    ('GND', 'B', 0.2, [(-15.838, -8.95), (-15.2, -8.95)]),
    ('GND', 'B', 0.2, [(-12.963, -8.45), (-13.6, -8.45)]),
    ('GND', 'B', 0.2, [(-14.65, -7.262), (-14.65, -8.0)]),
    # ---- I2C straight across to the haptic driver (the trunk to the other devices leaves on L3) -----------
    ('I2C_SCL', 'B', 0.2, M((-9.4, 12.32), (-12.33, 12.32), (-12.75, 11.9), (-13.2, 11.9))),
    ('I2C_SDA', 'B', 0.2, M((-9.4, 13.59), (-11.56, 13.59), (-12.75, 12.4), (-13.2, 12.4))),
    ('I2C_SDA', V, 0.5, M((-10.5, 13.59))), ('I2C_SCL', V, 0.5, M((-11.2, 12.32))),     # the trunk drops to L3
    # ---- expander fan-out (U202 at (-9.0, -13.1)): every pin to its own via (staggered), the lines run on L3 --
    # EXP_RST_N (pin 1) leaves south on B, clear of the speaker pair, passes under the expander's supply cap and runs
    # east on B to a via beside the face switch's keep-out
    ('EXP_RST_N', 'B', 0.2, [(-10.4375, -12.35), (-10.95, -12.35), (-10.95, -9.6), (-4.0, -9.6), (-4.0, -2.6),
                             (4.45, -2.6)]), ('EXP_RST_N', V, 0.5, [(4.45, -2.6)]),     # east of SW301's keep-out
    # on to TP11 / the pull-up on L3 under the service field, designed so the router need not find it (left to the
    # router, the L3 leg pocketed +3V3 against TOF_INT_N's and the run detoured: audit-3 rebuild)
    ('EXP_RST_N', 'I2', 0.2, [(4.45, -2.6), (4.45, -2.3), (6.35, -2.3), (7.75, -0.9), (8.55, -0.9), (9.1, -1.45)]),
    ('EXP_RST_N', V, 0.6, [(9.1, -1.45)]), ('EXP_RST_N', 'B', 0.2, [(9.1, -1.45), (8.8, -1.15), (8.8, -0.5)]),
    # (from this via the router takes it to TP11, R205 and GPIO39, with the plane check on L3)
    # GPIO0 pull-up's +3V3 end: its plane via west of it, clear of the XRST name
    ('+3V3', 'B', 0.3, M((10.4, 2.74), (9.6, 2.74))), ('+3V3', V, 0.6, M((9.6, 2.74))),
    ('LCD_PWR_EN', 'B', 0.15, [(-10.6, -13.35), (-11.45, -13.35)]), ('LCD_PWR_EN', V, 0.5, [(-11.45, -13.35)]),
    ('LCD_RST_N', 'B', 0.15, [(-10.6, -12.85), (-12.15, -12.85)]), ('LCD_RST_N', V, 0.5, [(-12.15, -12.85)]),
    ('AMP_SD_N', 'B', 0.15, [(-10.6, -13.85), (-12.15, -13.85)]), ('AMP_SD_N', V, 0.5, [(-12.15, -13.85)]),
    ('HAPTIC_EN', 'B', 0.15, [(-9.75, -14.7), (-9.75, -15.45)]), ('HAPTIC_EN', V, 0.5, [(-9.75, -15.45)]),
    ('IR_RX_PWR', 'B', 0.15, [(-8.25, -14.7), (-8.25, -15.45)]), ('IR_RX_PWR', V, 0.5, [(-8.25, -15.45)]),
    ('TOF_XSHUT', 'B', 0.15, [(-8.75, -14.7), (-8.75, -16.15)]), ('TOF_XSHUT', V, 0.5, [(-8.75, -16.15)]),
    ('CHG_CE_N', 'B', 0.15, [(-7.4, -13.85), (-6.55, -13.85)]), ('CHG_CE_N', V, 0.5, [(-6.55, -13.85)]),
    ('EXP_INT_N', 'B', 0.15, [(-7.4, -12.85), (-6.55, -12.85)]), ('EXP_INT_N', V, 0.5, [(-6.55, -12.85)]),
    ('SENSE_ALRT_N', 'B', 0.15, [(-7.4, -13.35), (-5.85, -13.35)]), ('SENSE_ALRT_N', V, 0.5, [(-5.85, -13.35)]),
    # the alert line runs east on B between the buck-boost and the USB-C parts to its pull-up R212 (the gauge and
    # the light sensor join it there)
    ('SENSE_ALRT_N', 'B', 0.2, [(-5.85, -13.35), (-5.85, -15.95), (7.0, -15.95), (7.75, -16.7), (11.9, -16.7),
                                 (12.35, -17.15), (12.35, -17.52), (13.09, -17.52)]),
    # IR LED switch gate resistor R503: IR_TX drops to L3 straight below it, north of the alert line
    ('IR_TX', 'B', 0.15, [(9.53, -18.56), (9.53, -17.55)]), ('IR_TX', V, 0.5, [(9.53, -17.55)]),
    # LED reservoir C505 beside the IR LEDs' VSYS via
    ('VSYS', 'B', 0.4, [(12.0, -19.3), (12.4, -18.9), (13.625, -18.9)]),
    ('I2C_SCL', 'B', 0.15, [(-7.4, -12.35), (-5.85, -12.35)]), ('I2C_SCL', V, 0.5, [(-5.85, -12.35)]),
    ('I2C_SDA', 'B', 0.15, [(-8.25, -11.66), (-8.25, -11.4), (-7.95, -11.1), (-7.45, -11.1)]),
    ('I2C_SDA', V, 0.5, [(-7.45, -11.1)]),
    # expander supply: pins 14/15 straight up into C205 and one +3V3 via; GND pin 16 into C205's other end (pin 16
    # reaches the plane through the exposed pad's vias: gnd_pad_vias link U202.16>U202.17)
    ('+3V3', 'B', 0.2, [(-9.25, -11.66), (-9.25, -10.95), (-8.75, -10.95)]),
    ('+3V3', 'B', 0.25, [(-8.75, -11.66), (-8.75, -10.2)]),
    ('+3V3', 'B', 0.3, [(-8.79, -10.2), (-7.9, -10.2)]), ('+3V3', V, 0.6, [(-7.9, -10.2)]),
    ('GND', 'B', 0.25, [(-9.75, -11.66), (-9.75, -10.2)]),
    ('+3V3', 'B', 0.25, [(-3.36, -12.85), (-2.3, -12.85)]), ('+3V3', V, 0.6, [(-2.3, -12.85)]),   # R206, into core 1
    # ---- ToF U402 (F): I2C down one via each beside pins 9/10, INT through its pull-up to L3, XSHUT at R210 --
    ('I2C_SDA', 'F', 0.15, [(-9.44, -17.75), (-9.06, -17.37), (-9.06, -17.04)]), ('I2C_SDA', V, 0.5, [(-9.06, -17.04)]),
    ('I2C_SCL', 'F', 0.15, [(-10.14, -17.38), (-9.76, -17.00), (-9.76, -16.67)]), ('I2C_SCL', V, 0.5, [(-9.76, -16.67)]),
    ('TOF_INT_N', 'F', 0.2, [(-8.02, -18.50), (-6.32, -18.50), (-6.32, -17.59)]), ('TOF_INT_N', V, 0.5, [(-6.32, -17.59)]),
    ('+3V3', 'F', 0.25, [(-5.30, -18.50), (-5.30, -17.20)]), ('+3V3', V, 0.6, [(-5.30, -17.20)]),
    ('TOF_XSHUT', 'F', 0.15, [(-7.18, -20.72), (-6.41, -20.72)]), ('TOF_XSHUT', V, 0.5, [(-6.41, -20.72)]),
    # ---- module right column: slow and F-bound pins to staggered vias ---------------------------------------
    ('IR_TX', 'B', 0.15, M((9.4, 9.78), (10.25, 9.78))), ('IR_TX', V, 0.5, M((10.25, 9.78))),          # GPIO38
    ('EXP_RST_N', 'B', 0.15, M((9.4, 11.05), (10.95, 11.05))), ('EXP_RST_N', V, 0.5, M((10.95, 11.05))),  # GPIO39
    ('IR_RX', 'B', 0.15, M((9.4, 12.32), (10.25, 12.32))), ('IR_RX', V, 0.5, M((10.25, 12.32))),
    ('HALL_A', 'B', 0.15, M((9.4, 13.59), (10.95, 13.59))), ('HALL_A', V, 0.5, M((10.95, 13.59))),
    ('HALL_B', 'B', 0.15, M((9.4, 14.86), (10.25, 14.86))), ('HALL_B', V, 0.5, M((10.25, 14.86))),
    ('HALL_FAST', 'B', 0.15, M((9.4, 18.67), (10.25, 18.67))), ('HALL_FAST', V, 0.5, M((10.25, 18.67))),
    # GPIO0 (pin 27): one via south of its pull-up R202, up into the pull-up, on to the BOOT probe TP8 in the
    # channel between R202 and R205, and on F to the face switch round its keep-out
    ('PRESS_N', 'B', 0.2, M((9.4, 4.7), (10.6, 4.7))), ('PRESS_N', V, 0.5, M((10.6, 4.7))),
    ('PRESS_N', 'B', 0.2, M((10.6, 4.7), (10.6, 3.76))),
    ('PRESS_N', 'B', 0.2, M((10.6, 3.76), (11.05, 3.76)) + [(11.05, 0.2), (11.6, -0.35)]),
    ('PRESS_N', 'F', 0.2, M((10.6, 4.7)) + [(5.0, -2.15), (5.0, -3.75), (4.4, -4.35), (3.9, -4.35)]),
    # ---- module top row right of the display group: slow lines to L3 ----------------------------------------
    ('EXP_INT_N', 'B', 0.15, M((3.175, 2.8), (3.175, 2.0))), ('EXP_INT_N', V, 0.5, M((3.175, 2.0))),
    ('TOF_INT_N', 'B', 0.15, M((5.715, 2.8), (5.715, 2.0))), ('TOF_INT_N', V, 0.5, M((5.715, 2.0))),
    ('LCD_BL_CTRL', 'B', 0.15, M((6.985, 2.8), (6.985, 2.0))), ('LCD_BL_CTRL', V, 0.5, M((6.985, 2.0))),
    # ---- touch: pins 4/5/6 rise beside the module to their series R on F; three nested electrode lanes on F
    ('TOUCH_LEFT', 'B', 0.2, M((-8.75, 17.4), (-10.1, 17.4))), ('TOUCH_LEFT', V, 0.5, M((-10.1, 17.4))),
    ('TOUCH_LEFT', 'F', 0.2, M((-10.1, 17.4), (-10.99, 17.4))),
    ('TOUCH_TOP', 'B', 0.2, M((-8.75, 16.13), (-10.1, 16.13))), ('TOUCH_TOP', V, 0.5, M((-10.1, 16.13))),
    ('TOUCH_TOP', 'F', 0.2, M((-10.1, 16.13), (-10.99, 16.13))),
    ('TOUCH_REAR', 'B', 0.2, M((-8.75, 14.86), (-10.1, 14.86))), ('TOUCH_REAR', V, 0.5, M((-10.1, 14.86))),
    ('TOUCH_REAR', 'F', 0.2, M((-10.1, 14.86), (-10.99, 14.86))),
    # the lanes step west 1.25 mm to their pre-audit diagonals, so the spring, ESD and arc ends did not move
    ('TOUCH_LEFT_E', 'F', 0.2, M((-12.01, 17.4), (-13.26, 17.4)) + [(-20.01, 9.4), (-24.6, 9.4)]),
    ('TOUCH_TOP_E', 'F', 0.2, M((-12.01, 16.13), (-13.26, 16.13)) + [(-20.84, 7.3), (-21.6, 7.3)]),
    ('TOUCH_REAR_E', 'F', 0.2, M((-12.01, 14.86), (-13.26, 14.86)) + [(-17.9, 8.97), (-17.9, -6.9), (-18.4, -7.4), (-20.4, -7.4), (-20.9, -7.9)]),
    ('TOUCH_REAR_E', V, 0.5, [(-20.9, -7.9)]),                                       # outboard of the speaker loop
    ('TOUCH_REAR_E', 'B', 0.2, [(-20.9, -7.9), (-20.9, -8.3), (-20.4, -8.8)]),
    ('TOUCH_REAR_E', 'B', 0.2, [(-20.9, -8.3), (-21.4, -8.8), (-22.4, -8.8)]),
    # ---- module EN and 3V3 corner: EN through its pull-up and delay cap, out to L3 past the bulk cap -------
    ('MCU_EN', 'B', 0.2, M((-8.75, 18.8), (-12.9, 18.8))), ('MCU_EN', V, 0.5, M((-12.9, 18.8))),
    ('+3V3', 'B', 0.5, M((-8.75, 19.94), (-12.625, 19.94))),                    # module feed: 0.5 mm, two
    ('+3V3', V, 0.6, M((-11.23, 19.94))),                                           # plane vias (TX peaks 355 mA); on C202's branch axis
    ('+3V3', 'B', 0.25, M((-11.23, 19.94), (-11.23, 21.05))),
    ('+3V3', 'B', 0.3, M((-12.4, 19.94), (-12.4, 21.0))), ('+3V3', V, 0.6, M((-12.4, 21.0))),
    ('GND', 'B', 0.3, M((-10.27, 21.05), (-8.75, 21.05))),
    # ---- haptic driver: HAPTIC_EN through its pull-down to L3; both GND pins to one via under the body ------
    ('HAPTIC_EN', 'B', 0.2, M((-13.5, 13.4), (-12.75, 13.4), (-12.4, 13.75), (-12.4, 14.34))),
    ('GND', 'B', 0.25, M((-17.7, 12.4), (-15.85, 12.4), (-15.35, 12.9), (-13.5, 12.9))),
    ('GND', 'B', 0.25, M((-16.2, 12.4)) + [(-16.2, 13.55), (-16.45, 13.8)]), ('GND', V, 0.6, [(-16.45, 13.8)]),
    # (the driver moved in with the module; its EN and GND vias stay outboard of the touch lanes on F)
    ('GND', 'B', 0.3, M((-16.52, 10.0), (-15.75, 10.0))), ('GND', V, 0.6, M((-15.75, 10.0))),    # C503 return
    # ---- service field: pull-ups face their probe pads, +3V3 ends join TP3; the trunk drops to L3 here ----
    ('I2C_SCL', 'B', 0.2, [(6.0, -3.3), (7.6, -3.3), (8.11, -2.79), (8.2, -2.79)]),
    ('I2C_SCL', 'B', 0.2, [(6.0, -3.3), (6.0, -4.1), (5.5, -4.6)]), ('I2C_SCL', V, 0.5, [(5.5, -4.6)]),   # trunk via
                                                                    # out of the SCL name's column
    ('I2C_SDA', 'B', 0.2, [(6.0, -0.5), (6.9, -0.5), (7.4, -1.0), (7.4, -1.6), (8.25, -1.6), (9.44, -2.79)]),
    ('I2C_SDA', V, 0.5, [(8.2, -1.6)]),                        # (east of the SDA name's spot beside TP9)
    ('+3V3', 'B', 0.3, [(8.2, -3.81), (8.2, -4.3), (9.44, -4.3), (9.44, -3.81)]),
    ('+3V3', 'B', 0.3, [(8.8, -4.3), (8.8, -6.1)]), ('+3V3', V, 0.6, [(8.8, -4.85)]),
    # ---- fuel gauge U103: I2C out of its east flank to staggered vias (the lines run on L3) ----------------
    ('I2C_SDA', 'B', 0.2, [(14.688, -13.8), (15.45, -13.8)]), ('I2C_SDA', V, 0.5, [(15.45, -13.8)]),
    ('I2C_SCL', 'B', 0.2, [(14.688, -14.3), (15.6, -14.3), (16.1, -14.8)]), ('I2C_SCL', V, 0.5, [(16.1, -14.8)]),
    ('SENSE_ALRT_N', 'F', 0.2, [(10.9, -18.72), (11.42, -18.72), (11.85, -18.29)]), ('SENSE_ALRT_N', V, 0.5, [(11.85, -18.29)]),  # ALS INT
    # ---- Hall wired-OR: each sensor's FAST output drops to L3 beside its pin ---------------------------------
    ('HALL_FAST', 'F', 0.2, [(23.32, 13.75), (23.72, 14.15)]), ('HALL_FAST', V, 0.5, [(23.72, 14.15)]),
    ('GND', 'F', 0.25, [(22.23, 12.55), (22.78, 13.1)]),                                    # U301 GND pin to its tab,
    ('GND', 'F', 0.3, [(22.78, 13.15), (22.07, 13.86)]), ('GND', V, 0.6, [(22.07, 13.86)]),  # one via for both
    ('HALL_FAST', 'F', 0.2, [(21.76, 16.12), (21.4, 16.48), (21.4, 16.7)]), ('HALL_FAST', V, 0.5, [(21.4, 16.7)]),
    # ---- IR receiver supply switch: IR_RX_PWR from the expander arrives on L3 beside the filter R505 ---------
    ('IR_RX_PWR', 'F', 0.15, [(22.47, 6.75), (23.35, 6.75)]), ('IR_RX_PWR', V, 0.5, [(23.35, 6.75)]),
    # IR receiver (turned: VCC and OUT face the ring): OUT up into its pull-up; VCC round the inside of the
    # pins to the pull-up, the RC filter and C506
    ('IR_RX', 'F', 0.2, [(24.15, 1.27), (24.15, 3.6)]),
    ('IR_RX_VCC', 'F', 0.25, [(24.15, -1.27), (24.15, -2.6), (22.83, -3.92)]),
    ('IR_RX_VCC', 'F', 0.25, [(23.55, -1.27), (22.9, -0.62), (22.9, 3.41), (23.09, 3.6)]),
    ('IR_RX_VCC', 'F', 0.25, [(22.9, 3.6), (22.9, 4.9), (21.45, 6.35), (21.45, 6.75)]),
    # ---- microphone (bottom port): the ground ring's own via ---------------------------------------------
    # mic supply: MIC_VDD from pad 5 round to C407, across to R402 and C406, round to its probe pad TP14
    ('MIC_VDD', 'B', 0.3, [(21.99, 7.37), (22.4, 7.78), (22.4, 8.63), (21.99, 9.04), (21.59, 9.04)]),
    ('MIC_VDD', 'B', 0.3, [(20.39, 9.15), (23.3, 9.15)]),
    ('MIC_VDD', 'B', 0.3, [(23.3, 9.15), (23.3, 10.54), (22.09, 11.75)]),
    # ---- Tag-Connect J201 (turned so RXD/TXD/GND face the module in its pin order) --------------------------
    ('UART_RX', 'B', 0.2, M((8.75, 16.13), (14.36, 16.13), (14.56, 16.33))),
    ('UART_TX', 'B', 0.2, M((8.75, 17.4), (14.36, 17.4), (14.56, 17.6))),
    ('GND', 'B', 0.3, M((14.56, 18.87), (13.6, 18.87))), ('GND', V, 0.6, M((13.6, 18.87))),
    ('PRESS_N', 'B', 0.2, M((15.83, 16.33), (16.9, 16.33))), ('PRESS_N', V, 0.5, M((16.9, 16.33))),
    ('+3V3', 'B', 0.3, M((15.83, 17.6), (16.9, 17.6))), ('+3V3', V, 0.6, M((16.9, 17.6))),
    # ---- right touch: pin 39 through R308 beside it, one via, then an F lane onto the arc's lower end, inboard of
    # the Tag-Connect and clear of the antenna boundary (ODD JOBS 2, 125)
    ('TOUCH_RIGHT', 'B', 0.2, M((8.75, 19.94), (10.75, 19.94), (11.09, 19.6))),
    ('TOUCH_RIGHT_E', 'B', 0.2, M((12.11, 19.6), (12.65, 19.6))), ('TOUCH_RIGHT_E', V, 0.5, M((12.65, 19.6))),
    ('TOUCH_RIGHT_E', 'F', 0.2, M((12.65, 19.6)) + [(12.65, 13.5), (16.7, 9.45), (24.6, 9.45)]),
    # ---- PDM pair (B): data from pin 29 into pad 1, clock from pin 30 round pad 1 into pad 4; both drop 1.25 mm
    # together on nested 45-degree lines past the module's escape vias (the module moved in, the mic did not)
    ('MIC_DATA', 'B', 0.2, M((8.75, 7.24), (11.8, 7.24)) + [(13.05, 7.24), (19.98, 7.24), (20.11, 7.37), (20.31, 7.37)]),
    ('MIC_CLK', 'B', 0.2, M((8.75, 8.51), (11.8, 8.51)) + [(13.05, 8.51), (20.75, 8.51), (21.15, 8.11), (21.15, 6.95),
                                                          (21.55, 6.55), (21.99, 6.55)]),
    # ---- IMU U401 on F above the module's top row ------------------------------------------------------------
    # +3V3: pins 2, 3 and 12 join pin 5 under the body; pin 5 feeds C402, pin 8 feeds C401, one via each to
    # the L3 +3V3 plane at the caps. GND: pins 6/7 and both cap returns meet one via between the caps.
    ('+3V3', 'F', 0.15, [(2.038, 6.75), (3.7, 6.75), (3.7, 6.088)]),
    ('+3V3', 'F', 0.15, [(2.038, 7.25), (2.7, 7.25)]),
    ('+3V3', 'F', 0.15, [(2.7, 6.75), (2.7, 7.912)]),
    ('+3V3', 'F', 0.2, [(2.7, 7.912), (2.7, 8.45), (1.72, 8.45), (1.72, 9.25)]),
    ('+3V3', 'F', 0.3, [(1.72, 9.25), (0.82, 9.25)]), ('+3V3', V, 0.6, [(0.82, 9.25)]),
    ('+3V3', 'F', 0.2, [(4.363, 7.75), (5.05, 7.75), (5.323, 8.023), (5.323, 9.25)]),
    ('+3V3', 'F', 0.3, [(5.323, 9.25), (6.2, 9.25)]), ('+3V3', V, 0.6, [(6.2, 9.25)]),
    ('GND', 'F', 0.25, [(3.2, 7.912), (3.2, 8.5), (3.7, 8.5), (3.7, 7.912)]),
    ('GND', 'F', 0.3, [(3.52, 8.5), (3.52, 9.25)]),
    ('GND', 'F', 0.3, [(2.68, 9.25), (4.363, 9.25)]), ('GND', V, 0.6, [(3.52, 9.25)]),
    ('GND', 'F', 0.25, [(2.038, 6.25), (1.1, 6.25)]), ('GND', V, 0.6, [(1.1, 6.25)]),
    # INT1 round the left of the part, INT2 round the right, each down one via beside its module pin
    ('IMU_INT1', 'F', 0.15, [(2.038, 7.75), (1.0, 7.75), (0.4, 7.15)] + M((0.4, 2.6), (1.0, 2.0), (1.905, 2.0))),
    ('IMU_INT1', V, 0.5, M((1.905, 2.0))), ('IMU_INT1', 'B', 0.15, M((1.905, 2.0), (1.905, 2.8))),
    ('IMU_INT2', 'F', 0.15, [(4.363, 7.25), (5.0, 7.25), (5.3, 6.95)] + M((5.3, 2.6), (4.7, 2.0), (4.445, 2.0))),
    ('IMU_INT2', V, 0.5, M((4.445, 2.0))), ('IMU_INT2', 'B', 0.15, M((4.445, 2.0), (4.445, 2.8))),
    # I2C up from the top row to the L3 trunk
    ('I2C_SDA', 'F', 0.15, [(2.7, 6.088), (2.7, 5.0)]), ('I2C_SDA', V, 0.5, [(2.7, 5.0)]),
    ('I2C_SCL', 'F', 0.15, [(3.2, 6.088), (3.2, 5.25), (3.45, 5.0)]), ('I2C_SCL', V, 0.5, [(3.45, 5.0)]),
    # ---- pads the plane-via search finds no site for (gnd_pad_vias: NO SITE) ----------------------------------
    # module corner grounds at the antenna boundary (ODD JOBS 5/129): the module's 100 nF C202 returns straight
    # into pin 1 (the module's own ground; no via site between C201, C202, R201 and the pin row), pin 40 takes a
    # via of its own; gnd_fence.py adds the vias along the antenna boundary under the module
    ('GND', 'B', 0.3, M((-8.75, 21.21), (-9.39, 21.21), (-9.75, 20.85), (-10.27, 20.85))),
    ('GND', 'B', 0.3, M((8.75, 21.21), (9.55, 21.21), (10.1, 20.66))), ('GND', V, None, M((10.1, 20.66))),
    # light sensor: C405 joins U403's VDD, which takes the one plane via
    ('+3V3', 'F', 0.25, [(7.8, -19.87), (8.3, -19.37), (9.0, -19.37)]),
    ('+3V3', 'F', 0.25, [(9.0, -19.37), (9.569, -19.939)]), ('+3V3', V, None, [(9.569, -19.939)]),
    # fuel gauge CTG (pin 1, to GND per MAX17048) into its exposed pad
    ('GND', 'B', 0.2, [(12.71, -13.8), (13.45, -13.8), (13.45, -14.1)]),
    # microphone: the port ring (pad 3) joins GND pad 2 beside it
    ('GND', 'B', 0.25, [(20.5, 6.546), (20.5, 6.0), (20.7, 5.8)]),                  # ends inside the ring's copper
    # UVLO divider bottom R112: the pour cannot reach its GND end between the divider tracks, own via below it
    ('GND', 'B', 0.25, [(4.0, -7.99), (4.0, -7.2)]), ('GND', V, None, [(4.0, -7.2)]),
    # Hall pull-down R306: boxed in by the Hall lines, its own via south-east of the pad
    ('GND', 'F', 0.25, [(18.895, 15.427), (19.5, 16.032)]), ('GND', V, None, [(19.5, 16.032)]),
    # ---- three expander lines that compete for the north-west corner (HAPTIC_EN to the haptic driver, TOF_XSHUT
    # to the ToF sensor, IR_RX_PWR to the IR receiver's supply switch): the grid router finds them in one order and
    # loses one of them in another, so the paths it found together are kept here (2026-10-05 audit-2 layout)
    ('HAPTIC_EN', 'F', 0.20, [(-6.2, 3.5), (-5.6, 2.9)]),
    ('HAPTIC_EN', 'F', 0.20, [(-10.6, 9.85), (-12.8, 9.85)]),
    ('HAPTIC_EN', 'F', 0.20, [(-13.2, 10.25), (-13.2, 12.8)]),
    ('HAPTIC_EN', 'F', 0.20, [(-12.8, 9.85), (-13.2, 10.25)]),
    ('HAPTIC_EN', 'F', 0.20, [(-10.2, 9.45), (-10.6, 9.85)]),
    ('HAPTIC_EN', 'F', 0.20, [(-5.6, 2.9), (-5.6, -7.85)]),
    ('HAPTIC_EN', 'F', 0.20, [(-10.2, 7.9), (-10.2, 9.45)]),
    ('HAPTIC_EN', V, 0.6, [(-13.2, 12.8)]),
    ('HAPTIC_EN', V, 0.6, [(-10.2, 7.9)]),
    ('HAPTIC_EN', V, 0.6, [(-5.6, -7.85)]),
    ('HAPTIC_EN', V, 0.6, [(-6.2, 3.5)]),
    ('HAPTIC_EN', 'B', 0.20, [(-13.2, 12.8), (-13.2, 12.45)]),
    ('HAPTIC_EN', 'B', 0.20, [(-13.2, 12.45), (-13.5, 12.15)]),
    ('HAPTIC_EN', 'I2', 0.20, [(-5.6, -10.6), (-7.8, -12.8)]),
    ('HAPTIC_EN', 'I2', 0.20, [(-7.8, -12.8), (-7.8, -13.9)]),
    ('HAPTIC_EN', 'I2', 0.20, [(-5.6, -7.85), (-5.6, -10.6)]),
    ('HAPTIC_EN', 'I2', 0.20, [(-6.2, 7.9), (-6.2, 3.5)]),
    ('HAPTIC_EN', 'I2', 0.20, [(-7.8, -13.9), (-9.0, -15.1)]),
    ('HAPTIC_EN', 'I2', 0.20, [(-9.0, -15.1), (-9.75, -15.1)]),
    ('HAPTIC_EN', 'I2', 0.20, [(-9.75, -15.1), (-9.75, -15.45)]),
    ('HAPTIC_EN', 'I2', 0.20, [(-10.2, 7.9), (-6.2, 7.9)]),
    ('TOF_XSHUT', 'F', 0.20, [(-7.175, -20.712), (-7.974, -20.712)]),
    ('TOF_XSHUT', 'F', 0.20, [(-7.974, -20.712), (-8.775, -19.912)]),
    ('TOF_XSHUT', V, 0.6, [(-8.0, -17.65)]),
    ('TOF_XSHUT', 'B', 0.20, [(-8.0, -17.65), (-8.0, -16.9)]),
    ('TOF_XSHUT', 'B', 0.20, [(-8.0, -16.9), (-8.75, -16.15)]),
    ('TOF_XSHUT', 'I2', 0.20, [(-8.35, -18.0), (-8.0, -17.65)]),
    ('TOF_XSHUT', 'I2', 0.20, [(-8.35, -20.72), (-8.35, -18.0)]),
    ('TOF_XSHUT', 'I2', 0.20, [(-6.41, -20.72), (-8.35, -20.72)]),
    ('IR_RX_PWR', 'F', 0.20, [(20.7, 8.9), (19.95, 8.15)]),
    ('IR_RX_PWR', 'F', 0.20, [(18.55, 6.75), (19.95, 8.15)]),
    ('IR_RX_PWR', 'F', 0.20, [(18.55, 4.65), (18.55, 6.75)]),
    ('IR_RX_PWR', 'F', 0.20, [(7.75, -22.85), (16.4, -22.85)]),
    ('IR_RX_PWR', 'F', 0.20, [(21.95, -17.3), (21.95, 1.25)]),
    ('IR_RX_PWR', 'F', 0.20, [(22.47, 8.13), (21.7, 8.9)]),
    ('IR_RX_PWR', 'F', 0.20, [(21.95, 1.25), (18.55, 4.65)]),
    ('IR_RX_PWR', 'F', 0.20, [(16.4, -22.85), (21.95, -17.3)]),
    ('IR_RX_PWR', 'F', 0.20, [(22.47, 6.75), (22.47, 8.13)]),
    ('IR_RX_PWR', 'F', 0.20, [(21.7, 8.9), (20.7, 8.9)]),
    ('IR_RX_PWR', V, 0.6, [(-9.4, -20.55)]),
    ('IR_RX_PWR', V, 0.6, [(7.75, -22.85)]),
    ('IR_RX_PWR', 'B', 0.20, [(5.3, -24.75), (-5.2, -24.75)]),
    ('IR_RX_PWR', 'B', 0.20, [(7.0, -21.55), (6.1, -21.55)]),
    ('IR_RX_PWR', 'B', 0.20, [(6.1, -21.55), (5.7, -21.95)]),
    ('IR_RX_PWR', 'B', 0.20, [(7.75, -22.85), (7.75, -22.3)]),
    ('IR_RX_PWR', 'B', 0.20, [(5.7, -24.35), (5.3, -24.75)]),
    ('IR_RX_PWR', 'B', 0.20, [(-5.2, -24.75), (-9.4, -20.55)]),
    ('IR_RX_PWR', 'B', 0.20, [(7.75, -22.3), (7.0, -21.55)]),
    ('IR_RX_PWR', 'B', 0.20, [(5.7, -21.95), (5.7, -24.35)]),
    ('IR_RX_PWR', 'I2', 0.20, [(-9.4, -18.2), (-9.4, -20.55)]),
    ('IR_RX_PWR', 'I2', 0.20, [(-8.25, -15.45), (-8.25, -15.85)]),
    ('IR_RX_PWR', 'I2', 0.20, [(-7.65, -16.45), (-9.4, -18.2)]),
    ('IR_RX_PWR', 'I2', 0.20, [(-8.25, -15.85), (-7.65, -16.45)]),
    # ---- IR_TX (GPIO38, the 38 kHz carrier) from the LED switch's gate resistor at 12 o'clock to the module: one L3
    # run down the empty east half (outside the +3V3 core under the PDM lanes), one via, a short F leg into the
    # module's escape via; it keeps out of the service field
    ('IR_TX', 'I2', 0.2, [(9.53, -17.55), (9.88, -17.2), (14.55, -17.2), (16.9, -14.85), (16.9, 2.0), (15.55, 3.35),
                          (12.45, 3.35)]),                     # (between the gauge's I2C vias and the H2 screw ring)
    ('IR_TX', V, 0.6, [(12.45, 3.35)]),
    ('IR_TX', 'F', 0.2, [(12.45, 3.35), (10.25, 5.55), (10.25, 8.53)]),
]

if __name__ == '__main__':
    run('local', [], ADD)
