"""MAO 'wheel stone' enclosure: a round river stone whose upper shoulder is the dial.

Coordinates in mm: origin on the dial axis, +x right (3 o'clock), +y towards 6 o'clock (the person), +z up; the
base stands on z = 0. Board frame as in hardware/mao (KiCad y down = +y here); angles in this file are measured
from +x towards +y (clockwise seen from above, 0 = 3 o'clock, 90 = 6 o'clock, 270 = 12 o'clock).

The face stack is the board's own (design/mechanical.py): switch stems 1.5 mm, panel rear 2.7 mm and window
underside 4.9 mm above F.Cu, 0.25 mm press travel.

Parts (each a field f < 0 inside, on one voxel grid):
  base     the stone below the joint: floor, cell rails, board posts with pins, USB-C tunnel, IR windows,
           speaker grille, the ODD JOBS mark underneath
  frame    fixed: a bearing wall round the board, the 0.6 mm deck the wheel rides on (over the Hall pair),
           ribs holding the board rim down; screwed to the base with 3 x M2 from below
  wheel    the stone's upper shoulder, turning: solid (about 14 g in PETG, a little flywheel), riding the deck on its
           flat underside, which carries the 30-pole strip at r 26.3 over the Hall pair; a skirt hides the joint;
           fine grip grooves on the slope; its inner edge steps down under the bezel lip
  bezel    fixed, fitted last: a thin tube inside the deck with a lip that holds the wheel down (outer edge) and
           the window up (inner edge); three snap tongues catch under the deck
  carrier  the face, moving: a tray under the panel resting on the three press switches, six posts up to the
           window (the sensors and the tail pass between them), the tail pocket and the well the loop turns in,
           a key into the bezel against rotation
  window   1 mm clear PMMA disc bonded to the posts; the switch springs push it up against the bezel lip
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
BRAND = os.path.join(HERE, '..', '..', 'brand', 'odd-jobs-symbol.json')

# ----------------------------------------------------------------- the stone
R_STONE = 41.0
Z_MID, H_TOP = 7.0, 20.0           # widest at z 7, crown 20
U_TOP, U_BOT = 22.0, 8.0
WALL, FLOOR = 2.0, 1.4             # the floor is thinner under the cell (its underside sits at z 1.6)
Z_JOIN = 11.8                       # the wheel starts here at the outside (USB plug stays in the base)

# ----------------------------------------------------------------- A1 stack (design/mechanical.py)
BOARD_R, Z_PCB0, Z_PCB1 = 29.0, 9.4, 11.0
CELL = (-17.75, 17.75, -20.6, 9.4, 1.6, 6.0)                       # x0 x1 y0 y1 z0 z1
SPEAKER = (-23.74, 0.6, 8.0, 15.0, 3.0)                            # centre, radial, along the rim, height (B side)
PINS = [(18.6, -16.7), (-18.6, -16.7)]                             # H2, H1
SWITCHES = [(13.449, -1.177), (-6.022, 12.915), (-7.743, -11.059)]  # SW301..303: the even triangle (2026-10-07)
Z_SW_TOP = Z_PCB1 + 1.5
PRESS_TRAVEL = 0.25
Z_PANEL = Z_PCB1 + 2.7              # panel rear plane
PANEL_R, LEDGE = 17.8, 2.14         # round outline; the glass ledge at 9 o'clock under the tail
Z_GLASS = Z_PANEL + 2.0
Z_WIN0 = Z_PCB1 + 4.9
Z_WIN1 = Z_WIN0 + 1.0
TAIL_HALF = 6.0                     # tail 9.5 wide (+-0.1), the 13.05 mm section stays in the face
TAIL_SLOT_X = (-19.3, -18.3)
TAIL_WELL = (5.6, -5.75, 9.9, 5.75)                               # shortened 2026-10-07, clear of SW301
USB = (0.0, -29.6, 7.8)
IR_ANGLES = (-120.0, -60.0)         # side-firing LEDs under the board, 11 and 1 o'clock
Z_IR = 8.6

# frame
WALL_R0, WALL_R1 = 31.4, 33.4
Z_DECK0, Z_DECK1 = 12.0, 12.6
DECK_R0 = 24.1
JOIN = [(32.4, 0.0), (32.4, 150.0), (32.4, 300.0)]   # M2 inserts in the frame wall (clear of USB 270, antenna 90)
RIBS = (60.0, 120.0, 210.0, 330.0)

# bezel
TUBE_R0, TUBE_R1 = 23.1, 24.0
LIP_R0, LIP_R1 = 22.4, 25.4
Z_LIP0, Z_LIP1 = Z_WIN1, Z_WIN1 + 0.6
HOOKS = (70.0, 190.0, 300.0)        # snap tongues, clear of the parts under the deck edge
IR_RX = (0.0, 9.0, Z_PCB1 + 4.4)    # angle, half width (deg), top of the notch: U503 is 4.0 mm tall

# wheel
WHEEL_R_IN = 24.6
Z_FLANGE0 = 12.75                   # the wheel's flat underside over the deck (0.15 mm)
STRIP_R, STRIP_W, STRIP_D = 26.3, 3.2, 1.6
WHEEL_STEP_R = LIP_R1 + 0.3         # the inner edge steps down under the lip
Z_WHEEL_STEP = Z_LIP0 - 0.3

# carrier
TRAY_R = 19.4
Z_TRAY0 = Z_SW_TOP + 0.1
POSTS = (40.0, 100.0, 140.0, 215.0, 285.0, 320.0)   # clear of the IR receiver (0), ToF (242) and the tail (180)
POST_R0, POST_R1, POST_HALF = 19.2, 22.4, 1.2
KEY_ANGLE = 100.0
WINDOW_R = 22.9


def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def top_h(u):
    return Z_MID + (H_TOP - Z_MID) * np.sqrt(np.clip(1 - (1 - np.clip(u, 0, U_TOP) / U_TOP) ** 2, 0, 1))


def bot_h(u):
    return Z_MID - Z_MID * np.sqrt(np.clip(1 - (1 - np.clip(u, 0, U_BOT) / U_BOT) ** 2, 0, 1))


def cyl(X, Y, Z, cx, cy, r, z0, z1):
    return np.maximum(np.hypot(X - cx, Y - cy) - r, np.maximum(z0 - Z, Z - z1))


def box(X, Y, Z, x0, x1, y0, y1, z0, z1):
    return np.maximum(np.maximum(np.maximum(x0 - X, X - x1), np.maximum(y0 - Y, Y - y1)), np.maximum(z0 - Z, Z - z1))


def annulus(r, Z, r0, r1, z0, z1):
    return np.maximum(np.maximum(r0 - r, r - r1), np.maximum(z0 - Z, Z - z1))


def sector(X, Y, deg, half_mm):
    """Distance from the radial band of half width `half_mm` along direction `deg` (only on that side)."""
    a = math.radians(deg)
    along = X * math.cos(a) + Y * math.sin(a)
    across = np.abs(-X * math.sin(a) + Y * math.cos(a))
    return np.maximum(across - half_mm, -along)


def polar(rr, deg):
    return rr * math.cos(math.radians(deg)), rr * math.sin(math.radians(deg))


class WheelStone:
    def __init__(self, res=0.3):
        self.res = res
        self.xs = np.arange(-44.0 + 0.29 * res, 44.0 + 1e-9, res)
        self.ys = np.arange(-44.0 + 0.41 * res, 44.0 + 1e-9, res)
        self.zs = np.arange(-1.5 + 0.37 * res, 22.5 + 1e-9, res)

    def grid(self):
        X2, Y2 = np.meshgrid(self.xs.astype(np.float32), self.ys.astype(np.float32))
        return X2[None], Y2[None], self.zs.astype(np.float32)[:, None, None]

    def fields(self, pressed=False):
        X, Y, Z = self.grid()
        r = np.hypot(X, Y)
        u = np.clip(R_STONE - r, 0, None)
        top, bot = top_h(u), bot_h(u)
        stone = np.maximum(np.maximum(r - R_STONE, Z - top), bot - Z)
        inner_bot = FLOOR + (bot + WALL - FLOOR) * sstep(0.0, 0.6, bot)
        inner = np.maximum(np.maximum(r - (R_STONE - WALL), Z - (top - WALL)), inner_bot - Z)

        # ------------------------------------------------------------- base
        base = np.maximum(np.maximum(stone, -inner), Z - Z_JOIN)
        base = np.minimum(base, np.maximum(annulus(r, Z, WALL_R0, R_STONE - WALL + 0.2, 0.0, 2.0), stone))  # ledge
        x0, x1, y0, y1, z0, z1 = CELL
        for px, py in PINS:                                         # board posts with locating pins: beside the
            sx = math.copysign(1.0, px)                             # cell up to its top, a full collar above it
            post = cyl(X, Y, Z, px + sx * 1.4, py, 2.6, 0.0, Z_PCB0)
            post = np.maximum(post, (x1 + 0.3 - X) if px > 0 else (X - (x0 - 0.3)))
            post = np.minimum(post, cyl(X, Y, Z, px, py, 2.0, z1 + 0.3, Z_PCB0))
            base = np.minimum(base, np.maximum(post, stone))
            base = np.minimum(base, cyl(X, Y, Z, px, py, 0.95, Z_PCB0 - 0.1, Z_PCB1 + 0.8))
        for rr, deg in JOIN:                                        # screw bores up into the frame wall
            jx, jy = polar(rr, deg)
            base = np.maximum(base, -cyl(X, Y, Z, jx, jy, 1.15, -2, 2.2))
            base = np.maximum(base, -cyl(X, Y, Z, jx, jy, 2.2, -2, 1.4))
        # cell rails
        rails = np.minimum(box(X, Y, Z, x0 - 1.8, x0 - 0.3, y0, y1, 0, z0 + 2.5),
                           box(X, Y, Z, x1 + 0.3, x1 + 1.8, y0, y1, 0, z0 + 2.5))
        base = np.minimum(base, np.maximum(rails, stone))
        ux, uy, uz = USB                                            # USB-C cable tunnel at 12 o'clock
        base = np.maximum(base, -box(X, Y, Z, ux - 6.2, ux + 6.2, -60, uy - 0.2, uz - 3.4, uz + 3.4))
        for deg in IR_ANGLES:                                       # IR windows (side-firing LEDs)
            dx, dy = math.cos(math.radians(deg)), math.sin(math.radians(deg))
            along = X * dx + Y * dy
            across = np.hypot(-X * dy + Y * dx, Z - Z_IR)
            base = np.maximum(base, -np.maximum(across - 1.5, 28.0 - along))
        for gy in np.arange(-6.0, 6.01, 2.0):                       # speaker grille (9 o'clock)
            for gx in (-27.0, -24.5, -22.0):
                base = np.maximum(base, -cyl(X, Y, Z, gx, gy + 0.6, 0.55, -2, 3.0))
        base = np.maximum(base, -self.mark_deboss(Z))

        # ------------------------------------------------------------- frame
        wall = annulus(r, Z, WALL_R0, WALL_R1, 2.0, Z_DECK1)
        deck = annulus(r, Z, DECK_R0, WALL_R1, Z_DECK0, Z_DECK1)
        frame = np.minimum(wall, deck)
        for rr, deg in JOIN:                                        # insert bosses, below the wheel skirt
            jx, jy = polar(rr, deg)
            frame = np.minimum(frame, cyl(X, Y, Z, jx, jy, 2.4, 2.0, Z_JOIN - 1.0))
            frame = np.maximum(frame, -cyl(X, Y, Z, jx, jy, 1.6, 1.0, 6.6))
        for deg in RIBS:                                            # ribs pressing the board rim down
            bx, by = polar(BOARD_R - 1.0, deg)
            frame = np.minimum(frame, cyl(X, Y, Z, bx, by, 1.1, Z_PCB1 + 0.05, Z_DECK0 + 0.1))
        frame = np.maximum(frame, -box(X, Y, Z, -6.4, 6.4, -60, -28.0, 2.0, 11.4))   # USB receptacle and cable
        for deg in IR_ANGLES:
            dx, dy = math.cos(math.radians(deg)), math.sin(math.radians(deg))
            along = X * dx + Y * dy
            across = np.hypot(-X * dy + Y * dx, Z - Z_IR)
            frame = np.maximum(frame, -np.maximum(across - 1.6, 26.0 - along))

        # ------------------------------------------------------------- wheel
        # solid (a sealed hollow under a nearly flat roof does not print); clear of the deck under the flange
        wheel = np.maximum(stone, np.maximum(Z_JOIN + 0.2 - Z, WHEEL_R_IN - r))
        wheel = np.maximum(wheel, -np.maximum(r - (WALL_R1 + 0.4), Z - Z_FLANGE0))
        # the strip groove, open into the bore (a skin under 0.2 mm would not print); the strip is self-adhesive
        wheel = np.maximum(wheel, -np.maximum(np.maximum(WHEEL_R_IN - 1.0 - r, r - (STRIP_R + STRIP_W / 2)),
                                              Z - (Z_FLANGE0 + STRIP_D)))
        wheel = np.maximum(wheel, -np.maximum(r - WHEEL_STEP_R, Z_WHEEL_STEP - Z))      # step under the lip
        rib = 0.18 * np.cos(120 * np.arctan2(Y, X)) * sstep(30.0, 34.0, r)
        wheel = np.maximum(wheel, stone + np.clip(rib, 0, None))   # shallow grip grooves in the slope

        # ------------------------------------------------------------- bezel
        tube = annulus(r, Z, TUBE_R0, TUBE_R1, Z_DECK0, Z_LIP1)
        foot = annulus(r, Z, TUBE_R1, WHEEL_R_IN - 0.15, Z_DECK1, Z_DECK1 + 0.4)
        lip = annulus(r, Z, LIP_R0, LIP_R1, Z_LIP0, Z_LIP1)
        bezel = np.minimum(np.minimum(tube, foot), lip)
        for deg in HOOKS:
            half = 1.5
            tongue_z0, slit_top = Z_DECK0 - 0.55, Z_DECK1 + 2.6
            band = np.maximum(sector(X, Y, deg, half), np.maximum(TUBE_R0 - r, r - TUBE_R1))
            bezel = np.minimum(bezel, np.maximum(band, np.maximum(tongue_z0 - Z, Z - (Z_DECK0 + 0.3))))   # tongue below
            barb = np.maximum(sector(X, Y, deg, half), np.maximum(TUBE_R1 - 0.2 - r, r - (DECK_R0 + 0.45)))
            ramp = (Z - tongue_z0) - (r - TUBE_R1) * 0.9                # lead-in: the barb grows outward going up
            bezel = np.minimum(bezel, np.maximum(np.maximum(barb, -ramp), np.maximum(tongue_z0 - Z, Z - (Z_DECK0 - 0.05))))
            for side in (-1, 1):                                     # slits free the tongue (and the foot over it)
                slit = np.maximum(np.abs(np.abs(-X * math.sin(math.radians(deg)) + Y * math.cos(math.radians(deg))) - (half + 0.2)) - 0.2,
                                  np.maximum(-(X * math.cos(math.radians(deg)) + Y * math.sin(math.radians(deg))) + 20.0, Z - slit_top))
                bezel = np.maximum(bezel, -slit)
            bezel = np.maximum(bezel, -np.maximum(np.maximum(sector(X, Y, deg, half + 0.4), TUBE_R1 - 0.05 - r),
                                                  np.maximum(Z_DECK1 - 0.1 - Z, Z - (Z_DECK1 + 0.5))))  # no foot on it
        a = IR_RX                                                    # notch over the IR receiver
        bezel = np.maximum(bezel, -np.maximum(sector(X, Y, a[0], polar(TUBE_R1, a[1])[1]), np.maximum(Z_DECK0 - 1 - Z, Z - a[2])))
        key_slot = np.maximum(np.maximum(sector(X, Y, KEY_ANGLE, 1.0), np.maximum(TUBE_R0 - 0.1 - r, r - (TUBE_R0 + 0.45))),
                              np.maximum(Z_DECK0 - 1 - Z, Z - Z_WIN0))
        bezel = np.maximum(bezel, -key_slot)

        # ------------------------------------------------------------- carrier, window, panel (move when pressed)
        dz = -PRESS_TRAVEL if pressed else 0.0
        Zc = Z - dz
        tray = cyl(X, Y, Zc, 0, 0, TRAY_R, Z_TRAY0, Z_PANEL)
        tray = np.maximum(tray, -box(X, Y, Zc, -TRAY_R - 1, 8.6, -TAIL_HALF, TAIL_HALF, Z_PANEL - 0.4, Z_PANEL + 1))    # tail pocket
        wx0, wy0, wx1, wy1 = TAIL_WELL
        cup = box(X, Y, Zc, wx0 - 0.6, wx1 + 0.6, wy0 - 0.6, wy1 + 0.6, Z_PANEL - 2.4, Z_PANEL)
        cup = np.maximum(cup, -box(X, Y, Zc, wx0, wx1, wy0, wy1, Z_PANEL - 1.8, Z_PANEL + 1))
        tray = np.maximum(np.minimum(tray, cup), -box(X, Y, Zc, wx0, wx1, wy0, wy1, Z_PANEL - 1.8, Z_PANEL + 1))
        tray = np.maximum(tray, -box(X, Y, Zc, -TRAY_R - 2, TAIL_SLOT_X[1] + 0.6, -TAIL_HALF, TAIL_HALF, 0, 30))      # tail drop
        rim = annulus(r, Zc, PANEL_R + 0.3, PANEL_R + 1.0, Z_PANEL, Z_GLASS - 0.3)
        rim = np.maximum(rim, -box(X, Y, Zc, -40, -12.0, -TAIL_HALF - 0.6, TAIL_HALF + 0.6, 0, 30))                    # ledge, tail
        carrier = np.minimum(tray, rim)
        carrier = np.maximum(carrier, -box(X, Y, Zc, 18.55, 40, -2.95, 2.95, 0, 30))           # round the IR receiver
        for sx, sy in SWITCHES:                                     # bosses onto the switch stems
            carrier = np.minimum(carrier, cyl(X, Y, Zc, sx, sy, 1.0, Z_SW_TOP, Z_TRAY0 + 0.2))
        for deg in POSTS:
            post = np.maximum(sector(X, Y, deg, POST_HALF), np.maximum(np.maximum(POST_R0 - 0.6 - r, r - POST_R1),
                                                                        np.maximum(Z_TRAY0 - Zc, Zc - Z_WIN0)))
            carrier = np.minimum(carrier, post)
        key = np.maximum(np.maximum(sector(X, Y, KEY_ANGLE, 0.7), np.maximum(POST_R1 - 0.1 - r, r - (TUBE_R0 + 0.3))),
                         np.maximum(Z_WIN0 - 2.5 - Zc, Zc - (Z_WIN0 - 0.2)))
        carrier = np.minimum(carrier, key)
        window = np.maximum(r - WINDOW_R, np.maximum(Z_WIN0 - Zc, Zc - Z_WIN1))
        panel = np.minimum(cyl(X, Y, Zc, 0, 0, PANEL_R, Z_PANEL + 0.45, Z_GLASS),
                           box(X, Y, Zc, -PANEL_R - LEDGE, -14.0, -TAIL_HALF + 0.5, TAIL_HALF - 0.5, Z_PANEL + 0.45, Z_PANEL + 1.2))

        return {'base': base, 'frame': frame, 'wheel': wheel, 'bezel': bezel, 'carrier': carrier, 'window': window,
                'panel': panel, 'stone': stone}

    def board(self):
        """The board stack: PCB, every part body with a 3D model, the cell and the speaker, as boxes
        (name, [(x0, x1, y0, y1, z0, z1), ...]); the PCB is a disc with the two pin holes."""
        out = []
        parts = json.load(open(os.path.join(HERE, 'board_parts.json')))['parts']
        for p in parts:
            z0, z1 = (Z_PCB1, Z_PCB1 + p['height']) if p['side'] == 'F' else (Z_PCB0 - p['height'], Z_PCB0)
            out.append((p['ref'], [(x0, x1, y0, y1, z0, z1) for x0, y0, x1, y1 in p['boxes']]))
        out.append(('cell', [CELL]))
        cx, cy, w, l, h = SPEAKER
        out.append(('speaker', [(cx - w / 2, cx + w / 2, cy - l / 2, cy + l / 2, Z_PCB0 - h, Z_PCB0)]))
        return out

    def pcb(self):
        X, Y, Z = self.grid()
        f = cyl(X, Y, Z, 0, 0, BOARD_R, Z_PCB0, Z_PCB1)
        for px, py in PINS:
            f = np.maximum(f, -cyl(X, Y, Z, px, py, 1.1, 0, 20))
        return f

    def mark_deboss(self, Z):
        d = json.load(open(BRAND))
        res = self.res
        im = Image.new('L', (len(self.xs), len(self.ys)), 0)
        s = 18.0 / 671.0
        cx, cy = 337.5, 274.5
        ImageDraw.Draw(im).polygon([((-(px - cx) * s - self.xs[0]) / res, ((py - cy) * s + 8.0 - self.ys[0]) / res)
                                    for px, py in d['points']], fill=255)
        m = np.array(im) > 0
        d2 = (ndimage.distance_transform_edt(~m) - ndimage.distance_transform_edt(m)) * res
        return np.maximum(d2[None], Z - 0.5)

    def report(self):
        us = np.linspace(0, U_TOP, 400)
        return {
            'size (mm)': 'D %.0f x %.1f' % (2 * R_STONE, H_TOP),
            'wheel OD at the joint (mm)': round(2 * (R_STONE - float(np.interp(Z_JOIN, top_h(us), us))), 1),
            'wheel bore / step under the lip (mm)': '%.1f / %.1f' % (2 * WHEEL_R_IN, 2 * WHEEL_STEP_R),
            'wheel height at the outside (mm)': round(H_TOP - Z_JOIN, 1),
            'face well: wheel inner edge above the lip (mm)': round(float(top_h(R_STONE - WHEEL_STEP_R)) - Z_LIP1, 1),
            'strip face to Hall top (mm)': round(Z_FLANGE0 - (Z_PCB1 + 0.4), 2),
            'window: diameter, under the lip (mm)': '%.1f, %.1f' % (2 * WINDOW_R, WINDOW_R - LIP_R0),
            'window underside above F.Cu (mm)': round(Z_WIN0 - Z_PCB1, 2),
            'snap barb under the deck (mm)': round(DECK_R0 + 0.45 - TUBE_R1, 2),
        }


# ---------------------------------------------------------------- checks
SWITCH_CONTACT = [{'carrier', 'SW301'}, {'carrier', 'SW302'}, {'carrier', 'SW303'}]   # the bosses press the stems


def clash(ws, depth=0.06):
    """Every pair of bodies (enclosure parts, board, its parts, cell, speaker) that overlap deeper than `depth` mm,
    at rest and pressed. Faces that only touch (the posts under the board, the window on the lip) do not count;
    the carrier's bosses on the switch stems are the one designed overlap when pressed."""
    vox = ws.res ** 3
    xs, ys, zs = ws.xs, ws.ys, ws.zs
    found = []
    for state in ('rest', 'pressed'):
        F = ws.fields(pressed=state == 'pressed')
        F.pop('stone')
        F['pcb'] = ws.pcb()
        inside = {n: f < -depth for n, f in F.items()}
        names = list(F)
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                n = int(np.count_nonzero(inside[a] & inside[b]))
                if n:
                    found.append({'state': state, 'a': a, 'b': b, 'mm3': round(n * vox, 3)})
        for ref, boxes in ws.board():
            for x0, x1, y0, y1, z0, z1 in boxes:
                i0, i1 = np.searchsorted(xs, x0 + depth), np.searchsorted(xs, x1 - depth)
                j0, j1 = np.searchsorted(ys, y0 + depth), np.searchsorted(ys, y1 - depth)
                k0, k1 = np.searchsorted(zs, z0 + depth), np.searchsorted(zs, z1 - depth)
                for a in names:
                    if a == 'pcb' or {a, ref} in SWITCH_CONTACT:
                        continue
                    n = int(np.count_nonzero(inside[a][k0:k1, j0:j1, i0:i1]))
                    if n:
                        found.append({'state': state, 'a': a, 'b': ref, 'mm3': round(n * vox, 3)})
    return found


if __name__ == '__main__':
    import sys
    res = float(sys.argv[1]) if len(sys.argv) > 1 else 0.2
    ws = WheelStone(res)
    for k, v in ws.report().items():
        print('%-48s %s' % (k, v))
    out = clash(ws)
    for c in out:
        print('CLASH', c)
    print('clash check:', len(out), 'findings at', res, 'mm')
