"""MAO_MAIN A1 mechanical datums (A0 puck, MINI-1 module) (board millimetres, origin = puck axis, +x right, +y towards
6 o'clock, viewed from the face). Every placement script and the mechanical doc read these
numbers; nothing is eyeballed (ODD JOBS 72/73).

Concept (docs/hardware/mao-mechanical.md): a round puck, FDM-printed in up to two materials. A
rotating ring dial surrounds the round face. The face (clear window + display on a printed carrier)
floats 0.25 mm on a tact switch at the PCB centre: pressing MAO's face is the button. The PCB is
screwed to the printed chassis; battery and speaker sit in the base below the PCB.

Clock positions: 12 o'clock = the face's "up" = the back of MAO (USB-C, IR, cable side);
6 o'clock = towards the person facing MAO.
"""
import math

PUCK_OD = 64.0              # enclosure outer diameter
WALL = 2.0                  # side wall
PCB_R = 29.0                # board radius (58 mm disc; 1 mm clearance to the wall, ODD JOBS 136)
PCB_THICK = 1.6

# Display: 1.28" round GC9A01 panel, centred on the axis, image "up" towards 12 o'clock.
DISPLAY_ACTIVE_D = 32.4
DISPLAY_STANDOFF = 2.7      # panel rear to PCB top surface (foam + carrier)
DISPLAY_STACK_H = 2.0      # panel (1.56) on its carrier face
WINDOW_AIR = 0.2           # panel glass to window underside
WINDOW_Z = DISPLAY_STANDOFF + DISPLAY_STACK_H + WINDOW_AIR   # window underside above F.Cu: 4.9
PRESS_TRAVEL = 0.25        # the face assembly moves down this far when pressed
# The tallest part under the window border is the IR receiver (IRM-H6xxT, 4.0 +-0.3 mm with its dome): the
# window keeps WINDOW_Z - PRESS_TRAVEL - 4.3 = 0.35 mm over it at worst case, pressed (ODD JOBS 135).
ZONE_A_MAX_H = 1.2          # max part height on F.Cu under the panel (ODD JOBS 75)

# Ring dial: rotating ring, the face window is fixed inside it.
RING_ID = 48.0              # 6 mm sensor band between panel and ring (IR receiver is 5 mm wide)
RING_OD = PUCK_OD
RING_POLE_PAIRS = 15        # 30 poles: 30 detents / 15 quadrature cycles per rev, as the LCDkit EC11
RING_MAGNET_R = 26.3        # mean radius of the pole track above the PCB
# The poles are a flexible ferrite multipole strip in the ring's lower lip, not sintered NdFeB discs: the ring
# passes over the antenna at 6 o'clock, and ferrite in rubber does not conduct (ODD JOBS 2, 3). DRV5012 operates
# at <= +-3.3 mT; the strip is specified for >= 8 mT peak at HALL_GAP. No mechanical detent: firmware ticks the
# LRA at every step.
HALL_R = 26.3               # Hall sensors sit under the pole track
HALL_GAP = 1.5              # pole-strip face to sensor top (through the ring's lower lip)
# Quadrature: pole pitch 12 deg, electrical period 24 deg; half a pole pitch (6 deg = 2.76 mm at
# r 26.3) is 90 deg electrical. Both packages fit side by side at that spacing.
HALL_ANGLES = (120.0, 126.0)    # degrees, clockwise from 12 o'clock: between the right touch arc and the antenna band

# Sensor window: an annulus of the cover window between the panel and the ring.
DISPLAY_OUTLINE_R = 17.8   # LCM 35.6 mm round outline (the glass ledge under the tail adds 2.14 mm at 9 o'clock)
WINDOW_ANNULUS = (DISPLAY_OUTLINE_R + 0.5, RING_ID / 2 - 0.5)   # radii on F.Cu that see out
SENSOR_R = 21.2             # centre radius of the window-border sensors
TOF_R = 20.9                # VL53L4CD: 2.4 mm radial, outer corner r 22.2 (ring lip starts at r 24.0)
IR_RX_R = 21.0              # IRM-H6xxT turned so its 4.0 mm side is radial: body r 19.0-23.0, corners r 23.1
TOF_ANGLE = -28.0           # 11 o'clock ("forehead"), clockwise from 12
IR_RX_ANGLE = 90.0          # 3 o'clock, top-view receiver under the border (12 o'clock is the USB-C shell)

