"""Brush-ink drawings for the MAO silk easter eggs (owner decision 2026-10-06: the sleeping cat and the sake set).

A stroke is a centre path (cubic Bezier segments) and a pressure profile; its outline is the path offset by
+-width/2 with round ends and a low, smooth wobble along the normal, so lines do not look ruled. Every width stays
>= SILK_MIN (JLC's silk minimum line is 0.153 mm). Each drawing returns closed polygons (lists of (x, y) in mm,
+y towards 6 o'clock, centred on its own box); kanji.py unions them, checks them with the same opening / closing
test as the kanji and writes them into brand/kanji-eggs.json, and silk.py places them beside their kanji.
Plain Python (no KiCad).
"""
import math
import random

SILK_MIN = 0.17


def cubic(p0, p1, p2, p3, n=24):
    out = []
    for k in range(n + 1):
        t = k / n
        a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t ** 2, t ** 3
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return out


def path(*segs, n=24):
    """Chain cubic segments given as (p0, p1, p2, p3); returns dense points."""
    pts = []
    for s in segs:
        c = cubic(*s, n=n)
        pts += c if not pts else c[1:]
    return pts


def resample(pts, step=0.04):
    out = [pts[0]]
    acc = 0.0
    for a, b in zip(pts, pts[1:]):
        seg = math.dist(a, b)
        if seg == 0:
            continue
        t = step - acc
        while t <= seg:
            out.append((a[0] + (b[0] - a[0]) * t / seg, a[1] + (b[1] - a[1]) * t / seg))
            t += step
        acc = seg - (t - step)
    if math.dist(out[-1], pts[-1]) > 1e-6:
        out.append(pts[-1])
    return out


# pressure profiles: t in 0..1 -> 0..1
def press_flick(t):
    """Land firmly, carry, lift off into a fine tail."""
    return min(1.0, 0.55 + t * 4.0) * (1.0 - 0.85 * t ** 2.2)


def press_dab(t):
    """Thin in, full in the middle, thin out."""
    return max(0.0, math.sin(math.pi * min(1.0, max(0.0, t)))) ** 0.8


def press_even(t):
    return 0.8 + 0.2 * math.sin(math.pi * t)


def arch(t):
    """Heavier in the middle of a long contour line, never thin."""
    return 0.5 + 0.5 * math.sin(math.pi * t)


