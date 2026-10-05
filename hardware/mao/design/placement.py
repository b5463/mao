"""MAO_MAIN A0 placement, 4-layer board: ref -> (x, y, rotation, side) in board mm (origin = puck axis,
y towards 6 o'clock, viewed from the face). Rotation is KiCad's: degrees counter-clockwise on screen,
applied after the flip for B.Cu parts. Every position is explicit; support parts are pinned against the
pin they serve (pin() below, pad positions from the real footprints through geo.py).

Zones, viewed from the face (brief: functional zones, radial logic, minimum crossings):
  F (display side)  under the panel only the face switch, the display connector and its two capacitors;
                    in the window band the proximity (11), light (1) and IR receiver (3 o'clock); the
                    Hall pair under the ring (4 o'clock); touch arcs at the rim; the USB ESD
  B, 6 o'clock      ESP32-S3 module, antenna over the board notch
  B, 12 o'clock     USB-C, the IR LEDs either side and their driver
  B, 1-2 o'clock    power section (charger, buck-boost, gauge, battery plug), opposite the antenna
  B, 3-4 o'clock    microphone under its window port; Tag-Connect at the module's UART pins
  B, 9 o'clock      display support under the connector, speaker amplifier at its springs
  B, 7-8 o'clock    haptic driver at the LRA, the REAR touch spring at the module's touch pins
  B, centre         the I/O expander upper left, among the loads it switches; the service field below
                    the power section (the IMU is on F above the module, beside its interrupt pins)
The module's pins are assigned (pinmap.py) so every group leaves towards its destination in order:
left column touch / I2C / I2S / USB, top row display (in the connector's own pin order) and IMU, right
column mic / IR / Hall / UART.
"""
import mechanical as m

P = {}
# F.Cu part keep-out: the display FPC folds from the panel edge (r 17.8 at 9 o'clock) into the J301
# front; no part may sit under the fold.
KEEP_F = [(-20.0, -5.6, -15.2, 5.6)]
# B.Cu part keep-out: the display bus corridor from the module's top row to the vias beside the connector;
# only its own series resistors sit in it.
KEEP_B = [(-11.6, -0.4, 1.3, 2.75)]
BUS_PARTS = ('R301', 'R302')


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
    for b in boxes:
        for x, y in ((b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3])):
            if math.hypot(x, y) > m.PCB_R - 0.45:
                return False
            if abs(x) < m.NOTCH_W / 2 + m.ANTENNA_COPPER_SETBACK and y > m.NOTCH_Y - 0.6:
                return False
        for a in m.SCREW_ANGLES + (m.PEG_ANGLE,):
            cx, cy = m.polar(m.MOUNT_R, a)
            r = m.MOUNT_KEEPOUT_D / 2
            if b[0] < cx + r and cx - r < b[2] and b[1] < cy + r and cy - r < b[3]:
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
        if other == ref or op is None or other.startswith('H') or op[3] != place[3] and not other.startswith('E'):
            continue                     # touch arcs carry copper on both faces
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


# ==== anchors: module, USB-C, mechanics ==============================================================
P['U201'] = (0.0, m.MODULE_CY, 0, 'B')
P['J101'] = (0.0, m.USB_FRONT_Y + 3.65, 0, 'B')      # B.Cu flip mirrors y: mating face towards 12 o'clock
P['SW301'] = (m.PRESS_XY[0], m.PRESS_XY[1], 0, 'F')
for ref, a in (('H1', m.SCREW_ANGLES[0]), ('H2', m.SCREW_ANGLES[1]), ('H3', m.PEG_ANGLE)):
    x, y = m.polar(m.MOUNT_R, a)
    P[ref] = (round(x, 3), round(y, 3), 0, 'F')

