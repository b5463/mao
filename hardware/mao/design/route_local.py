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
SCLK and MOSI at the pins, and rise through one via each beside the connector: two staggered columns,
so every stub passes between two vias.

Fan-out (the KINO/6-layer lesson: design the escapes, then route): I2S as three nested lanes into the
amplifier, I2C straight across to the haptic driver, a via for every expander pin and for every slow or
F-bound module pin, so the router can take L3 without paying for new vias in crowded pin rows.
"""
from handroute import run

V = 'V'
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
    ('USB_DP', 'F', 0.2, [(-0.5, -18.712), (-1.1, -18.112), (-1.1, -16.1), (-7.6, -9.6), (-7.6, 4.7)]),
    ('USB_DN', 'F', 0.2, [(1.2, -19.4), (1.2, -17.7), (0.79, -17.29), (0.5, -17.29)]),    # U101.5
    ('USB_DN', 'F', 0.2, [(0.5, -17.29), (0.5, -16.7), (-0.2, -16.0), (-6.8, -9.4), (-6.8, 5.57), (-7.2, 5.97),
                          (-7.6, 5.97)]),
    ('GND', 'F', 0.2, [(-0.5, -17.29), (-0.5, -16.6)]),                               # ESD ground, between the pair
    ('GND', V, 0.5, [(-0.5, -16.6)]),
    ('USB_DP', V, 0.5, [(-7.6, 4.7)]),
    ('USB_DN', V, 0.5, [(-7.6, 5.97)]),
    ('USB_DP', 'B', 0.2, [(-7.6, 4.7), (-8.75, 4.7)]),                                # module pin 14
    ('USB_DN', 'B', 0.2, [(-7.6, 5.97), (-8.75, 5.97)]),                              # module pin 13
    # ---- face switch: each pair of legs is one contact inside the part; join them under the body -------
    ('PRESS_N', 'F', 0.3, [(-3.1, -4.35), (3.1, -4.35)]),
    ('GND', 'F', 0.3, [(-3.1, -0.65), (3.1, -0.65)]),          # and its GND legs (the left one has no via site)
    # ---- display bus: vias beside the connector (J301 pins 10-14 at x -12) -------------------------------
    ('LCD_CS', 'F', 0.15, [(-11.7, 0.25), (-10.95, 0.25)]),
    ('LCD_CS', V, 0.5, [(-10.95, 0.25)]),
    ('LCD_SCLK_P', 'F', 0.15, [(-11.7, 0.75), (-10.3, 0.75)]),
    ('LCD_SCLK_P', V, 0.5, [(-10.3, 0.75)]),
    ('LCD_MOSI_P', 'F', 0.15, [(-11.7, 1.25), (-10.95, 1.25)]),
    ('LCD_MOSI_P', V, 0.5, [(-10.95, 1.25)]),
    ('LCD_DC', 'F', 0.15, [(-11.7, 1.75), (-10.3, 1.75)]),
    ('LCD_DC', V, 0.5, [(-10.3, 1.75)]),
    ('LCD_TE', 'F', 0.15, [(-11.7, 2.25), (-10.95, 2.25)]),
    ('LCD_TE', V, 0.5, [(-10.95, 2.25)]),
    # ---- display bus on B: five nested lanes from the module's top row (pins 17-21) to the vias ----------
    ('LCD_SCLK', 'B', 0.2, [(-0.635, 2.8), (-0.635, 1.9)]),                          # pin 20 -> R301
    ('LCD_MOSI', 'B', 0.2, [(-1.905, 2.8), (-1.905, 1.9)]),                          # pin 19 -> R302
    ('LCD_CS', 'B', 0.15, [(0.635, 2.8), (0.635, -0.3), (-7.0, -0.3), (-7.55, 0.25), (-10.95, 0.25)]),
    ('LCD_SCLK_P', 'B', 0.15, [(-0.635, 0.73), (-0.635, 0.2), (-7.0, 0.2), (-7.55, 0.75), (-10.3, 0.75)]),
    ('LCD_MOSI_P', 'B', 0.15, [(-1.905, 0.7), (-7.0, 0.7), (-7.55, 1.25), (-10.95, 1.25)]),
    ('LCD_DC', 'B', 0.15, [(-3.175, 2.8), (-3.175, 1.2), (-7.0, 1.2), (-7.55, 1.75), (-10.3, 1.75)]),
    ('LCD_TE', 'B', 0.15, [(-4.445, 2.8), (-4.445, 1.7), (-7.0, 1.7), (-7.55, 2.25), (-10.95, 2.25)]),
    # ---- module corner and left column: USB_PRESENT (pin 15) and BOARD_ID (pin 12) drop to L3 ---------------
    ('USB_PRESENT_N', 'B', 0.2, [(-7.0, 3.45), (-8.25, 3.45)]),
    ('USB_PRESENT_N', V, 0.5, [(-8.25, 3.45)]),
    ('BOARD_ID', 'B', 0.2, [(-9.4, 7.24), (-10.3, 7.24)]),
    ('BOARD_ID', V, 0.5, [(-10.3, 7.24)]),
    # ---- I2S to the amplifier: three nested lanes, right-side pins entered square ---------------------------
    ('AMP_LRCLK', 'B', 0.2, [(-9.4, 8.51), (-9.99, 8.51), (-14.75, 3.75), (-15.56, 3.75)]),
    ('AMP_BCLK', 'B', 0.2, [(-9.4, 9.78), (-9.72, 9.78), (-14.75, 4.75), (-15.56, 4.75)]),
    ('AMP_DIN', 'B', 0.2, [(-9.4, 11.05), (-11.4, 11.05), (-16.25, 6.2), (-16.25, 5.6)]),
    # ---- I2C straight across to the haptic driver (the trunk to the other devices leaves on L3) -----------
    ('I2C_SCL', 'B', 0.2, [(-9.4, 12.32), (-12.33, 12.32), (-12.75, 11.9), (-13.2, 11.9)]),
    ('I2C_SDA', 'B', 0.2, [(-9.4, 13.59), (-11.56, 13.59), (-12.75, 12.4), (-13.2, 12.4)]),
    ('I2C_SDA', V, 0.5, [(-10.5, 13.59)]), ('I2C_SCL', V, 0.5, [(-11.2, 12.32)]),       # the trunk drops to L3
    # ---- expander fan-out (U202 at (-9.0, -13.1)): every pin to its own via (staggered), the lines run on L3 --
    ('EXP_RST_N', 'B', 0.15, [(-10.6, -12.35), (-11.45, -12.35)]), ('EXP_RST_N', V, 0.5, [(-11.45, -12.35)]),
    ('LCD_PWR_EN', 'B', 0.15, [(-10.6, -13.35), (-11.45, -13.35)]), ('LCD_PWR_EN', V, 0.5, [(-11.45, -13.35)]),
    ('LCD_RST_N', 'B', 0.15, [(-10.6, -12.85), (-12.15, -12.85)]), ('LCD_RST_N', V, 0.5, [(-12.15, -12.85)]),
    ('AMP_SD_N', 'B', 0.15, [(-10.6, -13.85), (-12.15, -13.85)]), ('AMP_SD_N', V, 0.5, [(-12.15, -13.85)]),
    ('HAPTIC_EN', 'B', 0.15, [(-9.75, -14.7), (-9.75, -15.45)]), ('HAPTIC_EN', V, 0.5, [(-9.75, -15.45)]),
    ('IR_RX_PWR', 'B', 0.15, [(-8.25, -14.7), (-8.25, -15.45)]), ('IR_RX_PWR', V, 0.5, [(-8.25, -15.45)]),
    ('TOF_XSHUT', 'B', 0.15, [(-8.75, -14.7), (-8.75, -16.15)]), ('TOF_XSHUT', V, 0.5, [(-8.75, -16.15)]),
    ('CHG_N', 'B', 0.15, [(-7.4, -13.85), (-6.55, -13.85)]), ('CHG_N', V, 0.5, [(-6.55, -13.85)]),
    ('EXP_INT_N', 'B', 0.15, [(-7.4, -12.85), (-6.55, -12.85)]), ('EXP_INT_N', V, 0.5, [(-6.55, -12.85)]),
    ('SENSE_ALRT_N', 'B', 0.15, [(-7.4, -13.35), (-5.85, -13.35)]), ('SENSE_ALRT_N', V, 0.5, [(-5.85, -13.35)]),
    ('I2C_SCL', 'B', 0.15, [(-7.4, -12.35), (-5.85, -12.35)]), ('I2C_SCL', V, 0.5, [(-5.85, -12.35)]),
    ('I2C_SDA', 'B', 0.15, [(-8.25, -11.66), (-8.25, -11.4), (-7.95, -11.1), (-7.45, -11.1)]),
    ('I2C_SDA', V, 0.5, [(-7.45, -11.1)]),
    # expander supply: pins 14/15 straight up into C205, GND pin 16 into its other end, one via each
    ('+3V3', 'B', 0.2, [(-9.25, -11.66), (-9.25, -10.95), (-8.75, -10.95)]),
    ('+3V3', 'B', 0.25, [(-8.75, -11.66), (-8.75, -10.2)]),
    ('+3V3', 'B', 0.3, [(-8.79, -10.2), (-7.9, -10.2)]), ('+3V3', V, 0.6, [(-7.9, -10.2)]),
    ('GND', 'B', 0.25, [(-9.75, -11.66), (-9.75, -10.2)]),
    ('GND', 'B', 0.3, [(-9.75, -10.2), (-10.65, -10.2)]), ('GND', V, 0.6, [(-10.65, -10.2)]),
    ('+3V3', 'B', 0.25, [(-3.36, -12.85), (-2.3, -12.85)]), ('+3V3', V, 0.6, [(-2.3, -12.85)]),   # R206, into core 1
    # CHG_N pull-up R107 beside the expander's CHG_N via (its +3V3 via also feeds the ToF's INT pull-up on F)
    ('CHG_N', 'B', 0.15, [(-6.55, -13.85), (-6.15, -14.25), (-6.15, -14.59)]),
    ('+3V3', 'B', 0.25, [(-6.15, -15.61), (-5.44, -16.32)]), ('+3V3', V, 0.6, [(-5.44, -16.32)]),
    # ---- ToF U402 (F): I2C down one via each beside pins 9/10, INT through its pull-up to L3, XSHUT at R210 --
    ('I2C_SDA', 'F', 0.15, [(-9.58, -18.01), (-9.2, -17.63), (-9.2, -17.3)]), ('I2C_SDA', V, 0.5, [(-9.2, -17.3)]),
    ('I2C_SCL', 'F', 0.15, [(-10.28, -17.64), (-9.9, -17.26), (-9.9, -16.93)]), ('I2C_SCL', V, 0.5, [(-9.9, -16.93)]),
    ('TOF_INT_N', 'F', 0.2, [(-8.16, -18.76), (-6.46, -18.76), (-6.46, -17.85)]), ('TOF_INT_N', V, 0.5, [(-6.46, -17.85)]),
    ('+3V3', 'F', 0.25, [(-5.44, -18.76), (-5.44, -16.32)]),
    ('TOF_XSHUT', 'F', 0.15, [(-7.32, -20.98), (-6.55, -20.98)]), ('TOF_XSHUT', V, 0.5, [(-6.55, -20.98)]),
    # ---- display rail switch U105: CT, QOD and the output straight off their pins ------------------------
    ('LCD_SW_CT', 'B', 0.2, [(-12.46, -8.35), (-12.46, -9.57)]),
    ('LCD_SW_QOD', 'B', 0.2, [(-12.46, -7.42), (-10.95, -7.42)]),
    ('3V3_LCD', 'B', 0.3, [(-12.46, -6.42), (-9.7, -6.42)]),
    # ---- module right column: slow and F-bound pins to staggered vias ---------------------------------------
    ('EXP_RST_N', 'B', 0.15, [(9.4, 9.78), (10.25, 9.78)]), ('EXP_RST_N', V, 0.5, [(10.25, 9.78)]),
    ('IR_TX', 'B', 0.15, [(9.4, 11.05), (10.95, 11.05)]), ('IR_TX', V, 0.5, [(10.95, 11.05)]),
    ('IR_RX', 'B', 0.15, [(9.4, 12.32), (10.25, 12.32)]), ('IR_RX', V, 0.5, [(10.25, 12.32)]),
    ('HALL_A', 'B', 0.15, [(9.4, 13.59), (10.95, 13.59)]), ('HALL_A', V, 0.5, [(10.95, 13.59)]),
    ('HALL_B', 'B', 0.15, [(9.4, 14.86), (10.25, 14.86)]), ('HALL_B', V, 0.5, [(10.25, 14.86)]),
    ('HALL_FAST', 'B', 0.15, [(9.4, 18.67), (10.25, 18.67)]), ('HALL_FAST', V, 0.5, [(10.25, 18.67)]),
    ('PRESS_N', 'B', 0.2, [(9.4, 4.7), (9.6, 4.7), (10.4, 3.9)]),                    # pin 27 -> R202
    ('PRESS_N', 'B', 0.2, [(10.4, 3.76), (11.2, 3.76)]), ('PRESS_N', V, 0.5, [(11.2, 3.76)]),   # -> F, face switch
    ('PRESS_N', 'F', 0.2, [(11.2, 3.76), (5.0, -2.44), (5.0, -3.75), (4.4, -4.35), (3.9, -4.35)]),  # to SW301 pin 1,
                                                                                      # round its F keep-out
    ('PRESS_N', 'B', 0.2, [(11.2, 3.76), (11.05, 3.61), (11.05, 1.0), (11.35, 0.7)]),   # BOOT probe TP8, between R202/R205
    # ---- module top row right of the display group: slow lines to L3 ----------------------------------------
    ('EXP_INT_N', 'B', 0.15, [(3.175, 2.8), (3.175, 2.0)]), ('EXP_INT_N', V, 0.5, [(3.175, 2.0)]),
    ('TOF_INT_N', 'B', 0.15, [(5.715, 2.8), (5.715, 2.0)]), ('TOF_INT_N', V, 0.5, [(5.715, 2.0)]),
    ('LCD_BL_PWM', 'B', 0.15, [(6.985, 2.8), (6.985, 2.0)]), ('LCD_BL_PWM', V, 0.5, [(6.985, 2.0)]),
    # ---- touch: pins 4/5/6 rise beside the module to their series R on F; three nested electrode lanes on F
    ('TOUCH_LEFT', 'B', 0.2, [(-8.75, 17.4), (-10.1, 17.4)]), ('TOUCH_LEFT', V, 0.5, [(-10.1, 17.4)]),
    ('TOUCH_LEFT', 'F', 0.2, [(-10.1, 17.4), (-10.99, 17.4)]),
    ('TOUCH_TOP', 'B', 0.2, [(-8.75, 16.13), (-10.1, 16.13)]), ('TOUCH_TOP', V, 0.5, [(-10.1, 16.13)]),
    ('TOUCH_TOP', 'F', 0.2, [(-10.1, 16.13), (-10.99, 16.13)]),
    ('TOUCH_REAR', 'B', 0.2, [(-8.75, 14.86), (-10.1, 14.86)]), ('TOUCH_REAR', V, 0.5, [(-10.1, 14.86)]),
    ('TOUCH_REAR', 'F', 0.2, [(-10.1, 14.86), (-10.99, 14.86)]),
    ('TOUCH_LEFT_E', 'F', 0.2, [(-12.01, 17.4), (-20.6, 8.81), (-21.92, 8.81), (-22.84, 7.89), (-26.34, 7.89)]),
    ('TOUCH_TOP_E', 'F', 0.2, [(-12.01, 16.13), (-18.9, 9.24), (-19.92, 8.22), (-19.92, 7.25)]),
    ('TOUCH_REAR_E', 'F', 0.2, [(-12.01, 14.86), (-18.1, 8.77), (-18.1, -6.9)]),
    ('TOUCH_REAR_E', V, 0.5, [(-18.1, -6.9)]),
    ('TOUCH_REAR_E', 'B', 0.2, [(-18.1, -6.9), (-19.6, -6.9), (-20.4, -7.7), (-20.4, -8.8)]),
    # ---- module EN and 3V3 corner: EN through its pull-up and delay cap, out to L3 past the bulk cap -------
    ('MCU_EN', 'B', 0.2, [(-8.75, 18.8), (-12.9, 18.8)]), ('MCU_EN', V, 0.5, [(-12.9, 18.8)]),
    ('GND', 'B', 0.3, [(-11.7, 17.82), (-12.8, 17.82)]), ('GND', V, 0.6, [(-12.8, 17.82)]),
    ('+3V3', 'B', 0.3, [(-8.75, 19.94), (-12.625, 19.94)]),
    ('+3V3', 'B', 0.25, [(-11.23, 19.94), (-11.23, 21.05)]),
    ('+3V3', 'B', 0.3, [(-12.4, 19.94), (-12.4, 21.0)]), ('+3V3', V, 0.6, [(-12.4, 21.0)]),
    ('GND', 'B', 0.3, [(-10.27, 21.05), (-8.75, 21.05)]),
    # ---- haptic driver: HAPTIC_EN through its pull-down to L3; both GND pins to one via under the body ------
    ('HAPTIC_EN', 'B', 0.2, [(-13.5, 13.4), (-12.75, 13.4), (-12.4, 13.75), (-12.4, 14.34)]),
    ('HAPTIC_EN', 'B', 0.2, [(-13.8, 13.4), (-13.8, 13.9), (-15.1, 15.2)]), ('HAPTIC_EN', V, 0.5, [(-15.1, 15.2)]),
    ('GND', 'B', 0.25, [(-17.7, 12.4), (-15.85, 12.4), (-15.35, 12.9), (-13.5, 12.9)]),
    ('GND', 'B', 0.25, [(-16.2, 12.4), (-16.2, 13.55), (-16.45, 13.8)]), ('GND', V, 0.6, [(-16.45, 13.8)]),
    ('GND', 'B', 0.3, [(-16.52, 10.0), (-15.75, 10.0)]), ('GND', V, 0.6, [(-15.75, 10.0)]),      # C503 return
    # ---- charger status out of the power section: one via to F each, then down to L3 beyond the VSYS band
    ('CHG_N', 'B', 0.15, [(7.95, -11.03), (7.53, -11.03), (7.25, -10.75)]), ('CHG_N', V, 0.5, [(7.25, -10.75)]),
    ('CHG_N', 'F', 0.15, [(7.25, -10.75), (7.25, -10.25), (3.6, -6.6)]), ('CHG_N', V, 0.5, [(3.6, -6.6)]),
    ('USB_PRESENT_N', 'B', 0.2, [(9.25, -8.76), (8.1, -8.76)]), ('USB_PRESENT_N', V, 0.5, [(8.1, -8.76)]),
    ('USB_PRESENT_N', 'F', 0.2, [(8.1, -8.76), (5.9, -6.56), (4.35, -6.56)]), ('USB_PRESENT_N', V, 0.5, [(4.35, -6.56)]),
    # ---- service field: pull-ups face their probe pads, +3V3 ends join TP3; the trunk drops to L3 here ----
    ('I2C_SCL', 'B', 0.2, [(6.0, -2.7), (7.6, -2.7), (8.11, -2.19), (8.2, -2.19)]), ('I2C_SCL', V, 0.5, [(7.1, -2.7)]),
    ('I2C_SDA', 'B', 0.2, [(6.0, 0.3), (6.9, 0.3), (7.4, -0.2), (7.4, -1.0), (8.25, -1.0), (9.44, -2.19)]),
    ('I2C_SDA', V, 0.5, [(7.4, -1.0)]),
    ('+3V3', 'B', 0.3, [(8.2, -3.21), (8.2, -3.7), (9.44, -3.7), (9.44, -3.21)]),
    ('+3V3', 'B', 0.3, [(8.8, -3.7), (8.8, -5.7)]), ('+3V3', V, 0.6, [(8.8, -4.45)]),
    ('+3V3', 'B', 0.3, [(9.25, -7.74), (9.25, -6.0)]),     # USB_PRESENT pull-up R106 into TP3 (it sits over the VSYS band)
    # ---- fuel gauge U103: I2C and ALERT out of its right flank to L3 (the L3 VSYS band lies to its left) ----
    ('I2C_SDA', 'B', 0.2, [(16.39, -13.45), (17.1, -13.45), (17.3, -13.25)]), ('I2C_SDA', V, 0.5, [(17.3, -13.25)]),
    ('I2C_SCL', 'B', 0.2, [(16.39, -13.95), (17.15, -13.95), (17.3, -14.1)]), ('I2C_SCL', V, 0.5, [(17.3, -14.1)]),
    ('SENSE_ALRT_N', 'B', 0.2, [(16.39, -14.95), (16.39, -15.41), (16.2, -15.6)]), ('SENSE_ALRT_N', V, 0.5, [(16.2, -15.6)]),
    ('SENSE_ALRT_N', 'B', 0.2, [(15.81, -16.7), (16.2, -16.31), (16.2, -15.6)]),           # its pull-up R212
    ('SENSE_ALRT_N', 'F', 0.2, [(10.79, -18.27), (11.6, -18.27)]), ('SENSE_ALRT_N', V, 0.5, [(11.6, -18.27)]),
    ('SENSE_ALRT_N', 'B', 0.2, [(11.6, -18.27), (15.0, -18.27), (15.81, -17.46), (15.81, -16.7)]),  # ALS -> R212
    ('+3V3', 'B', 0.25, [(14.79, -16.7), (14.79, -17.6)]), ('+3V3', V, 0.6, [(14.79, -17.6)]),
    # ---- Hall wired-OR: each sensor's FAST output drops to L3 beside its pin ---------------------------------
    ('HALL_FAST', 'F', 0.2, [(23.32, 13.75), (23.72, 14.15)]), ('HALL_FAST', V, 0.5, [(23.72, 14.15)]),
    ('GND', 'F', 0.25, [(22.23, 12.55), (22.78, 13.1)]),                                    # U301 GND pin to its tab,
    ('GND', 'F', 0.3, [(22.78, 13.15), (22.07, 13.86)]), ('GND', V, 0.6, [(22.07, 13.86)]),  # one via for both
    ('HALL_FAST', 'F', 0.2, [(21.76, 16.12), (21.4, 16.48), (21.4, 16.7)]), ('HALL_FAST', V, 0.5, [(21.4, 16.7)]),
    # ---- IR receiver supply switch: IR_RX_PWR from the expander arrives on L3 beside the filter R505 ---------
    ('IR_RX_PWR', 'F', 0.15, [(22.47, 6.75), (23.35, 6.75)]), ('IR_RX_PWR', V, 0.5, [(23.35, 6.75)]),
    # IR receiver supply: U503 VCC down into the RC filter R505/C506, and R506 (the output pull-up) to it
    ('IR_RX_VCC', 'F', 0.25, [(22.3, 3.15), (22.3, 5.5), (21.6, 6.2), (21.6, 6.75)]),
    ('IR_RX_VCC', 'F', 0.25, [(20.07, 6.7), (21.45, 6.7)]),
    ('IR_RX_VCC', 'F', 0.25, [(17.63, 4.95), (17.63, 5.6), (18.68, 6.65), (20.07, 6.65)]),
    # ---- microphone (bottom port): the ground ring's own via, towards the rim -----------------------------------------
    ('GND', 'B', 0.3, [(22.1, 5.3), (23.3, 5.3)]), ('GND', V, 0.6, [(23.3, 5.3)]),     # ring round the sound port
    # mic supply: MIC_VDD from pad 5 down into C407, across to R402 and C406, round to its probe pad TP14
    ('MIC_VDD', 'B', 0.3, [(21.59, 7.55), (21.59, 9.04)]),
    ('MIC_VDD', 'B', 0.3, [(20.39, 9.1), (23.09, 9.1)]),
    ('MIC_VDD', 'B', 0.3, [(23.09, 9.15), (23.09, 10.75), (22.09, 11.75)]),
    # ---- Tag-Connect J201 (turned so RXD/TXD/GND face the module in its pin order) --------------------------
    ('UART_RX', 'B', 0.2, [(8.75, 16.13), (14.36, 16.13), (14.56, 16.33)]),
    ('UART_TX', 'B', 0.2, [(8.75, 17.4), (14.36, 17.4), (14.56, 17.6)]),
    ('GND', 'B', 0.3, [(14.56, 18.87), (13.6, 18.87)]), ('GND', V, 0.6, [(13.6, 18.87)]),
    ('PRESS_N', 'B', 0.2, [(15.83, 16.33), (16.9, 16.33)]), ('PRESS_N', V, 0.5, [(16.9, 16.33)]),
    ('+3V3', 'B', 0.3, [(15.83, 17.6), (16.9, 17.6)]), ('+3V3', V, 0.6, [(16.9, 17.6)]),
    # ---- right touch: pin 39 through R308, the lead along the bottom edge and up to one via, then F onto
    # the electrode's lower end (the right arc is F-only; its plane cut keeps vias out of the sector)
    ('TOUCH_RIGHT', 'B', 0.2, [(8.75, 19.94), (9.5, 19.94), (10.34, 20.78), (10.34, 21.3)]),
    ('TOUCH_RIGHT_E', 'B', 0.2, [(11.36, 21.3), (17.6, 21.3), (18.2, 20.7), (18.2, 10.5)]),
    ('TOUCH_RIGHT_E', V, 0.5, [(18.2, 10.5)]),
    ('TOUCH_RIGHT_E', 'F', 0.2, [(18.2, 10.5), (19.25, 9.45), (24.9, 9.45)]),
    # ---- PDM pair (B): data level with pin 29 straight into pad 1, clock from pin 30 round pad 1 into pad 4
    ('MIC_DATA', 'B', 0.2, [(8.75, 7.24), (19.8, 7.24)]),
    ('MIC_CLK', 'B', 0.2, [(8.75, 8.51), (20.8, 8.51), (20.8, 6.9), (21.6, 6.9)]),
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
    ('IMU_INT1', 'F', 0.15, [(2.038, 7.75), (1.0, 7.75), (0.4, 7.15), (0.4, 2.6), (1.0, 2.0), (1.905, 2.0)]),
    ('IMU_INT1', V, 0.5, [(1.905, 2.0)]), ('IMU_INT1', 'B', 0.15, [(1.905, 2.0), (1.905, 2.8)]),
    ('IMU_INT2', 'F', 0.15, [(4.363, 7.25), (5.0, 7.25), (5.3, 6.95), (5.3, 2.6), (4.7, 2.0), (4.445, 2.0)]),
    ('IMU_INT2', V, 0.5, [(4.445, 2.0)]), ('IMU_INT2', 'B', 0.15, [(4.445, 2.0), (4.445, 2.8)]),
    # I2C up from the top row to the L3 trunk
    ('I2C_SDA', 'F', 0.15, [(2.7, 6.088), (2.7, 5.0)]), ('I2C_SDA', V, 0.5, [(2.7, 5.0)]),
    ('I2C_SCL', 'F', 0.15, [(3.2, 6.088), (3.2, 5.25), (3.45, 5.0)]), ('I2C_SCL', V, 0.5, [(3.45, 5.0)]),
]

if __name__ == '__main__':
    run('local', [], ADD)
