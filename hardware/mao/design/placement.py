"""MAO_MAIN A1 placement, 4-layer board: ref -> (x, y, rotation, side) in board mm (origin = puck axis,
y towards 6 o'clock, viewed from the face). Rotation is KiCad's: degrees counter-clockwise on screen,
applied after the flip for B.Cu parts. Every position is explicit; support parts are pinned against the
pin they serve (pin() below, pad positions from the real footprints through geo.py).

A1 keeps A0's puck: every part that A1 did not change stays where A0 put it (USB-C, IR, display connector,
amplifier and speaker, haptic driver and LRA pads, window sensors, Hall pair, face switch, battery chain,
fasteners, fiducials). What moved:
  B, 6 o'clock      ESP32-S3-MINI-1 (15.4 x 20.5) in the WROOM's place, antenna end at y 27.6 over a
                    narrower notch; EN RC at pin 45, the 3V3 caps and the BOOT pull-up at pins 1-4, the USB
                    22R at pins 23/24
  F, 1-2 o'clock    BQ25185 in the BQ24073's place under the panel (its heat stays off the cell, ODD JOBS 78),
                    BAT and SYS towards the battery chain, IN and the programming pins towards the board centre
  B, 1 o'clock      TPS62840 in the TPS63802's place, below the charger: SYS -> LINK_REG -> CIN -> VIN east,
                    SW -> inductor -> COUT west, +3V3 into the L3 plane (TI SLVSEC6D 11.2)
  F, 9 o'clock      panel rail switch (TPS22916C) over J301's VCI pad
  B, 9-10 o'clock   backlight current sink (TLV9061, DMG2302UK, 3.3R) at J301's VLED- pad, where the AW9364 was
  F, 3 o'clock      IR receiver rail switch (TPS22916C) beside the receiver's filter
  B, centre         a second service-field block for the Gate C fixture pads, between the first and the module
Removed with their parts: the expander, the microphone, the light sensor, the touch electrodes and springs.
"""
import mechanical as m

P = {}
# F.Cu part keep-out: the display tail drops past the slot at 9 o'clock.
KEEP_F = [m.TAIL_F_CLEAR, m.TAIL_WELL]
# B.Cu part keep-outs: the tail corridor between the slot and J301 (no part at all).
KEEP_B = [m.TAIL_CORRIDOR]
BUS_PARTS = ()


def rim(r, a, side='F', extra=0.0):
    """Part on a circle, its x axis tangential."""
    x, y = m.polar(r, a)
    return (round(x, 3), round(y, 3), round((-a + extra) % 360, 3), side)


GAP = 0.2            # courtyard to courtyard on one face: a 0.2 mm track can never be squeezed out


def _clear(ref, place):
    """The part's courtyard is free of every placed part on its face, of the edge and the keep-outs."""
    import math
    import geo
    boxes = geo.courtyard(ref, place)
    big = ref == 'U201'                  # the module's courtyard includes Espressif's antenna keep-out: off the board
    for b in boxes:
        for x, y in ((b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3])):
            if big:
                break
            if math.hypot(x, y) > m.PCB_R - 0.45:
                return False
            if ref.startswith('C') and math.hypot(x, y) > m.PCB_R - m.MLCC_EDGE + 0.25:
                return False             # MLCC pads >= 1.5 mm from the milled edge (courtyard ~0.25 beyond the pad)
            if abs(x) < m.NOTCH_W / 2 + m.ANTENNA_COPPER_SETBACK and y > m.NOTCH_Y - 0.6:
                return False
        for a in m.SCREW_ANGLES + (m.PEG_ANGLE,):
            cx, cy = m.polar(m.MOUNT_R, a)
            r = ((m.PEG_KEEPOUT_D if place[3] == 'F' else m.PEG_KEEPOUT_D_B) if a == m.PEG_ANGLE else
                 m.BOSS_KEEPOUT_D_F if place[3] == 'F' else m.HEAD_KEEPOUT_D_B) / 2
            nx, ny = min(max(cx, b[0]), b[2]), min(max(cy, b[1]), b[3])
            if math.hypot(nx - cx, ny - cy) < r:
                return False
        for k in (KEEP_F if place[3] == 'F' else KEEP_B if ref not in BUS_PARTS else []):
            if b[0] < k[2] and k[0] < b[2] and b[1] < k[3] and k[1] < b[3]:
                return False
    for other, op in P.items():          # drilled pads of parts on either face (copper or bare hole + 0.3 mm)
        if other == ref or op is None or other.startswith('H'):
            continue
        for hx, hy, hr in geo.holes(other, op):
            for b in boxes:
                if b[0] < hx + hr + 0.3 and hx - hr - 0.3 < b[2] and b[1] < hy + hr + 0.3 and hy - hr - 0.3 < b[3]:
                    return False
    for other, op in P.items():
        if other == ref or op is None or other.startswith('H') or op[3] != place[3]:
            continue
        for b in boxes:
            for o in geo.courtyard(other, op):
                if b[0] < o[2] + GAP and o[0] < b[2] + GAP and b[1] < o[3] + GAP and o[1] < b[3] + GAP:
                    return False
    return True