# Display tail. The stock Winstar tail (70.1 mm, spec section 8) leaves the panel at 9 o'clock (image up = 12
# o'clock: the GC9A01 rotates only in 90 deg steps) and folds back under the panel at its bending area. Its ~45 mm
# of slack is one long loop under the panel: two flat layers (2 x 0.11 mm) in a 0.4 mm pocket of the face carrier,
# inward to about x +8, where the loop turns in a 2.4 mm deep well of the carrier (TAIL_WELL: fold radius >= 1 mm,
# room for the +-0.5 mm tail tolerance), and back out. It leaves the face at r ~19 and drops through a routed slot
# just outside the panel edge; under the board it bends inward into J301 on B.Cu, whose entry faces the slot. Each
# press flexes the long loop, not a fold. Lateral order is kept through every fold (all fold axes are tangential):
# panel pin 1 is at the 12 o'clock end.
TAIL_ANGLE = 270.0
TAIL_W = 9.5                # tail end width (9.50 +-0.1); the 13.05 mm section near the panel stays in the face
TAIL_SLOT = (-19.3, -5.75, -18.3, 5.75)    # routed slot x0, y0, x1, y1: 1.0 mm wide, round ends, NPTH (0.35 mm
                                           # clear of the speaker's SPK- pad, 0.5 mm outside the panel outline)
TAIL_ENTRY_X = -14.0        # J301 FPC entry edge on B.Cu: 4.5 mm for the tail's bend from vertical to the entry
TAIL_CORRIDOR = (-18.3, -5.9, -14.4, 5.9)  # B.Cu between slot and J301's courtyard: tracks allowed, no parts
TAIL_F_CLEAR = (-19.4, -6.4, -17.7, 6.4)   # F.Cu on the slot's inboard side: no parts (the tail drops there)
TAIL_WELL = (6.0, -5.75, 12.0, 5.75)       # F.Cu under the carrier's well where the tail loop turns: no parts

# Antenna: ESP32-S3-MINI-1 on B.Cu at 6 o'clock, long axis radial, antenna over a notch in the board edge.
# Footprint RF_Module:ESP32-S2-MINI-1 (the S3-MINI-1 land, Gate C audit): body (F.Fab) x +-7.7, y -9.75 ... 10.25 about
# the footprint origin, antenna section the 4.5 mm at the -y end; on B.Cu the flip mirrors y, so the antenna end is at
# origin + 9.75 and the antenna section starts at origin + 5.25. Pads: columns x +-7.0 (1-15, 31-45), the row at the
# board-centre end 9.55 from the origin (16-30), the row at the antenna end 4.45 from it (46-60, GND), EPAD 61.
MODULE_ANGLE = 180.0        # 6 o'clock
MODULE_W, MODULE_L = 15.4, 20.0
ANTENNA_L = 4.5             # module antenna section length (no copper underneath)
# The module's antenna-end corners (x +-7.7) set its position: they stay inside the board circle (r 28.89 at the
# corner = 1 mm to the wall, ODD JOBS 135, 136). r = hypot(7.7, 27.6) = 28.65.
MODULE_OUTER_R = 27.6                 # antenna end (y)
MODULE_CY = MODULE_OUTER_R - 9.75     # footprint origin (y), 17.85 (the body centre is 0.25 towards the board centre)
MODULE_SHIFT = 14.70 - 15.95          # A0 copper drawn for the WROOM's pre-audit position (kept for designed copper
                                      # of parts that did not move with the module: haptic driver, LRA)
