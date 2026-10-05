"""The stills set (routerenv python): every render from shots.py stills on the ODD JOBS backdrop, 3840 x 2160.

    python plates.py        -> out/stills/NN-name.jpg, out/stills/contact-sheet.jpg, out/renders/*.png (transparent)

The four edge views share one plate (board-edges). Type sizes are the film's, doubled for 4K.
"""
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import compose as cp

HERE = Path(__file__).resolve().parent
ST = HERE / 'build' / 'stills'
OUT = HERE / 'out' / 'stills'
RAW = HERE / 'out' / 'renders'
W, H = 3840, 2160
M = 128

PLATES = [  # name, title, detail
    ('board-top', 'Top side', 'MAO_MAIN A0  ·  F.CU  ·  FACES THE DISPLAY'),
    ('board-bottom', 'Bottom side', 'MAO_MAIN A0  ·  B.CU  ·  MODULE, USB-C, SERVICE FIELD'),
    ('board-edges', 'Four edges', 'TOP SIDE UP  ·  Ø58 MM  ·  1.6 MM  ·  4 LAYERS'),
    ('board-iso-face', 'Top side, three-quarter', 'ESP32-S3 ANTENNA AT 6 O\'CLOCK, IN ITS OWN NOTCH'),
    ('board-iso-back', 'Bottom side, three-quarter', 'USB-C, MODULE, SPEAKER AND THE SERVICE FIELD'),
    ('board-low-face', 'Top side, low', 'SENSORS RING THE DISPLAY AREA'),
    ('board-low-back', 'Bottom side, low', 'THE MODULE AND ITS ANTENNA NOTCH'),
    ('macro-cat', 'The cat', 'F.SILK  ·  BESIDE THE FACE SWITCH, SW301: BOOP'),
    ('macro-mark', 'The mark', 'ODD JOBS  —  MADE FOR BAD IDEAS  ·  S/N FIELD'),
    ('macro-field', 'Service field', 'RST  ·  BOOT  ·  SDA  ·  SCL  ·  3V3  ·  BAT: NAMES, NOT NUMBERS'),
    ('macro-lives', 'Nine lives', 'B.SILK  ·  UNDER THE BATTERY, FOR WHOEVER OPENS IT'),
    ('macro-antenna', 'Antenna keep-out', 'NO COPPER, NO PARTS UNDER THE ANTENNA'),
    ('macro-module', 'The module', 'ESP32-S3-WROOM-1-N8R2  ·  8 MB FLASH  ·  2 MB PSRAM'),
    ('puck-hero', 'MAO', 'CONCEPT ENCLOSURE  ·  Ø64 MM  ·  FDM, TWO MATERIALS'),
    ('puck-front', 'Front', 'BONE BASE  ·  BLACK DIAL RING  ·  GOLD SEAM'),
    ('puck-top', 'Top', 'THE FACE, THE WINDOW BORDER, THE INDEX DOT'),
    ('puck-face', 'Face', 'THE FIRMWARE\'S OWN FACE, 1:1 ON 240 × 240'),
    ('puck-under', 'Underside', 'FOUR FEET AND THE MARK'),
    ('puck-bottom', 'Bottom', 'FOUR FEET AND THE MARK'),
    ('puck-exploded', 'Exploded', 'BEZEL  ·  WINDOW  ·  RING  ·  DISPLAY  ·  BOARD  ·  CELL  ·  LRA  ·  BASE'),
]
EDGES = [('board-front', "FROM 6 O'CLOCK  ·  ANTENNA EDGE"), ('board-back', "FROM 12 O'CLOCK  ·  USB-C EDGE"),
         ('board-left', "FROM 9 O'CLOCK"), ('board-right', "FROM 3 O'CLOCK")]


def backdrop():
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    r = np.sqrt(((xx - W * 0.5) / (W * 0.60)) ** 2 + ((yy - H * 0.45) / (H * 0.80)) ** 2)
    k = cp._smooth(0.0, 1.3, r)[..., None]
    bg = np.array([35, 35, 39], np.float32) * (1 - k) + np.array([11, 11, 13], np.float32) * k
    vig = (1.0 - 0.30 * cp._smooth(0.7, 1.35, r))[..., None]
    return bg, vig


BG, VIG = backdrop()


def finish(c):
    a = np.asarray(c.convert('RGB'), np.float32) * VIG
    a += np.random.default_rng(7).normal(0.0, 0.8, (H, W, 1)).astype(np.float32)
    return Image.fromarray(a.clip(0, 255).astype(np.uint8), 'RGB')


def text(c, x, y, s, kind, size, color=cp.TEXT, alpha=1.0, tracking=0.0, anchor='l'):
    m, width, asc, pad = cp.text_mask(s, kind, size, tracking)
    if anchor == 'r':
        x -= width
    layer = Image.new('RGBA', m.size, color + (0,))
    layer.putalpha(m if alpha >= 0.999 else m.point(lambda v: int(v * alpha)))
    c.alpha_composite(layer, (int(round(x - pad)), int(round(y - asc - pad))))