# ==== power section: 1-2 o'clock (B), TI reference layouts (ODD JOBS 7-12) ===========================
# One straight chain: battery (J102) -> R108 link -> Q101 reverse-polarity FET -> VBAT -> BQ24073 (U102)
# <- VBUS from the USB-C; VSYS leaves U102 into the TPS63802 (U104) input cap. U104 per SLVSEU9D fig. 12-1:
# inductor over the power pins, CIN at VIN, COUT at VOUT, PGND strip between the pin rows. VSYS and +3V3
# leave the section on L3 (power_regions.py).
UX, UY = 3.0, -11.0
P['U104'] = (UX, UY, 90, 'B')
P['L101'] = (UX, UY - 2.59, 180, 'B')
P['C105'] = (UX + 2.57, UY, 270, 'B')
P['C106'] = (UX - 2.57, UY, 270, 'B')
P['C107'] = (UX - 4.11, UY, 270, 'B')
P['R112'] = (UX + 1.0, UY + 2.5, 270, 'B')
P['R111'] = (UX + 2.1, UY + 2.5, 90, 'B')
P['R114'] = (UX - 0.5, UY + 2.5, 270, 'B')
P['R113'] = (UX - 1.6, UY + 2.5, 90, 'B')
P['U102'] = (UX + 6.41, UY - 0.775, 180, 'B')
P['C102'] = (UX + 2.57, UY - 3.06, 90, 'B')
P['R104'] = (UX + 4.4, UY - 3.96, 90, 'B')
P['C101'] = (UX + 6.14, UY - 3.9, 0, 'B')
P['R103'] = (UX + 9.55, UY - 2.8, 0, 'B')
P['R105'] = (UX + 9.55, UY - 3.84, 0, 'B')
P['C103'] = (UX + 9.35, UY, 270, 'B')
P['Q101'] = (UX + 12.1, UY + 0.175, 0, 'B')
P['R108'] = (UX + 15.26, UY + 1.635, 90, 'B')
P['J102'] = (UX + 16.26, UY + 7.285, 180, 'B')
P['U103'] = (UX + 12.4, UY - 3.2, 0, 'B')
P['C104'] = (UX + 10.27, UY - 5.0, 180, 'B')
P['R109'] = (UX + 11.25, UY + 3.0, 270, 'B')
P['R110'] = (UX + 17.75, UY + 3.25, 0, 'B')
P['R106'] = (9.25, -8.25, 270, 'B')         # PGOOD pull-up under the charger's pin 7
P['R107'] = (-6.15, -15.1, 90, 'B')          # CHG pull-up at the expander's CHG_N via (outside the L3 VSYS band)
P['D101'] = (4.65, -19.5, 0, 'B')           # VBUS TVS on the way from the receptacle
P['R101'] = (-1.6, -19.5, 270, 'B')         # CC1 Rd
P['R102'] = (1.9, -17.25, 270, 'B')         # CC2 Rd
P['U101'] = (0.0, -18.0, 270, 'F')          # TPD2E2U06 on the D+/D- vias: the pair runs on F over L2 GND

# ==== IR transmit: 12 o'clock edge (B) =================================================================
P['D501'] = rim(27.7, m.IR_TX_ANGLES[0], 'B', extra=180)
P['D502'] = rim(27.7, m.IR_TX_ANGLES[1], 'B', extra=180)
P['Q501'] = (9.6, -21.2, 90, 'B')          # low-side switch right of the USB-C, drain up to the cathodes
pin('R502', 90, 'B', '2', 'D502', '2', 0.0, 1.5)      # anode resistors at their LEDs, VSYS end inwards
pin('R501', 90, 'B', '2', 'D501', '2', 0.0, 1.5)
pin('R503', 0, 'B', '2', 'Q501', '1', 0.0, 1.3)       # gate resistor under the gate
pin('R504', 90, 'B', '1', 'Q501', '1', 1.3, 0.0)      # gate pull-down beside it
P['C505'] = (10.4, -16.9, 180, 'B')        # LED reservoir, VSYS end on the L3 VSYS corridor east of the charger input

