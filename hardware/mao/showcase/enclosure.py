"""Concept enclosure for MAO_MAIN A0 showcase renders (plain Python, writes VRML 2.0 for KiCad's 3D viewer).

NOT the production enclosure: a made-up puck drawn from the real interface numbers in design/mechanical.py, so the
board sits in it the way the specification says (stack heights, ring, window, USB-C opening, IR windows, speaker
grille, cell, speaker, LRA). Every part is its own .wrl so the exploded view can move it alone.

    python3 enclosure.py           -> showcase/models/*.wrl

Coordinates: board mm, origin on the puck axis, +y towards 6 o'clock, z up from the board's top copper (the board
occupies z -1.6 .. 0). KiCad's VRML unit is 0.1 inch and its model y axis points the other way.
"""
import json
import math
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'design'))
import mechanical as m                                   # noqa: E402

OUT = HERE / 'models'
U = 1 / 2.54                                             # mm -> VRML unit

# palette: ODD JOBS monochrome, the board's ENIG gold as the one accent
BONE = (0.905, 0.890, 0.860)
BLACK = (0.035, 0.035, 0.038)
SOFT_BLACK = (0.075, 0.075, 0.080)
GLOSS_BLACK = (0.010, 0.010, 0.012)
GOLD = (0.78, 0.62, 0.30)
EYE = (0xF1 / 255, 0xEC / 255, 0xE2 / 255)               # COLOR_EYE in mao_character_draw.c
MOUTH = (0xC9 / 255, 0xC3 / 255, 0xB8 / 255)
CELL = (0.70, 0.71, 0.73)
STEEL = (0.55, 0.56, 0.58)
IR_FILTER = (0.16, 0.02, 0.03)


class Mesh:
    """Faces carry the direction they must face (board coordinates); write() orders each face so its normal
    points that way in VRML space, which is y-flipped (KiCad lights one side of a face)."""
    def __init__(self):
        self.v, self.f = [], []

    def vert(self, x, y, z):
        self.v.append((x, y, z))
        return len(self.v) - 1

    def quad(self, a, b, c, d, out):
        self.f.append(((a, b, c, d), out))

    def tri(self, a, b, c, out):
        self.f.append(((a, b, c), out))

    def oriented(self):
        res = []
        for f, (ox, oy, oz) in self.f:
            p = [(self.v[i][0], -self.v[i][1], self.v[i][2]) for i in f[:3]]
            u = [p[1][k] - p[0][k] for k in range(3)]; w = [p[2][k] - p[0][k] for k in range(3)]
            n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
            res.append(f if n[0] * ox - n[1] * oy + n[2] * oz >= 0 else tuple(reversed(f)))
        return res


def shape(mesh, color, spec=0.25, shin=0.3, transp=0.0, emissive=None, crease=0.6):
    pts = ', '.join('%.4f %.4f %.4f' % (x * U, -y * U, z * U) for x, y, z in mesh.v)
    idx = ', '.join(', '.join(str(i) for i in f) + ', -1' for f in mesh.oriented())
    em = 'emissiveColor %.3f %.3f %.3f ' % emissive if emissive else ''
    return ('Shape { appearance Appearance { material Material { diffuseColor %.3f %.3f %.3f specularColor %.3f %.3f %.3f '
            'shininess %.2f transparency %.2f %s} } geometry IndexedFaceSet { creaseAngle %.2f solid TRUE '
            'coord Coordinate { point [ %s ] } coordIndex [ %s ] } }'
            % (color + (spec, spec, spec) + (shin, transp, em, crease, pts, idx)))


def write(name, shapes):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / (name + '.wrl')).write_text('#VRML V2.0 utf8\n' + '\n'.join(shapes) + '\n')