def pin(ref, rot, side, own, owner, owner_pin, dx, dy, reach=3.0):
    """Support part against the pin it serves: its pad `own` aims at (dx, dy) from pad `owner_pin` of an
    already placed part; the nearest free spot (0.1 mm steps, within `reach` mm) is taken."""
    import geo
    ox, oy = geo.pad(owner, owner_pin, P[owner])
    px, py = geo.pad(ref, own, (0.0, 0.0, rot, side))
    tx, ty = ox + dx - px, oy + dy - py
    steps = int(reach / 0.1)
    cands = sorted(((i * 0.1, j * 0.1) for i in range(-steps, steps + 1) for j in range(-steps, steps + 1)
                    if (i * i + j * j) <= steps * steps), key=lambda d: d[0] ** 2 + d[1] ** 2)
    for ddx, ddy in cands:
        place = (round(tx + ddx, 3), round(ty + ddy, 3), rot, side)
        if _clear(ref, place):
            P[ref] = place
            return
    P[ref] = (round(tx, 3), round(ty, 3), rot, side)
    print('placement: no free spot for %s near %s.%s' % (ref, owner, owner_pin))


def at(ref, x, y, rot, side):
    P[ref] = (x, y, rot, side)


# ==== anchors: module, USB-C, mechanics ==============================================================
at('U201', 0.0, m.MODULE_CY, 0, 'B')
at('J101', 0.0, m.USB_FRONT_Y + 3.65, 0, 'B')      # B.Cu flip mirrors y: mating face towards 12 o'clock
at('SW301', m.PRESS_XY[0], m.PRESS_XY[1], 0, 'F')
for ref, a in (('H1', m.SCREW_ANGLES[0]), ('H2', m.SCREW_ANGLES[1]), ('H3', m.PEG_ANGLE)):
    x, y = m.polar(m.MOUNT_R, a)
    at(ref, round(x, 3), round(y, 3), 0, 'F')

# ==== USB entry (B, A0 places) ==========================================================================
at('D101', 4.65, -19.5, 0, 'B')           # VBUS TVS on the way from the receptacle
at('R101', -1.6, -19.5, 270, 'B')         # CC1 Rd
at('R102', 1.9, -17.25, 270, 'B')         # CC2 Rd
at('U101', 0.0, -18.0, 270, 'F')          # TPD2E2U06 on the D+/D- vias: the pair runs on F over L2 GND
at('TP6', 5.5, -16.95, 0, 'B')            # VBUS probe at the TVS
at('R120', 7.4, -16.6, 90, 'B')           # VBUS_SENSE divider beside the probe: VBUS end north
at('R121', 8.8, -16.6, 90, 'B')

# ==== battery chain (B, A0 places): J102 -> LINK_BAT R108 -> reverse-polarity Q101 -> VBAT ==============
UX, UY = 3.0, -11.0
at('Q101', UX + 12.1, UY + 0.175, 0, 'B')
at('R108', UX + 15.26, UY + 1.635, 90, 'B')
at('J102', UX + 16.26, UY + 7.285, 180, 'B')
at('U103', 13.7, -14.55, 0, 'B')          # gauge: clear of the H2 screw head
at('C104', 11.3, -15.05, 90, 'B')         # gauge VDD beside its VBAT pins
at('R109', UX + 11.25, UY + 3.0, 0, 'B')  # gate pull-down, flat between the FET and the BAT probe pad
at('R110', UX + 17.75, UY + 3.25, 0, 'B')