# ==== display: connector on F under the panel at 9 o'clock, support on B beneath ======================
# The 18-pin tail leaves the panel at 9 o'clock, folds under it and enters from the rim side; pins 1-18
# run from 12 to 6 o'clock, so CS..TE (10-14) meet the module's display group (pins 21..17) in order.
P['J301'] = (-12.0, 0.0, 270, 'F')
pin('C302', 90, 'F', '1', 'J301', '7', 1.6, -1.6)    # panel + backlight bulk above the bus
pin('C301', 90, 'F', '1', 'J301', '16', 1.6, 1.4)    # panel VCI below it
# The display bus runs on B (over the L3 +3V3 region) from the module's top row and rises through one via
# per line beside the connector, so the USB pair can come down on F (over the L2 ground) across it.
pin('R301', 90, 'B', '1', 'U201', '20', 0.0, -1.7)   # 22R on SCLK and MOSI at the module pins
pin('R302', 90, 'B', '1', 'U201', '19', 0.0, -1.7)
P['U105'] = (-13.6, -7.4, 0, 'B')           # display rail switch above the bus, under the connector's upper half
P['R115'] = (-10.95, -6.92, 270, 'B')      # QOD discharge square to pins 5/6
P['C109'] = (-9.7, -6.78, 90, 'B')        # output cap beside it
P['C108'] = (-12.46, -10.05, 90, 'B')       # CT straight out of pin 4
pin('R116', 90, 'B', '1', 'U105', '3', 0.0, -1.3)    # EN pull-down at its pin
P['Q301'] = (-9.6, -3.6, 0, 'B')            # backlight switch above the bus, at the VLED- pin; PWM up on L3
pin('R304', 0, 'B', '2', 'Q301', '1', -1.45, 0.0)    # gate resistor left of the gate, PWM from L3
pin('R305', 90, 'B', '1', 'Q301', '1', -0.6, -1.25)  # gate pull-down
pin('R303', 90, 'B', '2', 'Q301', '3', 0.0, -1.65)   # LED current resistor on the drain

# ==== module support (B) ===============================================================================
# touch series R on F, on their pin rows: pins 4/5/6 rise through one via each and the three electrode
# leads leave as nested lanes on F (route_local.py), clear of the back's haptic and audio corner
for _ref, _pin in (('R307', '4'), ('R309', '5'), ('R310', '6')):
    import geo as _geo
    _x, _y = _geo.pad('U201', _pin, P['U201'])
    P[_ref] = (round(_x - 2.75, 3), round(_y, 3), 180, 'F')
P['R201'] = (-10.45, 19.3, 90, 'B')        # EN pull-up between pins 2 and 3
P['C203'] = (-11.7, 18.3, 90, 'B')        # EN delay: EN end on the EN row, GND end above it
P['C202'] = (-10.75, 21.05, 0, 'B')        # 3V3 HF: GND end at pin 1, 3V3 end under R201
P['C201'] = (-13.4, 20.15, 180, 'B')        # 3V3 bulk at the end of the 3V3 row

P['R202'] = (10.4, 3.25, 270, 'B')         # GPIO0 pull-up beside pin 27, PRESS_N end towards the pin

# ==== audio: amplifier beside the speaker, 9 o'clock (B) ===============================================
P['U501'] = (-17.0, 4.0, 90, 'B')           # I2S pins face the module's left column in its pin order
# Speaker CMS-150803 (15 x 8 x 3) under the board, long side along the rim, between the cell (x -17.75) and
# the wall (r 30), clear of the REAR spring and the LRA pads; contacts on its inner edge (the outer edge
# would land on the LEFT touch arc). Its back is ~0.25 mm under B.Cu, so its courtyard keeps B parts out.
P['LS501'] = (m.SPEAKER_CENTRE[0], m.SPEAKER_CENTRE[1], 0, 'B')
# The amplifier's supply capacitors sit on F above the 0.9 mm gap between its VDD pins and the speaker,
# fed by one via there that drops into the L3 VSYS band (route_power.py).
P['C502'] = (-19.6, 1.7, 90, 'F')           # 100 nF, VSYS end (pad 1) towards the via
P['C501'] = (-21.0, 1.2, 90, 'F')           # 10 uF beside it
pin('R208', 90, 'B', '1', 'U501', '4', 0.0, 1.35)    # SD_MODE pull-down at its pin

