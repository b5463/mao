"""Radial sections of the wheel stone with the board stack drawn in, one panel per angle, for review.

    python section_wheelstone.py out.png 0 33 70 100 180 242 270

Each panel: the plane through the dial axis at that angle (angles from +x towards +y, 0 = 3 o'clock), radius
0..42 left to right, z up. Grey stone parts, plum wheel, near-black bezel, blue window, steel-blue press finger, green PCB, orange board
parts, the cell and the speaker.
"""
import math
import sys

import numpy as np
from PIL import Image, ImageDraw

import wheelstone as w

S = 24                       # px per mm
R0, R1, Z0, Z1 = 0.0, 42.0, -0.5, 21.0
COLS = {'base': (176, 168, 158), 'frame': (122, 118, 124), 'wheel': (122, 92, 142), 'bezel': (38, 38, 44),
        'carrier': (78, 78, 90), 'window': (150, 200, 232), 'panel': (20, 20, 30), 'finger': (70, 96, 128)}


def panel(deg, pressed=False):
    rs = np.arange(R0, R1, 1.0 / S)
    zs = np.arange(Z0, Z1, 1.0 / S)
    R, Zg = np.meshgrid(rs, zs)
    X, Y = R * math.cos(math.radians(deg)), R * math.sin(math.radians(deg))
    ws = w.WheelStone(1.0 / S)
    ws.grid = lambda: (X[None].astype(np.float32), Y[None].astype(np.float32), Zg[None].astype(np.float32))
    ws.mark_deboss = lambda Z: np.full(R.shape, 10.0, np.float32)[None]
    F = ws.fields(pressed)
    img = np.full(R.shape + (3,), 246, np.uint8)
    img[(Zg >= w.Z_PCB0) & (Zg <= w.Z_PCB1) & (R <= w.BOARD_R)] = (44, 112, 64)
    for ref, boxes in ws.board():
        c = (232, 150, 60) if ref not in ('cell', 'speaker') else ((200, 190, 120) if ref == 'cell' else (150, 120, 90))
        for x0, x1, y0, y1, z0, z1 in boxes:
            img[(X >= x0) & (X <= x1) & (Y >= y0) & (Y <= y1) & (Zg >= z0) & (Zg <= z1)] = c
    for n, c in COLS.items():
        img[F[n][0] < 0] = c
    im = Image.fromarray(img[::-1])
    d = ImageDraw.Draw(im)
    d.text((8, 6), '%g deg%s' % (deg, ' pressed' if pressed else ''), fill=(0, 0, 0))
    for zz in range(0, 21, 5):                                      # z ticks every 5 mm
        y = int((Z1 - zz) * S)
        d.line([(0, y), (10, y)], fill=(0, 0, 0))
    return im


def main(out, angles):
    ims = [panel(a) for a in angles]
    sheet = Image.new('RGB', (ims[0].width, sum(i.height for i in ims) + 4 * len(ims)), (255, 255, 255))
    y = 0
    for i in ims:
        sheet.paste(i, (0, y))
        y += i.height + 4
    sheet.save(out)
    print(out, sheet.size)


if __name__ == '__main__':
    main(sys.argv[1], [float(a) for a in sys.argv[2:]] or [0, 70, 100, 180, 242])
