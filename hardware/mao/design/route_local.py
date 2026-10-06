"""Designed signal copper of MAO_MAIN A1, 4 layers (KiCad python): <KiCad python> route_local.py add

Drawn from the real pad coordinates (geo.py) of placement.py and locked, so the grid router works around it.
Only what decides the board's topology is designed here; the router joins everything else.

USB (ODD JOBS 23): at the receptacle the interleaved contacts are untangled at its pads (D+ joins on F over
two vias, D- joins on B under them) and the ESD array U101 sits in the pair, exactly as A0. From the ESD array
the router takes the pair (USB first in its order) to the 22R series resistors at module pins 23/24.
Everything A1 kept from A0 at the same place keeps A0's local copper: the face switch's leg joins, J301's VCI
and ground drops, the amplifier's SD_MODE and ground stubs, the ToF sensor's escape vias, the fuel gauge's I2C
vias, the Hall pair's vias, the IR receiver's supply and output, the haptic driver, the service-field I2C
pull-ups. New for A1: the module's 3V3 corner and the IMU's supply drops for its new decoupling.
"""
from handroute import run
import mechanical as _m

V = 'V'


def M(*pts):
    """A0 copper drawn at the WROOM's pre-audit position (parts that did not move: haptic driver)."""
    return [(x, round(y + _m.MODULE_SHIFT, 3)) for x, y in pts]