def tube(mesh, r0, r1, z0, z1, a0=0.0, a1=360.0, n=None, caps=True):
    """Annular sector r0..r1, z0..z1, angles clockwise from 12 o'clock (degrees)."""
    n = n or max(8, int(abs(a1 - a0) / 2.5))
    full = abs(a1 - a0) >= 360.0
    rings = []
    for k in range(n + (0 if full else 1)):
        a = math.radians(a0 + (a1 - a0) * k / n)
        s, c = math.sin(a), -math.cos(a)
        rings.append([mesh.vert(r * s, r * c, z) for r, z in ((r0, z0), (r1, z0), (r1, z1), (r0, z1))])
    seq = rings + ([rings[0]] if full else [])
    angs = [a0 + (a1 - a0) * k / n for k in range(n + (0 if full else 1))]
    angs = angs + ([angs[0] + 360.0] if full else [])
    for k, (p, q) in enumerate(zip(seq, seq[1:])):
        am = math.radians((angs[k] + angs[k + 1]) / 2)
        rad = (math.sin(am), -math.cos(am), 0.0)
        outs = [(0, 0, -1), rad, (0, 0, 1), (-rad[0], -rad[1], 0.0)]
        for i in range(4):
            j = (i + 1) % 4
            if not caps and i in (0, 2):
                continue
            mesh.quad(p[i], q[i], q[j], p[j], outs[i])
    if not full and caps:
        for ring, a, sgn in ((rings[0], a0, -1), (rings[-1], a1, 1)):
            t = math.radians(a)
            mesh.quad(*ring, out=(sgn * math.cos(t), sgn * math.sin(t), 0.0))


def disc(mesh, r, z0, z1, cx=0.0, cy=0.0, n=96):
    top, bot = [], []
    for k in range(n):
        a = 2 * math.pi * k / n
        x, y = cx + r * math.sin(a), cy - r * math.cos(a)
        bot.append(mesh.vert(x, y, z0)); top.append(mesh.vert(x, y, z1))
    ct, cb = mesh.vert(cx, cy, z1), mesh.vert(cx, cy, z0)
    for k in range(n):
        j = (k + 1) % n
        am = 2 * math.pi * (k + 0.5) / n
        mesh.tri(ct, top[k], top[j], (0, 0, 1)); mesh.tri(cb, bot[j], bot[k], (0, 0, -1))
        mesh.quad(bot[k], bot[j], top[j], top[k], (math.sin(am), -math.cos(am), 0))


def box(mesh, x0, y0, z0, x1, y1, z1):
    p = [mesh.vert(x, y, z) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    for (a, b, c, d), out in (((0, 1, 3, 2), (0, 0, -1)), ((4, 6, 7, 5), (0, 0, 1)), ((0, 4, 5, 1), (0, -1, 0)),
                              ((2, 3, 7, 6), (0, 1, 0)), ((0, 2, 6, 4), (-1, 0, 0)), ((1, 5, 7, 3), (1, 0, 0))):
        mesh.quad(p[a], p[b], p[c], p[d], out)


def rounded_rect(mesh, cx, cy, w, h, z0, z1, n=10):
    """Capsule-ish rounded rectangle (radius = half the short side), like LV_RADIUS_CIRCLE."""
    r = min(w, h) / 2
    pts = []
    for (ox, oy, a0) in ((w / 2 - r, h / 2 - r, 0), (-(w / 2 - r), h / 2 - r, 90), (-(w / 2 - r), -(h / 2 - r), 180),
                         (w / 2 - r, -(h / 2 - r), 270)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((cx + ox + r * math.cos(a), cy + oy + r * math.sin(a)))
    prism(mesh, pts, z0, z1)


def prism(mesh, pts, z0, z1):
    """Extrude a simple (star-shaped from its centroid) polygon."""
    cx = sum(p[0] for p in pts) / len(pts); cy = sum(p[1] for p in pts) / len(pts)
    top = [mesh.vert(x, y, z1) for x, y in pts]; bot = [mesh.vert(x, y, z0) for x, y in pts]
    ct, cb = mesh.vert(cx, cy, z1), mesh.vert(cx, cy, z0)
    n = len(pts)
    for k in range(n):
        j = (k + 1) % n
        mesh.tri(ct, top[k], top[j], (0, 0, 1)); mesh.tri(cb, bot[j], bot[k], (0, 0, -1))
        mx, my = (pts[k][0] + pts[j][0]) / 2 - cx, (pts[k][1] + pts[j][1]) / 2 - cy
        mesh.quad(bot[k], bot[j], top[j], top[k], (mx, my, 0))


def polygon_fill(mesh, poly, z, out=(0, 0, 1)):
    """Ear-clipping triangulation of a simple polygon at height z (for the ODD JOBS mark)."""
    pts = list(poly)
    area = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
    if area < 0:
        pts.reverse()
    idx = [mesh.vert(x, y, z) for x, y in pts]
    rem = list(range(len(pts)))
    def inside(p, a, b, c):
        def s(p1, p2, p3): return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
        d1, d2, d3 = s(p, a, b), s(p, b, c), s(p, c, a)
        return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))
    guard = 0
    while len(rem) > 3 and guard < 20000:
        guard += 1
        for i in range(len(rem)):
            a, b, c = rem[i - 1], rem[i], rem[(i + 1) % len(rem)]
            pa, pb, pc = pts[a], pts[b], pts[c]
            if (pb[0] - pa[0]) * (pc[1] - pa[1]) - (pb[1] - pa[1]) * (pc[0] - pa[0]) <= 0:
                continue
            if any(inside(pts[k], pa, pb, pc) for k in rem if k not in (a, b, c)):
                continue
            mesh.tri(idx[a], idx[b], idx[c], out); rem.pop(i); break
        else:
            break
    if len(rem) == 3:
        mesh.tri(*[idx[k] for k in rem], out=out)


