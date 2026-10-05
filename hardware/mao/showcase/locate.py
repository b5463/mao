"""Screen positions of the exploded parts, for the film's callouts (plain Python + routerenv for the image maths).

    python3 locate.py render     -> build/locate/<frame>_<part>.png   (KiCad, basic quality, same cameras as the film)
    routerenv/python locate.py measure  -> build/labels.json

Each part is rendered alone with the board, and compared with the board on its own: the pixels that change are the
part. The board itself is measured from the board-only render.
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / 'build' / 'locate'
FRAMES = (90, 179)                         # start and end of the exploded hold in seg_explode
PARTS = ['bezel', 'window', 'ring', 'display', 'cell', 'lra', 'base']


def do_render():
    import render
    import shots
    seg = shots.seg_explode()
    jobs = []
    for i in FRAMES:
        fr = seg[i]
        jobs.append(({'parts': {}, 'cam': fr['cam']}, OUT / ('%03d_board.png' % i)))
        for p in PARTS:
            jobs.append(({'parts': {p: fr['parts'][p]}, 'cam': fr['cam']}, OUT / ('%03d_%s.png' % (i, p))))
    render.batch(jobs, workers=3, quality='basic')


def do_measure():
    import numpy as np
    from PIL import Image

    def load(p):
        return np.asarray(Image.open(p).convert('RGBA')).astype(np.int16)

    res = {}
    for i in FRAMES:
        board = load(OUT / ('%03d_board.png' % i))
        out = {}
        solid = board[..., 3] > 200
        out['board'] = box(solid)
        for p in PARTS:
            im = load(OUT / ('%03d_%s.png' % (i, p)))
            diff = (np.abs(im - board).max(axis=2) > 40) & (im[..., 3] > 200)
            out[p] = box(diff)
        res[str(i)] = out
    (HERE / 'build' / 'labels.json').write_text(json.dumps(res, indent=1))
    for i, o in res.items():
        print(i, {k: (v['cy'], v['right']) for k, v in o.items()})


def box(mask):
    import numpy as np
    ys, xs = np.nonzero(mask)
    cy = float(np.median(ys))
    band = mask[int(cy) - 3:int(cy) + 4]                 # right edge on the part's centre row
    bx = np.nonzero(band.any(axis=0))[0]
    return {'x0': int(xs.min()), 'x1': int(xs.max()), 'y0': int(ys.min()), 'y1': int(ys.max()),
            'cy': cy, 'right': int(bx.max()) if len(bx) else int(xs.max())}


if __name__ == '__main__':
    {'render': do_render, 'measure': do_measure}[sys.argv[1]]()