ADD = [
    # ---- USB at the receptacle (B pads, y -21.905), A0 -------------------------------------------------
    ('USB_C_DP', 'B', 0.2, [(-0.25, -21.3), (-0.25, -20.75)]),                        # A6
    ('USB_C_DP', 'B', 0.2, [(0.75, -21.3), (0.75, -20.75)]),                          # B6
    ('USB_C_DP', V, 0.5, [(-0.25, -20.75)]),
    ('USB_C_DP', V, 0.5, [(0.75, -20.75)]),
    ('USB_C_DP', 'F', 0.2, [(-0.25, -20.75), (0.75, -20.75)]),                        # D+ joins on F
    ('USB_C_DN', 'B', 0.15, [(0.25, -21.3), (0.25, -19.9)]),                          # A7, between the D+ vias
    ('USB_C_DN', 'B', 0.2, [(-0.75, -21.3), (-0.75, -19.9)]),                         # B7
    ('USB_C_DN', 'B', 0.2, [(-0.75, -19.9), (0.25, -19.9), (0.75, -19.4), (1.2, -19.4)]),   # D- joins on B
    ('USB_C_DN', V, 0.5, [(1.2, -19.4)]),
    ('USB_C_DP', 'F', 0.2, [(-0.25, -20.75), (-0.5, -20.5), (-0.5, -18.712)]),        # U101.3
    ('USB_C_DN', 'F', 0.2, [(1.2, -19.4), (1.2, -17.7), (0.79, -17.29), (0.5, -17.29)]),    # U101.5
    ('GND', 'F', 0.2, [(-0.5, -17.29), (-0.5, -16.6)]),                               # ESD ground, between the pair
    ('GND', V, 0.5, [(-0.5, -16.6)]),
    # ---- face switch: each pair of legs is one contact inside the part; join them under the body -------
    ('PRESS_N', 'F', 0.3, [(-3.1, -4.35), (3.1, -4.35)]),
    ('GND', 'F', 0.3, [(-3.1, -0.65), (3.1, -0.65)]),
    # ---- J301 (A0 place): VCI (pad 3) through one via under the housing to C301 on F; GND pad 10 to the plane
    ('3V3_LCD', 'B', 0.3, [(-11.15, 3.25), (-12.4, 3.25)]), ('3V3_LCD', V, 0.5, [(-12.4, 3.25)]),
    ('3V3_LCD', 'F', 0.3, [(-12.4, 3.25), (-11.78, 2.63), (-11.78, 2.0)]),
    ('GND', 'B', 0.3, [(-11.15, -0.25), (-12.3, -0.25)]), ('GND', V, 0.5, [(-12.3, -0.25)]),
    # ---- amplifier (A0): SD_MODE to its F pull-down through one via; GND pins into the exposed pad -------------
    ('AMP_SD', 'B', 0.2, [(-15.838, -9.45), (-16.6, -9.45)]), ('AMP_SD', V, 0.5, [(-16.6, -9.45)]),
    ('AMP_SD', 'F', 0.2, [(-16.6, -9.45), (-15.95, -9.45)]),
    ('GND', 'B', 0.2, [(-15.838, -8.95), (-15.2, -8.95)]),
    ('GND', 'B', 0.2, [(-12.963, -8.45), (-13.6, -8.45)]),
    ('GND', 'B', 0.2, [(-14.65, -7.262), (-14.65, -8.0)]),
    ('GND', 'F', 0.25, [(-15.95, -10.47), (-15.25, -10.47), (-14.88, -10.1)]),       # R208 into the amp's F pad
    ('GND', 'F', 0.3, [(-14.88, -10.1), (-14.88, -9.2)]),
    # ---- ToF U402 (F, A0): I2C down one via each beside pins 9/10, INT through its pull-up, XSHUT at R210 --
    ('I2C_SDA', 'F', 0.15, [(-9.44, -17.75), (-9.06, -17.37), (-9.06, -17.04)]), ('I2C_SDA', V, 0.5, [(-9.06, -17.04)]),
    ('I2C_SCL', 'F', 0.15, [(-10.14, -17.38), (-9.76, -17.00), (-9.76, -16.67)]), ('I2C_SCL', V, 0.5, [(-9.76, -16.67)]),
    ('TOF_INT_N', 'F', 0.2, [(-8.02, -18.50), (-6.32, -18.50), (-6.32, -17.59)]), ('TOF_INT_N', V, 0.5, [(-6.32, -17.59)]),
    ('+3V3', 'F', 0.25, [(-5.30, -18.50), (-5.30, -17.20)]), ('+3V3', V, 0.6, [(-5.30, -17.20)]),
    ('TOF_XSHUT', 'F', 0.15, [(-7.18, -20.72), (-6.41, -20.72)]), ('TOF_XSHUT', V, 0.5, [(-6.41, -20.72)]),
    # IR LED switch gate resistor R503: IR_TX drops to L3 straight below it
    ('IR_TX', 'B', 0.15, [(9.53, -18.56), (9.53, -17.55)]), ('IR_TX', V, 0.5, [(9.53, -17.55)]),
    # ---- fuel gauge U103 (A0): I2C out of its east flank to staggered vias; CTG into its exposed pad ---------
    ('I2C_SDA', 'B', 0.2, [(14.688, -13.8), (15.45, -13.8)]), ('I2C_SDA', V, 0.5, [(15.45, -13.8)]),
    ('I2C_SCL', 'B', 0.2, [(14.688, -14.3), (15.6, -14.3), (16.1, -14.8)]), ('I2C_SCL', V, 0.5, [(16.1, -14.8)]),
    ('GND', 'B', 0.2, [(12.71, -13.8), (13.45, -13.8), (13.45, -14.1)]),
    # ---- Hall pair (A0): FAST to L3 beside each pin, one GND via for both, the pull-down's own GND via ---------
    ('HALL_FAST', 'F', 0.2, [(23.32, 13.75), (23.72, 14.15)]), ('HALL_FAST', V, 0.5, [(23.72, 14.15)]),
    ('GND', 'F', 0.25, [(22.23, 12.55), (22.78, 13.1)]),
    ('GND', 'F', 0.3, [(22.78, 13.15), (22.07, 13.86)]), ('GND', V, 0.6, [(22.07, 13.86)]),
    ('HALL_FAST', 'F', 0.2, [(21.76, 16.12), (21.4, 16.48), (21.4, 16.7)]), ('HALL_FAST', V, 0.5, [(21.4, 16.7)]),
    ('GND', 'F', 0.25, [(18.895, 15.427), (19.5, 16.032)]), ('GND', V, None, [(19.5, 16.032)]),
    # ---- IR receiver (A0): OUT up into its pull-up; VCC round the inside of the pins to the pull-up, the RC
    # filter and C506
    ('IR_RX', 'F', 0.2, [(24.15, 1.27), (24.15, 3.6)]),
    ('IR_RX_VCC', 'F', 0.25, [(24.15, -1.27), (24.15, -2.6), (22.83, -3.92)]),
    ('IR_RX_VCC', 'F', 0.25, [(23.55, -1.27), (22.9, -0.62), (22.9, 3.41), (23.09, 3.6)]),
    ('IR_RX_VCC', 'F', 0.25, [(22.9, 3.6), (22.9, 6.32), (22.47, 6.75)]),            # (A1: R505 turned, VCC east)
    # ---- TPS22916C load switches (F, 0.4 mm WCSP): every ball escapes on a 0.15 mm line, outward -------------
    # IR receiver rail U504 (turned 180): VOUT -> R505, VIN -> C508, ON -> R211, GND -> its own via
    ('AUX_3V3', 'F', 0.15, [(19.6, 4.8), (19.95, 5.15), (20.95, 5.15), (21.45, 5.65), (21.45, 6.75)]),
    ('+3V3', 'F', 0.15, [(19.2, 4.8), (18.85, 5.15), (17.3, 5.15)]),
    ('AUX_PWR_EN', 'F', 0.15, [(19.2, 4.4), (18.85, 4.05), (18.4, 3.6), (16.05, 3.6)]),
    ('GND', 'F', 0.15, [(19.6, 4.4), (19.95, 4.05), (20.3, 3.7)]), ('GND', V, None, [(20.3, 3.7)]),
    # panel rail U105: VOUT -> C109, VIN -> C110, ON -> R116, GND -> its own via
    ('3V3_LCD', 'F', 0.15, [(-9.2, 4.0), (-9.6, 4.0), (-10.35, 4.75), (-10.92, 4.75)]),
    ('+3V3', 'F', 0.15, [(-8.8, 4.0), (-7.0, 4.0)]),
    ('LCD_PWR_EN', 'F', 0.15, [(-8.8, 4.4), (-8.45, 4.75), (-7.36, 4.75), (-6.96, 5.15)]),
    ('GND', 'F', 0.15, [(-9.2, 4.4), (-9.85, 5.05)]), ('GND', V, None, [(-9.85, 5.05)]),
    # ---- haptic driver (A0): HAPTIC_EN through its pull-down; both GND pins to one via under the body ------
    ('HAPTIC_EN', 'B', 0.2, M((-13.5, 13.4), (-12.75, 13.4), (-12.4, 13.75), (-12.4, 14.34))),
    ('GND', 'B', 0.25, M((-17.7, 12.4), (-15.85, 12.4), (-15.35, 12.9), (-13.5, 12.9))),
    ('GND', 'B', 0.25, M((-16.2, 12.4)) + [(-16.2, 13.55), (-16.45, 13.8)]), ('GND', V, 0.6, [(-16.45, 13.8)]),
    ('GND', 'B', 0.3, M((-16.52, 10.0), (-15.75, 10.0))), ('GND', V, 0.6, M((-15.75, 10.0))),    # C503 return
    # ---- service field (A0): pull-ups face their probe pads, +3V3 ends join TP3; the trunk drops to L3 here ----
    ('I2C_SCL', 'B', 0.2, [(6.0, -3.3), (7.6, -3.3), (8.11, -2.79), (8.2, -2.79)]),
    ('I2C_SCL', 'B', 0.2, [(6.0, -3.3), (6.0, -4.1), (5.5, -4.6)]), ('I2C_SCL', V, 0.5, [(5.5, -4.6)]),
    ('I2C_SDA', 'B', 0.2, [(6.0, -0.5), (6.9, -0.5), (7.4, -1.0), (7.4, -1.6), (8.25, -1.6), (9.44, -2.79)]),
    ('I2C_SDA', V, 0.5, [(8.2, -1.6)]),
    ('+3V3', 'B', 0.3, [(8.2, -3.81), (8.2, -4.3), (9.44, -4.3), (9.44, -3.81)]),
    ('+3V3', 'B', 0.3, [(8.8, -4.3), (8.8, -6.1)]), ('+3V3', V, 0.6, [(8.8, -4.85)]),
    # ---- IMU U401 on F above the module's top row (A1: ICM-42670-P; VDDIO pin 5 -> C402 10 nF, VDD pin 8 ->
    # C401 100 nF, one +3V3 via at each cap; GND pins 6/7 and both cap returns meet one via between the caps;
    # pin 1 (AP_AD0, 0x68) to its own GND via)
    ('+3V3', 'F', 0.2, [(2.7, 7.912), (2.7, 8.45), (1.72, 8.45), (1.72, 9.25)]),
    ('+3V3', 'F', 0.3, [(1.72, 9.25), (0.82, 9.25)]), ('+3V3', V, 0.6, [(0.82, 9.25)]),
    ('+3V3', 'F', 0.2, [(4.363, 7.75), (5.05, 7.75), (5.323, 8.023), (5.323, 9.25)]),
    ('+3V3', 'F', 0.3, [(5.323, 9.25), (5.323, 10.1)]), ('+3V3', V, 0.6, [(5.323, 10.1)]),
    ('GND', 'F', 0.25, [(3.2, 7.912), (3.2, 8.5), (3.7, 8.5), (3.7, 7.912)]),
    ('GND', 'F', 0.3, [(3.52, 8.5), (3.52, 9.25)]),
    ('GND', 'F', 0.3, [(2.68, 9.25), (4.363, 9.25)]), ('GND', V, 0.6, [(3.52, 9.25)]),
    # ---- module 3V3 corner (B): pad 3 -> C202 -> C201 -> one plane via; GND pad 2 to C202's ground ------------
    ('+3V3', 'B', 0.5, [(-7.0, 19.55), (-10.1, 19.55)]),
    ('+3V3', 'B', 0.4, [(-10.1, 19.55), (-11.1, 19.55)]), ('+3V3', V, None, [(-11.1, 19.55)]),
    ('GND', 'B', 0.3, [(-7.0, 20.4), (-8.6, 20.4)]),
]

if __name__ == '__main__':
    run('local', [], ADD)