# ---- stack (mechanical.py; z from the board's top copper) -----------------------------------------------------
R_OUT = m.PUCK_OD / 2                       # 32.0
R_IN = R_OUT - m.WALL                       # 30.0
Z_FLOOR = -1.6 - m.ZONE_B_MAX_H - 0.3 - m.BATTERY_ENVELOPE[2]   # cell bottom: -10.2
Z_BOTTOM = Z_FLOOR - 1.2                    # base floor underside: -11.4
Z_WIN = m.WINDOW_Z                          # window underside 4.9
Z_TOP = Z_WIN + 1.0 + 0.6                   # rim top 6.5
Z_SPLIT = 1.8                               # base meets the ring just above the Hall sensors' ring lip
R_WIN = 23.5


def base():
    shapes = []
    shell = Mesh()
    # wall with the USB-C opening at 12 o'clock (receptacle on B: z -4.8 .. -1.6) and IR windows at +-21 deg
    usb_half = math.degrees(4.6 / R_OUT)
    tube(shell, R_IN, R_OUT, Z_BOTTOM, Z_SPLIT, usb_half, 360 - usb_half)
    tube(shell, R_IN, R_OUT, Z_BOTTOM, -5.0, -usb_half, usb_half)
    tube(shell, R_IN, R_OUT, -1.4, Z_SPLIT, -usb_half, usb_half)
    disc(shell, R_IN + 0.05, Z_BOTTOM, Z_BOTTOM + 1.2)
    shapes.append(shape(shell, BONE, spec=0.08, shin=0.15))
    # USB-C opening chamfer shadow (a dark liner inside the opening)
    liner = Mesh()
    tube(liner, R_IN - 0.02, R_OUT - 0.4, -5.0, -1.4, -usb_half + 0.3, usb_half - 0.3, caps=True)
    shapes.append(shape(liner, SOFT_BLACK, spec=0.05))
    # IR windows: deep red IR filters flush in the wall at the LED heights
    for a in m.IR_TX_ANGLES:
        ir = Mesh()
        tube(ir, R_OUT - 0.3, R_OUT + 0.02, -3.6, -1.0, a - 2.6, a + 2.6, n=6)
        shapes.append(shape(ir, IR_FILTER, spec=0.6, shin=0.8))
    # speaker grille at 9 o'clock: 7 slots
    gr = Mesh()
    for k in range(7):
        a = 270 - 9 + 3 * k
        tube(gr, R_OUT - 0.25, R_OUT + 0.02, -9.0, -4.0, a - 0.55, a + 0.55, n=2)
    shapes.append(shape(gr, BLACK, spec=0.02))
    # feet and the ODD JOBS mark on the underside
    feet = Mesh()
    for a in (45, 135, 225, 315):
        x, y = 22 * math.sin(math.radians(a)), -22 * math.cos(math.radians(a))
        disc(feet, 2.2, Z_BOTTOM - 0.4, Z_BOTTOM, x, y, n=40)
    shapes.append(shape(feet, SOFT_BLACK, spec=0.08))
    shapes.append(mark_shape(Z_BOTTOM - 0.02, 20.0, (0.0, 0.0), BLACK, flip=True))
    # a thin gold line where base meets ring (the accent)
    line = Mesh()
    tube(line, R_OUT - 0.05, R_OUT + 0.03, Z_SPLIT - 0.25, Z_SPLIT, n=180)
    shapes.append(shape(line, GOLD, spec=0.7, shin=0.6))
    write('base', shapes)