ANTENNA_EDGE_Y = MODULE_CY + 5.25     # antenna section starts here (footprint F.Fab), 23.1
MODULE_TOP_Y = MODULE_CY - 10.25      # body edge towards the board centre, 7.6
MODULE_PIN_ROW_Y = MODULE_CY - 9.55   # pins 16-30, 8.3
NOTCH_W = MODULE_W + 6.0              # board cut-out under the antenna: 3 mm each side
NOTCH_Y = ANTENNA_EDGE_Y              # notch inner edge = antenna boundary (no copper beneath)
NOTCH_FILLET = 1.0                    # milled inner corners, drawn deliberately
ANTENNA_COPPER_SETBACK = 3.0          # the all-layer keep-out reaches this far beyond the notch's sides; towards the
                                      # board it starts 0.6 mm inside the antenna boundary (the module's own pads
                                      # 46-60 and its GND pins are the only copper there)

# Back edge (12 o'clock): USB-C on B.Cu; the two IR LEDs flank it on B.Cu, firing out of the base wall.
USB_ANGLE = 0.0
USB_FRONT_Y = -29.6            # receptacle mating face, 0.6 mm past the board edge
IR_TX_ANGLES = (-21.0, 21.0)

# Centre: face-press switch on F.Cu under the display; the face carrier's boss presses its stem.
PRESS_XY = (0.0, -2.5)
PRESS_SIDE = 'F'

# Base: battery under the PCB, shifted towards 12 o'clock so its can sits >= 12 mm (radial) from the
# antenna and the centre of mass stays near the axis. B.Cu parts may be up to 3.2 mm tall (module);
# a 0.3 mm insulating pad separates the cell from them (ODD JOBS 48).
BATTERY_ENVELOPE = (35.5, 30.0, 5.1)     # w, d, h: PKCELL LP503035 class, 500 mAh, PCM, 10k NTC
# Cell lead: leaves the cell's 3 o'clock end and plugs into J102 (JST SH 3-pin, 2 o'clock on B.Cu,
# opening towards 6 o'clock): ~25 mm of lead, re-terminated to 1 BAT- / 2 NTC / 3 BAT+. A1 keeps the SH: the Gate C
# JST PH side-entry housing stands 4.85 mm over B.Cu (KiCad STEP of S3B-PH-K; the SM4-TB uses the same housing),
# against ZONE_B_MAX_H below, and its mated plug would sit on the cell (owner decision, A1 report).
BATTERY_CENTRE = (0.0, -5.6)         # cell edges y -20.6 / 9.4: 12.05 mm from the antenna boundary (y 21.45)
ZONE_B_MAX_H = 3.2
# Speaker: Same Sky CMS-150803-088S-X8 (15 x 8 x 3 mm, own spring contacts) under the board at 9 o'clock, long
# side along the rim: 1.8 mm from the cell, >= 0.8 mm from the wall at its corners. A round 15 mm part does not
# fit: the crescent beside the 35.5 mm cell is 12.2 mm wide. Its contacts land on pads LS501 (B.Cu).
SPEAKER_SIZE = (8.0, 15.0, 3.0)          # x (radial), y (along the rim), height
SPEAKER_CENTRE = (-23.74, 0.6)
LRA_ANGLE = 232.0            # in the base under the board, 7-8 o'clock: far from IMU, Hall sensors and mic
LRA_R = 21.0                 # centre radius of the 8 mm coin: its steel can is ~7.4 mm from the antenna's corner,
                             # inside the 15 mm metal guideline (the only free spot in the base; RSSI checked at
                             # bring-up)