# ==== haptic: driver at the LRA, 7-8 o'clock (B) =======================================================
P['U502'] = (-15.6, 12.4, 180, 'B')         # SCL/SDA level with the module's I2C pins, LRA side to the pads
P['J501'] = rim(23.6, 242.0, 'B')            # LRA lead pads
P['C503'] = (-17.0, 10.0, 0, 'B')          # VDD above pin 10, GND end towards the body
P['C504'] = (-14.4, 9.4, 90, 'B')          # REG, above the driver: the I2C and I2S lanes stay open
P['R209'] = (-12.4, 14.85, 270, 'B')       # EN pull-down below pin 5, clear of the SDA lane


# ==== expander: upper left (B), among the loads it switches ==========================================
P['U202'] = (-9.0, -13.1, 0, 'B')
P['C205'] = (-9.27, -10.2, 180, 'B')     # VCC over pins 14/15, GND end over pin 16
P['R206'] = (-3.87, -12.85, 180, 'B')      # INT pull-up beyond the expander's fan-out vias, +3V3 end into core 1
# board ID divider (static, read once at boot) left of the expander; its line arrives on L3
pin('R204', 90, 'B', '1', 'U202', '4', -4.6, 0.4, reach=4.0)
pin('R203', 90, 'B', '2', 'R204', '1', -1.2, 0.0, reach=4.0)
pin('C204', 90, 'B', '1', 'R204', '1', 1.2, 0.0, reach=4.0)
pin('R207', 90, 'B', '1', 'J301', '15', 1.6, 1.4)    # display reset pull-down at the connector's RESET pin


# ==== IMU: centre (B), beside its interrupt pins ======================================================
P['U401'] = (3.2, 7.0, 0, 'F')            # F, above the module near the axis: INT1/INT2 drop to pins 22/24 beside it
P['C402'] = (2.2, 9.25, 0, 'F')            # under pin 5 (+3V3 out left, GND in to the shared via)
P['C401'] = (4.843, 9.25, 180, 'F')        # under pin 8 (+3V3 out right)

# ==== microphone under its port, 3-4 o'clock (B) =======================================================
P['MK401'] = rim(22.0, m.MIC_ANGLE, 'B', extra=90)
P['C407'] = (21.59, 9.52, 270, 'B')         # VDD HF under pad 5, VDD end towards it
pin('R402', 90, 'B', '2', 'MK401', '5', -1.2, 1.6)   # 100R from MIC_PWR
pin('C406', 90, 'B', '1', 'MK401', '5', 1.2, 1.6)    # 1 uF
pin('R403', 0, 'B', '1', 'U201', '28', 2.6, -0.4)    # MIC_PWR pull-down at the module

# ==== touch electrodes, springs, ESD ===================================================================
_rmid = (m.TOUCH_ARC_R[0] + m.TOUCH_ARC_R[1]) / 2
P['E301'] = rim(_rmid, m.TOUCH_LEFT_ANGLE, 'F')
P['E302'] = rim(_rmid, m.TOUCH_RIGHT_ANGLE, 'F')
P['J302'] = rim(m.SENSOR_R, 250.0, 'F', extra=90)      # TOP spring under the window border
P['J303'] = (-22.4, -8.8, 0, 'B')                     # REAR spring to the base electrode, 10 o'clock beside the speaker
pin('D301', 90, 'F', '1', 'E301', '1', 1.4, 0.0)
pin('D302', 270, 'F', '1', 'E302', '1', -1.4, 0.0)
P['D303'] = (-20.67, 3.55, 0, 'F')          # TOP spring ESD, beside the amplifier's F-side capacitors
pin('D304', 0, 'B', '1', 'J303', '1', 2.0, 0.0)

# ==== Tag-Connect at the module's UART pins (B) ========================================================
P['J201'] = (15.2, 17.6, 90, 'B')       # RXD, TXD level with the module's pins 36, 37; GND below
P['R308'] = (10.85, 21.3, 0, 'B')          # TOUCH_RIGHT series R below pin 39, its lead along the bottom edge

