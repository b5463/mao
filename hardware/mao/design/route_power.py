"""Designed copper of the MAO_MAIN A1 power section (KiCad python): <KiCad python> route_power.py add

Every item is drawn from the pad coordinates of the placement in placement.py and locked, so the routers
work around it. ODD JOBS 7-12, 16, 85-88. What A1 kept from A0 keeps A0's copper (USB-C VBUS entry, the
battery chain J102 -> LINK_BAT -> Q101, the amplifier's supply via, speaker, LRA and IR LED lines); the
BQ25185 / TPS62840 section is new:
  VSYS   charger SYS (F) -> C102 -> two vias (x 11.8) -> LINK_REG R122 (B) and the L3 VSYS band; the SYS probe
         TP4 from the same vias; one via at every other load (amplifier, IR LED anodes and their reservoir,
         J301 VLED+ with its F bypass C305)
  REG    R122 -> CIN C105 -> VIN/EN (pins 2, 4); PGND pin 1 to CIN's ground (TI SLVSEC6D 11.2: the input loop
         VIN - CIN - GND as small as possible)
  SW     pin 7 straight to the inductor
  +3V3   inductor -> COUT C106 straight down, VOS (pin 8) sensed at COUT, two vias into the L3 +3V3 region
  VSET   pin 5 to R111, short, nothing else on it (< 100 pF)
VBUS on F, VBAT between the faces, the charger's programming and status pins and the NTC are left to the
router (netrules widths), which sees this copper as fixed.
"""
from handroute import run

V = 'V'
import mechanical as _m


def M(*pts):
    """A0 copper drawn at the WROOM's pre-audit position (parts that did not move: haptic driver, LRA)."""
    return [(x, round(y + _m.MODULE_SHIFT, 3)) for x, y in pts]


