"""MAO_MAIN A0 mechanical datums (board millimetres, origin = puck axis, +x right, +y towards
6 o'clock, viewed from the face). Every placement script and the mechanical doc read these
numbers; nothing is eyeballed (ODD JOBS 72/73).

Concept (docs/hardware/mao-mechanical.md): a round puck. A rotating ring dial surrounds the round
face. The top unit (cover window, display, ring bearing, this PCB) floats a little above the base
and presses a switch at the PCB centre. Battery and speaker live in the base.

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
DISPLAY_OUTLINE_R = 19.1    # panel outline radius incl. frame (LH128R-class 38.1 mm)
DISPLAY_STANDOFF = 2.2      # panel rear to PCB top surface (foam + carrier)
ZONE_A_MAX_H = 1.2          # max part height on F.Cu under the panel (ODD JOBS 75)

# Ring dial: rotating ring, the face window is fixed inside it.
RING_ID = 46.0
RING_OD = PUCK_OD
RING_POLE_PAIRS = 15        # 30 poles: 30 detents / 15 quadrature cycles per rev, as the LCDkit EC11
RING_MAGNET_R = 25.5        # mean radius of the pole track above the PCB
HALL_R = 25.5               # Hall sensors sit under the pole track
HALL_GAP = 1.5              # magnet face to sensor top (through the ring's lower lip)
# Quadrature: electrical period = 360/15 = 24 deg; 90 deg electrical = 6 deg mechanical.
# Two sensors 30 deg apart (= 360 + 90 electrical) give A/B in quadrature with room for both packages.
HALL_ANGLES = (112.0, 142.0)    # degrees, measured clockwise from 12 o'clock

# Sensor window: an annulus of the cover window between the panel and the ring.
WINDOW_ANNULUS = (DISPLAY_OUTLINE_R + 0.6, RING_ID / 2 - 0.5)   # radii on F.Cu that see out
TOF_ANGLE = -32.0           # 11 o'clock-ish ("forehead"), clockwise from 12
ALS_ANGLE = 32.0            # 1 o'clock-ish
MIC_ANGLE = 155.0           # top-port mic under the ring/window gap, away from the speaker

# Antenna: WROOM-1 on B.Cu at 6 o'clock, long axis radial, antenna over a notch in the board edge.
MODULE_ANGLE = 180.0        # 6 o'clock
MODULE_W, MODULE_L = 18.0, 25.5
ANTENNA_L = 6.4             # module antenna section length (no copper underneath)
MODULE_OUTER_R = PCB_R - 0.3          # antenna end just inside the board circle
NOTCH_W = MODULE_W + 6.0              # board cut-out under the antenna: 3 mm each side
NOTCH_DEPTH = ANTENNA_L + 0.8         # from the board edge towards the centre
ANTENNA_COPPER_SETBACK = 3.0          # pours stop this far from the notch on every layer

# Back edge (12 o'clock): USB-C on B.Cu, IR emitter + receiver side-looking at the edge.
USB_ANGLE = 0.0
IR_TX_ANGLE = -22.0
IR_RX_ANGLE = 22.0

# Touch electrodes. LEFT/RIGHT are copper arcs at the board rim sensing through the ring;
# TOP (window border) and REAR (base) are spring contacts to enclosure electrodes.
TOUCH_ARC_SPAN = 50.0        # degrees per side electrode
TOUCH_LEFT_ANGLE = -90.0     # 9 o'clock
TOUCH_RIGHT_ANGLE = 90.0     # 3 o'clock
TOUCH_ARC_R = (PCB_R - 2.6, PCB_R - 0.4)

# Centre: face-press switch on B.Cu, actuated by a nub in the base.
PRESS_XY = (0.0, -3.0)

# Base: battery in the 12 o'clock half, speaker and LRA away from antenna and Hall sensors.
BATTERY_ENVELOPE = (40.0, 22.0, 5.2)     # w, d, h  (e.g. 402245-class 1S LiPo)
BATTERY_CENTRE = (0.0, -13.5)
ZONE_B_MAX_H = 1.0           # max part height on B.Cu over the battery (ODD JOBS 48/75)
SPEAKER_D = 15.0
SPEAKER_ANGLE = 245.0        # ~8 o'clock in the base
LRA_ANGLE = 70.0             # on the PCB underside, ~2 o'clock, far from the IMU

# Mounting to the top-unit chassis: two M2 screws on the back half (no metal near the antenna,
# ODD JOBS 69) and one plastic locating peg (NPTH, no screw) on the antenna half.
MOUNT_R = 25.0
SCREW_ANGLES = (-30.0, 30.0)
PEG_ANGLE = 225.0
MOUNT_HOLE_D = 2.2           # M2 clearance
PEG_HOLE_D = 2.0             # NPTH for a 1.9 mm printed peg
MOUNT_KEEPOUT_D = 4.6        # screw head / insert boss on both faces (ODD JOBS 68)


def polar(r, angle_deg):
    """Board coordinates of a point at radius r, angle clockwise from 12 o'clock."""
    a = math.radians(angle_deg)
    return (r * math.sin(a), -r * math.cos(a))
