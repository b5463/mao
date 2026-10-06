"""Assembly drawing of MAO_MAIN A1, one A3 sheet per side (plain Python + matplotlib). ODD JOBS 177, 183.

    python asm_dump.py            (KiCad python: geometry to .cache/mao-routing/asm-dump.json)
    python assembly_drawing.py    (writes outputs/fab/ASSEMBLY-MAO_MAIN_A1-top.pdf / -bottom.pdf and PNG previews)

Every part's reference is printed, at 4.5:1 so a 0.42 mm board-scale reference is 1.9 mm on paper. Pads are drawn as
plain grey shapes (no pad numbers to collide with), part bodies as their Fab outline. A reference sits inside its
part where it fits (turned along a tall part); otherwise at the nearest free spot outside, inside or beyond the
board edge, with a leader to the part. No two references overlap, none covers another part's body, none crosses
the board edge line. The back is drawn as seen from the back (mirrored), as it is assembled and probed. DNP parts
are crossed out.
"""
import json
import math
import os
import sys
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mechanical as m                                   # noqa: E402
from netrules import CACHE, ROOT, NAME                   # noqa: E402

SCALE = 4.5
PAGE = (420.0, 297.0)
CENTRE = (148.5, 148.5)            # board centre on the sheet (mm); the right third holds the title and notes
H = 0.42                           # reference height, board mm
CW = 0.35                          # character advance, board mm (DejaVu Sans at H)
PT = H * SCALE / 0.72 / 25.4 * 72  # font size: cap height = 0.72 em


def bbox(pts):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return (min(xs), min(ys), max(xs), max(ys))


def overlap(a, b, g=0.0):
    return a[0] < b[2] + g and b[0] < a[2] + g and a[1] < b[3] + g and b[1] < a[3] + g


def near_pt(q, p):
    return (min(max(p[0], q[0]), q[2]), min(max(p[1], q[1]), q[3]))


def place(parts):
    """Reference boxes (board mm) and leaders for the parts of one side."""
    body = {}
    for p in parts:
        pts = [pt for poly in (p['court'] or p['fab'] or p['pads']) for pt in poly] or [(p['x'], p['y'])]
        body[p['ref']] = bbox(pts)
    placed, leaders, out = [], [], {}
    R = m.PCB_R

    def edge_cross(q):
        rs = [math.hypot(x, y) for x in (q[0], q[2]) for y in (q[1], q[3])]
        nx, ny = near_pt(q, (0.0, 0.0))
        rmin = math.hypot(nx, ny)
        return rmin < R + 0.15 and max(rs) > R - 0.15

    def free(q, ref):
        if any(overlap(q, o, 0.12) for o in placed): return False
        if any(overlap(q, body[r], 0.05) for r in body if r != ref): return False
        if edge_cross(q): return False
        return abs(q[0]) < R + 9 and abs(q[1]) < R + 9

    def crosses(a, c, ref):
        n = max(2, int(math.hypot(c[0] - a[0], c[1] - a[1]) / 0.1)); hits = 0
        for i in range(1, n):
            x = a[0] + (c[0] - a[0]) * i / n; y = a[1] + (c[1] - a[1]) * i / n
            q = (x - .05, y - .05, x + .05, y + .05)
            if any(overlap(q, o) for o in placed): return None
            hits += sum(1 for r in body if r != ref and overlap(q, body[r]))
        return hits

    order = sorted(parts, key=lambda p: (body[p['ref']][2] - body[p['ref']][0]) * (body[p['ref']][3] - body[p['ref']][1]))
    pending = []
    for p in order:                                # inside the part first
        b = body[p['ref']]; w, h = CW * len(p['ref']) + 0.1, H + 0.12
        bw, bh = b[2] - b[0], b[3] - b[1]
        cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        for vert in ((False, True) if bw >= bh else (True, False)):
            tw, th = (h, w) if vert else (w, h)
            if tw <= bw - 0.05 and th <= bh - 0.05:
                q = (cx - tw / 2, cy - th / 2, cx + tw / 2, cy + th / 2)
                if not any(overlap(q, o, 0.05) for o in placed):
                    placed.append(q); out[p['ref']] = (q, vert, None); break
        else:
            pending.append(p)
    for p in pending:                              # then outside, nearest free spot with a leader
        b = body[p['ref']]; w, h = CW * len(p['ref']) + 0.1, H + 0.12
        best = None
        for vert in (False, True):
            tw, th = (h, w) if vert else (w, h)
            for k in range(1, 60):
                g = 0.15 * k
                cands = []
                for t in range(-12, 13):
                    cx = (b[0] + b[2]) / 2 + t * 0.25; cy = (b[1] + b[3]) / 2 + t * 0.25
                    cands += [(cx, b[1] - g - th / 2), (cx, b[3] + g + th / 2), (b[0] - g - tw / 2, cy), (b[2] + g + tw / 2, cy)]
                for c in cands:
                    q = (c[0] - tw / 2, c[1] - th / 2, c[0] + tw / 2, c[1] + th / 2)
                    if not free(q, p['ref']): continue
                    e = near_pt(b, c); s = near_pt((q[0] - .1, q[1] - .1, q[2] + .1, q[3] + .1), e)
                    L = math.hypot(e[0] - s[0], e[1] - s[1])
                    x = crosses(s, e, p['ref']) if L > 0.05 else 0
                    if x is None: continue
                    score = L + 1.5 * x + (0.2 if vert else 0)
                    if best is None or score < best[0]: best = (score, q, vert, (s, e) if L > 0.3 else None)
                if best and g > best[0] + 1.0: break
        if best:
            placed.append(best[1]); out[p['ref']] = (best[1], best[2], best[3])
        else:
            c = ((b[0] + b[2]) / 2, (b[1] + b[3]) / 2); q = (c[0] - w / 2, c[1] - h / 2, c[0] + w / 2, c[1] + h / 2)
            out[p['ref']] = (q, False, None); print('assembly drawing: no free spot for', p['ref'], flush=True)
    return out, body