ADD = [
    # ---- VBUS (A0) -------------------------------------------------------------------------------------
    ('VBUS', 'B', 0.5, [(2.45, -21.6), (2.45, -20.4), (3.2, -19.65)]),                # A9/B4 -> D101
    ('VBUS', 'B', 0.5, [(-2.45, -21.6), (-2.45, -20.55)]),                            # A4/B9 -> L3 strip
    ('VBUS', V, None, [(-2.45, -20.55)]),
    ('VBUS', 'B', 0.5, [(3.2, -19.5), (3.2, -18.2)]),
    ('VBUS', V, None, [(3.2, -18.2)]),                                               # strip end, B -> F
    ('VBUS', 'B', 0.4, [(3.2, -18.2), (4.45, -16.95), (5.5, -16.95)]),               # probe TP6
    ('VBUS', 'B', 0.25, [(5.5, -16.95), (6.36, -16.09), (7.4, -16.09)]),             # VBUS_SENSE divider top
    # ---- battery chain (A0) -------------------------------------------------------------------------------
    ('BAT_RAW', 'B', 0.6, [(18.26, -7.903), (18.26, -5.715)]),                        # J102.3 -> LINK_BAT R108
    ('BAT_IN', 'B', 0.8, [(16.037, -10.825), (18.26, -10.825)]),                      # R108 -> Q101 drain
    ('BAT_RPP_G', 'B', 0.3, [(14.16, -9.875), (14.16, -8.84), (13.74, -8.42), (13.74, -8.0)]),   # gate -> R109
    # ---- charger U102 (F, WSON-10, 0.4 mm pitch): every pin to its part on designed copper ------------------
    # SYS (pin 1) south into C102, then two vias (x 11.6) to LINK_REG R122 (B), the SYS probe TP4 and the L3 band
    ('VSYS', 'F', 0.25, [(10.51, -10.975), (10.51, -10.6)]),
    ('VSYS', 'F', 0.5, [(10.51, -10.6), (10.51, -9.525)]),
    ('VSYS', 'F', 0.6, [(10.51, -9.525), (11.6, -9.525)]),
    ('VSYS', 'F', 0.4, [(11.6, -9.525), (11.6, -8.725)]),
    ('VSYS', V, None, [(11.6, -9.525)]), ('VSYS', V, None, [(11.6, -8.725)]),
    ('VSYS', 'B', 0.6, [(11.6, -9.525), (9.45, -9.525)]),                            # -> R122 (LINK_REG)
    ('VSYS', 'B', 0.4, [(11.6, -9.525), (11.6, -8.725)]),
    ('VSYS', 'B', 0.3, [(11.6, -8.725), (11.6, -6.1)]),                              # SYS probe TP4
    # BAT (pin 2) east, then south-east into C103 and two vias; on B to Q101's source, the gauge and TP5
    ('VBAT', 'F', 0.2, [(10.51, -11.375), (11.1, -11.375)]),
    ('VBAT', 'F', 0.3, [(11.1, -11.375), (12.0, -11.375), (12.4, -10.975), (13.2, -10.975)]),
    ('VBAT', 'F', 0.3, [(12.4, -10.975), (12.4, -9.4)]),
    ('VBAT', V, None, [(12.4, -10.2)]), ('VBAT', V, None, [(12.4, -9.4)]),
    ('VBAT', 'B', 0.4, [(12.4, -10.2), (12.4, -9.4)]),
    ('VBAT', 'B', 0.6, [(12.4, -10.2), (12.4, -10.6), (13.575, -11.775), (14.162, -11.775)]),   # -> Q101 source
    ('VBAT', 'B', 0.3, [(12.4, -10.6), (11.95, -11.05), (11.95, -14.55), (12.712, -14.55)]),    # gauge pins 2/3
    ('VBAT', 'B', 0.3, [(11.95, -14.55), (11.3, -14.55)]),                                      # its VDD cap C104
    ('VBAT', 'B', 0.3, [(12.4, -9.4), (12.4, -7.0), (13.3, -6.1), (14.3, -6.1)]),              # BAT probe TP5
    # STAT2 (pin 3) and /CE (pin 4) east to their resistors; IN (pin 10) south into C101; ISET (8), ILIM (7)
    # and STAT1 (9) west; TS/MR (6) north
    ('CHG_STAT2', 'F', 0.15, [(10.51, -11.775), (14.39, -11.775)]),
    ('CHG_CE_N', 'F', 0.15, [(10.51, -12.175), (11.4, -12.175), (11.9, -12.675), (11.9, -13.09)]),
    ('VBUS', 'F', 0.3, [(8.31, -10.975), (8.31, -9.78)]),
    ('CHG_ISET', 'F', 0.2, [(8.31, -11.775), (7.06, -11.775)]),
    ('CHG_ILIM', 'F', 0.2, [(8.31, -12.175), (7.86, -12.175), (7.06, -12.975)]),
    ('CHG_STAT1', 'F', 0.15, [(8.31, -11.375), (7.715, -11.375), (7.0, -10.66), (7.0, -9.79), (6.81, -9.6)]),
    ('BAT_NTC', 'F', 0.2, [(8.31, -12.575), (8.31, -14.0)]), ('BAT_NTC', V, 0.5, [(8.31, -14.0)]),
    # IN (pin 10) -> C101, then east round C101's ground to one via and on L3 into the VBUS strip at the receptacle
    ('VBUS', 'F', 0.3, [(8.31, -9.78), (9.0, -9.78), (9.0, -7.9)]), ('VBUS', V, None, [(9.0, -7.9)]),
    ('VBUS', 'I2', 0.4, [(9.0, -7.9), (9.0, -12.0), (5.0, -16.0), (5.0, -17.3)]),
    # STAT1: from its pull-up on F through one via; the router takes it to the module
    ('CHG_STAT1', 'F', 0.15, [(6.81, -9.6), (6.81, -8.7)]), ('CHG_STAT1', V, 0.5, [(6.81, -8.7)]),
    # ---- 3.2 V buck U104 (B) ------------------------------------------------------------------------------
    ('REG_IN', 'B', 0.5, [(9.45, -11.0), (7.75, -11.0)]),                            # R122 -> CIN C105
    ('REG_IN', 'B', 0.3, [(7.75, -11.0), (6.75, -11.0)]),
    ('REG_IN', 'B', 0.25, [(6.75, -10.25), (6.75, -11.25)]),
    ('REG_IN', 'B', 0.25, [(6.15, -11.25), (6.75, -11.25)]),                         # VIN (pin 2)
    ('REG_IN', 'B', 0.25, [(6.15, -10.25), (6.75, -10.25)]),                         # EN (pin 4) = VIN
    ('GND', 'B', 0.3, [(6.15, -11.75), (6.95, -12.55), (7.75, -12.55)]),             # GND (pin 1) -> CIN ground
    ('GND', 'B', 0.2, [(6.15, -10.75), (5.5, -10.75), (5.5, -11.75), (6.15, -11.75)]),   # MODE (3) and STOP (6)
    ('GND', 'B', 0.2, [(4.85, -10.75), (5.5, -10.75)]),                              # to GND (1) under the body
    ('REG_SW', 'B', 0.4, [(4.85, -11.25), (3.35, -11.25)]),                          # SW -> L101
    ('+3V3', 'B', 0.5, [(1.9, -11.25), (1.9, -13.275)]),                             # L101 -> COUT C106
    ('+3V3', 'B', 0.25, [(4.85, -11.75), (4.35, -11.75), (3.15, -12.95), (2.2, -12.95)]),   # VOS at COUT
    ('+3V3', 'B', 0.4, [(1.9, -13.275), (0.9, -13.275)]), ('+3V3', V, None, [(0.9, -13.275)]),
    ('+3V3', 'B', 0.4, [(1.9, -12.2), (0.9, -12.2)]), ('+3V3', V, None, [(0.9, -12.2)]),
    ('REG_VSET', 'B', 0.2, [(4.85, -10.25), (4.4, -10.25), (3.66, -9.51)]),         # VSET -> R111
    # ---- VSYS loads (A0 vias where the parts did not move) ------------------------------------------------------
    # amplifier: VDD pins 7/8 -> one via below the part into the L3 VSYS branch -> C502, C501 on F
    ('VSYS', 'B', 0.3, [(-14.15, -10.137), (-14.15, -10.6), (-13.9, -10.85)]),
    ('VSYS', 'B', 0.3, [(-13.65, -10.137), (-13.65, -10.6), (-13.9, -10.85)]),
    ('VSYS', V, 0.6, [(-13.9, -10.85)]),
    ('VSYS', 'F', 0.3, [(-13.9, -10.85), (-14.53, -10.85), (-14.8, -11.12)]),          # C502 (100 nF)
    ('VSYS', 'F', 0.3, [(-14.8, -11.12), (-15.6, -11.12), (-15.87, -11.39), (-15.87, -13.4)]),    # C501 (10 uF)
    # IR LED anodes and their reservoir
    ('VSYS', V, None, [(-12.45, -22.1)]),
    ('VSYS', V, None, [(6.627, -22.3)]),
    ('VSYS', V, None, [(12.0, -19.3)]),
    ('VSYS', 'B', 0.4, [(12.0, -19.3), (12.4, -18.9), (13.625, -18.9)]),             # reservoir C505
    # backlight: J301 VLED+ (pad 12) -> one via under the connector in the L3 VSYS bar -> C305 on F
    ('VSYS', 'B', 0.3, [(-11.15, -1.25), (-12.3, -1.25)]), ('VSYS', V, None, [(-12.3, -1.25)]),
    ('VSYS', 'F', 0.3, [(-12.3, -1.25), (-11.95, -0.9), (-11.78, -0.9)]),
    # ---- speaker (B, A0): both outputs leave the amplifier's east pins and loop under it; SPK+ climbs the tail
    # corridor's west edge to the 6 o'clock pad, SPK- turns up into the 12 o'clock pad ------------------------
    ('SPK_P', 'B', 0.3, [(-12.963, -9.45), (-12.3, -9.45), (-12.3, -11.15), (-12.7, -11.55)]),
    ('SPK_P', 'B', 0.4, [(-12.7, -11.55), (-17.3, -11.55), (-17.7, -11.15), (-17.7, 6.2), (-18.1, 6.6), (-19.9, 6.6)]),
    ('SPK_N', 'B', 0.3, [(-12.963, -8.95), (-11.8, -8.95), (-11.8, -11.75), (-12.2, -12.15)]),
    ('SPK_N', 'B', 0.4, [(-12.2, -12.15), (-18.45, -12.15), (-18.85, -11.75), (-18.85, -7.25), (-19.9, -6.2)]),
    # ---- LRA (A0) ----------------------------------------------------------------------------------------
    ('LRA_P', 'B', 0.3, M((-18.2, 12.9)) + [(-19.35, 11.65), (-20.0, 12.3)]),                    # pin 7 -> J501.1
    ('LRA_N', 'B', 0.3, M((-18.2, 11.9)) + [(-19.5, 10.65), (-20.15, 10.0), (-20.8, 10.0)]),
    # ---- IR LED cathodes (A0): the switch drain to D502 on B, to D501 by an F bar between the USB-C shell legs
    ('IR_LED_K', 'B', 0.4, [(9.6, -22.6), (9.6, -23.6), (10.9, -24.9)]),
    ('IR_LED_K', V, None, [(9.6, -23.6)]),
    ('IR_LED_K', 'F', 0.4, [(9.6, -23.6), (8.6, -24.6), (-7.0, -24.6), (-7.6, -25.2)]),
    ('IR_LED_K', V, None, [(-7.6, -25.2)]),
    ('IR_LED_K', 'B', 0.4, [(-7.6, -25.2), (-8.4, -26.0)]),
    # ---- ground (A0): TVS both pad ends, USB-C ground pins to the shell legs, battery minus ----------------------
    ('GND', 'B', 0.5, [(6.1, -19.5), (6.1, -18.25)]), ('GND', V, None, [(6.1, -18.25)]),
    ('GND', 'B', 0.5, [(6.1, -19.5), (6.1, -20.7)]), ('GND', V, None, [(6.1, -20.7)]),
    ('GND', 'B', 0.4, [(-3.25, -22.2), (-4.32, -22.2)]),                             # J101 A1/B12 -> shell leg
    ('GND', 'B', 0.4, [(3.25, -22.2), (4.32, -22.2)]),                                # J101 A12/B1 -> shell leg
    ('GND', 'B', 0.5, [(20.26, -5.0), (20.26, -4.2)]), ('GND', V, None, [(20.26, -4.2)]),   # J102 battery minus
    ('GND', 'B', 0.5, [(20.4, -5.715), (21.2, -5.715)]), ('GND', V, None, [(21.2, -5.715)]),
]

if __name__ == '__main__':
    run('power', [], ADD)