def chrome(c, k, title, detail, scrim=False):
    if scrim:
        a = cp._smooth(H * 0.62, H, np.arange(H, dtype=np.float32))[:, None] * np.ones((1, W), np.float32)
        s = Image.new('RGBA', (W, H), (6, 6, 7, 0))
        s.putalpha(Image.fromarray((a * 0.75 * 255).astype(np.uint8), 'L'))
        c.alpha_composite(s)
    mk = cp.mark(44, cp.TEXT)
    c.alpha_composite(mk, (M, 104 - mk.height // 2))
    text(c, M + mk.width + 20, 120, 'ODD JOBS', 'medium', 26, cp.TEXT, 0.9, tracking=8)
    text(c, W - M, 120, 'MAO  ·  PLATE %02d' % k, 'mono-medium', 26, cp.GOLD, tracking=6, anchor='r')
    text(c, M, H - 190, title, 'light', 84, cp.TEXT)
    text(c, M + 4, H - 120, detail, 'mono', 28, cp.GREY, tracking=3)
    text(c, W - M, H - 120, 'MAO_MAIN A0  —  CONCEPT ENCLOSURE  ·  2026-10', 'mono', 24, cp.GREY, 0.7, tracking=3, anchor='r')
    m, L = 60, 32
    for x, y, sx, sy in ((m, m, 1, 1), (W - m, m, -1, 1), (m, H - m, 1, -1), (W - m, H - m, -1, -1)):
        c.alpha_composite(Image.new('RGBA', (L + 1, 2), (255, 255, 255, 56)), (min(x, x + sx * L), y - (0 if sy > 0 else 1)))
        c.alpha_composite(Image.new('RGBA', (2, L + 1), (255, 255, 255, 56)), (x - (0 if sx > 0 else 1), min(y, y + sy * L)))


def base():
    return Image.fromarray(BG.clip(0, 255).astype(np.uint8), 'RGB').convert('RGBA')


def centred(c, im):
    a = np.asarray(im.getchannel('A'), np.float32) * cp.feather(im.size, 'ltrb', 140)   # shadow cut at the render edge
    im = im.copy()
    im.putalpha(Image.fromarray(a.astype(np.uint8), 'L'))
    c.alpha_composite(im, ((W - im.width) // 2, (H - im.height) // 2))


def plate(k, name, title, detail):
    c = base()
    if name == 'board-edges':                                    # four strips, scaled so the tall parts fit
        top, step, band, sk = 120, 435, 600, 0.70
        for j, (e, label) in enumerate(EDGES):
            im = Image.open(ST / (e + '.png')).convert('RGBA')
            a = np.asarray(im.getchannel('A'))
            rows = np.nonzero((a > 200).sum(axis=1) > im.width * 0.5)[0]       # the board edge: the wide rows
            cy = int(rows.mean())
            strip = im.crop((0, cy - band // 2, im.width, cy + band // 2))
            strip = strip.resize((round(strip.width * sk), round(strip.height * sk)), Image.LANCZOS)
            y0 = top + step * j
            c.alpha_composite(strip, ((W - strip.width) // 2, y0))
            l1, _, l2 = label.partition('  ·  ')
            ym = y0 + strip.height // 2
            text(c, M + 4, ym - (6 if l2 else -10), l1, 'mono-medium', 24, cp.GOLD, tracking=4)
            if l2:
                text(c, M + 4, ym + 34, l2, 'mono', 22, cp.GREY, tracking=3)
    elif name.startswith('macro'):                               # full bleed: cover the plate, no feather
        im = Image.open(ST / (name + '.png')).convert('RGBA')
        k = max(W / im.width, H / im.height)
        im = im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
        x0, y0 = (im.width - W) // 2, (im.height - H) // 2
        c.alpha_composite(im.crop((x0, y0, x0 + W, y0 + H)))
    else:
        centred(c, cp.clip_above(Image.open(ST / (name + '.png')).convert('RGBA'), 16))     # no floor-shadow smear
    chrome(c, k, title, detail, scrim=name.startswith('macro'))
    return finish(c)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    RAW.mkdir(parents=True, exist_ok=True)
    only = set(sys.argv[1:])
    thumbs = []
    for k, (name, title, detail) in enumerate(PLATES, 1):
        p = OUT / ('%02d-%s.jpg' % (k, name))
        if not only or name in only:
            plate(k, name, title, detail).save(p, quality=94, subsampling=0)
            print(p.name, flush=True)
        thumbs.append((p, title))
    for f in ST.glob('*.png'):                                   # the transparent renders, for reuse
        shutil.copy2(f, RAW / f.name)
    tw, th, cols = 480, 270, 4
    rows = (len(thumbs) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * tw + (cols + 1) * 16, rows * (th + 44) + 140), (14, 14, 16))
    sc = sheet.convert('RGBA')
    mk = cp.mark(30, cp.TEXT)
    sc.alpha_composite(mk, (16, 40))
    tl = ImageDraw.Draw(sc)
    hdr = Image.new('RGBA', sc.size, (0, 0, 0, 0))
    for s_, x, y, kind, size, col, tr in (('MAO  —  STILLS', 16 + mk.width + 16, 64, 'medium', 22, cp.TEXT, 6),
                                          ('MAO_MAIN A0  ·  CONCEPT ENCLOSURE  ·  KICAD RAYTRACE  ·  2026-10',
                                           16, 112, 'mono', 14, cp.GREY, 2)):
        m, width, asc, pad = cp.text_mask(s_, kind, size, tr)
        layer = Image.new('RGBA', m.size, col + (0,))
        layer.putalpha(m)
        hdr.alpha_composite(layer, (x - pad, y - asc - pad))
    sc.alpha_composite(hdr)
    for i, (p, title) in enumerate(thumbs):
        x, y = 16 + (i % cols) * (tw + 16), 140 + (i // cols) * (th + 44)
        if p.exists():
            sc.paste(Image.open(p).resize((tw, th), Image.LANCZOS), (x, y))
        m, width, asc, pad = cp.text_mask('%02d  %s' % (i + 1, title), 'mono', 13, 1.5)
        layer = Image.new('RGBA', m.size, cp.GREY + (0,))
        layer.putalpha(m)
        sc.alpha_composite(layer, (x - pad, y + th + 24 - asc - pad))
    del tl
    sc.convert('RGB').save(OUT / 'contact-sheet.jpg', quality=92)
    print('plates:', len(thumbs), 'in', OUT)


if __name__ == '__main__':
    main()
