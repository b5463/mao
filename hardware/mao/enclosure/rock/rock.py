"""MAO 'rock' enclosure: a smooth, slightly irregular river stone around the A1 board.

Coordinates in mm: origin on the dial axis, +x right (3 o'clock), +y towards
6 o'clock (the person), +z up; the base stands on z = 0.

Every part is a field f < 0 inside, on one voxel grid; render_rock.py raymarches
them and export_rock.py turns them into STL. Run (Python with numpy, scipy,
scikit-image, pillow, trimesh):
    python export_rock.py 0.3        # the STL parts into ./stl
    python render_rock.py 0.3 all    # rock_*.png renders
The stack follows A1's mechanics
(docs/hardware/mao-mechanical.md): cell, board, panel carrier under a window,
the dial ring riding a thin track over the Hall sensors.
"""
import math

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

# ----------------------------------------------------------------- A1 stack (z in mm)
BOARD_R, Z_PCB0, Z_PCB1 = 29.0, 9.4, 11.0         # PCB 1.6 mm
Z_BPARTS = 6.2                                     # tallest B-side part (USB-C, module) bottom
CELL = (-17.75, 17.75, -20.6, 9.4, 1.6, 6.0)       # cell 35.5 x 30 x 4.4 (insulator on top)
RING_R_IN, RING_R_OUT = 24.0, 30.0                 # dial ring ID 48 / OD 60 (pole strip at r 26.3)
Z_TRACK0, Z_SEAT = 12.4, 13.0                      # thin track the ring rides on (0.6 mm over the Hall pair)
Z_RING_TOP = 19.0
WINDOW_R, Z_WINDOW = 23.3, 18.2
WALL = 2.0                                         # shell wall
Z_SPLIT = 5.0                                      # top shell / base parting line
PINS = [(18.6, -16.7), (-18.6, -16.7)]             # the board's two holes (r 25, +-48 deg): locating pins
JOIN = [(32.4, 0.0), (32.4, 205.0), (32.4, 300.0)]  # shell-joining M2 screws (r, deg; clear of USB and antenna)
USB = (0.0, -29.6, 7.8)                            # USB-C mating face centre (x, y, z)

# ----------------------------------------------------------------- the stone
CENTRE = (-2.6, 2.2)            # the stone sits a little towards the lower left: the mark's tail
R0 = 43.0
HARM = [(2, 0.050, math.radians(208)), (3, 0.022, 1.1), (4, 0.010, 2.6), (5, 0.009, 0.4)]
H_TOP = 21.0                    # crown height
Z_MID = 6.0                     # where top and bottom curvature meet on the rim
U_TOP, U_BOT = 24.0, 9.0        # how far in the top dome / the bottom rounding reach


def outline():
    pts = []
    for k in range(720):
        th = 2 * math.pi * k / 720
        r = R0 * (1 + sum(a * math.cos(n * th - ph) for n, a, ph in HARM))
        pts.append((CENTRE[0] + r * math.cos(th), CENTRE[1] + r * math.sin(th)))
    return pts


def sdf2d(xs, ys):
    res = xs[1] - xs[0]
    im = Image.new('L', (len(xs), len(ys)), 0)
    ImageDraw.Draw(im).polygon([((x - xs[0]) / res, (y - ys[0]) / res) for x, y in outline()], fill=255)
    m = np.array(im) > 0
    d = (ndimage.distance_transform_edt(~m) - ndimage.distance_transform_edt(m)) * res
    return ndimage.gaussian_filter(d, 1.2 / res)       # a smooth field: no facets on the stone


def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def top_h(u):
    return Z_MID + (H_TOP - Z_MID) * np.sqrt(np.clip(1 - (1 - np.clip(u, 0, U_TOP) / U_TOP) ** 2, 0, 1))


def bot_h(u):
    return Z_MID - Z_MID * np.sqrt(np.clip(1 - (1 - np.clip(u, 0, U_BOT) / U_BOT) ** 2, 0, 1))


def box(X, Y, Z, x0, x1, y0, y1, z0, z1):
    return np.maximum(np.maximum(np.maximum(x0 - X, X - x1), np.maximum(y0 - Y, Y - y1)), np.maximum(z0 - Z, Z - z1))


def cyl(X, Y, Z, cx, cy, r, z0, z1):
    return np.maximum(np.hypot(X - cx, Y - cy) - r, np.maximum(z0 - Z, Z - z1))


