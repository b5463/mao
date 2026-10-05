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
# F.Cu part keep-out: the display tail drops past the slot at 9 o'clock.
KEEP_F = [m.TAIL_F_CLEAR]
# B.Cu part keep-outs: the tail corridor between the slot and J301 (no part at all), and the display bus
# corridor from the module's top row to the J301 pads (only its own series resistors sit in it).
KEEP_B = [m.TAIL_CORRIDOR, (-10.2, -0.1, 1.3, 2.75)]
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
            if ref.startswith('C') and math.hypot(x, y) > m.PCB_R - m.MLCC_EDGE + 0.25:
                return False             # MLCC pads >= 1.5 mm from the milled edge (courtyard ~0.25 beyond the pad)
            if abs(x) < m.NOTCH_W / 2 + m.ANTENNA_COPPER_SETBACK and y > m.NOTCH_Y - 0.6:
                return False
        for a in m.SCREW_ANGLES + (m.PEG_ANGLE,):
            cx, cy = m.polar(m.MOUNT_R, a)
            r = ((m.PEG_KEEPOUT_D if place[3] == 'F' else m.PEG_KEEPOUT_D_B) if a == m.PEG_ANGLE else
                 m.BOSS_KEEPOUT_D_F if place[3] == 'F' else m.HEAD_KEEPOUT_D_B) / 2
            # circle against box: nearest point of the box to the centre
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
P['C111'] = (UX, UY + 4.7, 270, 'B')          # UVLO filter on BB_EN, below the divider
# The charger (up to 0.6 W while charging) sits on F.Cu under the panel, not over the cell (ODD JOBS 78),
# directly above its old B spot and turned 180: BAT pins face the reverse-polarity FET, OUT pins sit over the
# buck-boost input capacitor, IN faces the board centre where VBUS arrives on its L3 strip. VBAT and VSYS
# change faces through designed via pairs (route_power.py). The buck-boost stays on B: ~45 mW typical.
P['U102'] = (9.41, -11.775, 180, 'F')
P['C103'] = (13.2, -11.78, 0, 'F')         # BAT, at pins 2/3
P['C102'] = (6.1, -12.6, 90, 'F')           # OUT, at pins 10/11, over C105 on B
P['C101'] = (8.66, -8.6, 270, 'F')          # IN, at pin 13
P['R105'] = (9.95, -8.3, 270, 'F')         # TD to GND (termination on), at pin 15
P['R103'] = (11.25, -8.3, 270, 'F')        # ISET, at pin 16
P['R104'] = (6.3, -10.05, 180, 'F')         # ILIM, at pin 12
P['Q101'] = (UX + 12.1, UY + 0.175, 0, 'B')
P['R108'] = (UX + 15.26, UY + 1.635, 90, 'B')
P['J102'] = (UX + 16.26, UY + 7.285, 180, 'B')
P['U103'] = (13.7, -14.55, 0, 'B')           # gauge in the charger's old B spot: clear of the H2 screw head
P['C104'] = (11.3, -15.05, 90, 'B')         # gauge VDD beside its VBAT pins
P['R109'] = (UX + 11.25, UY + 3.0, 270, 'B')
P['R110'] = (UX + 17.75, UY + 3.25, 0, 'B')
P['R106'] = (9.16, -15.1, 90, 'F')          # PGOOD pull-up at pin 7
P['R117'] = (11.9, -14.6, 90, 'F')           # /CE pull-down below pin 4: charging on from reset
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
P['C505'] = (14.4, -18.9, 0, 'B')          # LED reservoir beside the IR LEDs' VSYS via (VSYS end west)

# ==== display: J301 on B at the tail slot (9 o'clock), rail switch on F, backlight driver beside it ======
# Entry faces the slot (rotation 90 on B); panel pin 1 meets pad 18 at the 12 o'clock end, so the bus pins
# CS..TE (panel 10-14, pads 9-5) face the module's display group (pins 21..17) in order.
P['J301'] = (round(m.TAIL_ENTRY_X + 2.85, 3), 0.0, 90, 'B')
# The display bus runs on B from the module's top row straight to the pads, no vias.
pin('R301', 90, 'B', '1', 'U201', '20', 0.0, -1.7)   # 22R on SCLK and MOSI at the module pins
pin('R302', 90, 'B', '1', 'U201', '19', 0.0, -1.7)
# Backlight: AW9364 above the bus, LED sinks towards VLED- (pad 11), VIN and its cap at VLED+ (pad 12).
P['U303'] = (-8.3, -2.4, 270, 'B')
pin('C305', 90, 'B', '1', 'U303', '3', 0.0, -1.4, reach=3.0)   # VIN cap under pin 3
# Display rail switch on F under the panel, its output dropping to pad 3 (VCI) through one via.
P['U105'] = (-11.4, -9.15, 0, 'F')           # left of the USB pair (x -7.6/-6.8 on F), below the amp's caps
P['C110'] = (-14.4, -9.8, 180, 'F')         # VIN cap in line with pin 1; its GND end drops into the amp's F thermal pad
P['R115'] = (-8.7, -9.0, 90, 'F')           # QOD discharge between pins 5 and 6 (the USB pair runs at x -7.6 on F)
P['C109'] = (-8.7, -11.15, 90, 'F')          # output cap at pin 6
P['C301'] = (-11.3, 3.1, 0, 'F')           # panel VCI HF beside the VCI via (J301 pad 3 is below it on B)
P['C302'] = (-11.6, 1.5, 0, 'F')           # panel bulk

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
P['U501'] = (-14.4, -8.7, 0, 'B')           # 10 o'clock, above the tail corridor: I2S pins face the corridor the
                                            # lanes come down; outputs loop under it to the speaker pads (SPK+ up
                                            # the corridor's west edge); VDD down to an L3 VSYS branch