# ==== charger (F, under the panel, in the BQ24073's place) ===========================================
# Turned 180: SYS (pin 1) and BAT (pin 2) face the battery chain to the east, IN (10) and the programming pins
# (ISET, ILIM, TS) face the board centre. VBAT and VSYS change faces through vias beside the part.
at('U102', 9.41, -11.775, 180, 'F')
# The 0.4 mm-pitch pins fan out on designed copper (route_power.py): SYS and IN straight south into their caps,
# BAT east then south-east to its cap and two vias, STAT2 and /CE east, ISET / ILIM / STAT1 west, NTC north.
at('C103', 13.2, -10.5, 270, 'F')                          # BAT 1 uF, VBAT end north
at('C102', 10.51, -8.75, 270, 'F')                         # SYS 10 uF 25 V straight below pin 1
at('C101', 8.31, -9.3, 270, 'F')                           # IN 1 uF straight below pin 10
at('R103', 6.55, -11.775, 180, 'F')                        # ISET 1.43k level with pin 8
at('R104', 6.55, -12.975, 180, 'F')                        # ILIM 18k
at('R117', 11.9, -13.6, 90, 'F')                           # /CE pull-down north-east of pin 4
at('R119', 14.9, -11.775, 180, 'F')                        # STAT2 pull-up level with pin 3
at('R118', 6.3, -9.6, 0, 'F')                              # STAT1 pull-up

# ==== 3.2 V buck (B, below the charger), TI SLVSEC6D 11.2: VIN/GND pair with CIN, SW to the inductor, COUT ===
# Turned 180: VIN/EN (REG_IN) east towards LINK_REG and the charger's SYS, SW and VOS west to L101 and COUT.
at('U104', 5.5, -11.0, 180, 'B')
pin('C105', 90, 'B', '1', 'U104', '2', 1.3, 0.25)         # CIN 4.7 uF across VIN and GND (pins 2, 1)
pin('L101', 180, 'B', '1', 'U104', '7', -1.5, 0.0)        # SW -> inductor
at('C106', 1.9, -14.05, 90, 'B')                          # COUT 10 uF straight below the inductor's +3V3 end
at('R111', 3.15, -9.4, 180, 'B')                          # VSET 102k (no capacitance on VSET)
at('R122', 9.45, -10.2, 90, 'B')                          # LINK_REG: VSYS (south) -> REG_IN (north) beside CIN

# ==== IR transmit: 12 o'clock edge (B, A0 places) =====================================================
P['D501'] = rim(27.7, m.IR_TX_ANGLES[0], 'B', extra=180)
P['D502'] = rim(27.7, m.IR_TX_ANGLES[1], 'B', extra=180)
at('Q501', 9.6, -21.2, 90, 'B')
pin('R502', 90, 'B', '2', 'D502', '2', 0.0, 1.5)
pin('R501', 90, 'B', '2', 'D501', '2', 0.0, 1.5)
pin('R503', 0, 'B', '2', 'Q501', '1', 0.0, 1.3)
pin('R504', 90, 'B', '1', 'Q501', '1', 1.3, 0.0)
at('C505', 14.4, -18.9, 0, 'B')

