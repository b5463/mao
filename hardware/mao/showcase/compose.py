"""Compose the MAO showcase film (routerenv python: numpy + PIL): build/out/NNNN.jpg, 1920 x 1080, 30 fps, 57 s.

    python compose.py all [workers]          every frame
    python compose.py range a b [workers]    frames a .. b-1
    python compose.py frames 120 400 ...     single frames into test/c_NNNN.jpg, for checking

The 3D comes from shots.py (build/frames/<segment>), the macro shots from build/stills, the callout anchors from
locate.py (build/labels.json). Timing matches audio.py: brand 0-3, form 3-9, layers 9-15, board 15-27,
details 27-42, interaction 42-51, end card 51-57.
"""
import json
import math
import sys
from functools import lru_cache
from multiprocessing import Pool
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
B = HERE / 'build'
FR, ST, OUT = B / 'frames', B / 'stills', B / 'out'
W, H, FPS = 1920, 1080, 30
DUR = 57.0
N = int(DUR * FPS)

TEXT = (242, 238, 230)
GREY = (154, 154, 154)
GOLD = (201, 162, 74)
HN = '/System/Library/Fonts/HelveticaNeue.ttc'
MONO = '/System/Library/Fonts/SFNSMono.ttf'
HN_INDEX = {'regular': 0, 'bold': 1, 'ultralight': 5, 'light': 7, 'medium': 10, 'thin': 12}
LEFT = 150                                   # text column


# ---- timing helpers -----------------------------------------------------------------------------------------------
def ease(t):
    t = min(1.0, max(0.0, t))
    return t * t * t * (t * (6 * t - 15) + 10)


def lerp(a, b, t):
    return a + (b - a) * t


def ramp(t, t0, d):
    return ease((t - t0) / d)


def window(t, t_in, t_out, d_in=0.6, d_out=0.5):
    """0 -> 1 from t_in, back to 0 by t_out."""
    return ramp(t, t_in, d_in) * (1.0 - ramp(t, t_out - d_out, d_out))


# ---- type ---------------------------------------------------------------------------------------------------------
@lru_cache(None)
def font(kind, size):
    if kind == 'mono' or kind.startswith('mono-'):
        f = ImageFont.truetype(MONO, size)
        f.set_variation_by_name(kind.split('-')[1].title() if '-' in kind else 'Regular')
        return f
    return ImageFont.truetype(HN, size, index=HN_INDEX[kind])


@lru_cache(4096)
def text_mask(s, kind, size, tracking):
    """Coverage mask of a line of text; returns (mask, width, ascent, pad)."""
    f = font(kind, size)
    asc, desc = f.getmetrics()
    if tracking:
        advs = [f.getlength(c) for c in s]
        width = sum(advs) + tracking * (len(s) - 1)
    else:
        width = f.getlength(s)
    pad = 6
    m = Image.new('L', (int(math.ceil(width)) + 2 * pad, asc + desc + 2 * pad), 0)
    d = ImageDraw.Draw(m)
    if tracking:
        x = pad
        for c, a in zip(s, advs):
            d.text((x, pad), c, font=f, fill=255)
            x += a + tracking
    else:
        d.text((pad, pad), s, font=f, fill=255)
    return m, width, asc, pad


def text_width(s, kind, size, tracking=0.0):
    return text_mask(s, kind, size, tracking)[1]


def put(canvas, img, x, y):
    """alpha_composite that tolerates layers hanging off the canvas."""
    x, y = int(round(x)), int(round(y))
    sx0, sy0 = max(0, -x), max(0, -y)
    sx1, sy1 = min(img.width, W - x), min(img.height, H - y)
    if sx1 <= sx0 or sy1 <= sy0:
        return
    if (sx0, sy0, sx1, sy1) != (0, 0, img.width, img.height):
        img = img.crop((sx0, sy0, sx1, sy1))
    canvas.alpha_composite(img, (x + sx0, y + sy0))


def text(canvas, x, y, s, kind, size, color=TEXT, alpha=1.0, tracking=0.0, anchor='l'):
    """Draw s with its baseline at y; anchor l / m / r on x."""
    if alpha <= 0.004:
        return
    m, width, asc, pad = text_mask(s, kind, size, tracking)
    if anchor == 'm':
        x -= width / 2
    elif anchor == 'r':
        x -= width
    layer = Image.new('RGBA', m.size, color + (0,))
    layer.putalpha(m if alpha >= 0.999 else m.point(lambda v: int(v * alpha + 0.5)))
    put(canvas, layer, x - pad, y - asc - pad)


