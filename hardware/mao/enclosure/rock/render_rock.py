"""Raymarched renders of the MAO rock: assembled, underside, cut-away and exploded."""
import math
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

from rock import Rock

PINK = np.array([0.95, 0.63, 0.77])
MATS = {
    'top_shell': (np.array([0.70, 0.67, 0.63]), 0.06),     # warm stone matte PETG
    'base': (np.array([0.62, 0.59, 0.56]), 0.05),
    'ring': (np.array([0.30, 0.27, 0.33]), 0.14),          # dark plum TPU band
    'window': (np.array([0.03, 0.03, 0.05]), 0.9),
}


class Scene:
    def __init__(self, res, cut=False, explode=False):
        self.rk = Rock(res)
        f = self.rk.fields()
        if not explode:                       # assembled: the halves as one body (no crack at the parting plane)
            f = dict(f)
            f['top_shell'] = f['assembled']
            f.pop('base')
        self.names = [n for n in MATS if n in f]
        # true signed distances (the shape fields are steep near the rim; a ray would punch through)
        self.F, self.G = [], []
        for n in self.names:
            occ = f[n] < 0
            sd = (ndimage.distance_transform_edt(~occ) - ndimage.distance_transform_edt(occ)) * res
            self.G.append(sd)                                     # stepping: a true distance
            self.F.append(ndimage.gaussian_filter(f[n], 0.6))     # surface and shading: the smooth shape field
        self.res = res
        self.o = np.array([self.rk.xs[0], self.rk.ys[0], self.rk.zs[0]])
        self.cut = cut
        self.off = {n: np.zeros(3) for n in self.names}
        if explode:
            self.off.update({'top_shell': np.array([0, 0, 14.0]), 'ring': np.array([0, 0, 30.0]),
                             'window': np.array([0, 0, 40.0])})

    def one(self, P, m, grid=None):
        Q = P - self.off[self.names[m]]
        idx = [(Q[:, 2] - self.o[2]) / self.res, (Q[:, 1] - self.o[1]) / self.res, (Q[:, 0] - self.o[0]) / self.res]
        v = ndimage.map_coordinates((grid or self.F)[m], idx, order=1, mode='nearest', cval=1e3)
        # outside the grid: far away
        out = ((Q < self.o + 0.3) | (Q > self.o + (np.array(self.F[m].shape[::-1]) - 1) * self.res - 0.3)).any(1)
        v[out] = np.maximum(v[out], 0.6)
        if self.cut:
            v = np.maximum(v, P[:, 0] - 0.0)                 # remove the x > 0 half
        return v

    def sample(self, P):
        vals = np.stack([self.one(P, m) for m in range(len(self.names))])
        dist = np.stack([self.one(P, m, self.G) for m in range(len(self.names))]).min(0)
        return vals.min(0), vals.argmin(0), dist

    def normal(self, P, mat):
        n = np.zeros_like(P)
        e = self.res
        for m in np.unique(mat):
            sel = mat == m
            Q = P[sel]
            g = np.stack([self.one(Q + d, m) - self.one(Q - d, m) for d in np.eye(3) * e], 1)
            n[sel] = g / (np.linalg.norm(g, axis=1, keepdims=True) + 1e-9)
        return n