# Speaker CMS-150803 (15 x 8 x 3) under the board, long side along the rim, between the cell (x -17.75) and
# the wall (r 30), clear of the REAR spring and the LRA pads; contacts on its inner edge (the outer edge
# would land on the LEFT touch arc). Its back is ~0.25 mm under B.Cu, so its courtyard keeps B parts out.
P['LS501'] = (m.SPEAKER_CENTRE[0], m.SPEAKER_CENTRE[1], 0, 'B')
# VDD pins face the amp's L3 VSYS branch: their capacitors sit on F over the VDD via below the part.
P['C502'] = (-14.8, -11.6, 90, 'F')         # 100 nF over the VDD via
P['C501'] = (-15.1, -13.4, 0, 'F')          # 10 uF beside it
P['R208'] = (-15.95, -9.96, 90, 'F')        # SD_MODE pull-down on F, beside the amplifier's pin 4 via
P['R507'] = (-13.6, -14.9, 180, 'F')        # SD_MODE series resistor on its F line, at the expander's via

# ==== haptic: driver at the LRA, 7-8 o'clock (B) =======================================================
P['U502'] = (-15.6, 12.4, 180, 'B')         # SCL/SDA level with the module's I2C pins, LRA side to the pads
P['J501'] = rim(23.6, 242.0, 'B')[:2] + (90, 'B')   # LRA lead pads (wire pads: rotation cardinal, ODD JOBS 90)
P['C503'] = (-17.0, 10.0, 0, 'B')          # VDD above pin 10, GND end towards the body
P['C504'] = (-14.4, 9.4, 90, 'B')          # REG, above the driver: the I2C lanes stay open
P['R209'] = (-12.4, 14.85, 270, 'B')       # EN pull-down below pin 5, clear of the SDA lane


# ==== expander: upper left (B), among the loads it switches ==========================================
P['U202'] = (-9.0, -13.1, 0, 'B')
P['C205'] = (-9.27, -10.2, 180, 'B')     # VCC over pins 14/15, GND end over pin 16
P['R206'] = (-3.87, -12.85, 180, 'B')      # INT pull-up beyond the expander's fan-out vias, +3V3 end into core 1
# board ID divider (static, read once at boot) left of the expander; its line arrives on L3
# board ID divider (static, read once at boot) in the charger's old B spot; its line arrives on L3
P['R204'] = (7.3, -14.7, 90, 'B')
P['R203'] = (8.4, -14.7, 90, 'B')
P['C204'] = (6.2, -14.7, 90, 'B')
pin('R207', 90, 'B', '1', 'U202', '2', 0.0, -1.6, reach=4.0)   # display reset pull-down at the expander's P0
P['R116'] = (-12.2375, -7.0, 270, 'F')      # rail switch ON pull-down, straight below pin 3 (clear of its courtyard)


# ==== IMU: centre (B), beside its interrupt pins ======================================================
P['U401'] = (3.2, 7.0, 0, 'F')            # F, above the module near the axis: INT1/INT2 drop to pins 22/24 beside it
P['C402'] = (2.2, 9.25, 0, 'F')            # under pin 5 (+3V3 out left, GND in to the shared via)
P['C401'] = (4.843, 9.25, 180, 'F')        # under pin 8 (+3V3 out right)

# ==== microphone under its port, 3-4 o'clock (B) =======================================================
P['MK401'] = rim(22.0, m.MIC_ANGLE, 'B')[:2] + (0, 'B')   # the port hole is what matters: rotation cardinal
P['C407'] = (21.59, 9.52, 270, 'B')         # VDD HF under pad 5, VDD end towards it
P['R402'] = (20.39, 9.66, 90, 'B')         # 100R from MIC_PWR
P['C406'] = (23.3, 9.0, 90, 'B')           # 1 uF
pin('R403', 0, 'B', '1', 'U201', '28', 2.6, -0.4)    # MIC_PWR pull-down at the module