def mark_shape(z, width, centre, color, flip=False):
    """The ODD JOBS mark (brand/odd-jobs-symbol.json), filled, width mm, centred."""
    d = json.loads((HERE.parent / 'brand' / 'odd-jobs-symbol.json').read_text())
    pts = d['points']
    # outlines are stored as one list of [x, y] pixels per outline, or a flat list with separators
    outlines = pts if pts and isinstance(pts[0][0], (list, tuple)) else [pts]
    xs = [p[0] for o in outlines for p in o]; ys = [p[1] for o in outlines for p in o]
    sx = width / (max(xs) - min(xs)); cx0, cy0 = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    mesh = Mesh()
    for o in outlines:
        poly = [((-1 if flip else 1) * (p[0] - cx0) * sx + centre[0], (p[1] - cy0) * sx + centre[1]) for p in o]
        polygon_fill(mesh, poly, z, (0, 0, -1) if flip else (0, 0, 1))
    return shape(mesh, color, spec=0.1)


def ring():
    shapes = []
    body = Mesh()
    tube(body, 24.0, R_OUT - 0.4, Z_SPLIT, Z_TOP, n=180)
    shapes.append(shape(body, BLACK, spec=0.12, shin=0.2))
    knurl = Mesh()                                       # TPU grip band: 120 ribs
    for k in range(120):
        a = 3.0 * k
        tube(knurl, R_OUT - 0.45, R_OUT + 0.15, Z_SPLIT + 0.6, Z_TOP - 0.6, a - 0.85, a + 0.85, n=2)
    shapes.append(shape(knurl, SOFT_BLACK, spec=0.05, shin=0.1))
    dot = Mesh()                                         # index dot at 12 o'clock
    disc(dot, 0.55, Z_TOP, Z_TOP + 0.03, 0.0, -28.2, n=24)
    shapes.append(shape(dot, GOLD, spec=0.8, shin=0.7))
    write('ring', shapes)


def bezel():
    b = Mesh()
    tube(b, R_WIN - 0.6, 24.0, Z_WIN + 1.0, Z_TOP, n=180)
    tube(b, R_WIN, 24.0, Z_WIN - 0.6, Z_WIN + 1.0, n=180)
    write('bezel', [shape(b, BONE, spec=0.1, shin=0.2)])


def window():
    shapes = []
    clear = Mesh()                                       # the clear centre is its own model (glass.wrl): KiCad's
    disc(clear, 18.4, Z_WIN, Z_WIN + 1.0, n=120)         # raytracer veils what lies under a transparent face, and
    write('glass', [shape(clear, (0.92, 0.95, 0.97), spec=0.6, shin=0.95, transp=0.96)])   # real glass reads clear
    border = Mesh()                                      # back-printed black border
    tube(border, 18.4, R_WIN, Z_WIN, Z_WIN + 1.0, n=180)
    shapes.append(shape(border, GLOSS_BLACK, spec=0.9, shin=0.95))
    # sensor apertures in the border: ToF (11), light (1), IR receive (3 o'clock); mic hole
    ap = Mesh()
    for a, r in ((m.TOF_ANGLE, 1.5), (m.ALS_ANGLE, 1.0), (m.IR_RX_ANGLE, 1.6)):
        x, y = m.SENSOR_R * math.sin(math.radians(a)), -m.SENSOR_R * math.cos(math.radians(a))
        disc(ap, r, Z_WIN + 1.0, Z_WIN + 1.02, x, y, n=32)
    x, y = m.SENSOR_R * math.sin(math.radians(m.MIC_ANGLE)), -m.SENSOR_R * math.cos(math.radians(m.MIC_ANGLE))
    disc(ap, 0.4, Z_WIN + 1.0, Z_WIN + 1.02, x, y, n=16)
    shapes.append(shape(ap, (0.10, 0.10, 0.12), spec=0.9, shin=0.9))
    write('window', shapes)