# Mounting to the top-unit chassis: two M2 screws on the back half (no metal near the antenna,
# ODD JOBS 69) and one plastic locating peg (NPTH, no screw) on the antenna half.
MOUNT_R = 25.0
SCREW_ANGLES = (-48.0, 48.0)
PEG_ANGLE = 225.0
MOUNT_HOLE_D = 2.2           # M2 clearance
PEG_HOLE_D = 2.0             # NPTH for a 1.9 mm printed peg
MOUNT_KEEPOUT_D = 4.6        # copper-free on every layer: metal screw and brass insert (ODD JOBS 68, 69)
# Fasteners (ODD JOBS 68, 70): M2 x 5 pan-head screw (ISO 7045, head d 4.0) from below, through the board into
# an M2 x 3 brass heat-set insert (OD 3.2) in a d 6.0 boss of the top chassis. The insert is set in the printed
# part before assembly, so no heat reaches the board. Part-free areas: the boss on F, the head on B.
BOSS_KEEPOUT_D_F = 6.5
HEAD_KEEPOUT_D_B = 5.5
PEG_KEEPOUT_D = 4.6          # printed d 1.9 peg with a d 4 shoulder on F
PEG_KEEPOUT_D_B = 3.0        # only the peg tip (d 1.9) passes below B
# Edge (ODD JOBS 71, 144, 145): pours and planes stop 0.5 mm from the milled edge; MLCC pads >= 1.5 mm from it.
POUR_EDGE = 0.5
MLCC_EDGE = 1.5
# Breakaway tabs for a panelised order (ODD JOBS 142, 145): two fixed spots, clear of the touch arcs, the antenna
# band, USB-C and the IR LEDs; at each, no copper on any layer within TAB_CLEAR of the edge over TAB_ARC of rim
# (a mouse bite then cuts laminate only).
TAB_ANGLES = (130.0, 328.0)        # 328, not 320: the L3 VSYS band rounds H1's screw ring (312 deg) next to the rim, and a
                                   # tab sector over that ring's angles necked it to 0.27 mm (electrical audit 3, I-1)
TAB_ARC, TAB_CLEAR = 4.0, 1.5
# Service-field names (silk.py): each probe pad's name has a via-free spot of its own, so no router, stitching or
# pad via lands where the name has to go (ODD JOBS 103). ref: (name, side) with side 'right' (upright beside the
# pad) or 'above' (level, over it). The pad positions come from placement.py. Rows 2.8 mm apart.
FIELD_NAMES = {'TP2': ('GND', 'right'), 'TP3': ('3V3', 'right'), 'TP4': ('SYS', 'right'), 'TP5': ('BAT', 'right'),
               'TP10': ('SCL', 'right'), 'TP1': ('GND', 'right'), 'TP16': ('GND', 'right'),
               'TP9': ('SDA', 'right'), 'TP8': ('BOOT', 'right'), 'TP7': ('RST', 'right'),
               # A1 fixture rows (Gate C pads): wake inputs, backlight reference, amplifier enable, frame sync,
               # charger factory mode and /CE, the spares
               'TP17': ('BL', 'right'), 'TP18': ('AMP', 'right'), 'TP19': ('TE', 'right'), 'TP20': ('PRS', 'right'),
               'TP21': ('HLA', 'right'), 'TP22': ('TSMR', 'right'), 'TP23': ('CE', 'right'), 'TP24': ('IO21', 'right'),
               'TP25': ('IO26', 'right')}
FIELD_TEXT = 0.8                   # the names' height: the rows are 2.8 mm apart
# The back's maker mark and identity (silk.py): a reserved via-free spot, so routing changes cannot take it. Over the
# cell (seen with the base off, at bring-up and service).
B_IDENT_AT = (-0.2, -6.2)          # mark centre; 'MAO A1' and the date below it (A1: 2 mm east of A0's, for the
                                   # backlight sink)
B_IDENT_KEEPOUT = [(-2.65, -8.2, 2.15, -4.2), (-3.5, -4.2, 3.1, -1.0)]   # mark; text lines (vias only)
VSYS_CORNER_KEEPOUT = None         # A0 only (the L3 VSYS branch's turn into the AW9364 bar)


def field_name_box(x, y, name, side, dy=0.0):
    """Board box a service-field name takes beside (or over) its pad at (x, y), slid dy along the pad."""
    r = 0.6 if name in ('GND', '3V3', 'SYS', 'BAT', 'VBUS') else 0.5
    L = 0.72 * len(name) + 0.25           # the stroke font at 0.8 mm runs 0.6-0.7 mm a glyph (BOOT 2.97 mm long)
    y += dy
    if side == 'right':                    # the name's box (0.95 mm across) and its 0.05 mm gap to a via
        return (x + r + 0.15, y - L / 2 - 0.1, x + r + 1.3, y + L / 2 + 0.1)
    return (x - L / 2 - 0.1, y - r - 1.3, x + L / 2 + 0.1, y - r - 0.15)


def polar(r, angle_deg):
    """Board coordinates of a point at radius r, angle clockwise from 12 o'clock."""
    a = math.radians(angle_deg)
    return (r * math.sin(a), -r * math.cos(a))