def stroke(pts, wmax, profile=press_flick, wmin=SILK_MIN, wobble=0.025, seed=1):
    """Outline (closed polygon) of a brush stroke along pts."""
    rnd = random.Random(seed)
    ph1, ph2 = rnd.uniform(0, 6.28), rnd.uniform(0, 6.28)
    pts = resample(pts)
    n = len(pts)
    length = sum(math.dist(a, b) for a, b in zip(pts, pts[1:])) or 1.0
    left, right, ends, s = [], [], [], 0.0
    for i, p in enumerate(pts):
        if i:
            s += math.dist(pts[i - 1], p)
        t = min(1.0, s / length)
        a, b = pts[max(i - 1, 0)], pts[min(i + 1, n - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / d, dx / d
        w = max(wmin, wmax * profile(t))
        off = wobble * (math.sin(ph1 + 9.0 * t) + 0.6 * math.sin(ph2 + 23.0 * t))
        cx, cy = p[0] + nx * off, p[1] + ny * off
        left.append((cx + nx * w / 2, cy + ny * w / 2))
        right.append((cx - nx * w / 2, cy - ny * w / 2))
        ends.append((cx, cy, w))

    def cap(c, r, a0):
        return [(c[0] + r * math.cos(a0 + math.pi * k / 10), c[1] + r * math.sin(a0 + math.pi * k / 10)) for k in range(11)]
    (x0, y0, w0), (x1, y1, w1) = ends[0], ends[-1]
    a_end = math.atan2(pts[-1][1] - pts[-2][1], pts[-1][0] - pts[-2][0]) - math.pi / 2
    a_start = math.atan2(pts[0][1] - pts[1][1], pts[0][0] - pts[1][0]) - math.pi / 2
    return left + cap((x1, y1), w1 / 2, a_end) + right[::-1] + cap((x0, y0), w0 / 2, a_start)


def blob(cx, cy, r, n=18):
    return [(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n)) for k in range(n)]


# ------------------------------------------------------------------------------------------------- the cat
def cat():
    """Maomao's namesake: a cat asleep in a curl, nose in its tail. ~7.2 x 4.4 mm."""
    P = []
    # the back: one long arch from behind the ears to the rump
    P.append(stroke(path(((-1.15, -1.05), (-0.2, -2.35), (2.3, -2.25), (3.1, -0.2)),
                         ((3.1, -0.2), (3.35, 0.55), (3.0, 1.1), (2.45, 1.25))), 0.42, arch, seed=3))
    # the tail: from the rump along the floor and round the face, tip flicked up
    P.append(stroke(path(((2.45, 1.3), (1.2, 1.55), (-0.7, 1.6), (-1.95, 1.25)),
                         ((-1.95, 1.25), (-2.7, 1.0), (-2.95, 0.55), (-2.6, 0.2))), 0.36, press_flick, seed=5))
    # the head: an open loop, chin tucked down
    P.append(stroke(path(((-1.15, -1.05), (-1.0, -0.45), (-1.2, 0.35), (-1.75, 0.55)),
                         ((-1.75, 0.55), (-2.45, 0.75), (-3.05, 0.15), (-2.85, -0.6)),
                         ((-2.85, -0.6), (-2.75, -0.95), (-2.45, -1.15), (-2.2, -1.15))), 0.3, press_even, seed=7))
    # ears: two quick peaks, each one stroke up and down
    P.append(stroke(path(((-2.85, -0.75), (-3.0, -1.2), (-3.05, -1.6), (-2.95, -1.85)),
                         ((-2.95, -1.85), (-2.65, -1.65), (-2.45, -1.4), (-2.25, -1.15))), 0.22, press_even, seed=9))
    P.append(stroke(path(((-1.95, -1.2), (-1.75, -1.55), (-1.55, -1.8), (-1.35, -1.95)),
                         ((-1.35, -1.95), (-1.25, -1.65), (-1.2, -1.35), (-1.18, -1.05))), 0.22, press_even, seed=11))
    # closed eyes: two little downturned smiles, and the nose
    P.append(stroke(cubic((-2.55, -0.42), (-2.42, -0.22), (-2.22, -0.22), (-2.1, -0.4)), 0.2, press_even, wobble=0.0))
    P.append(stroke(cubic((-1.8, -0.4), (-1.68, -0.22), (-1.5, -0.22), (-1.38, -0.4)), 0.2, press_even, wobble=0.0))
    P.append(blob(-1.92, -0.02, 0.13))
    # a front paw peeking under the chin
    P.append(stroke(cubic((-1.55, 0.95), (-1.25, 0.8), (-0.85, 0.85), (-0.7, 1.15)), 0.26, press_dab, seed=13))
    return P


def zzz():
    """Three brushed z's drifting up from the cat's head, three strokes each (clean corners)."""
    P = []
    for cx, cy, h in ((-3.65, -2.55, 0.6), (-4.45, -3.45, 0.82), (-5.4, -4.5, 1.05)):
        w = 0.75 * h
        a, b = (cx - w / 2, cy - h / 2), (cx + w / 2, cy - h / 2 + 0.03)
        c, d = (cx - w / 2, cy + h / 2), (cx + w / 2, cy + h / 2 - 0.02)
        for seg in ((a, b), (b, c), (c, d)):
            P.append(stroke(list(seg), 0.22, press_even, wobble=0.0))
    return P


# ------------------------------------------------------------------------------------------------- sake
def sake():
    """A tokkuri (sake bottle) and a small choko cup: the bottle a round body, a slim neck and a flared lip in two
    side strokes that swell at the belly, a painted band on the shoulder; the cup low and wide beside it. ~6.6 x 6.4 mm."""
    P = []
    bx = -1.0

    def side(sg, seed):
        return stroke(path(((bx + sg * 0.62, -3.15), (bx + sg * 0.42, -2.9), (bx + sg * 0.34, -2.4), (bx + sg * 0.36, -1.75)),
                           ((bx + sg * 0.36, -1.75), (bx + sg * 0.4, -1.1), (bx + sg * 1.75, -0.7), (bx + sg * 1.78, 0.9)),
                           ((bx + sg * 1.78, 0.9), (bx + sg * 1.8, 2.2), (bx + sg * 1.2, 2.75), (bx + sg * 0.75, 2.85))),
                      0.34, lambda t: 0.55 + 0.45 * math.sin(math.pi * min(1.0, t * 1.15)), wobble=0.006, seed=seed)
    P.append(side(-1, 71))
    P.append(side(1, 72))
    P.append(stroke(cubic((bx - 0.85, 2.88), (bx - 0.3, 2.98), (bx + 0.3, 2.98), (bx + 0.85, 2.88), 12), 0.3, press_dab, seed=73))
    P.append(stroke(cubic((bx - 0.66, -3.15), (bx - 0.3, -3.38), (bx + 0.3, -3.38), (bx + 0.66, -3.15), 12), 0.22, press_even, wobble=0.0))
    P.append(stroke(cubic((bx - 1.15, -0.55), (bx - 0.5, -0.32), (bx + 0.5, -0.32), (bx + 1.15, -0.55), 18), 0.24, press_dab, seed=74))
    cx, cy = 2.3, 1.9
    P.append(stroke(cubic((cx - 1.05, cy - 0.45), (cx - 1.0, cy + 0.55), (cx + 1.0, cy + 0.55), (cx + 1.05, cy - 0.45), 22),
                    0.3, lambda t: 0.6 + 0.4 * math.sin(math.pi * t), seed=75))
    P.append(stroke(cubic((cx - 1.05, cy - 0.45), (cx - 0.45, cy - 0.7), (cx + 0.45, cy - 0.7), (cx + 1.05, cy - 0.45), 16),
                    0.2, press_even, wobble=0.0))
    P.append(stroke(cubic((cx - 0.38, cy + 0.62), (cx - 0.1, cy + 0.72), (cx + 0.1, cy + 0.72), (cx + 0.38, cy + 0.62), 10),
                    0.26, press_dab, seed=76))
    return P


def centred(polys):
    xs = [x for p in polys for x, _ in p]
    ys = [y for p in polys for _, y in p]
    cx, cy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    return [[(x - cx, y - cy) for x, y in p] for p in polys]


# name: (layouts, the first that fits wins: each a list of polygons), meaning, where (silk.py places them)
DRAWINGS = {
    'cat': (lambda: [centred(cat() + zzz()), centred(cat())], 'the sleeping cat (Maomao, "cat cat")',
            'F, beside 猫猫'),
    'sake_set': (lambda: [centred(sake())], 'a sake bottle and cup', 'B, under the cell beside 酒'),
}