# ==== display: J301 on B at the tail slot (A0 place), rail switch on F over its VCI pad =================
at('J301', round(m.TAIL_ENTRY_X + 2.85, 3), 0.0, 90, 'B')
pin('R301', 0, 'B', '1', 'U201', '16', 0.0, -1.9)         # 22R on SCLK and MOSI at the module pins
pin('R302', 90, 'B', '1', 'U201', '15', -1.6, 0.0)
at('U105', -9.0, 4.2, 0, 'F')                              # TPS22916C: VOUT drops to J301 pad 3 (VCI)
at('C109', -11.5, 4.75, 180, 'F')                          # VOUT 1 uF, its 3V3_LCD end towards the switch
at('C110', -6.45, 3.85, 0, 'F')                              # VIN 1 uF
at('R116', -6.45, 5.15, 0, 'F')                             # ON pull-down
at('C301', -11.3, 2.0, 0, 'F')                             # panel VCI HF beside the VCI via
# Backlight sink on B at J301's VLED- (pad 11) and VLED+ (pad 12), in the AW9364's place
at('Q302', -8.2, -1.7, 180, 'B')                          # drain west to VLED- (J301 pad 11)
at('R317', -4.4, -0.7, 0, 'B')                             # Rs 3.3R: BL_SENSE west, GND east
at('R314', -5.0, -2.2, 0, 'B')                             # feedback isolation: BL_SENSE west, BL_FB east
at('C307', -5.0, -3.5, 0, 'B')                             # DNP compensation: BL_DRIVE west, BL_FB east
at('R316', -7.0, -4.6, 90, 'B')                            # gate pull-down under the gate
at('R315', -8.5, -4.6, 90, 'B')                            # gate resistor from the op-amp output
at('U304', -7.6, -7.6, 0, 'B')                             # OUT/GND/IN+ west, V+/IN- east
at('C306', -4.8, -6.6, 90, 'B')                            # op-amp supply at V+
at('R313', -8.4, -10.1, 0, 'B')                            # reference divider bottom (BL_REF west of IN+)
at('R312', -6.3, -10.1, 0, 'B')                            # reference divider top
at('R311', -6.3, -11.4, 0, 'B')                            # LCD_BL pull-down
at('C305', -11.3, -0.9, 0, 'F')                            # VLED+ bypass on F over J301 pad 12's via
at('TP13', -3.0, -10.3, 0, 'B')                             # LCD rail probe

# ==== module support (B) ===============================================================================
pin('C202', 270, 'B', '1', 'U201', '3', -1.6, 0.0)         # 3V3 HF at pad 3, GND end at pin 2
pin('C201', 270, 'B', '1', 'U201', '3', -3.1, 0.0)         # 3V3 bulk beside it
pin('R202', 0, 'B', '2', 'U201', '4', -1.6, -0.6)          # BOOT pull-up at pin 4
pin('R201', 90, 'B', '2', 'U201', '45', 1.6, 0.0)          # EN pull-up at pin 45
pin('C203', 90, 'B', '1', 'U201', '45', 2.9, 0.0)          # EN delay
at('R216', -0.5, 6.0, 270, 'B')                           # USB 22R at pins 23 (D-) and 24 (D+), module end south
at('R215', 1.6, 6.0, 270, 'B')
at('C207', -1.75, 6.0, 90, 'B')                          # DNP 10 pF on the module side
at('C206', 2.9, 6.0, 90, 'B')

# ==== audio: amplifier beside the speaker, 9 o'clock (B, A0 places) =====================================
at('U501', -14.4, -8.7, 0, 'B')
at('LS501', m.SPEAKER_CENTRE[0], m.SPEAKER_CENTRE[1], 0, 'B')
at('C502', -14.8, -11.6, 90, 'F')          # 100 nF over the VDD via
at('C501', -15.1, -13.4, 0, 'F')           # 10 uF beside it
at('R208', -15.95, -9.96, 90, 'F')         # SD_MODE pull-down on F, beside the amplifier's pin 4 via

# ==== haptic: driver at the LRA, 7-8 o'clock (B, A0 places) ============================================
DYM = m.MODULE_SHIFT
at('U502', -15.6, round(12.4 + DYM, 3), 180, 'B')
P['J501'] = rim(23.6, 242.0, 'B')[:2] + (90, 'B')
at('C503', -17.0, round(10.0 + DYM, 3), 0, 'B')           # VDD (now +3V3) above pin 10
at('C504', -14.4, round(9.4 + DYM, 3), 90, 'B')           # REG
at('R209', -12.4, round(14.85 + DYM, 3), 270, 'B')        # EN pull-down below pin 5

# ==== IMU on F above the module, near the axis (A0 place) ==============================================
at('U401', 3.2, 7.0, 0, 'F')
at('C402', 2.2, 9.25, 0, 'F')              # VDDIO 10 nF under pin 5
at('C401', 4.843, 9.25, 180, 'F')          # VDD 100 nF under pin 8
at('C408', 6.7, 7.0, 0, 'F')                                # VDD 2.2 uF X7R
at('R404', 0.3, 7.75, 0, 'F')                              # INT1 pull-up level with pin 4

