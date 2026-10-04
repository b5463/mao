"""MAO_MAIN A0 placement: ref -> (x, y, rotation, side) in board mm (origin = puck axis, y towards
6 o'clock, viewed from the face). Rotation is KiCad's: degrees counter-clockwise on screen, applied
after the flip for B.Cu parts.

Zones (ODD JOBS standard, MAO picture), viewed from the face:
  6 o'clock      ESP32-S3 module (B), antenna over the board notch
  12 o'clock     USB-C and the IR LEDs at the back edge (B)
  1-2 o'clock    power section (B): battery link, charger, buck-boost, fuel gauge; battery plug
  centre         face-press switch and IMU (F); GPIO expander (B)
  9 o'clock      display FPC land and its support parts under the panel (F); audio (B)
  3 o'clock      IR receiver (F) and microphone (B) under the window border
  4-5 o'clock    ring Hall sensors (F); Tag-Connect (B) at the module's UART pins
  1-2 o'clock    service field (B) between the charger and the module: rails, BOOT, I2C, XRST, spare
  7-8 o'clock    haptic driver and LRA pads (B)
  window border  proximity at 11, ambient light at 1 o'clock (F)
Rim parts follow the circle (radial/tangential); everything else sits at 0/90 degrees.
"""
import mechanical as m


def rim(r, a, side='F', extra=0.0):
    """Part on a circle, its x axis tangential."""
    x, y = m.polar(r, a)
    return (round(x, 3), round(y, 3), round((-a + extra) % 360, 3), side)


P = {}

# ---- module and its own support parts (B) --------------------------------------------------------
P['U201'] = (0.0, m.MODULE_CY, 0, 'B')

# ---- power entry at the back edge (B) ------------------------------------------------------------
P['J101'] = (0.0, m.USB_FRONT_Y + 3.65, 0, 'B')   # B.Cu flip mirrors y: mating face towards 12 o'clock

# ---- power section: upper right (B), designed by hand (ODD JOBS 7-12) -------------------------------
# One straight chain, no crossing on B: battery (J102, 2 o'clock) -> R108 link -> Q101 reverse-polarity
# FET -> VBAT -> BQ24073 (U102) <- VBUS from the USB-C above; VSYS leaves U102 to the left straight into
# the TPS63802 (U104) input cap. U104 follows TI's layout (SLVSEU9D fig. 12-1): power pins facing the
# inductor above, CIN at the VIN end, COUT at the VOUT end, PGND strip between the pin rows. VSYS for
# the loads drops to F.Cu beside U102 (F has In1 GND under it); +3V3 is the In2 plane.
# Coordinates from courtyards just touching (no overlap); U104 is the datum (UX, UY).
UX, UY = 3.0, -11.0
P['U104'] = (UX, UY, 90, 'B')               # VIN top-right, VOUT top-left
P['L101'] = (UX, UY - 2.59, 180, 'B')       # L1 pad over pin 9, L2 over pin 7
P['C105'] = (UX + 2.57, UY, 270, 'B')       # CIN at the VIN end
P['C106'] = (UX - 2.57, UY, 270, 'B')       # COUT at the VOUT end
P['C107'] = (UX - 4.11, UY, 270, 'B')       # COUT
P['R112'] = (UX + 1.0, UY + 2.5, 270, 'B')  # EN divider bottom, under EN
P['R111'] = (UX + 2.1, UY + 2.5, 90, 'B')   # EN divider top (VSYS)
P['R114'] = (UX - 0.5, UY + 2.5, 270, 'B')  # FB bottom, under FB
P['R113'] = (UX - 1.6, UY + 2.5, 90, 'B')   # FB top (+3V3)
P['U102'] = (UX + 6.41, UY - 0.775, 180, 'B')   # OUT pins level with CIN; IN top, BAT right
P['C102'] = (UX + 2.57, UY - 3.06, 90, 'B')     # charger OUT, above CIN
P['R104'] = (UX + 4.4, UY - 3.96, 90, 'B')      # ILIM, straight above the pin
P['C101'] = (UX + 6.14, UY - 3.9, 0, 'B')       # charger IN, on the VBUS riser
P['R103'] = (UX + 9.55, UY - 2.8, 0, 'B')       # ISET
P['R105'] = (UX + 9.55, UY - 3.84, 0, 'B')      # TD
P['C103'] = (UX + 9.35, UY, 270, 'B')           # charger BAT, level with the BAT pins
P['Q101'] = (UX + 12.1, UY + 0.175, 0, 'B')     # reverse-polarity P-FET: VBAT pin level with BAT
P['R108'] = (UX + 15.26, UY + 1.635, 90, 'B')   # 0R battery link (measure here), level with the drain
P['J102'] = (UX + 16.26, UY + 7.285, 180, 'B')  # battery: plug enters from 6 o'clock side, over the cell end
P['U103'] = (UX + 12.4, UY - 3.2, 0, 'B')       # fuel gauge, VBAT from the FET source
P['C104'] = (UX + 10.27, UY - 5.0, 180, 'B')    # gauge VDD, on the VBAT sense trace
P['R109'] = (UX + 11.25, UY + 3.0, 270, 'B')    # FET gate to GND, straight below the gate pin
P['R110'] = (UX + 17.75, UY + 3.25, 0, 'B')     # NTC substitute (DNP), beside J102 on the NTC tap
P['D101'] = (4.65, -19.5, 0, 'B')       # VBUS TVS on the way from the receptacle
# USB entry: CC Rd beside their pins (B), ESD on F under the window border, on the D+/D- vias
P['R101'] = (-1.6, -19.5, 270, 'B')     # CC1 Rd
P['R102'] = (1.9, -17.25, 270, 'B')      # CC2 Rd
P['U101'] = (0.0, -18.0, 90, 'F')        # TPD2E2U06

