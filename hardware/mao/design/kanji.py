"""Brush-calligraphy kanji for the MAO silk easter eggs (owner decision 2026-10-06: kanji only).

The glyphs come from Yuji Syuku (Kinuta Font Factory, SIL Open Font License 1.1; font and licence in
hardware/mao/brand/). Each word becomes FILLED silk polygons (outline + holes), never KiCad stroke text, so the brush
shape survives. KiCad's python has no fontTools, so this runs in a plain Python with fontTools, numpy, scipy, Pillow
and shapely, skia-pathops (ROUTER_PY), the way the maker mark is traced once into brand/odd-jobs-symbol.json:

    python kanji.py            writes brand/kanji-eggs.json (the contours, in mm, centred on (0, 0), +y down) and
                               outputs/KANJI-CHECK.json (the stroke / gap check below); exits 1 if a word fails
    python kanji.py --preview out.png   also a black-and-white preview of every word at 100 px/mm

silk.py reads brand/kanji-eggs.json and places each word (mirrored on B.SilkS so it reads from the back). A word of
two characters is also traced in the other direction (across / top to bottom), so silk.py can take whichever fits.

Process check (JLC silk minimum line 0.153 mm): every stroke and every gap must stay >= ~0.17 mm. Each word is
rasterised at RASTER px/mm (overlapping brush strokes merged first, skia-pathops) and a morphological opening with a
PROBE_MM disc may remove < MAX_LOSS of its ink (the owner's test: thin strokes and the brush's hair-thin tails vanish
under the opening). Gaps: a closing with the same disc may add < MAX_GAIN (it also fills every inside corner of the
strokes, which costs no legibility, hence the looser limit; gaps narrower than the disc fill in too).
The em sizes in EGGS are the smallest (0.2 mm steps) that pass both; Yuji Syuku's tapering brush strokes need
3.6-5.0 mm em for the opening alone, more than the 2.2-3.2 mm first estimated (outputs/KANJI-CHECK.json lists
the smallest passing em of each word).
"""
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent                                   # hardware/mao
FONT = ROOT / 'brand' / 'YujiSyuku-Regular.ttf'
OUT = ROOT / 'brand' / 'kanji-eggs.json'
CHECK = ROOT / 'outputs' / 'KANJI-CHECK.json'

# silk.py places them
EGGS = {
    'maomao': ('猫猫', 4.0, False, 'Maomao, "cat cat"', 'F, beside the face press and "boop"'),
    'gin':    ('銀',   4.0, False, 'silver', 'B, beside the BAT LINK 0R'),
    'kusuri': ('薬',   5.0, False, 'medicine', 'F, beside the BQ25185 charger'),
    'sake':   ('酒',   4.4, False, 'sake', 'B, under the cell, where "9 lives" was'),
    'dokumi': ('毒見', 4.4, True, 'poison tasting', 'B, under the speaker (top to bottom along it), where "meow" was'),
}                       # name: (text, em mm, vertical, meaning, where)
RASTER = 100.0          # px/mm (>= 50)
PROBE_MM = 0.16         # opening / closing disc diameter
MAX_LOSS = 0.02         # opening: ink lost
MAX_GAIN = 0.05         # closing: ink added (inside corners and narrow gaps)
FLAT_STEPS = 10         # points per curve segment
SIMPLIFY_MM = 0.003     # polygon simplification after the union (far below the 0.16 mm probe)