# ==== touch electrodes, springs, ESD ===================================================================
_rmid = (m.TOUCH_ARC_R[0] + m.TOUCH_ARC_R[1]) / 2
P['E301'] = rim(_rmid, m.TOUCH_LEFT_ANGLE, 'F')
P['E302'] = rim(_rmid, m.TOUCH_RIGHT_ANGLE, 'F')
P['J302'] = (-21.6, 7.3, 270, 'F')                     # TOP spring under the window border (r 22.8), outboard of the slot
P['J303'] = (-22.4, -8.8, 0, 'B')                     # REAR spring to the base electrode, 10 o'clock beside the speaker
P['D301'] = (-21.6, 10.45, 90, 'F')        # LEFT ESD hanging off its lane, inboard of the arc
pin('D302', 270, 'F', '1', 'E302', '1', -1.4, 0.0)
pin('D303', 0, 'F', '1', 'J302', '1', 1.8, 0.8, reach=4.0)   # TOP spring ESD beside the spring
pin('D304', 0, 'B', '1', 'J303', '1', 2.0, 0.0)

# ==== Tag-Connect at the module's UART pins (B) ========================================================
P['J201'] = (15.2, 17.6, 90, 'B')       # RXD, TXD level with the module's pins 36, 37; GND below
P['R308'] = (11.6, 19.6, 0, 'B')           # TOUCH_RIGHT series R beside pin 39, inboard of the antenna fringe

# ==== F: window-border sensors, Hall pair ==============================================================
P['U402'] = rim(m.TOF_R, m.TOF_ANGLE, 'F')      # its field of view is set by the window aperture, axis radial
_x, _y, _r, _s = rim(m.SENSOR_R, m.ALS_ANGLE, 'F')
P['U403'] = (_x, _y, 0, 'F')                       # photodiode: orientation has no function (ODD JOBS 90)
_x, _y, _r, _s = rim(m.IR_RX_R, m.IR_RX_ANGLE, 'F', extra=90)
P['U503'] = (_x, _y, 0, 'F')                       # 4.0 mm side radial at 3 o'clock (rotation 0 there)
P['U301'] = rim(m.HALL_R, m.HALL_ANGLES[0], 'F')  # tangential: both latches on the pole-track arc, exactly 6 deg
P['U302'] = rim(m.HALL_R, m.HALL_ANGLES[1], 'F')  # apart (a cardinal rotation would collide at 2.76 mm spacing)
pin('C403', 90, 'F', '1', 'U402', '11', -1.5, -0.2)   # proximity decoupling on its outer flank, clear of the expander vias below

pin('C404', 90, 'F', '1', 'U402', '1', -1.6, -1.0)
P['R401'] = (-5.81, -18.5, 180, 'F')       # TOF_INT pull-up square to pin 7
pin('R210', 90, 'F', '1', 'U402', '5', 1.2, -0.6)
pin('C405', 90, 'F', '1', 'U403', '1', -1.2, -0.5)
pin('R212', 90, 'B', '1', 'U103', '5', -1.6, 0.0, reach=4.0)   # ALERT pull-up beside the gauge
P['C506'] = (23.6, -3.9, 0, 'F')            # VCC cap tangentially beside pin 4 (pins 3/4 face the ring)
P['R505'] = (21.96, 6.75, 180, 'F')        # RC filter: VCC end towards C506, enable end out right
P['R506'] = (23.6, 3.6, 0, 'F')             # OUT pull-up beside pin 3
P['R211'] = (20.46, 8.15, 0, 'F')          # IR_RX_PWR pull-down on the enable, below the filter
P['C303'] = rim(24.0, 117.0, 'F')[:2] + (0, 'F')     # VCC caps inboard of the latches, under the ring lip
P['C304'] = rim(24.0, 124.0, 'F')[:2] + (0, 'F')     # (0.55 mm tall: lip limit 0.95), 4.5 mm from the edge
P['R306'] = rim(24.0, 130.0, 'F')[:2] + (0, 'F')     # GND end east, its via south-east of it

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
P['TP13'] = (-6.4, -7.2, 0, 'B')           # LCD rail probe on B, beside the backlight driver
pin('TP14', 0, 'B', '1', 'C406', '1', -1.0, 2.6, reach=4.0)   # MIC supply at its filter
pin('TP15', 0, 'B', '1', 'U503', '4', -0.6, 2.0)      # IR receiver supply, under the receiver

# ==== fiducials ========================================================================================
P['FID1'] = (15.6, -20.2, 0, 'F')
P['FID2'] = (5.5, 16.0, 0, 'F')
P['FID3'] = (-20.0, -9.0, 0, 'F')
P['FID4'] = (-22.7, -13.9, 0, 'B')
P['FID5'] = (21.0, -13.0, 0, 'B')
P['FID6'] = (19.0, 18.2, 0, 'B')             # 2.2 mm clear of R306's GND via

P = {k: v for k, v in P.items() if v is not None}

# No auto-placed parts: every support part is pinned above (brief: passives at their pins).
AUTO = []


PLACE = P