def draw(side, data, path):
    parts = [p for p in data['parts'] if p['side'] == side]
    mir = -1 if side == 'B' else 1
    X = lambda x: CENTRE[0] + mir * x * SCALE
    Y = lambda y: CENTRE[1] - y * SCALE
    labels, body = place(parts)
    fig = plt.figure(figsize=(PAGE[0] / 25.4, PAGE[1] / 25.4))
    ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, PAGE[0]); ax.set_ylim(0, PAGE[1]); ax.axis('off')
    ax.add_patch(plt.Rectangle((8, 8), PAGE[0] - 16, PAGE[1] - 16, fill=False, lw=0.6))
    for line in data['edge']:
        ax.plot([X(x) for x, _ in line], [Y(y) for _, y in line], color='black', lw=0.9)
    for p in parts:
        for poly in p['pads']:
            ax.add_patch(Polygon([(X(x), Y(y)) for x, y in poly], closed=True, fc='#d6d6d6', ec='#9a9a9a', lw=0.2))
        for line in p['fab']:
            ax.plot([X(x) for x, _ in line], [Y(y) for _, y in line], color='#303030', lw=0.35)
        if p['dnp']:
            b = body[p['ref']]
            ax.plot([X(b[0]), X(b[2])], [Y(b[1]), Y(b[3])], color='black', lw=0.5)
            ax.plot([X(b[0]), X(b[2])], [Y(b[3]), Y(b[1])], color='black', lw=0.5)
    for ref, (q, vert, leader) in labels.items():
        cx, cy = (q[0] + q[2]) / 2, (q[1] + q[3]) / 2
        ax.text(X(cx), Y(cy), ref, fontsize=PT, family='DejaVu Sans', ha='center', va='center',
                rotation=90 if vert else 0)
        if leader:
            (sx, sy), (ex, ey) = leader
            ax.plot([X(sx), X(ex)], [Y(sy), Y(ey)], color='black', lw=0.25)
            ax.plot([X(ex)], [Y(ey)], marker='o', ms=0.9, color='black')
    tx = 300.0
    lines = [('MAO_MAIN A1', 14, 'bold'), ('Assembly drawing, %s' % ('top (F.Cu, face side)' if side == 'F' else
                                                                    'bottom (B.Cu), seen from the back'), 10, 'normal'),
             ('', 6, 'normal'),
             ('Scale %.1f : 1 on A3. All %d parts on this side, every reference.' % (SCALE, len(parts)), 7, 'normal'),
             ('Pads grey, bodies as their Fab outline; a reference sits in its part', 7, 'normal'),
             ('or beside it with a leader (dot on the part). DNP parts crossed out.', 7, 'normal'),
             ('Values, MPN and LCSC numbers: BOM-%s-JLC.csv.' % NAME, 7, 'normal'),
             ('Placement: CPL-%s-JLC.csv. Board rev A1, 2026-10.' % NAME, 7, 'normal'),
             ('', 6, 'normal'), ('ODD JOBS', 9, 'bold'), ('design-as-code: hardware/mao/design', 7, 'normal')]
    y = 270.0
    for s, size, weight in lines:
        ax.text(tx, y, s, fontsize=size, fontweight=weight, family='DejaVu Sans', ha='left', va='top'); y -= size * 0.55 + 2.2
    ax.text(X(0), Y(-m.PCB_R) + 4, '12 o\'clock: USB-C', fontsize=6, ha='center', va='bottom', color='#505050')
    ax.text(X(0), Y(m.PCB_R) - 4, '6 o\'clock: antenna notch', fontsize=6, ha='center', va='top', color='#505050')
    fig.savefig(path, format='pdf')
    fig.savefig(str(path).replace('.pdf', '.png'), dpi=150)
    plt.close(fig)
    return sum(1 for v in labels.values() if v[2]), len(labels)


def main():
    data = json.loads((CACHE / 'asm-dump.json').read_text())
    out = ROOT / 'outputs' / 'fab'
    out.mkdir(parents=True, exist_ok=True)
    for side, tag in (('F', 'top'), ('B', 'bottom')):
        nlead, n = draw(side, data, out / ('ASSEMBLY-%s-%s.pdf' % (NAME, tag)))
        print('assembly drawing %s: %d references, %d with a leader' % (tag, n, nlead), flush=True)


if __name__ == '__main__':
    main()