def render(sc, eye, target, out, W, H, fov=26.0, ortho=None, up=(0, 0, 1), ss=2, bg=(0.94, 0.93, 0.95), zlim=(-2, 70)):
    eye = np.array(eye, float)
    f = np.array(target, float) - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, np.array(up, float))
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    W2, H2 = W * ss, H * ss
    jj, ii = np.meshgrid(np.arange(W2), np.arange(H2))
    sx = (jj + 0.5) / W2 * 2 - 1
    sy = 1 - (ii + 0.5) / H2 * 2
    if ortho:
        O = eye + (sx * W / H * ortho)[..., None] * r + (sy * ortho)[..., None] * u
        D = np.broadcast_to(f, O.shape).copy()
    else:
        t = math.tan(math.radians(fov) / 2)
        D = f + (sx * W / H * t)[..., None] * r + (sy * t)[..., None] * u
        D /= np.linalg.norm(D, axis=-1, keepdims=True)
        O = np.broadcast_to(eye, D.shape).copy()
    O, D = O.reshape(-1, 3), D.reshape(-1, 3)
    n = len(O)
    lo = np.array([sc.rk.xs[0], sc.rk.ys[0], zlim[0]])
    hi = np.array([sc.rk.xs[-1], sc.rk.ys[-1], zlim[1]])
    with np.errstate(divide='ignore', invalid='ignore'):
        t0 = np.nanmax(np.minimum((lo - O) / D, (hi - O) / D), axis=1)
        t1 = np.nanmin(np.maximum((lo - O) / D, (hi - O) / D), axis=1)
    t = np.maximum(t0, 0)
    alive = t1 > t
    hit = np.zeros(n, bool)
    mat = np.zeros(n, int)
    prev = np.full(n, 0.08)
    for _ in range(900):
        idx = np.nonzero(alive & ~hit)[0]
        if len(idx) == 0:
            break
        fval, m, dist = sc.sample(O[idx] + D[idx] * t[idx, None])
        done = fval < 0
        # inside: back up half a step and bisect to the smooth field's zero
        if done.any():
            di = idx[done]
            lo_t, hi_t = t[di] - prev[di], t[di]
            for _ in range(7):
                mid = (lo_t + hi_t) / 2
                fv, mm, _ = sc.sample(O[di] + D[di] * mid[:, None])
                inside = fv < 0
                hi_t = np.where(inside, mid, hi_t)
                lo_t = np.where(inside, lo_t, mid)
            t[di] = hi_t
            _, mm, _ = sc.sample(O[di] + D[di] * hi_t[:, None])
            hit[di] = True
            mat[di] = mm
        nd = idx[~done]
        step = np.where(dist[~done] > 1.2, dist[~done] * 0.8, 0.08)
        prev[nd] = step
        t[nd] += step
        alive[nd] = t[nd] < t1[nd]
    # background: soft vertical gradient
    bgc = np.array(bg)[None] * (0.94 + 0.06 * (sy.reshape(-1, 1) + 1) / 2)
    img = bgc.copy()
    hi_idx = np.nonzero(hit)[0]
    P = O[hi_idx] + D[hi_idx] * t[hi_idx, None]
    N = sc.normal(P, mat[hi_idx])
    L1 = np.array([-0.4, -0.5, 0.77]); L1 /= np.linalg.norm(L1)
    L2 = np.array([0.7, 0.35, 0.3]); L2 /= np.linalg.norm(L2)
    V = -D[hi_idx]
    col = np.zeros((len(hi_idx), 3))
    for mi, name in enumerate(sc.names):
        sel = mat[hi_idx] == mi
        if not sel.any():
            continue
        base, spec = MATS[name]
        Nn = N[sel]
        wrap = 0.5 * (1 + Nn @ L1)                                # soft wrap light: stone-like
        diff = 0.10 + 0.70 * wrap ** 1.6 + 0.18 * np.clip(Nn @ L2, 0, 1)
        Hh = L1 + V[sel]
        Hh /= np.linalg.norm(Hh, axis=1, keepdims=True)
        sp = spec * np.clip((Nn * Hh).sum(1), 0, 1) ** (80 if name == 'window' else 10)
        rim = 0.10 * (1 - np.clip((Nn * V[sel]).sum(1), 0, 1)) ** 3
        c = base * diff[:, None] + (sp + rim)[:, None]
        if name == 'window':
            Q = P[sel] - sc.off['window']
            ex = np.minimum(np.hypot((Q[:, 0] + 5.6) / 2.9, (Q[:, 1] + 0.8) / 4.2),
                            np.hypot((Q[:, 0] - 5.6) / 2.9, (Q[:, 1] + 0.8) / 4.2))
            c = c + PINK[None] * (np.clip(1.15 - ex, 0, 1) ** 0.8)[:, None] * 0.95
        col[sel] = c
    img[hi_idx] = np.clip(col, 0, 1)
    img = (np.power(np.clip(img, 0, 1), 1 / 1.7) * 255).astype(np.uint8).reshape(H2, W2, 3)
    Image.fromarray(img).resize((W, H), Image.LANCZOS).save(out)


if __name__ == '__main__':
    res = float(sys.argv[1]) if len(sys.argv) > 1 else 0.3
    which = sys.argv[2] if len(sys.argv) > 2 else 'all'
    if which in ('all', 'main'):
        sc = Scene(res)
        render(sc, (-120, 150, 110), (0, 2, 7), 'rock_iso.png', 1400, 1000)
        render(sc, (0, 0, 220), (0, 0, 0), 'rock_top.png', 1100, 1100, ortho=56, up=(0, -1, 0))
        render(sc, (0, 210, 10), (0, 0, 9), 'rock_side.png', 1400, 640, fov=24)
        render(sc, (110, -150, -95), (0, 0, 6), 'rock_under.png', 1400, 1000, up=(0, 0, 1))
    if which in ('all', 'cut'):
        sc = Scene(res, cut=True)
        render(sc, (150, 120, 95), (0, 0, 8), 'rock_cut.png', 1400, 1000)
    if which in ('all', 'explode'):
        sc = Scene(res, explode=True)
        render(sc, (-150, 185, 120), (0, 2, 20), 'rock_exploded.png', 1200, 1300, fov=30)
    print('done')