# ==== Tag-Connect at the module's UART pins (B) ========================================================
at('J201', 14.4, 16.6, 90, 'B')

# ==== F: window-border sensors, Hall pair (A0 places) ==================================================
P['U402'] = rim(m.TOF_R, m.TOF_ANGLE, 'F')
_x, _y, _r, _s = rim(m.IR_RX_R, m.IR_RX_ANGLE, 'F', extra=90)
P['U503'] = (_x, _y, 0, 'F')
P['U301'] = rim(m.HALL_R, m.HALL_ANGLES[0], 'F')
P['U302'] = rim(m.HALL_R, m.HALL_ANGLES[1], 'F')
pin('C403', 90, 'F', '1', 'U402', '11', -1.5, -0.2)
pin('C404', 90, 'F', '1', 'U402', '1', -1.6, -1.0)
at('R401', -5.81, -18.5, 180, 'F')         # TOF_INT pull-up square to pin 7
pin('R210', 90, 'F', '1', 'U402', '5', 1.2, -0.6)
at('C506', 23.6, -3.9, 0, 'F')             # IR receiver VCC cap beside pin 4
at('R506', 23.6, 3.6, 0, 'F')              # OUT pull-up beside pin 3
at('R505', 21.96, 6.75, 0, 'F')            # RC filter from the switched rail (AUX_3V3 west, towards U504)
at('U504', 19.4, 4.6, 180, 'F')            # TPS22916C for the receiver rail: VOUT towards R505
at('C508', 17.3, 4.6, 90, 'F')             # its VIN 1 uF, west of its courtyard
at('R211', 16.05, 4.0, 270, 'F')            # AUX_PWR_EN pull-down west of the switch's VIN cap
P['C303'] = rim(24.0, 117.0, 'F')[:2] + (0, 'F')
P['C304'] = rim(24.0, 124.0, 'F')[:2] + (0, 'F')
P['R306'] = rim(24.0, 130.0, 'F')[:2] + (0, 'F')
pin('R318', 90, 'F', '2', 'SW301', '1', 0.0, -1.6)         # PRESS_N pull-up above the switch
pin('C308', 90, 'F', '1', 'SW301', '1', -1.1, -1.6)        # DNP RC

# ==== pull-downs at the parts they hold off =============================================================
pin('R207', 0, 'B', '1', 'J301', '4', 1.6, 0.9, reach=4.0)   # display reset, at J301's RESET pad

# ==== service field (B): A0's block below the power section, the Gate C fixture block below it ==========
_field = {'TP2': (6.0, -6.1), 'TP3': (8.8, -6.1), 'TP4': (11.6, -6.1), 'TP5': (14.3, -6.1),
          'TP10': (6.0, -3.3), 'TP1': (11.6, -3.3), 'TP16': (14.3, -3.3),
          'TP9': (6.0, -0.5), 'TP8': (11.6, -0.5), 'TP7': (14.3, -0.5),
          'TP17': (3.2, 2.3), 'TP18': (6.0, 2.3), 'TP19': (8.8, 2.3), 'TP20': (11.6, 2.3), 'TP21': (14.3, 2.3),
          'TP22': (6.0, 5.1), 'TP23': (8.8, 5.1), 'TP24': (11.6, 5.1), 'TP25': (14.3, 5.1)}
for _ref, (_x, _y) in _field.items():
    at(_ref, _x, _y, 0, 'B')
at('R214', 8.2, -3.3, 270, 'B')           # I2C pull-ups in the field's free position
at('R213', 9.44, -3.3, 270, 'B')
pin('TP15', 0, 'B', '1', 'U503', '4', -0.6, 2.0)      # IR receiver supply, under the receiver

# ==== fiducials ========================================================================================
at('FID1', 15.6, -20.2, 0, 'F')
at('FID2', 5.5, 16.0, 0, 'F')
at('FID3', -20.0, -9.0, 0, 'F')
at('FID4', -22.7, -13.9, 0, 'B')
at('FID5', 21.0, -13.0, 0, 'B')
at('FID6', 19.0, 18.2, 0, 'B')

P = {k: v for k, v in P.items() if v is not None}

# No auto-placed parts: every support part is pinned above (brief: passives at their pins).
AUTO = []


PLACE = P