def hline(canvas, x0, x1, y, color, alpha, width=1):
    """Blended (ImageDraw on an RGBA canvas replaces pixels, alpha and all)."""
    if alpha <= 0.004 or x1 - x0 < 0.5:
        return
    put(canvas, Image.new('RGBA', (int(round(x1 - x0)) + 1, width), color + (int(255 * alpha),)), x0, y)


def vline(canvas, x, y0, y1, color, alpha, width=1):
    if alpha <= 0.004 or y1 - y0 < 0.5:
        return
    put(canvas, Image.new('RGBA', (width, int(round(y1 - y0)) + 1), color + (int(255 * alpha),)), x, y0)


@lru_cache(8)
def dot_img(r, color):
    ss = 4
    m = Image.new('L', ((2 * r + 2) * ss, (2 * r + 2) * ss), 0)
    ImageDraw.Draw(m).ellipse((ss, ss, (2 * r + 1) * ss, (2 * r + 1) * ss), fill=255)
    m = m.resize((2 * r + 2, 2 * r + 2), Image.LANCZOS)
    img = Image.new('RGBA', m.size, color + (0,))
    img.putalpha(m)
    return img


def dot(canvas, x, y, r, color, alpha):
    if alpha <= 0.004:
        return
    img = dot_img(r, color)
    if alpha < 0.999:
        img = img.copy()
        img.putalpha(img.getchannel('A').point(lambda v: int(v * alpha + 0.5)))
    put(canvas, img, x - r - 1, y - r - 1)


