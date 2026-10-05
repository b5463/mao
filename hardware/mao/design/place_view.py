"""Placement review before any copper (system python + Pillow): overlaps, edge and keep-out hits,
ratsnest length and crossings, and a picture of both faces.

    place_view.py [out_dir]

Reads placement.PLACE through geo.py (pads and courtyards from the real footprints). For every net it
builds the minimum spanning tree of its pads; a connection is 'F', 'B' or 'FB' (changes face). Two
connections cross when their straight lines intersect on a face both use. Crossings are what placement
has to remove before routing (brief: "minimise crossings at placement level"); GND and +3V3 are left
out (planes). Writes place-F.png (face view) and place-B.png (seen from the back, mirrored).
"""
import itertools
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

import geo
import mechanical as m
from placement import PLACE, KEEP_F

PLANES = ('GND', '+3V3')
GAP = 0.2                     # courtyard to courtyard on one face


def mst(points):
    if len(points) < 2:
        return []
    inside, edges = {0}, []
    while len(inside) < len(points):
        best = None
        for i in inside:
            for j in range(len(points)):
                if j in inside:
                    continue
                d = math.dist(points[i][:2], points[j][:2])
                if best is None or d < best[0]:
                    best = (d, i, j)
        inside.add(best[2])
        edges.append((points[best[1]], points[best[2]]))
    return edges


def seg_cross(a, b, c, d):
    def orient(p, q, r):
        v = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
        return 0 if abs(v) < 1e-9 else (1 if v > 0 else -1)
    if max(a[0], b[0]) < min(c[0], d[0]) or max(c[0], d[0]) < min(a[0], b[0]):
        return False
    if max(a[1], b[1]) < min(c[1], d[1]) or max(c[1], d[1]) < min(a[1], b[1]):
        return False
    o1, o2, o3, o4 = orient(a, b, c), orient(a, b, d), orient(c, d, a), orient(c, d, b)
    return o1 * o2 < 0 and o3 * o4 < 0


def analyse():
    net = geo.netlist()
    pins = {}
    for ref, place in PLACE.items():
        for n, x, y, w, h, nm in geo.pads(ref, place):
            if nm and not nm.startswith('unconnected') and nm not in PLANES:
                pins.setdefault(nm, []).append((x, y, place[3], ref + '.' + n))
    conns = []
    for nm, pts in pins.items():
        for a, b in mst(pts):
            if math.dist(a[:2], b[:2]) < 1e-6:
                continue
            conns.append((nm, a, b, 'F' if a[2] == b[2] == 'F' else 'B' if a[2] == b[2] == 'B' else 'FB'))
    crossings = []
    for (n1, a, b, s1), (n2, c, d, s2) in itertools.combinations(conns, 2):
        if n1 == n2 or not (set(s1) & set(s2)):
            continue
        if seg_cross(a[:2], b[:2], c[:2], d[:2]):
            crossings.append((n1, n2))
    return conns, crossings


def overlaps():
    out = []
    boxes = {'F': [], 'B': []}
    for ref, place in PLACE.items():
        for b in geo.courtyard(ref, place):
            boxes[place[3]].append((ref, b))
    for side, bl in boxes.items():
        for (r1, a), (r2, b) in itertools.combinations(bl, 2):
            if r1 == r2 or r1.startswith(('H', 'FID')) and r2.startswith(('H', 'FID')):
                continue
            if a[0] < b[2] + GAP and b[0] < a[2] + GAP and a[1] < b[3] + GAP and b[1] < a[3] + GAP:
                ov = min(a[2], b[2]) - max(a[0], b[0]), min(a[3], b[3]) - max(a[1], b[1])
                out.append((side, r1, r2, round(min(ov) + GAP, 2)))
    worst = {}
    for side, r1, r2, d in out:
        k = (side,) + tuple(sorted((r1, r2)))
        worst[k] = max(worst.get(k, 0), d)
    return sorted((k + (d,) for k, d in worst.items()), key=lambda t: -t[3])


def edge_hits():
    out = []
    for ref, place in PLACE.items():
        if ref.startswith(('E', 'H', 'J101', 'U201')):
            continue
        for b in geo.courtyard(ref, place):
            for x, y in ((b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3])):
                if math.hypot(x, y) > m.PCB_R - 0.3:
                    out.append((ref, 'edge'))
                    break
        if place[3] == 'F':
            for k in KEEP_F:
                for b in geo.courtyard(ref, place):
                    if b[0] < k[2] and k[0] < b[2] and b[1] < k[3] and k[1] < b[3]:
                        out.append((ref, 'FPC fold keep-out'))
    return sorted(set(out))


