"""Raymarched renders of the MAO wheel stone: assembled, top, side, underside, cut-away and exploded.

The raymarcher is the rock's (../rock/render_rock.py); only the scene differs.
"""
import os
import sys

import numpy as np
from scipy import ndimage

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rock'))
import render_rock as rr  # noqa: E402

from wheelstone import WheelStone  # noqa: E402

rr.MATS.clear()
rr.MATS.update({
    'base': (np.array([0.66, 0.63, 0.60]), 0.05),        # warm stone matte PETG
    'frame': (np.array([0.42, 0.40, 0.42]), 0.08),
    'wheel': (np.array([0.36, 0.28, 0.42]), 0.12),       # plum, the turning shoulder
    'bezel': (np.array([0.16, 0.15, 0.18]), 0.20),
    'carrier': (np.array([0.22, 0.22, 0.24]), 0.10),
    'finger': (np.array([0.58, 0.60, 0.63]), 0.55),       # stainless press finger, fixed to the base
    'window': (np.array([0.03, 0.03, 0.05]), 0.9),
})


class Scene(rr.Scene):
    def __init__(self, res, cut=False, explode=False):
        self.rk = WheelStone(res)
        f = self.rk.fields()
        self.names = [n for n in rr.MATS if n in f]
        self.F, self.G = [], []
        for n in self.names:
            occ = f[n] < 0
            self.G.append((ndimage.distance_transform_edt(~occ) - ndimage.distance_transform_edt(occ)) * res)
            self.F.append(ndimage.gaussian_filter(f[n].astype(np.float64), 0.6))
        self.res = res
        self.o = np.array([self.rk.xs[0], self.rk.ys[0], self.rk.zs[0]])
        self.cut = cut
        self.off = {n: np.zeros(3) for n in self.names}
        if explode:
            self.off.update({'frame': np.array([0, 0, 9.0]), 'wheel': np.array([0, 0, 22.0]),
                             'carrier': np.array([0, 0, 32.0]), 'window': np.array([0, 0, 40.0]),
                             'bezel': np.array([0, 0, 48.0])})


if __name__ == '__main__':
    res = float(sys.argv[1]) if len(sys.argv) > 1 else 0.3
    which = sys.argv[2] if len(sys.argv) > 2 else 'all'
    out = sys.argv[3] if len(sys.argv) > 3 else '.'
    p = lambda n: os.path.join(out, 'mao-wheelstone-%s.png' % n)
    if which in ('all', 'main'):
        sc = Scene(res)
        rr.render(sc, (-120, 150, 110), (0, 2, 8), p('iso'), 1400, 1000, zlim=(-2, 24))
        rr.render(sc, (0, 0, 220), (0, 0, 0), p('top'), 1100, 1100, ortho=46, up=(0, -1, 0), zlim=(-2, 24))
        rr.render(sc, (0, 210, 10), (0, 0, 9), p('side'), 1400, 560, fov=24, zlim=(-2, 24))
        rr.render(sc, (110, -150, -95), (0, 0, 6), p('under'), 1400, 1000, zlim=(-2, 24))
    if which in ('all', 'cut'):
        sc = Scene(res, cut=True)
        rr.render(sc, (150, 120, 70), (0, 0, 10), p('cut'), 1400, 1000, zlim=(-2, 24))
    if which in ('all', 'explode'):
        sc = Scene(res, explode=True)
        rr.render(sc, (-150, 185, 120), (0, 2, 26), p('exploded'), 1200, 1300, fov=32, zlim=(-2, 72))
    print('done')