class Rock:
    def __init__(self, res=0.3):
        self.res = res
        self.xs = np.arange(-52.0 + 0.29 * res, 50.0 + 1e-9, res)
        self.ys = np.arange(-50.0 + 0.41 * res, 54.0 + 1e-9, res)
        self.zs = np.arange(-1.5 + 0.37 * res, 23.0 + 1e-9, res)     # no grid plane on a design plane (z = 0)
        self.d2 = sdf2d(self.xs, self.ys)

    def fields(self):
        X2, Y2 = np.meshgrid(self.xs, self.ys)
        X, Y, Z = X2[None], Y2[None], self.zs[:, None, None]
        u = np.clip(-self.d2, 0, None)
        top, bot = top_h(u)[None], bot_h(u)[None]
        r = np.hypot(X, Y)
        # the stone and its 2 mm wall
        stone = np.maximum(np.maximum(self.d2[None], Z - top), bot - Z)
        inner = np.maximum(np.maximum(self.d2[None] + WALL, Z - (top - WALL)), (bot + WALL) - Z)
        shell = np.maximum(stone, -inner)
        # the dial's well through the top, and the thin track the ring rides on
        well = np.maximum(r - (RING_R_OUT + 0.4), Z_SEAT - Z)
        shell = np.maximum(shell, -well)
        # the deck the ring rides on: from the ring's inner edge out to the wall (0.6 mm over the Hall pair)
        deck = np.maximum(np.maximum((RING_R_IN - 0.6) - r, stone), np.maximum(Z_TRACK0 - Z, Z - Z_SEAT))
        shell = np.minimum(shell, deck)
        # three joining posts in the top shell (outside the board), M2 heat-set insert holes from below
        def polar(rr, deg):
            return rr * math.cos(math.radians(deg)), rr * math.sin(math.radians(deg))
        for rr, deg in JOIN:
            jx, jy = polar(rr, deg)
            post = np.maximum(cyl(X, Y, Z, jx, jy, 2.6, Z_SPLIT + 0.3, 22.0), stone)
            shell = np.minimum(shell, post)
            shell = np.maximum(shell, -cyl(X, Y, Z, jx, jy, 1.6, Z_SPLIT - 1, Z_SPLIT + 4.6))
        # ribs from the inner top press the board down at its rim (between the joining posts)
        for deg in (60, 150, 245, 335):
            bx, by = polar(BOARD_R - 1.2, deg)
            rib = np.maximum(cyl(X, Y, Z, bx, by, 1.2, Z_PCB1 + 0.05, 22.0), stone)
            shell = np.minimum(shell, rib)
        shell = np.maximum(shell, -np.maximum(r - (RING_R_OUT + 0.4), Z_SEAT - Z))      # keep the well clear
        # the USB-C tunnel at 12 o'clock: plug overmold 12.4 x 6.8 from outside to the port
        ux, uy, uz = USB
        tunnel = box(X, Y, Z, ux - 6.2, ux + 6.2, -60, uy - 0.2, uz - 3.4, uz + 3.4)
        shell = np.maximum(shell, -tunnel)
        # split: top shell above the parting line, base below (with a locating lip)
        # the parting line: the shells meet flush (the print's own seam marks it)
        top_shell = np.maximum(shell, Z_SPLIT - Z)
        base = np.maximum(shell, Z - Z_SPLIT)
        # rabbet: the base's lip rises 1.6 mm inside the top shell's wall, which is thinned to take it
        # (shallow: the wall is thinnest where the stone curves under, so the rabbet takes at most 0.5 of it)
        lip = np.maximum(np.maximum(inner - 0.3, -(inner + 1.1)), np.maximum((Z_SPLIT - 0.6) - Z, Z - (Z_SPLIT + 1.6)))
        base = np.minimum(base, np.maximum(lip, stone + 0.9))
        relief = np.maximum(np.maximum(inner - 0.5, -(inner + 1.4)), np.maximum(Z_SPLIT - Z, Z - (Z_SPLIT + 1.8)))
        top_shell = np.maximum(top_shell, -relief)
        # base: posts under the board's two holes with locating pins, and the joining screws' bores
        for px, py in PINS:
            post = np.maximum(cyl(X, Y, Z, px, py, 2.8, 0.0, Z_PCB0), stone)
            pin = cyl(X, Y, Z, px, py, 0.95, Z_PCB0 - 0.1, Z_PCB1 + 0.8)
            base = np.minimum(base, np.minimum(post, pin))
        for rr, deg in JOIN:
            jx, jy = rr * math.cos(math.radians(deg)), rr * math.sin(math.radians(deg))
            post = np.maximum(cyl(X, Y, Z, jx, jy, 2.6, 0.0, Z_SPLIT - 0.15), stone)
            base = np.minimum(base, post)
            base = np.maximum(base, -cyl(X, Y, Z, jx, jy, 1.15, -2, Z_SPLIT + 1))
            base = np.maximum(base, -cyl(X, Y, Z, jx, jy, 2.2, -2, 1.4))
        # cell pocket rails and the speaker grille (holes under the speaker, 9 o'clock)
        x0, x1, y0, y1, z0, z1 = CELL
        rails = np.minimum(box(X, Y, Z, x0 - 1.8, x0 - 0.3, y0, y1, 0, z0 + 2.5),
                           box(X, Y, Z, x1 + 0.3, x1 + 1.8, y0, y1, 0, z0 + 2.5))
        base = np.minimum(base, np.maximum(rails, stone))
        for gy in np.arange(-6.0, 6.01, 2.0):
            for gx in (-27.0, -24.5, -22.0):
                base = np.maximum(base, -cyl(X, Y, Z, gx, gy + 0.6, 0.55, -2, 3.0))
        # the ODD JOBS mark debossed 0.5 mm into the underside
        base = np.maximum(base, -self.mark_deboss(X2, Y2, Z))
        # the ring: rounded, 60 soft grip flutes, the pole-strip groove in its lower face
        ang = np.arctan2(Y, X)
        rr = r + 0.16 * np.cos(60 * ang)
        ring = np.maximum(np.maximum(RING_R_IN - rr, rr - RING_R_OUT), np.maximum(Z_SEAT + 0.1 - Z, Z - Z_RING_TOP))
        ring = np.maximum(ring, np.hypot(np.maximum(rr - (RING_R_OUT - 1.6), 0), np.maximum(Z - (Z_RING_TOP - 1.6), 0)) - 1.6)
        ring = np.maximum(ring, np.hypot(np.maximum((RING_R_IN + 0.8) - rr, 0), np.maximum(Z - (Z_RING_TOP - 0.8), 0)) - 0.8)
        groove = np.maximum(np.abs(r - 26.3) - 1.6, Z - (Z_SEAT + 0.1 + 1.6))
        ring = np.maximum(ring, -groove)
        window = np.maximum(r - WINDOW_R, np.maximum(Z - Z_WINDOW, (Z_WINDOW - 1.0) - Z))
        assembled = np.minimum(np.maximum(shell, Z_SPLIT - 0.2 - Z), base)   # both halves as one body (renders)
        return {'top_shell': top_shell, 'base': base, 'ring': ring, 'window': window, 'stone': stone,
                'assembled': assembled}

    def mark_deboss(self, X2, Y2, Z):
        """The mark as a 2D region on the underside (about 18 mm wide), depth 0.5 mm."""
        import json
        import os
        here = os.path.dirname(os.path.abspath(__file__))
        d = json.load(open(os.path.join(here, '..', '..', 'brand', 'odd-jobs-symbol.json')))
        res = self.res
        im = Image.new('L', (len(self.xs), len(self.ys)), 0)
        s = 18.0 / 671.0
        cx, cy = 337.5, 274.5
        # seen from below the image is mirrored in x
        ImageDraw.Draw(im).polygon([((-(px - cx) * s - self.xs[0]) / res, ((py - cy) * s + 8.0 - self.ys[0]) / res)
                                    for px, py in d['points']], fill=255)
        m = np.array(im) > 0
        d2 = (ndimage.distance_transform_edt(~m) - ndimage.distance_transform_edt(m)) * res
        return np.maximum(d2[None], Z - 0.5)

    def report(self):
        """Fit numbers: footprint, crown, wall round the board and the cell."""
        X, Y = np.meshgrid(self.xs, self.ys)
        u = np.clip(-self.d2, 0, None)
        inside = u > 0
        r = np.hypot(X, Y)
        top = top_h(u)
        out = {
            'footprint (mm)': '%.1f x %.1f' % (np.ptp(X[inside]), np.ptp(Y[inside])),
            'height (mm)': round(float(top.max()), 1),
            'nearest stone edge to the dial axis (mm)': round(float(np.min(r[~inside & (r < 60)])), 1),
            'board edge: free space to the inner wall, min (mm)': round(float(u[r <= BOARD_R].min() - WALL), 1),
            'inner wall top over the board edge, min (mm), board top %.1f' % Z_PCB1:
                round(float((top - WALL)[(r > BOARD_R - 0.5) & (r <= BOARD_R)].min()), 1),
            'stone top beside the ring, min..max (mm), ring top %.1f' % Z_RING_TOP:
                '%.1f..%.1f' % (float(top[(r > 30.6) & (r < 31.4)].min()), float(top[(r > 30.6) & (r < 31.4)].max())),
        }
        x0, x1, y0, y1, z0, z1 = CELL
        cell = (X >= x0) & (X <= x1) & (Y >= y0) & (Y <= y1)
        out['cell: free space to the inner wall, min (mm)'] = round(float(u[cell].min() - WALL), 1)
        return out