# ==== F: window-border sensors, Hall pair ==============================================================
P['U402'] = rim(m.SENSOR_R, m.TOF_ANGLE, 'F')
P['U403'] = rim(m.SENSOR_R, m.ALS_ANGLE, 'F')
P['U503'] = rim(m.SENSOR_R + 0.2, m.IR_RX_ANGLE, 'F')
P['U301'] = rim(m.HALL_R, m.HALL_ANGLES[0], 'F')
P['U302'] = rim(m.HALL_R, m.HALL_ANGLES[1], 'F')
pin('C403', 90, 'F', '1', 'U402', '11', -1.5, -0.2)   # proximity decoupling on its outer flank, clear of the expander vias below

pin('C404', 90, 'F', '1', 'U402', '1', -1.6, -1.0)
P['R401'] = (-5.95, -18.76, 180, 'F')     # TOF_INT pull-up square to pin 7
pin('R210', 90, 'F', '1', 'U402', '5', 1.2, -0.6)
pin('C405', 90, 'F', '1', 'U403', '1', -1.2, -0.5)
P['R212'] = (15.3, -16.7, 0, 'B')          # ALERT pull-up at the gauge's ALERT via (outside the L3 band)
pin('C506', 90, 'F', '1', 'U503', '4', -0.4, 2.2)
P['R505'] = (21.96, 6.75, 180, 'F')        # RC filter: VCC end towards C506, enable end out right
pin('R506', 0, 'F', '1', 'U503', '3', -1.0, 1.3)
P['R211'] = (20.46, 8.15, 0, 'F')          # IR_RX_PWR pull-down on the enable, below the filter
pin('C303', 0, 'F', '1', 'U301', '1', 0.0, -1.1)
pin('C304', 0, 'F', '1', 'U302', '1', 0.4, 1.1)
pin('R306', 0, 'F', '1', 'U302', '4', -1.0, 1.2)

# ==== service field: one compact zone (B) below the power section ======================================
#   rails straight from the power section above, a ground beside every rail (ODD JOBS 39);
#   signals from the module below
_field = {'TP2': (6.0, -5.7), 'TP3': (8.8, -5.7), 'TP4': (11.6, -5.7), 'TP5': (14.4, -5.7),
          'TP10': (6.0, -2.7), 'TP1': (11.6, -2.7), 'TP16': (14.4, -2.7),
          'TP9': (6.0, 0.3), 'TP11': (8.8, 0.3), 'TP8': (11.6, 0.3), 'TP7': (14.4, 0.3)}
for _ref, (_x, _y) in _field.items():
    P[_ref] = (_x, _y, 0, 'B')
P['TP6'] = (5.5, -16.95, 0, 'B')           # VBUS on the VBUS bus at the TVS
P['R214'] = (8.2, -2.7, 270, 'B')           # I2C pull-ups in the field's free position: SCL beside TP10,
P['R213'] = (9.44, -2.7, 270, 'B')          # SDA from TP9 round it; +3V3 ends up to TP3
# pull-ups that sit on a probed line go beside its probe pad, clear of every fan-out
pin('R205', 90, 'B', '2', 'TP11', '1', 1.6, 0.0)     # expander RESET
pin('TP13', 0, 'B', '1', 'C109', '1', 0.0, -2.2, reach=4.0)   # LCD rail at its switch
pin('TP14', 0, 'B', '1', 'C406', '1', -1.0, 2.6, reach=4.0)   # MIC supply at its filter
pin('TP15', 0, 'B', '1', 'U503', '4', -0.6, 2.0)      # IR receiver supply, under the receiver

# ==== fiducials ========================================================================================
P['FID1'] = (15.6, -20.2, 0, 'F')
P['FID2'] = (5.5, 16.0, 0, 'F')
P['FID3'] = (-20.0, -9.0, 0, 'F')
P['FID4'] = (-18.8, -11.6, 0, 'B')
P['FID5'] = (21.0, -13.0, 0, 'B')
P['FID6'] = (19.8, 15.4, 0, 'B')

P = {k: v for k, v in P.items() if v is not None}

# No auto-placed parts: every support part is pinned above (brief: passives at their pins).
AUTO = []


PLACE = P
