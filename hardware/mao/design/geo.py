"""Footprint geometry in plain Python: where every pad and courtyard of a placed part lands.

Reads the netlist (outputs/MAO_MAIN_A0.net: which footprint and which nets each part has) and
fp-geometry.json (fp_cache.py: pads and courtyard strips on F and B at rotation 0). Used by
placement.py to put support parts against the exact pin they serve, and by the placement review
(place_view.py: overlaps, keep-outs, ratsnest length and crossings). Board millimetres, centre origin, y towards 6 o'clock.
"""
import json
import math
import os

from netrules import NAME, OUTPUTS
from sexp import find, findall, parse

HERE = os.path.dirname(os.path.abspath(__file__))
_GEO = None
_NET = None


def geometry():
    global _GEO
    if _GEO is None:
        with open(os.path.join(HERE, 'fp-geometry.json')) as f:
            _GEO = json.load(f)
    return _GEO


def netlist():
    """ref -> {'fp': footprint id, 'pins': {pad number: net}}"""
    global _NET
    if _NET is None:
        root = parse((OUTPUTS / (NAME + '.net')).read_text())
        _NET = {}
        for comp in findall(find(root, 'components'), 'comp'):
            _NET[str(find(comp, 'ref')[1])] = {'fp': str(find(comp, 'footprint')[1]), 'pins': {}}
        for net in findall(find(root, 'nets'), 'net'):
            name = str(find(net, 'name')[1])
            for node in findall(net, 'node'):
                _NET[str(find(node, 'ref')[1])]['pins'][str(find(node, 'pin')[1])] = name
    return _NET


def rot(x, y, a):
    c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
    return x * c + y * s, -x * s + y * c


def pads(ref, place):
    """[(number, x, y, w, h, net)] of a part placed at place = (x, y, rotation, side)."""
    x, y, a, side = place
    g = geometry()[netlist()[ref]['fp']][side]
    nets = netlist()[ref]['pins']
    out = []
    for p in g['pads']:
        dx, dy = rot(p['x'], p['y'], a)
        w, h = (p['w'], p['h']) if round(a) % 180 == 0 else (p['h'], p['w'])
        out.append((p['n'], x + dx, y + dy, w, h, nets.get(p['n'], '')))
    return out


def holes(ref, place):
    """[(x, y, r)] of the part's drilled pads (plated or not): obstacles on both faces."""
    x, y, a, side = place
    out = []
    for p in geometry()[netlist()[ref]['fp']][side]['pads']:
        if p.get('kind') in ('PTH', 'NPTH'):
            dx, dy = rot(p['x'], p['y'], a)
            out.append((x + dx, y + dy, max(p['w'], p['h']) / 2 if p['kind'] == 'PTH' else p['drill'] / 2))
    return out


def pad(ref, num, place):
    for n, x, y, *_ in pads(ref, place):
        if n == str(num):
            return x, y
    raise KeyError((ref, num))


def courtyard(ref, place):
    """Axis-aligned boxes covering the part's courtyard strips (exact at multiples of 90 degrees)."""
    x, y, a, side = place
    out = []
    for b in geometry()[netlist()[ref]['fp']][side]['crt']:
        pts = [rot(px, py, a) for px, py in ((b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3]))]
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        out.append((x + min(xs), y + min(ys), x + max(xs), y + max(ys)))
    return out