@lru_cache(None)
def mark(height, color):
    d = json.loads((HERE.parent / 'brand' / 'odd-jobs-symbol.json').read_text())
    pts = np.array(d['points'], float)
    (x0, y0), (x1, y1) = pts.min(0), pts.max(0)
    ss = 4
    s = height * ss / (y1 - y0)
    w, h = int((x1 - x0) * s) + 4 * ss, int(height * ss) + 4 * ss
    m = Image.new('L', (w, h), 0)
    ImageDraw.Draw(m).polygon([((x - x0) * s + 2 * ss, (y - y0) * s + 2 * ss) for x, y in pts], fill=255)
    m = m.resize((w // ss, h // ss), Image.LANCZOS)
    img = Image.new('RGBA', m.size, color + (0,))
    img.putalpha(m)
    return img


def put_mark(canvas, cx, cy, height, color=TEXT, alpha=1.0, anchor='m'):
    if alpha <= 0.004:
        return
    img = mark(height, color)
    if alpha < 0.999:
        img = img.copy()
        img.putalpha(img.getchannel('A').point(lambda v: int(v * alpha + 0.5)))
    x = cx - img.width / 2 if anchor == 'm' else cx - 2
    put(canvas, img, x, cy - img.height / 2)


# ---- backdrop, grain, vignette -----------------------------------------------------------------------------------
_yy, _xx = np.mgrid[0:H, 0:W].astype(np.float32)
_r = np.sqrt(((_xx - W * 0.5) / (W * 0.60)) ** 2 + ((_yy - H * 0.45) / (H * 0.80)) ** 2)


def _smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


_k = _smooth(0.0, 1.3, _r)[..., None]
BACKDROP = (np.array([35, 35, 39], np.float32) * (1 - _k) + np.array([11, 11, 13], np.float32) * _k)
VIGNETTE = (1.0 - 0.30 * _smooth(0.7, 1.35, _r))[..., None].astype(np.float32)
BACKDROP_IMG = Image.fromarray(BACKDROP.clip(0, 255).astype(np.uint8), 'RGB').convert('RGBA')


def backdrop():
    return BACKDROP_IMG.copy()


def finish(canvas, f, fade=1.0):
    a = np.asarray(canvas.convert('RGB'), np.float32) * VIGNETTE
    rng = np.random.default_rng(1000 + f)
    a += rng.normal(0.0, 2.0, (H, W, 1)).astype(np.float32)          # luminance grain, also dithers the gradient
    a *= fade
    return Image.fromarray(a.clip(0, 255).astype(np.uint8), 'RGB')


# ---- 3D layers ----------------------------------------------------------------------------------------------------
@lru_cache(8)
def seg_frame(seg, i):
    p = FR / seg / ('%04d.png' % i)
    if not p.exists():
        return None
    return Image.open(p).convert('RGBA')


SEG_LEN = {'hero': 180, 'explode': 180, 'board': 360, 'dial': 270}


def seg_at(seg, i):
    return seg_frame(seg, max(0, min(SEG_LEN[seg] - 1, i)))


def clip_above(img, margin=8):
    """Drop the soft floor shadow KiCad bakes above the object (it reads as a smear over the exploded stack)."""
    a = np.asarray(img.getchannel('A'))
    rows = np.nonzero((a > 200).any(axis=1))[0]
    if not len(rows) or rows[0] <= margin:
        return img
    a = a.copy()
    a[:rows[0] - margin] = 0
    img = img.copy()
    img.putalpha(Image.fromarray(a, 'L'))
    return img


@lru_cache(8)
def feather(size, edges='ltrb', d=70):
    """Alpha ramp at the frame's edges: the baked shadow stops at the render's border, which shows once scaled.
    Leave out an edge the object itself runs off (the dial close-up runs off the bottom)."""
    w, h = size
    big = np.full(max(w, h), 1e9, np.float32)
    fl = np.arange(w, dtype=np.float32) if 'l' in edges else big[:w]
    fr = np.arange(w, dtype=np.float32)[::-1] if 'r' in edges else big[:w]
    ft = np.arange(h, dtype=np.float32) if 't' in edges else big[:h]
    fb = np.arange(h, dtype=np.float32)[::-1] if 'b' in edges else big[:h]
    fx, fy = np.minimum(fl, fr) / d, np.minimum(ft, fb) / d
    return np.clip(fy[:, None], 0, 1) * np.clip(fx[None, :], 0, 1)


def place(canvas, img, s=1.0, dx=0.0, dy=0.0, alpha=1.0, edges='ltrb'):
    """The KiCad frame centred on the canvas, scaled by s about the centre, moved by (dx, dy)."""
    if img is None or alpha <= 0.004:
        return
    a = np.asarray(img.getchannel('A'), np.float32) * feather(img.size, edges)
    img = img.copy()
    img.putalpha(Image.fromarray(a.astype(np.uint8), 'L'))
    icx, icy = img.width / 2, img.height / 2
    a = 1.0 / s
    pm = img.convert('RGBa').transform((W, H), Image.AFFINE,
                                       (a, 0, icx - (W / 2 + dx) * a, 0, a, icy - (H / 2 + dy) * a),
                                       resample=Image.BICUBIC)
    lay = pm.convert('RGBA')
    if alpha < 0.999:
        lay.putalpha(lay.getchannel('A').point(lambda v: int(v * alpha + 0.5)))
    canvas.alpha_composite(lay)


def mapped(img_size, p, s, dx, dy):
    iw, ih = img_size
    return W / 2 + s * (p[0] - iw / 2) + dx, H / 2 + s * (p[1] - ih / 2) + dy


# ---- scenes -------------------------------------------------------------------------------------------------------
def brand(f, t):
    c = backdrop()
    out = 1.0 - ramp(t, 2.25, 0.55)
    a_mark = ramp(t, 0.30, 0.9) * out
    put_mark(c, W / 2, 470 - 8 * (1 - ramp(t, 0.30, 1.2)), 118, TEXT, a_mark)
    text(c, W / 2, 610, 'ODD JOBS', 'medium', 26, TEXT, ramp(t, 0.8, 0.7) * out, tracking=12, anchor='m')
    half = 70 * ramp(t, 1.15, 0.8)
    hline(c, W / 2 - half, W / 2 + half, 642, GOLD, out)
    text(c, W / 2, 690, 'PRESENTS', 'mono', 12, GREY, ramp(t, 1.5, 0.6) * out, tracking=5, anchor='m')
    return c


def form_layers_transform(t):
    """One continuous move across the form and layers scenes: right of the title, then left of the callouts."""
    k = ramp(t, 8.3, 3.7)
    drift = (t - 12.0) / 3.0 if t > 12.0 else 0.0
    return lerp(0.92, 0.86, k) + 0.01 * drift, lerp(250, -205, k) - 10 * drift, lerp(10, -12, k)


def form(f, t):
    c = backdrop()
    s, dx, dy = form_layers_transform(t)
    place(c, seg_at('hero', f - 90), s, dx, dy, ramp(t, 3.0, 0.7))
    out = 1.0 - ramp(t, 7.9, 0.6)

    def line(y, s_, kind, size, color, t0, tracking=0.0, rise=18):
        a = ramp(t, t0, 0.7) * out
        text(c, LEFT, y + rise * (1 - ramp(t, t0, 0.9)) - 8 * ramp(t, 7.9, 0.6), s_, kind, size, color, a, tracking)

    line(420, 'INTRODUCING', 'medium', 17, GOLD, 3.7, tracking=7)
    line(628, 'MAO', 'thin', 232, TEXT, 4.0, tracking=4, rise=30)
    line(700, 'The ODD JOBS handheld controller.', 'light', 34, TEXT, 4.7)
    line(748, 'With a face.', 'light', 34, GREY, 5.2)
    return c


CALLOUTS = [  # part, name, detail
    ('bezel', 'Bezel', 'BONE, PRINTED'),
    ('window', 'Face window', 'TOF  ·  LIGHT  ·  IR  ·  MIC'),
    ('ring', 'Dial ring', '30 MAGNETIC DETENTS'),
    ('display', 'Round display', '1.28 IN  ·  240 × 240'),
    ('board', 'MAO_MAIN A0', 'ESP32-S3  ·  4 LAYERS  ·  Ø58 MM'),
    ('base', 'Base', '500 MAH CELL  ·  LRA  ·  IR WINDOWS'),
]
LABELS = json.loads((B / 'labels.json').read_text()) if (B / 'labels.json').exists() else None


def layers(f, t):
    c = backdrop()
    s, dx, dy = form_layers_transform(t)
    i = f - 270
    img = seg_at('explode', i)
    place(c, clip_above(img) if img is not None else None, s, dx, dy)
    out = 1.0 - ramp(t, 14.45, 0.45)
    a = ramp(t, 9.5, 0.7) * out
    text(c, LEFT, 196 + 16 * (1 - ramp(t, 9.5, 0.9)), 'Built in layers.', 'light', 54, TEXT, a)
    text(c, LEFT + 2, 238, 'CONCEPT ENCLOSURE  ·  FDM, TWO MATERIALS', 'mono', 14, GREY, ramp(t, 9.9, 0.7) * out, tracking=1.5)
    if LABELS is None:
        return c
    u = min(1.0, max(0.0, (i - 90) / 89.0))
    size = img.size if img is not None else (1904, 1064)
    col = None
    pts = []
    for part, name, detail in CALLOUTS:
        p0, p1 = LABELS['90'][part], LABELS['179'][part]
        p = (lerp(p0['right'], p1['right'], u), lerp(p0['cy'], p1['cy'], u))
        pts.append(mapped(size, p, s, dx, dy))
    col = max(x for x, _ in pts) + 70
    for k, ((part, name, detail), (ax, ay)) in enumerate(zip(CALLOUTS, pts)):
        t0 = 12.15 + 0.13 * k
        grow = ramp(t, t0, 0.45)
        la = out * min(1.0, grow * 3)
        ax += 12
        dot(c, ax, ay, 3, GOLD, la)
        hline(c, ax + 6, ax + 6 + (col - 14 - ax - 6) * grow, ay, GREY, 0.75 * out)
        ta = ramp(t, t0 + 0.30, 0.45) * out
        nx = col + 8 * (1 - ramp(t, t0 + 0.30, 0.6))
        text(c, nx, ay + 7, name, 'medium', 21, TEXT, ta)
        text(c, nx + text_width(name, 'medium', 21) + 18, ay + 6, detail, 'mono', 14, (178, 176, 170), ta, tracking=1.2)
    return c


BOARD_FRONT = ['ESP32-S3 module, antenna at the edge', 'Distance, light, IR and a microphone',
               'A magnetic dial, read by two Hall sensors', 'The face is the button']
BOARD_BACK = ['USB-C charging, fuel gauge, 500 mAh cell', 'Speaker amplifier and haptic driver',
              'Six-axis motion sensor', 'Labelled service pads for bring-up']


def board(f, t):
    c = backdrop()
    place(c, seg_at('board', f - 450), 0.88, 285 - 10 * (t - 15) / 12, 0)
    out = 1.0 - ramp(t, 26.35, 0.45)
    a = ramp(t, 15.4, 0.7) * out
    text(c, LEFT, 318 + 16 * (1 - ramp(t, 15.4, 0.9)), 'MAO_MAIN A0', 'light', 60, TEXT, a)
    text(c, LEFT + 2, 360, 'Ø58 MM  ·  4 LAYERS  ·  1.6 MM FR-4', 'mono', 14, GREY, ramp(t, 15.7, 0.7) * out, tracking=1.5)
    for side, items, t_in, t_out, n0 in (('TOP', BOARD_FRONT, 16.0, 20.9, 1), ('BOTTOM', BOARD_BACK, 21.5, 26.8, 5)):
        so = 1.0 - ramp(t, t_out - 0.45, 0.45)
        text(c, LEFT + 2, 452, side, 'mono-medium', 13, GOLD, ramp(t, t_in, 0.5) * so, tracking=4)
        for k, s_ in enumerate(items):
            t0 = t_in + 0.25 + 0.28 * k
            ia = ramp(t, t0, 0.6) * so
            y = 512 + 58 * k
            x = LEFT + 14 * (1 - ramp(t, t0, 0.8))
            text(c, x + 2, y, '%02d' % (n0 + k), 'mono', 13, GOLD, ia, tracking=1)
            text(c, x + 46, y + 1, s_, 'light', 28, TEXT, ia)
    return c


MACROS = [  # still, caption, detail, (zoom0, zoom1), (centre0, centre1)
    ('macro-cat', 'Under the face, a sleeping cat. Boop.', 'F.SILK  ·  BESIDE THE FACE SWITCH',
     (1.04, 1.15), ((0.47, 0.52), (0.52, 0.50))),
    ('macro-mark', 'Signed.', 'ODD JOBS  —  MADE FOR BAD IDEAS', (1.13, 1.03), ((0.52, 0.50), (0.48, 0.50))),
    ('macro-field', 'A service field with names, not numbers.', 'RST  ·  BOOT  ·  SDA  ·  SCL  ·  3V3  ·  BAT',
     (1.04, 1.15), ((0.44, 0.56), (0.50, 0.50))),
    ('macro-lives', 'Nine lives. Hidden under the battery.', 'B.SILK  ·  FOR WHOEVER OPENS IT',
     (1.14, 1.04), ((0.50, 0.46), (0.50, 0.52))),
    ('macro-antenna', 'Antenna clear at the edge.', 'NO COPPER, NO PARTS UNDER IT', (1.04, 1.13), ((0.50, 0.44), (0.50, 0.50))),
]
M0, MD = 27.0, 3.0


@lru_cache(2)
def still(name):
    return Image.open(ST / (name + '.png')).convert('RGBA')


@lru_cache(None)
def scrim_bottom():
    a = _smooth(540, 1060, np.arange(H, dtype=np.float32))[:, None] * np.ones((1, W), np.float32)
    img = Image.new('RGBA', (W, H), (6, 6, 7, 0))
    img.putalpha(Image.fromarray((a * 0.90 * 255).astype(np.uint8), 'L'))     # captions stay legible over white parts
    return img


@lru_cache(None)
def scrim_left():
    a = 1.0 - _smooth(150, 1000, np.arange(W, dtype=np.float32))[None, :] * np.ones((H, 1), np.float32)
    img = Image.new('RGBA', (W, H), (8, 8, 10, 0))
    img.putalpha(Image.fromarray((a * 0.55 * 255).astype(np.uint8), 'L'))
    return img


def macro_frame(k, t):
    name, cap, detail, (z0, z1), (c0, c1) = MACROS[k]
    im = still(name)
    u = (t - (M0 + MD * k) + 0.25) / (MD + 0.5)               # covers the crossfades on both sides
    z = lerp(z0, z1, u)
    ch = im.height / z
    cw = ch * W / H
    cx = lerp(c0[0], c1[0], ease(u) * 0.5 + u * 0.5) * im.width
    cy = lerp(c0[1], c1[1], ease(u) * 0.5 + u * 0.5) * im.height
    cx = min(max(cx, cw / 2), im.width - cw / 2)
    cy = min(max(cy, ch / 2), im.height - ch / 2)
    box = (cx - cw / 2, cy - ch / 2, cx + cw / 2, cy + ch / 2)
    c = backdrop()
    c.alpha_composite(im.resize((W, H), Image.LANCZOS, box=box))
    c.alpha_composite(scrim_bottom())
    t0 = M0 + MD * k
    a = window(t, t0 + 0.35, t0 + MD - 0.05, 0.5, 0.35)
    rise = 12 * (1 - ramp(t, t0 + 0.35, 0.8))
    text(c, LEFT + 2, 902 + rise, '%02d / %02d' % (k + 1, len(MACROS)), 'mono-medium', 13, GOLD, a, tracking=2)
    text(c, LEFT, 962 + rise, cap, 'light', 44, TEXT, a)
    text(c, LEFT + 2, 1000 + rise, detail, 'mono', 14, GREY, a, tracking=1.5)
    return c


def details(f, t):
    k = int((t - M0) // MD)
    k = min(len(MACROS) - 1, max(0, k))
    edge = M0 + MD * (k + 1)
    if k < len(MACROS) - 1 and t > edge - 0.25:
        return Image.blend(macro_frame(k, t), macro_frame(k + 1, t), ease((t - (edge - 0.25)) / 0.5))
    start = M0 + MD * k
    if k > 0 and t < start + 0.25:
        return Image.blend(macro_frame(k - 1, t), macro_frame(k, t), ease((t - (start - 0.25)) / 0.5))
    return macro_frame(k, t)


DIAL_WORDS = [  # word, detail, time in
    ('Turn.', '30 MAGNETIC DETENTS', 42.85),
    ('Press.', 'THE WHOLE FACE IS THE BUTTON', 46.50),
    ('It looks back.', "THE FIRMWARE'S OWN FACE, 1:1", 48.30),
]


def interaction(f, t):
    c = backdrop()
    out = 1.0 - ramp(t, 50.3, 0.6)
    place(c, seg_at('dial', f - 1260), 1.02, 360, 0, out, edges='ltr')     # runs off the bottom edge
    sa = 1.0 - ramp(t, 50.3, 0.6)
    if sa > 0.004:
        sl = scrim_left().copy() if sa < 0.999 else scrim_left()
        if sa < 0.999:
            sl.putalpha(sl.getchannel('A').point(lambda v: int(v * sa)))
        c.alpha_composite(sl)
    for k, (word, detail, t0) in enumerate(DIAL_WORDS):
        nxt = DIAL_WORDS[k + 1][2] if k + 1 < len(DIAL_WORDS) else 99
        dim = 1.0 - 0.6 * ramp(t, nxt, 0.5)
        a = ramp(t, t0, 0.55) * out
        y = 400 + 170 * k + 20 * (1 - ramp(t, t0, 0.8))
        text(c, LEFT - 36, y, word, 'thin', 84, TEXT, a * dim)          # clear of the bezel at dx 360
        text(c, LEFT - 30, y + 38, detail, 'mono', 14, GREY, ramp(t, t0 + 0.25, 0.6) * out * dim, tracking=1.5)
    return c


def end(f, t):
    c = backdrop()
    im = still('puck-hero')
    a = ramp(t, 51.0, 0.9)
    sc = 0.32 * (0.985 + 0.03 * (t - 51.0) / 6.0)                 # the puck sits above the name, ~670 px wide
    w, h = int(im.width * sc), int(im.height * sc)
    small = im.resize((w, h), Image.LANCZOS)
    small.putalpha(Image.fromarray((np.asarray(small.getchannel('A'), np.float32) * feather(small.size, 'ltrb', 60) * a)
                                   .astype(np.uint8), 'L'))           # the baked shadow stops at the still's edge
    put(c, small, W / 2 - w / 2, 336 - h / 2 + 12 * (1 - ramp(t, 51.0, 1.2)))     # puck centre ~ y 390
    text(c, W / 2, 770, 'MAO', 'thin', 124, TEXT, ramp(t, 51.6, 0.8), tracking=18, anchor='m')
    half = 60 * ramp(t, 52.0, 0.7)
    hline(c, W / 2 - half, W / 2 + half, 808, GOLD, 1.0)
    text(c, W / 2, 856, 'MADE FOR BAD IDEAS.', 'medium', 17, TEXT, ramp(t, 52.3, 0.7), tracking=7, anchor='m')
    ba = ramp(t, 52.8, 0.7)
    mw = mark(26, TEXT).width
    lw = text_width('ODD JOBS', 'medium', 14, 5)
    x0 = W / 2 - (mw + 12 + lw) / 2
    put_mark(c, x0, 952, 26, TEXT, ba, anchor='l')
    text(c, x0 + mw + 12, 958, 'ODD JOBS', 'medium', 14, TEXT, ba, tracking=5)
    text(c, W / 2, 1030, 'MAO_MAIN A0  ·  CONCEPT ENCLOSURE, NOT FINAL  ·  2026-10', 'mono', 12, GREY,
         ramp(t, 53.1, 0.7) * 0.8, tracking=1.5, anchor='m')
    return c


SECTIONS = [(3.0, 9.0, '01  FORM'), (9.0, 15.0, '02  LAYERS'), (15.0, 27.0, '03  BOARD'), (27.0, 42.0, '04  DETAILS'),
            (42.0, 51.0, '05  INTERACTION')]


def chrome(c, t):
    """The frame furniture: brand bug, section index, footer, crop marks."""
    g = window(t, 3.4, 50.7, 0.6, 0.5)
    if g <= 0.004:
        return
    put_mark(c, 64, 52, 22, TEXT, 0.85 * g, anchor='l')
    text(c, 64 + mark(22, TEXT).width + 10, 60, 'ODD JOBS', 'medium', 13, TEXT, 0.85 * g, tracking=4)
    for t0, t1, s in SECTIONS:
        a = window(t, t0 + 0.15, t1 + 0.15, 0.4, 0.3) * g
        text(c, W - 64, 60, s, 'mono-medium', 13, GOLD, a, tracking=3, anchor='r')
    fa = 0.7 * g * (1.0 - window(t, 41.9, 51.2, 0.4, 0.4))       # off in the close-up: it would sit on the bezel
    text(c, W - 64, H - 40, 'MAO_MAIN A0  —  CONCEPT ENCLOSURE', 'mono', 12, GREY, fa, tracking=1.5, anchor='r')
    m, L = 30, 16
    for x, y, sx, sy in ((m, m, 1, 1), (W - m, m, -1, 1), (m, H - m, 1, -1), (W - m, H - m, -1, -1)):
        hline(c, min(x, x + sx * L), max(x, x + sx * L), y, (255, 255, 255), 0.22 * g)
        vline(c, x, min(y, y + sy * L), max(y, y + sy * L), (255, 255, 255), 0.22 * g)


SCENES = [(0.0, brand), (3.0, form), (9.0, layers), (15.0, board), (27.0, details), (42.0, interaction), (51.0, end)]
BLENDS = {15.0: 0.5, 27.0: 0.5, 42.0: 0.5}                   # crossfade lengths at these cuts


def scene_at(t):
    for t0, fn in reversed(SCENES):
        if t >= t0:
            return t0, fn
    return SCENES[0]


def frame(f):
    t = f / FPS
    t0, fn = scene_at(t)
    c = fn(f, t)
    for cut, d in BLENDS.items():                            # crossfade around the cut
        if cut - d / 2 <= t < cut + d / 2:
            prev = [fn_ for s_, fn_ in SCENES if s_ < cut][-1]
            nxt = [fn_ for s_, fn_ in SCENES if s_ >= cut][0]
            c = Image.blend(prev(f, t), nxt(f, t), ease((t - (cut - d / 2)) / d))
    chrome(c, t)
    fade = ramp(t, 0.0, 0.5) * (1.0 - ramp(t, 55.6, 1.35))
    return finish(c, f, fade)


def write(f):
    frame(f).save(OUT / ('%04d.jpg' % f), quality=95, subsampling=0)
    return f


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    if what == 'frames':
        for a in sys.argv[2:]:
            frame(int(a)).save(HERE / 'test' / ('c_%04d.jpg' % int(a)), quality=92)
    else:                                                    # all [workers] | range a b [workers]
        OUT.mkdir(parents=True, exist_ok=True)
        if what == 'range':
            todo, rest = range(int(sys.argv[2]), int(sys.argv[3])), sys.argv[4:]
            todo = [f for f in todo if not (OUT / ('%04d.jpg' % f)).exists()]      # resumable
        else:
            todo, rest = range(N), sys.argv[2:]
        workers = int(rest[0]) if rest else 4
        with Pool(workers) as p:
            for k, _ in enumerate(p.imap_unordered(write, todo, chunksize=8)):
                if k % 150 == 0:
                    print('compose %d / %d' % (k, len(todo)), flush=True)
        print('compose: %d frames in %s' % (len(todo), OUT))