# ---- IR transmit at the back edge (B) ------------------------------------------------------------
P['D501'] = rim(27.7, m.IR_TX_ANGLES[0], 'B', extra=180)
P['D502'] = rim(27.7, m.IR_TX_ANGLES[1], 'B', extra=180)
P['Q501'] = (14.6, -19.0, 0, 'B')

# ---- expander and I2C hub (B, centre) ------------------------------------------------------------
P['U202'] = (-5.5, -12.8, 0, 'B')      # open ground between the USB lanes and the power section: every pin
                                       # fans out to its own via and its lines run on In2 (route_local.py)
P['C205'] = (-6.6, -9.85, 180, 'B')    # VCC decoupling on the supply pins' via
P['R208'] = (-9.6, -14.6, 90, 'B')     # AMP_SD pull-down on the line's via

# ---- audio: 9 o'clock (B), speaker below in the base ---------------------------------------------
P['U501'] = (-15.0, 1.0, 180, 'B')     # outputs face the springs, I2S/SD face the module
P['J501'] = (-23.6, 3.2, 0, 'B')       # SPK+ (lower): the amp's SPK_P pin is the lower one
P['J502'] = (-23.6, -3.2, 0, 'B')      # SPK- (upper)

# ---- haptic: 7-8 o'clock (B) ----------------------------------------------------------------------
P['U502'] = (-16.4, 9.6, 180, 'B')     # LRA outputs face J503, I2C/EN face the module
# amp and haptic support parts: designed so the I2S lanes from module pins 9/10/11 run free at x -11..-13
P['C501'] = (-17.6, 4.7, 270, 'B')     # amp bulk, on the VSYS bar under the amp
P['C502'] = (-15.5, 4.15, 270, 'B')     # amp HF, between the VSYS pins
P['C503'] = (-17.6, 7.1, 0, 'B')       # haptic VDD, on the VSYS feed to pin 10
P['C504'] = (-15.2, 6.6, 90, 'B')      # haptic REG, under pin 1
# board-ID divider under pin 15, clear of the USB corridor at x -10
P['R204'] = (-6.685, 1.2, 90, 'B')     # BOARD_ID to GND
P['C204'] = (-7.7, 1.2, 90, 'B')       # holds the divider for the ADC sample
P['R203'] = (-8.75, 1.2, 270, 'B')     # +3V3 to BOARD_ID
# display series resistors at the module (source termination, ODD JOBS 29), B under pins 20/19
# touch series resistors within 1 mm of module pins 4/5/6 (Espressif touch guide)
# module EN RC and 3V3 decoupling at pins 1-3 (lower left corner of the module)
P['R201'] = (-8.3, 18.0, 180, 'F')     # EN pull-up, on F under the module (EN via beside pin 3)
P['C203'] = (-8.3, 19.3, 0, 'F')       # EN delay
P['C202'] = (-11.0, 19.95, 180, 'B')   # 3V3 HF level with pin 2
P['C201'] = (-13.0, 20.5, 270, 'B')    # 3V3 bulk
P['R307'] = (-11.0, 17.4, 180, 'B')    # TOUCH_LEFT
P['R309'] = (-11.0, 16.1, 180, 'B')    # TOUCH_TOP
P['R310'] = (-11.0, 14.9, 180, 'B')    # TOUCH_REAR
P['R301'] = (-0.6, 1.45, 90, 'B')       # LCD_SCLK
P['R302'] = (-1.9, 1.45, 90, 'B')       # LCD_MOSI
P['J503'] = rim(23.6, 242.0, 'B', extra=0)