def contours(text, em, vertical=False):
    """The words' outlines in mm, centred on the em box, +y down: list of (points, signed area)."""
    from fontTools.pens.basePen import BasePen
    from fontTools.ttLib import TTFont

    class Flatten(BasePen):
        def __init__(self, gs):
            super().__init__(gs)
            self.out, self.cur = [], []

        def _moveTo(self, p):
            self.cur = [p]

        def _lineTo(self, p):
            self.cur.append(p)

        def _curveToOne(self, p1, p2, p3):
            p0 = self.cur[-1]
            for k in range(1, FLAT_STEPS + 1):
                t = k / FLAT_STEPS
                a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t ** 2, t ** 3
                self.cur.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))

        def _qCurveToOne(self, p1, p2):
            p0 = self.cur[-1]
            for k in range(1, FLAT_STEPS + 1):
                t = k / FLAT_STEPS
                a, b, c = (1 - t) ** 2, 2 * (1 - t) * t, t ** 2
                self.cur.append((a * p0[0] + b * p1[0] + c * p2[0], a * p0[1] + b * p1[1] + c * p2[1]))

        def _closePath(self):
            if len(self.cur) > 2:
                self.out.append(self.cur)
            self.cur = []

        _endPath = _closePath

    import pathops
    f = TTFont(str(FONT))
    gs, cmap, upm, hmtx = f.getGlyphSet(), f.getBestCmap(), f['head'].unitsPerEm, f['hmtx']
    s = em / upm
    mid = (f['hhea'].ascent + f['hhea'].descent) / 2
    names = [cmap[ord(ch)] for ch in text]
    pen_x = -sum(hmtx[n][0] for n in names) / 2
    out = []
    for k, n in enumerate(names):
        if vertical:                                # top to bottom, one em per character
            pen_x = -hmtx[n][0] / 2
            dy = (k - (len(names) - 1) / 2) * upm
        else:
            dy = 0.0
        path = pathops.Path()
        gs[n].draw(path.getPen(glyphSet=gs))
        path.simplify(fix_winding=True)            # the brush strokes overlap: one clean outline set, no overlaps
        pen = Flatten(gs)
        path.draw(pen)
        for c in pen.out:
            out.append([((pen_x + px) * s, (dy - (py - mid)) * s) for px, py in c])
        pen_x += hmtx[n][0]
    return out


def filled(cs):
    """The outlines (overlaps already removed) as shapely polygons with holes, by nesting depth: a contour inside an
    even number of others is an outline, inside an odd number a hole of the smallest outline round it."""
    from shapely.geometry import Polygon
    from shapely.ops import unary_union
    polys = [Polygon(c).buffer(0) for c in cs]
    polys = [p for p in polys if p.area > 1e-6]
    depth = [sum(1 for j, q in enumerate(polys) if j != i and q.area > p.area and q.contains(p.representative_point()))
             for i, p in enumerate(polys)]
    shapes = []
    for i, p in enumerate(polys):
        if depth[i] % 2:
            continue
        holes = [q for j, q in enumerate(polys) if depth[j] == depth[i] + 1 and p.contains(q.representative_point())]
        shapes.append(p.difference(unary_union(holes)) if holes else p)
    u = unary_union(shapes).simplify(SIMPLIFY_MM, preserve_topology=True)
    return [u] if u.geom_type == 'Polygon' else list(u.geoms)


def raster(polys, px):
    from PIL import Image, ImageDraw
    import numpy as np
    xs = [x for p in polys for x, _ in p.exterior.coords]
    ys = [y for p in polys for _, y in p.exterior.coords]
    pad = 0.5
    x0, y0 = min(xs) - pad, min(ys) - pad
    w, h = int((max(xs) - x0 + pad) * px) + 1, int((max(ys) - y0 + pad) * px) + 1
    img = Image.new('1', (w, h), 0)
    g = ImageDraw.Draw(img)
    T = lambda pts: [((x - x0) * px, (y - y0) * px) for x, y in pts]
    for p in polys:
        g.polygon(T(p.exterior.coords), fill=1)
        for r in p.interiors:
            g.polygon(T(r.coords), fill=0)
    return np.array(img, dtype=bool)


def check(polys):
    """Ink lost to an opening and gained by a closing with the PROBE_MM disc (fractions of the ink)."""
    import numpy as np
    from scipy import ndimage
    a = raster(polys, RASTER)
    r = PROBE_MM / 2 * RASTER
    n = int(math.ceil(r))
    yy, xx = np.mgrid[-n:n + 1, -n:n + 1]
    disc = xx ** 2 + yy ** 2 <= r * r
    ink = a.sum()
    opened = ndimage.binary_opening(a, structure=disc)
    closed = ndimage.binary_closing(a, structure=disc, border_value=0)
    return float((a & ~opened).sum() / ink), float((closed & ~a).sum() / ink), a