def display(blink=1.0, look=(0, 0), squint=0.0):
    """The round panel showing MAO's face as the firmware draws it (240 px over 32.4 mm, eyes 18 x 26 px at +-26 px,
    mouth 12 x 3 px 26 px below), warm off-white on black. blink 1 = open, 0 = closed; look = eye offset in px;
    squint 0..1 = the firmware's squash (wider, flatter: pleased)."""
    shapes = []
    panel = Mesh()
    disc(panel, m.DISPLAY_OUTLINE_R, m.DISPLAY_STANDOFF, m.DISPLAY_STANDOFF + 1.56, n=120)
    shapes.append(shape(panel, SOFT_BLACK, spec=0.3, shin=0.5))
    active = Mesh()
    disc(active, m.DISPLAY_ACTIVE_D / 2, m.DISPLAY_STANDOFF + 1.56, m.DISPLAY_STANDOFF + 1.57, n=120)
    shapes.append(shape(active, GLOSS_BLACK, spec=0.6, shin=0.9))
    k = m.DISPLAY_ACTIVE_D / 240                         # mm per pixel
    z = m.DISPLAY_STANDOFF + 1.575
    eyes = Mesh()
    h = max(0.2, 26 * k * blink * (1.0 - 0.60 * squint))
    ew = 18 * k * (1.0 + 0.30 * squint)
    for sx in (-1, 1):
        rounded_rect(eyes, sx * 26 * k + look[0] * k, look[1] * k, ew, h, z, z + 0.01)
    shapes.append(shape(eyes, EYE, spec=0.0, emissive=EYE))
    mouth = Mesh()
    rounded_rect(mouth, look[0] * k * 0.5, 26 * k + look[1] * k * 0.5, 12 * k, 3 * k, z, z + 0.01, n=4)
    shapes.append(shape(mouth, MOUTH, spec=0.0, emissive=tuple(c * 0.85 for c in MOUTH)))
    write(display_name(blink, look, squint), shapes)


def display_name(blink=1.0, look=(0, 0), squint=0.0):
    if blink == 1.0 and tuple(look) == (0, 0) and squint == 0.0:
        return 'display'
    return 'display_b%03d_x%+03d_y%+03d_s%03d' % (round(blink * 100), look[0], look[1], round(squint * 100))


def carrier():
    c = Mesh()
    tube(c, m.DISPLAY_OUTLINE_R - 3.0, m.DISPLAY_OUTLINE_R + 0.6, 0.35, m.DISPLAY_STANDOFF, n=120)
    for a in (60, 180, 300):                            # the three flexure arms
        tube(c, m.DISPLAY_OUTLINE_R + 0.6, 22.6, 1.6, 2.4, a - 4, a + 4, n=6)
    write('carrier', [shape(c, (0.20, 0.20, 0.21), spec=0.05)])


def cell():
    shapes = []
    w, d, h = m.BATTERY_ENVELOPE
    cx, cy = m.BATTERY_CENTRE
    c = Mesh()
    box(c, cx - w / 2, cy - d / 2, Z_FLOOR, cx + w / 2, cy + d / 2, Z_FLOOR + h)
    shapes.append(shape(c, CELL, spec=0.5, shin=0.6))
    lab = Mesh()
    box(lab, cx - w / 2 + 3, cy - d / 2 + 4, Z_FLOOR + h, cx + w / 2 - 3, cy + d / 2 - 4, Z_FLOOR + h + 0.02)
    shapes.append(shape(lab, (0.12, 0.30, 0.62), spec=0.2))
    ins = Mesh()                                          # Kapton insulator on top
    box(ins, cx - w / 2 + 0.5, cy - d / 2 + 0.5, Z_FLOOR + h + 0.02, cx + w / 2 - 0.5, cy + d / 2 - 0.5, Z_FLOOR + h + 0.3)
    shapes.append(shape(ins, (0.85, 0.55, 0.15), spec=0.4, shin=0.5, transp=0.35))
    write('cell', shapes)


def speaker():
    sw, sl, sh = m.SPEAKER_SIZE
    sx, sy = m.SPEAKER_CENTRE
    s = Mesh()
    box(s, sx - sw / 2, sy - sl / 2, -1.85 - sh, sx + sw / 2, sy + sl / 2, -1.85)
    mem = Mesh()
    box(mem, sx - sw / 2 + 0.8, sy - sl / 2 + 0.8, -1.85 - sh - 0.02, sx + sw / 2 - 0.8, sy + sl / 2 - 0.8, -1.85 - sh)
    write('speaker', [shape(s, BLACK, spec=0.2), shape(mem, (0.30, 0.30, 0.32), spec=0.1)])


def lra():
    x, y = m.polar(m.LRA_R, m.LRA_ANGLE)
    c = Mesh()
    disc(c, 4.0, Z_FLOOR, Z_FLOOR + 3.2, x, y, n=60)
    write('lra', [shape(c, STEEL, spec=0.7, shin=0.7)])


if __name__ == '__main__':
    base(); ring(); bezel(); window(); display(); carrier(); cell(); speaker(); lra()
    print('enclosure models in', OUT, sorted(p.name for p in OUT.glob('*.wrl')))