# ---- microphone: 3-4 o'clock (B), port up into the window border ----------------------------------
P['MK401'] = rim(22.0, m.MIC_ANGLE, 'B', extra=90)

# ---- touch electrodes and their ESD ---------------------------------------------------------------
_rmid = (m.TOUCH_ARC_R[0] + m.TOUCH_ARC_R[1]) / 2
P['E301'] = rim(_rmid, m.TOUCH_LEFT_ANGLE, 'F')
P['E302'] = rim(_rmid, m.TOUCH_RIGHT_ANGLE, 'F')
P['J302'] = rim(m.SENSOR_R, 250.0, 'F', extra=90)     # TOP spring under the window border
P['J303'] = (-21.0, -10.4, 0, 'B')                    # REAR spring to the base electrode (10 o'clock)

# ---- display interface (F, under the panel at 9 o'clock) ------------------------------------------
P['J301'] = (-11.9, 0.0, 270, 'F')      # land: pad 1 towards 6 o'clock, FPC tail towards the panel edge
P['Q301'] = (-8.4, 7.0, 0, 'F')         # backlight switch
P['U105'] = (-8.2, -6.8, 0, 'F')        # display rail switch

# ---- face press and IMU (F, centre) ---------------------------------------------------------------
P['SW301'] = (m.PRESS_XY[0], m.PRESS_XY[1], 0, 'F')
P['U401'] = (0.0, 8.6, 0, 'F')         # under the module, clear of its top pin row: vias can sit round it
P['C402'] = (-2.9, 8.1, 90, 'F')        # west of the IMU's GND / VDD pins, leaving its I2C pins a clear way north
P['C401'] = (2.0, 10.75, 0, 'F')        # south-east, by the second VDD pin

# ---- ring dial (F, under the pole track) ----------------------------------------------------------
P['U301'] = rim(m.HALL_R, m.HALL_ANGLES[0], 'F')
P['U302'] = rim(m.HALL_R, m.HALL_ANGLES[1], 'F')

# ---- window-border sensors (F) --------------------------------------------------------------------
P['U402'] = rim(m.SENSOR_R, m.TOF_ANGLE, 'F')
P['U403'] = rim(m.SENSOR_R, m.ALS_ANGLE, 'F')
P['U503'] = rim(m.SENSOR_R + 0.2, m.IR_RX_ANGLE, 'F')

# ---- service access (B): every probe pad where its signal already is (ODD JOBS 36-39, 158) ----------
# Tag-Connect at the module's UART pins, rotated so TXD0/RXD0 run straight from pins 37/36 into its
# top row (pinout in circuit.py); its EN, 3V3 and GPIO0 pads drop through vias below it.
P['J201'] = (13.5, 18.9, 0, 'B')
P['R308'] = (11.0, 21.5, 0, 'B')       # TOUCH_RIGHT series R at pin 39; the electrode lead runs under J201
# Service field above the module's right half, on the charger side (3.0 x 3.2 mm grid, one free position;
# each name stands beside its pad, alternating sides by row so no two names touch):
#   y -5.95 GND   3V3   SYS   BAT     rails from the power section straight above
#   y -2.9  SCL   ---   GND   GND     a ground beside every rail for a spring-tip probe (ODD JOBS 39)
#   y  0.3  SDA   XRST  BOOT  IO35    signals straight up from their module pins
_field = {'TP2': (4.9, -5.95), 'TP3': (7.9, -5.95), 'TP4': (10.9, -5.95), 'TP5': (13.9, -5.95),
          'TP10': (4.9, -2.9), 'TP1': (10.9, -2.9), 'TP16': (13.9, -2.9),
          'TP9': (4.9, 0.3), 'TP11': (7.9, 0.3), 'TP8': (10.9, 0.3), 'TP12': (13.9, 0.3)}
for _ref, (_x, _y) in _field.items():
    P[_ref] = (_x, _y, 0, 'B')
