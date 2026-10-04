"""MAO_MAIN A0 mechanical datums (board millimetres, origin = puck axis, +x right, +y towards
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
DISPLAY_STANDOFF = 2.2      # panel rear to PCB top surface (foam + carrier)
ZONE_A_MAX_H = 1.2          # max part height on F.Cu under the panel (ODD JOBS 75)

# Ring dial: rotating ring, the face window is fixed inside it.
RING_ID = 48.0              # 6 mm sensor band between panel and ring (IR receiver is 5 mm wide)
RING_OD = PUCK_OD
RING_POLE_PAIRS = 15        # 30 poles: 30 detents / 15 quadrature cycles per rev, as the LCDkit EC11
RING_MAGNET_R = 26.3        # mean radius of the pole track above the PCB
HALL_R = 26.3               # Hall sensors sit under the pole track
HALL_GAP = 1.5              # magnet face to sensor top (through the ring's lower lip)
# Quadrature: pole pitch 12 deg, electrical period 24 deg; half a pole pitch (6 deg = 2.76 mm at
# r 26.3) is 90 deg electrical. Both packages fit side by side at that spacing.
HALL_ANGLES = (120.0, 126.0)    # degrees, clockwise from 12 o'clock: between the right touch arc and the antenna band

# Sensor window: an annulus of the cover window between the panel and the ring.
DISPLAY_OUTLINE_R = 17.8   # LCM 35.6 mm round outline (the FPC ear adds 2.5 mm at 6 o'clock)
WINDOW_ANNULUS = (DISPLAY_OUTLINE_R + 0.5, RING_ID / 2 - 0.5)   # radii on F.Cu that see out
SENSOR_R = 21.2             # centre radius of the window-border sensors
TOF_ANGLE = -28.0           # 11 o'clock ("forehead"), clockwise from 12
ALS_ANGLE = 28.0            # 1 o'clock
IR_RX_ANGLE = 90.0          # 3 o'clock, top-view receiver under the border (12 o'clock is the USB-C shell)
MIC_ANGLE = 106.0           # bottom-port mic on B.Cu, port up through the board into the window border, far from speaker and buck-boost

# Antenna: WROOM-1 on B.Cu at 6 o'clock, long axis radial, antenna over a notch in the board edge.
MODULE_ANGLE = 180.0        # 6 o'clock
MODULE_W, MODULE_L = 18.0, 25.5
ANTENNA_L = 6.4             # module antenna section length (no copper underneath)
MODULE_OUTER_R = PCB_R - 0.3          # antenna end just inside the board circle
MODULE_CY = MODULE_OUTER_R - MODULE_L / 2          # module centre (y), 15.95
ANTENNA_EDGE_Y = MODULE_CY + 6.75                  # antenna section starts here (footprint F.Fab)
NOTCH_W = MODULE_W + 6.0              # board cut-out under the antenna: 3 mm each side
NOTCH_Y = ANTENNA_EDGE_Y              # notch inner edge = antenna boundary (no copper beneath)
NOTCH_FILLET = 1.0                    # milled inner corners, drawn deliberately
ANTENNA_COPPER_SETBACK = 3.0          # pours stop this far from the notch on every layer

# Back edge (12 o'clock): USB-C on B.Cu; the two IR LEDs flank it on B.Cu, firing out of the base wall.
USB_ANGLE = 0.0
USB_FRONT_Y = -29.6            # receptacle mating face, 0.6 mm past the board edge
IR_TX_ANGLES = (-21.0, 21.0)

# Touch electrodes. LEFT/RIGHT are copper arcs at the board rim sensing through the ring;
# TOP (window border) and REAR (base) are spring contacts to enclosure electrodes.
TOUCH_ARC_SPAN = 50.0        # degrees per side electrode
TOUCH_LEFT_ANGLE = -90.0     # 9 o'clock
TOUCH_RIGHT_ANGLE = 90.0     # 3 o'clock
TOUCH_ARC_R = (PCB_R - 2.6, PCB_R - 0.4)

# Centre: face-press switch on F.Cu under the display; the face carrier's boss presses its stem.
PRESS_XY = (0.0, -2.5)
PRESS_SIDE = 'F'

# Base: battery under the PCB, shifted towards 12 o'clock so its can sits >= 12 mm (radial) from the
# antenna and the centre of mass stays near the axis. B.Cu parts may be up to 3.2 mm tall (module);
# a 0.3 mm insulating pad separates the cell from them (ODD JOBS 48).
BATTERY_ENVELOPE = (35.5, 30.0, 5.1)     # w, d, h: PKCELL LP503035 class, 500 mAh, PCM, 10k NTC
# Cell lead: leaves the cell's 3 o'clock end and plugs into J102 (JST SH 3-pin, 2 o'clock on B.Cu,
# opening towards 6 o'clock): ~25 mm of lead, re-terminated to 1 BAT- / 2 NTC / 3 BAT+.
BATTERY_CENTRE = (0.0, -5.0)
ZONE_B_MAX_H = 3.2
SPEAKER_D = 15.0
SPEAKER_ANGLE = 270.0        # 9 o'clock in the base, left crescent beside the cell
LRA_ANGLE = 232.0            # on the PCB underside, 7-8 o'clock: far from IMU, Hall sensors and mic

# Mounting to the top-unit chassis: two M2 screws on the back half (no metal near the antenna,
# ODD JOBS 69) and one plastic locating peg (NPTH, no screw) on the antenna half.
MOUNT_R = 25.0
SCREW_ANGLES = (-48.0, 48.0)
PEG_ANGLE = 225.0
MOUNT_HOLE_D = 2.2           # M2 clearance
PEG_HOLE_D = 2.0             # NPTH for a 1.9 mm printed peg
MOUNT_KEEPOUT_D = 4.6        # screw head / insert boss on both faces (ODD JOBS 68)


def polar(r, angle_deg):
    """Board coordinates of a point at radius r, angle clockwise from 12 o'clock."""
    a = math.radians(angle_deg)
    return (r * math.sin(a), -r * math.cos(a))