COL = {'F': (220, 60, 60), 'B': (60, 110, 230), 'FB': (150, 60, 200)}


def draw(conns, out_dir, scale=24):
    S = int((2 * m.PCB_R + 4) * scale)
    font = ImageFont.load_default()
    for side in ('F', 'B'):
        img = Image.new('RGB', (S, S), (250, 250, 248))
        d = ImageDraw.Draw(img)
        mir = side == 'B'
        def P(x, y):
            return ((-x if mir else x) + m.PCB_R + 2) * scale, (y + m.PCB_R + 2) * scale
        c = P(0, 0)
        d.ellipse([c[0] - m.PCB_R * scale, c[1] - m.PCB_R * scale, c[0] + m.PCB_R * scale, c[1] + m.PCB_R * scale],
                  outline=(0, 0, 0), width=2)
        d.ellipse([c[0] - 17.8 * scale, c[1] - 17.8 * scale, c[0] + 17.8 * scale, c[1] + 17.8 * scale],
                  outline=(200, 200, 200), width=1)
        ny = m.NOTCH_Y
        a, b_ = P(-m.NOTCH_W / 2, ny), P(m.NOTCH_W / 2, m.PCB_R)
        d.rectangle([min(a[0], b_[0]), a[1], max(a[0], b_[0]), b_[1]], fill=(230, 230, 230))
        for ref, place in PLACE.items():
            own = place[3] == side
            for bx in geo.courtyard(ref, place):
                a, b_ = P(bx[0], bx[1]), P(bx[2], bx[3])
                d.rectangle([min(a[0], b_[0]), min(a[1], b_[1]), max(a[0], b_[0]), max(a[1], b_[1])],
                            outline=(40, 40, 40) if own else (205, 205, 205), width=2 if own else 1)
            if own:
                for n, x, y, w, h, nm in geo.pads(ref, place):
                    a, b_ = P(x - w / 2, y - h / 2), P(x + w / 2, y + h / 2)
                    fill = (150, 150, 150) if nm == 'GND' else (230, 150, 60) if nm == '+3V3' else (190, 170, 120)
                    d.rectangle([min(a[0], b_[0]), min(a[1], b_[1]), max(a[0], b_[0]), max(a[1], b_[1])], fill=fill)
                bx = geo.courtyard(ref, place)[0]
                t = P((bx[0] + bx[2]) / 2, (bx[1] + bx[3]) / 2)
                d.text((t[0] - 10, t[1] - 5), ref, fill=(0, 0, 0), font=font)
        for nm, a, b, s in conns:
            if side not in s:
                continue
            d.line([P(*a[:2]), P(*b[:2])], fill=COL[s], width=1)
        img.save(os.path.join(out_dir, 'place-%s.png' % side))


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out_dir, exist_ok=True)
    conns, crossings = analyse()
    ov, eh = overlaps(), edge_hits()
    total = sum(math.dist(a[:2], b[:2]) for _, a, b, _ in conns)
    by = {}
    for n1, n2 in crossings:
        for n in (n1, n2):
            by[n] = by.get(n, 0) + 1
    print('connections %d, ratsnest %.0f mm, crossings %d, face changes %d' % (
        len(conns), total, len(crossings), sum(1 for c in conns if c[3] == 'FB')))
    print('most crossed:', sorted(by.items(), key=lambda kv: -kv[1])[:25])
    from netrules import SLOW_OK
    hard = [(a, b) for a, b in crossings if a not in SLOW_OK and b not in SLOW_OK]
    hb = {}
    for n1, n2 in hard:
        hb.setdefault(n1, set()).add(n2); hb.setdefault(n2, set()).add(n1)
    print('outer-layer crossings (neither net may use L3): %d' % len(hard))
    for n, o in sorted(hb.items(), key=lambda kv: -len(kv[1]))[:20]:
        print('   %-14s x %s' % (n, ', '.join(sorted(o))))
    print('overlaps (%d):' % len(ov), ov[:60])
    print('edge / keep-out (%d):' % len(eh), eh)
    draw(conns, out_dir)


if __name__ == '__main__':
    main()
