"""Close-up of one board region from grid-dump.json, for planning hand routes (system python + Pillow).

    view_region.py x0 y0 x1 y1 F|B [out.png]

Board millimetres (centre origin, as every design script). Pads, tracks and vias of the chosen outer
layer are drawn in a colour per net with 'REF.pad NET' labels; vias show on both; the open
connections from outputs/DRC.json are drawn as thin red lines. The view is from the front (B is not
mirrored), so coordinates read the same on both layers. Diagnosis only: it never edits the board.
"""
import colorsys
import hashlib
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

from netrules import CACHE, DRC_JSON, ORIGIN

S = int(os.environ.get('VIEW_SCALE', 60))  # px per mm


def colour(net):
    if net == 'GND':
        return (90, 90, 90)
    if net == '+3V3':
        return (200, 60, 60)
    h = int(hashlib.md5(net.encode()).hexdigest()[:6], 16) / 0xFFFFFF
    r, g, b = colorsys.hsv_to_rgb(h, 0.55, 0.85)
    return int(r * 255), int(g * 255), int(b * 255)


def main():
    x0, y0, x1, y1 = map(float, sys.argv[1:5])
    side = sys.argv[5]
    out = sys.argv[6] if len(sys.argv) > 6 else str(CACHE / ('view-%s.png' % side))
    d = json.loads((CACHE / 'grid-dump.json').read_text())
    W, H = int((x1 - x0) * S), int((y1 - y0) * S)
    img = Image.new('RGB', (W, H), (250, 250, 246))
    g = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 13)
        small = ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc', 10)
    except OSError:
        font = small = ImageFont.load_default()
    P = lambda x, y: ((x - x0) * S, (y - y0) * S)
    # grid every 0.5 mm, labelled every 1 mm
    for i in range(int(x0 * 2), int(x1 * 2) + 1):
        x = i / 2
        g.line([P(x, y0), P(x, y1)], fill=(232, 232, 226) if i % 2 else (210, 210, 204))
        if not i % 2:
            g.text((P(x, y0)[0] + 2, 2), '%g' % x, fill=(150, 150, 150), font=small)
    for j in range(int(y0 * 2), int(y1 * 2) + 1):
        y = j / 2
        g.line([P(x0, y), P(x1, y)], fill=(232, 232, 226) if j % 2 else (210, 210, 204))
        if not j % 2:
            g.text((2, P(x0, y)[1] + 2), '%g' % y, fill=(150, 150, 150), font=small)
    for t in d['tracks']:
        if t['layer'] != side:
            continue
        g.line([P(t['x1'], t['y1']), P(t['x2'], t['y2'])], fill=colour(t['net']), width=max(1, int(t['w'] * S)))
    labels = []
    for p in d['pads']:
        if side not in p['layers']:
            continue
        b = p['box']
        if b[2] < x0 or b[0] > x1 or b[3] < y0 or b[1] > y1:
            continue
        c = colour(p['net'])
        if p.get('r'):
            g.ellipse([P(p['x'] - p['r'], p['y'] - p['r']), P(p['x'] + p['r'], p['y'] + p['r'])], fill=c, outline=(0, 0, 0))
        else:
            g.rectangle([P(b[0], b[1]), P(b[2], b[3])], fill=c, outline=(0, 0, 0))
        labels.append((p['x'], p['y'], '%s.%s %s' % (p['ref'], p['num'], p['net'] or '-')))
    for v in d['vias']:
        r = v['d'] / 2
        g.ellipse([P(v['x'] - r, v['y'] - r), P(v['x'] + r, v['y'] + r)], fill=colour(v['net']), outline=(0, 0, 0))
        g.ellipse([P(v['x'] - .15, v['y'] - .15), P(v['x'] + .15, v['y'] + .15)], fill=(255, 255, 255))
    try:
        drc = json.loads(DRC_JSON.read_text())
        for u in drc.get('unconnected_items', []):
            a, b = u['items'][0]['pos'], u['items'][1]['pos']
            g.line([P(a['x'] - ORIGIN, a['y'] - ORIGIN), P(b['x'] - ORIGIN, b['y'] - ORIGIN)], fill=(230, 0, 0), width=1)
    except (OSError, KeyError, ValueError):
        pass
    for x, y, s in labels:
        px, py = P(x, y)
        g.text((px + 1, py - 6), s, fill=(0, 0, 0), font=font)
    img.save(out)
    print(out)


if __name__ == '__main__':
    main()
