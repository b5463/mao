"""KiCad symbol library access and MAO custom symbol generation."""
import copy
import os

from sexp import Q, parse, find, findall, dumps

KICAD_SHARE = os.environ.get(
    'KICAD_SHARE', os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/SharedSupport'))
SYM_DIR = os.path.join(KICAD_SHARE, 'symbols')
FP_DIR = os.path.join(KICAD_SHARE, 'footprints')

_libs = {}


def load_lib(lib):
    if lib not in _libs:
        path = os.path.join(SYM_DIR, lib + '.kicad_sym')
        root = parse(open(path, encoding='utf-8').read())
        _libs[lib] = {s[1]: s for s in findall(root, 'symbol')}
    return _libs[lib]


def _rename_units(sym, old, new):
    for sub in findall(sym, 'symbol'):
        if sub[1].startswith(old + '_'):
            sub[1] = Q(new + sub[1][len(old):])


def library_symbol(lib_id):
    """Flattened copy of a library symbol, named 'Lib:Name' for lib_symbols."""
    lib, name = lib_id.split(':', 1)
    syms = load_lib(lib)
    node = copy.deepcopy(syms[name])
    ext = find(node, 'extends')
    if ext is not None:
        parent = copy.deepcopy(syms[ext[1]])
        _rename_units(parent, ext[1], name)
        # child properties override the parent's
        child_props = {p[1]: p for p in findall(node, 'property')}
        merged = [parent[0], Q(name)]
        for c in parent[2:]:
            if isinstance(c, list) and c and c[0] == 'property' and c[1] in child_props:
                merged.append(child_props.pop(c[1]))
            else:
                merged.append(c)
        # insert remaining child properties before the first sub-symbol
        idx = next((i for i, c in enumerate(merged) if isinstance(c, list) and c[0] == 'symbol'), len(merged))
        for p in child_props.values():
            merged.insert(idx, p)
        node = merged
    node[1] = Q(lib_id)
    return node


def symbol_pins(sym):
    """[(number, name, etype, x, y, angle, length, unit)] in library coordinates (Y up)."""
    out = []
    for sub in findall(sym, 'symbol'):
        unit = int(sub[1].rsplit('_', 2)[-2])
        for p in findall(sub, 'pin'):
            at = find(p, 'at')
            out.append((str(find(p, 'number')[1]), str(find(p, 'name')[1]), p[1],
                        float(at[1]), float(at[2]), int(float(at[3])),
                        float(find(p, 'length')[1]), unit))
    return out


def symbol_bbox(sym):
    xs, ys = [], []
    for sub in findall(sym, 'symbol'):
        for g in sub[2:]:
            if not isinstance(g, list):
                continue
            if g[0] == 'rectangle':
                for k in ('start', 'end'):
                    v = find(g, k)
                    xs.append(float(v[1])); ys.append(float(v[2]))
            elif g[0] == 'polyline':
                for xy in findall(find(g, 'pts'), 'xy'):
                    xs.append(float(xy[1])); ys.append(float(xy[2]))
            elif g[0] == 'circle':
                c = find(g, 'center'); r = float(find(g, 'radius')[1])
                xs += [float(c[1]) - r, float(c[1]) + r]; ys += [float(c[2]) - r, float(c[2]) + r]
            elif g[0] == 'pin':
                at = find(g, 'at'); xs.append(float(at[1])); ys.append(float(at[2]))
    if not xs:
        return (-2.54, -2.54, 2.54, 2.54)
    return (min(xs), min(ys), max(xs), max(ys))


# ---------------------------------------------------------------------------
# Custom IC symbols: a plain rectangle with pins on the sides, built from the
# datasheet pinout. Style matches the KiCad library (2.54 mm grid).
# ---------------------------------------------------------------------------

def _eff(size=1.27, hide=False, justify=None):
    e = ['effects', ['font', ['size', size, size]]]
    if justify:
        e.append(['justify'] + justify)
    if hide:
        e.append(['hide', 'yes'])
    return e


def _prop(name, value, x=0.0, y=0.0, hide=False, justify=None):
    return ['property', Q(name), Q(value), ['at', x, y, 0], _eff(hide=hide, justify=justify)]


def make_ic_symbol(lib_id, ref_prefix, pins, description='', body_width=None, pin_len=2.54):
    """pins: list of dicts {num, name, type, side: L/R/T/B} in drawing order.
    'type' is a KiCad electrical type (input, output, bidirectional, power_in,
    power_out, passive, open_collector, no_connect, tri_state)."""
    name = lib_id.split(':', 1)[1]
    sides = {s: [p for p in pins if p['side'] == s] for s in 'LRTB'}
    step = 2.54
    tb_step = 5.08            # top/bottom pins carry vertical labels: give each one room
    rows = max(len(sides['L']), len(sides['R']), 1)
    cols = max(len(sides['T']), len(sides['B']), 0)
    longest_l = max([len(p['name']) for p in sides['L']] + [2])
    longest_r = max([len(p['name']) for p in sides['R']] + [2])
    if body_width is None:
        body_width = max((cols + 1) * tb_step, (longest_l + longest_r) * 1.0 + 5.08)
    body_width = round(body_width / step) * step
    # vertical pin names of top/bottom pins need their own band inside the body
    t_band = (max([len(p['name']) for p in sides['T']] + [0]) * 1.0 + 1.27) if sides['T'] else 0
    b_band = (max([len(p['name']) for p in sides['B']] + [0]) * 1.0 + 1.27) if sides['B'] else 0
    t_rows = int(-(-t_band // step))
    b_rows = int(-(-b_band // step))
    total = rows + 1 + t_rows + b_rows
    half_w = body_width / 2
    top = ((total * step / 2) // step + 1) * step
    bot = top - total * step
    top_side_first = top - t_rows * step     # left/right pins start below the top band
    gfx = ['symbol', Q(name + '_0_1'),
           ['rectangle', ['start', -half_w, top], ['end', half_w, bot],
            ['stroke', ['width', 0.254], ['type', 'default']], ['fill', ['type', 'background']]]]
    pin_nodes = ['symbol', Q(name + '_1_1')]

    def add(p, x, y, ang):
        hide_name = p.get('hide_name', False)
        pin_nodes.append(['pin', p['type'], 'line', ['at', x, y, ang], ['length', pin_len],
                          ['name', Q(p['name']), _eff()], ['number', Q(str(p['num'])), _eff()]]
                         + ([['hide', 'yes']] if p.get('hidden') else []))

    for i, p in enumerate(sides['L']):
        add(p, -half_w - pin_len, top_side_first - (i + 1) * step, 0)
    for i, p in enumerate(sides['R']):
        add(p, half_w + pin_len, top_side_first - (i + 1) * step, 180)
    # top/bottom pins centred; with an even count they sit on the 1.27 mm half-grid, which is
    # still a valid KiCad connection grid (snapping to 2.54 would stack two pins on one point)
    for side, y, ang in (('T', top + pin_len, 270), ('B', bot - pin_len, 90)):
        n = len(sides[side])
        for i, p in enumerate(sides[side]):
            add(p, round((i - (n - 1) / 2) * tb_step, 2), y, ang)

    return ['symbol', Q(lib_id),
            ['pin_names', ['offset', 0.762]],
            ['exclude_from_sim', 'no'], ['in_bom', 'yes'], ['on_board', 'yes'],
            _prop('Reference', ref_prefix, -half_w, top + 1.27, justify=['left', 'bottom']),
            _prop('Value', name, -half_w, bot - 1.27, justify=['left', 'top']),
            _prop('Footprint', '', 0, 0, hide=True),
            _prop('Datasheet', '', 0, 0, hide=True),
            _prop('Description', description, 0, 0, hide=True),
            gfx, pin_nodes]


def derive_symbol(src_lib_id, new_lib_id, pin_types=None, value=None):
    """Copy a library symbol under a MAO name, optionally correcting electrical pin types where the
    library's choice contradicts how the part is wired (e.g. strap inputs typed bidirectional)."""
    node = library_symbol(src_lib_id)
    node[1] = Q(new_lib_id)
    old = src_lib_id.split(':', 1)[1]
    new = new_lib_id.split(':', 1)[1]
    _rename_units(node, old, new)
    for sub in findall(node, 'symbol'):
        for p in findall(sub, 'pin'):
            num = str(find(p, 'number')[1])
            if pin_types and num in pin_types:
                p[1] = pin_types[num]
    for prop in findall(node, 'property'):
        if prop[1] == 'Value' and value:
            prop[2] = Q(value)
    return node


def write_symbol_lib(path, symbols):
    root = ['kicad_symbol_lib', ['version', 20241209], ['generator', Q('mao_gen')],
            ['generator_version', Q('10.0')]]
    for s in symbols:
        s = copy.deepcopy(s)
        s[1] = Q(s[1].split(':', 1)[1])
        root.append(s)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(dumps(root) + '\n')
