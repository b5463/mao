"""Easter-egg line art for the MAO silk (silk.py). Local mm, y towards 6 o'clock, origin at the art centre: polylines
(stroked at silk.ART_STROKE) and filled dots (x, y, r). Pure geometry, previewed with PIL when run directly."""
import math

def arc(cx, cy, rx, ry, a0, a1, n=None):
    n = n or max(6, int(abs(a1 - a0) / 10))
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * k / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * k / n))) for k in range(n + 1)]

def on_circle(cx, cy, r, a):
    return (cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))

def sleeping_cat():
    """A cat asleep in a loaf, head down on its paws, tail round its front. ~6.6 x 3.4 mm.
    Returns (polylines, dots)."""
    hx, hy, hr = -2.2, -0.35, 1.0                      # head
    lines = []
    # back: from just behind the head's crown, over the back, down to the rump
    lines.append([on_circle(hx, hy, hr, -35)] + arc(0.55, 0.25, 2.55, 1.85, 200, 355)[1:] + [(3.15, 0.55)])
    # head outline, open where the back joins it
    lines.append(arc(hx, hy, hr, hr, -25, 300, 34))
    # ears: share the head outline at their bases
    lines.append([on_circle(hx, hy, hr, -150), (hx - 0.95, hy - 1.55), on_circle(hx, hy, hr, -105)])
    lines.append([on_circle(hx, hy, hr, -75), (hx + 0.35, hy - 1.6), on_circle(hx, hy, hr, -32)])
    # closed eyes, face on
    lines.append(arc(hx - 0.4, hy - 0.05, 0.24, 0.16, 15, 165, 6))
    lines.append(arc(hx + 0.4, hy - 0.05, 0.24, 0.16, 15, 165, 6))
    # tail: from the rump along the bottom to under the chin, tip curled up
    lines.append([(3.15, 0.55)] + arc(2.6, 0.55, 0.55, 0.5, 0, 90, 5)[1:] + [(-0.6, 1.05)] +
                 arc(-0.6, 0.65, 0.4, 0.4, 90, 210, 6)[1:])
    dots = [(hx, hy + 0.38, 0.1)]                      # nose
    return lines, dots

def paw(cx, cy, s=1.0):
    """One paw print, ~1.3 mm * s: a rounded main pad (closed outline) and four toe dots."""
    pad = arc(cx, cy + 0.2 * s, 0.33 * s, 0.26 * s, 0, 360, 16)
    toes = [(cx + dx * s, cy + dy * s, 0.12 * s) for dx, dy in ((-0.42, -0.2), (-0.16, -0.45), (0.16, -0.45), (0.42, -0.2))]
    return [pad], toes

def z_glyph(cx, cy, h):
    """A 'z' drawn as one stroke: top bar, diagonal, bottom bar (h tall, 0.75 h wide)."""
    w = 0.75 * h
    return [(cx - w / 2, cy - h / 2), (cx + w / 2, cy - h / 2), (cx - w / 2, cy + h / 2), (cx + w / 2, cy + h / 2)]


def zzz():
    """Three z of rising size drifting up and away from the sleeping head, as line art (no text rules apply)."""
    return [z_glyph(-3.6, -2.5, 0.6), z_glyph(-4.55, -3.45, 0.85), z_glyph(-5.7, -4.6, 1.1)]

if __name__ == '__main__':
    import sys
    from PIL import Image, ImageDraw, ImageFont
    S = 70
    img = Image.new('RGB', (14 * S, 9 * S), (18, 18, 18)); g = ImageDraw.Draw(img)
    T = lambda p: ((p[0] + 6.0) * S, (p[1] + 5.0) * S)
    lines, dots = sleeping_cat()
    for pl in lines:
        g.line([T(p) for p in pl], fill=(235, 235, 235), width=int(0.16 * S), joint='curve')
    for x, y, r in dots:
        g.ellipse([T((x - r, y - r)), T((x + r, y + r))], fill=(235, 235, 235))
    for x, y in ((4.6, 1.2), (5.6, 0.2), (6.8, 0.9)):
        pls, ds = paw(x, y)
        for pl in pls: g.line([T(p) for p in pl], fill=(235, 235, 235), width=int(0.16 * S))
        for px, py, r in ds: g.ellipse([T((px - r, py - r)), T((px + r, py + r))], fill=(235, 235, 235))
    for pl in zzz():
        g.line([T(p) for p in pl], fill=(235, 235, 235), width=int(0.16 * S))
    img.save(sys.argv[1])