def word(text, em, vertical=False):
    polys = filled(contours(text, em, vertical))
    loss, gain, img = check(polys)
    return polys, loss, gain, img


def smallest(text, vertical=False, lo=2.2, hi=8.0, step=0.2):
    """Smallest em (mm, 0.2 mm steps) at which the opening alone passes, and at which both tests pass."""
    em, opening = lo, None
    while em <= hi + 1e-9:
        _, loss, gain, _ = word(text, em, vertical)
        if opening is None and loss < MAX_LOSS:
            opening = round(em, 2)
        if loss < MAX_LOSS and gain < MAX_GAIN:
            return opening, round(em, 2)
        em += step
    return opening, None


def main():
    data, rep, ok = {'font': FONT.name, 'licence': 'SIL Open Font License 1.1 (brand/OFL-YujiSyuku.txt)',
                     'units': 'mm, centred on the em box, +y towards 6 o\'clock (F view)', 'words': {}}, {}, True
    previews = []
    for name, (text, em, vertical, meaning, where) in EGGS.items():
        min_open, min_em = smallest(text, vertical)
        layouts = [vertical] + ([not vertical] if len(text) > 1 else [])
        entry = {'text': text, 'em_mm': em, 'meaning': meaning, 'where': where, 'layouts': []}
        for lay in layouts:
            polys, loss, gain, img = word(text, em, lay)
            good = loss < MAX_LOSS and gain < MAX_GAIN
            ok &= good
            xs = [x for p in polys for x, _ in p.exterior.coords]
            ys = [y for p in polys for _, y in p.exterior.coords]
            size = [round(max(xs) - min(xs), 3), round(max(ys) - min(ys), 3)]
            entry['layouts'].append({'vertical': lay, 'size_mm': size,
                                     'polygons': [{'outline': [[round(x, 4), round(y, 4)] for x, y in p.exterior.coords[:-1]],
                                                   'holes': [[[round(x, 4), round(y, 4)] for x, y in r.coords[:-1]]
                                                             for r in p.interiors]} for p in polys]})
            key = name + ('_vertical' if lay else '') if len(layouts) > 1 else name
            rep[key] = {'text': text, 'em_mm': em, 'vertical': lay, 'size_mm': size,
                        'opening_loss_pct': round(100 * loss, 2), 'closing_gain_pct': round(100 * gain, 2),
                        'smallest_em_opening_mm': min_open, 'smallest_em_both_mm': min_em, 'pass': good}
            print('kanji %-17s em %.1f mm (%.2f x %.2f mm): opening loses %.2f %%, closing adds %.2f %%; smallest em: '
                  'opening %s, both %s mm -> %s' % (key, em, *size, 100 * loss, 100 * gain, min_open, min_em,
                                                     'pass' if good else 'FAIL'))
            if lay == vertical:
                previews.append(img)
        data['words'][name] = entry
    OUT.write_text(json.dumps(data, ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf-8')
    CHECK.write_text(json.dumps({'font': FONT.name, 'raster_px_per_mm': RASTER, 'probe_disc_mm': PROBE_MM,
                                 'max_opening_loss': MAX_LOSS, 'max_closing_gain': MAX_GAIN,
                                 'jlc_silk_min_line_mm': 0.153, 'words': rep},
                                ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    if '--preview' in sys.argv:
        from PIL import Image
        import numpy as np
        w = sum(i.shape[1] for i in previews) + 40 * len(previews)
        h = max(i.shape[0] for i in previews)
        sheet = np.zeros((h, w), dtype=np.uint8)
        x = 0
        for i in previews:
            sheet[:i.shape[0], x:x + i.shape[1]] = i * 255
            x += i.shape[1] + 40
        Image.fromarray(sheet).save(sys.argv[sys.argv.index('--preview') + 1])
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