P['TP6'] = (5.5, -16.95, 0, 'B')      # VBUS: on the VBUS bus between the TVS and the charger input
P['TP7'] = (-15.0, 18.7, 0, 'B')      # RST: left of the MCU_EN via beside module pin 3
P['TP13'] = (-8.4, -4.6, 0, 'B')      # LCD: under the display load switch
P['TP14'] = (19.9, 11.0, 0, 'B')      # MIC: beside the microphone supply cap
P['TP15'] = (22.6, 0.9, 0, 'B')       # IRV: under the IR receiver's supply filter
P['R205'] = (6.2, 8.51, 0, 'F')        # EXP_RST pull-up on F under the module, at the line's module via
P['R206'] = (3.9, 1.3, 180, 'F')       # EXP_INT pull-up at the line's module via (pin 23)
P['R212'] = (12.9, -18.3, 180, 'F')    # SENSE_ALRT pull-up beside the light sensor's INT pin
P['R209'] = (-16.4, 12.4, 180, 'B')    # HAPTIC_EN pull-down on the enable's via, under the driver
P['R106'] = (9.25, -8.25, 270, 'B')     # PGOOD pull-up under the charger's pin 7 (designed links)
P['R107'] = (7.25, -8.5, 180, 'B')      # CHG pull-up beside the charger's pin 9
P['R202'] = (11.0, 4.7, 180, 'B')      # GPIO0 pull-up on its own pin row: pin 27 runs straight into it
# microphone support (B), placed so the PDM clock reaches pad 4 over the microphone's north side and the
# supply leaves pad 5 east: MIC_PWR -> R403 pull-down -> R402 (100 R) -> C406 -> MK401 pad 5
P['R403'] = (21.25, 2.75, 180, 'B')
P['R402'] = (24.25, 5.0, 270, 'B')
P['C406'] = (24.25, 7.5, 270, 'B')

# ---- fasteners and fiducials ----------------------------------------------------------------------
for ref, a in (('H1', m.SCREW_ANGLES[0]), ('H2', m.SCREW_ANGLES[1]), ('H3', m.PEG_ANGLE)):
    x, y = m.polar(m.MOUNT_R, a)
    P[ref] = (round(x, 3), round(y, 3), 0, 'F')
P['FID1'] = (14.5, -15.5, 0, 'F')
P['FID2'] = (18.0, 20.2, 0, 'F')
P['FID3'] = (-20.0, -9.0, 0, 'F')
P['FID4'] = (-20.0, -4.0, 0, 'B')
P['FID5'] = (21.0, -13.0, 0, 'B')
P['FID6'] = (-21.5, 6.0, 0, 'B')


# ---- support parts: placed by place_auto.py at the closest free spot to the pads they serve -------
# (ref, owner, side, allowed rotations, search radius mm), in priority order: decoupling first.
Q4 = (0, 90, 180, 270)
AUTO = [
    # charger, battery, gauge: the rest of the power section is designed (above)
    # IR transmit
    ('R501', 'D501', 'B', Q4, 6), ('R502', 'D502', 'B', Q4, 6), ('R503', 'Q501', 'B', Q4, 5),
    ('R504', 'Q501', 'B', Q4, 5), ('C505', 'Q501', 'B', Q4, 6),
    # expander
    # audio, haptic, mic
    ('C407', 'MK401', 'B', Q4, 5),
    # touch ESD at each electrode
    ('D301', 'E301', 'B', Q4, 6), ('D302', 'E302', 'B', Q4, 6), ('D303', 'J302', 'F', Q4, 5),
    ('D304', 'J303', 'B', Q4, 5),
    # display (F, under the panel)
    ('C301', 'J301', 'F', Q4, 6), ('C302', 'J301', 'F', Q4, 7),
    ('R303', 'J301', 'F', Q4, 7), ('R304', 'Q301', 'F', Q4, 5),
    ('R305', 'Q301', 'F', Q4, 5), ('C109', 'U105', 'F', Q4, 5), ('C108', 'U105', 'F', Q4, 5),
    ('R115', 'U105', 'F', Q4, 5), ('R116', 'U105', 'F', Q4, 5),
    # sensors (F)
    ('C303', 'U301', 'F', Q4, 4),
    ('C304', 'U302', 'F', Q4, 4), ('R306', 'U301', 'F', Q4, 5), ('C403', 'U402', 'F', Q4, 4),
    ('C404', 'U402', 'F', Q4, 5), ('R401', 'U402', 'F', Q4, 5), ('C405', 'U403', 'F', Q4, 4),
    ('C506', 'U503', 'F', Q4, 5), ('R505', 'U503', 'F', Q4, 5), ('R506', 'U503', 'F', Q4, 5),
    # the expander's pull resistors sit at the far end of each line, not round the expander: its 0.5 mm
    # pins then fan out straight to vias (route_local.py) and the lines run on In2
    ('R207', 'J301', 'F', Q4, 7),
    ('R210', 'U402', 'F', Q4, 7), ('R211', 'R505', 'F', Q4, 7),
    ('R213', 'U401', 'F', Q4, 6), ('R214', 'U401', 'F', Q4, 6),
]
for _ref, _owner, _side, _a, _r in AUTO:
    _x, _y = P[_owner][0], P[_owner][1]
    P[_ref] = (_x, _y, 0, _side)

# F.Cu part keep-out: the display FPC folds from the panel edge (r 17.8 at 9 o'clock) onto the J301
# land; no part may sit under it (place_auto honours this).
KEEP_F = [(-18.6, -4.9, -13.6, 4.9)]

PLACE = P
