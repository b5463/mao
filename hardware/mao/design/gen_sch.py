"""Generate the MAO_MAIN schematic (root + one sheet per functional block).

Every pin ends in a net label, a power symbol or a no-connect flag, so each
block reads on its own and nets are unambiguous (and keep the names the board,
routing rules and procedures use). Where a two-pin support part sits on a net
that only joins it to one side pin of the block's IC (a feedback divider, a
timing or current-set resistor, a pull resistor, a filter cap), it is wired to
that pin as well: the pin's label names the wire and the part hangs off it in
a column beyond the labels, columns stepped so no wire crosses a part (ODD
JOBS 187, 188). Adjacent pins on the same rail share one rail symbol, joined
by a wire. Blocks are titled frames, one per block name; parts keep the order
given in circuit.py (main IC first, then its support parts).
"""
import copy
import math

import kicadlib
from model import stable_uuid
from sexp import Q, dumps, find, findall

GRID = 1.27
PAGE = {'A3': (420.0, 297.0)}
POWER_NETS = {}    # net -> power symbol lib_id (filled by setup_power_symbols)
PROJECT = 'MAO_MAIN_A1'


def snap(v):
    return round(v / GRID) * GRID


NOTE_PITCH = 2 * 1.27    # note lines on a grid multiple: text positions snap to GRID, so a 2.29 mm pitch printed
                         # alternately 1.27 and 2.54 mm apart and the close pairs touched (ODD JOBS audit 3)


def eff(size=1.27, justify=None, hide=False):
    e = ['effects', ['font', ['size', size, size]]]
    if justify:
        e.append(['justify'] + justify)
    if hide:
        e.append(['hide', 'yes'])
    return e


# --------------------------------------------------------------------------
# Power symbols: KiCad's own where they exist, otherwise derived from VCC.
# --------------------------------------------------------------------------

def setup_power_symbols(circuit, rails):
    """rails: {net: 'up'|'down'} - 'down' renders as a ground-style symbol."""
    for net, style in rails.items():
        lib_id = None
        if net == 'GND':
            lib_id = 'power:GND'
        elif net in ('+3V3', 'VBUS', 'PWR_FLAG'):
            lib_id = 'power:' + net
        if lib_id is None:
            lib_id = 'MAO:' + net
            node = copy.deepcopy(kicadlib.library_symbol('power:VCC'))
            node[1] = Q(lib_id)
            for p in findall(node, 'property'):
                if p[1] == 'Value':
                    p[2] = Q(net)
                if p[1] == 'Description':
                    p[2] = Q('Power rail ' + net)
            for sub in findall(node, 'symbol'):
                sub[1] = Q(sub[1].replace('VCC', net))
                for pin in findall(sub, 'pin'):
                    find(pin, 'name')[1] = Q(net)
            circuit.custom_symbols[lib_id] = node
        POWER_NETS[net] = (lib_id, style)


# --------------------------------------------------------------------------
# Geometry
# --------------------------------------------------------------------------

def rot_vec(x, y, a):
    """Rotate screen vector (Y down) CCW-on-screen by a degrees."""
    r = math.radians(a)
    return (x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r))


def pin_geometry(pin, at, rot):
    """Connection point and outward direction of a pin in sheet coordinates."""
    num, name, etype, px, py, pang, plen, unit = pin
    dx, dy = rot_vec(px, -py, rot)
    pt = (snap(at[0] + dx), snap(at[1] + dy))
    # the pin points from its connection point into the body
    inx, iny = rot_vec(math.cos(math.radians(pang)), -math.sin(math.radians(pang)), rot)
    out = (-round(inx), -round(iny))
    return pt, out


def dir_angle(d):
    return {(1, 0): 0, (0, -1): 90, (-1, 0): 180, (0, 1): 270}[d]


def wrap(text, width):
    words, lines, cur = text.split(), [], ''
    for w in words:
        if len(cur) + len(w) + 1 > width and cur:
            lines.append(cur)
            cur = w
        else:
            cur = (cur + ' ' + w).strip()
    if cur:
        lines.append(cur)
    return lines


def label_len(text):
    return len(text) * 1.0 + 2.5


# --------------------------------------------------------------------------
# Sheet content builders
# --------------------------------------------------------------------------

class Sheet:
    def __init__(self, stem, title, root_uuid, page):
        self.stem = stem
        self.title = title
        self.uuid = stable_uuid('sheet', stem)
        self.root_uuid = root_uuid
        self.page = page
        self.items = []
        self.lib_ids = set()
        self.pwr = 0

    @property
    def path(self):
        return '/%s/%s' % (self.root_uuid, self.uuid)

    def text(self, s, x, y, size=1.27, bold=False, justify=('left', 'bottom')):
        e = ['effects', ['font', ['size', size, size]] + ([['bold', 'yes']] if bold else []),
             ['justify'] + list(justify)]
        self.items.append(['text', Q(s), ['exclude_from_sim', 'no'], ['at', snap(x), snap(y), 0], e,
                           ['uuid', Q(stable_uuid('text', self.stem, s, x, y))]])

    def rect(self, x0, y0, x1, y1):
        self.items.append(['rectangle', ['start', x0, y0], ['end', x1, y1],
                           ['stroke', ['width', 0.15], ['type', 'dash'], ['color', 120, 120, 120, 1]],
                           ['fill', ['type', 'none']],
                           ['uuid', Q(stable_uuid('rect', self.stem, x0, y0))]])

    def label(self, net, pt, out, is_global):
        ang = dir_angle(out)
        just = ['left'] if ang in (0, 90) else ['right']
        u = Q(stable_uuid('lbl', self.stem, net, pt))
        if is_global:
            self.items.append(['global_label', Q(net), ['shape', 'bidirectional'],
                               ['at', pt[0], pt[1], ang], ['fields_autoplaced', 'yes'],
                               eff(justify=just), ['uuid', u]])
        else:
            self.items.append(['label', Q(net), ['at', pt[0], pt[1], ang],
                               ['fields_autoplaced', 'yes'], eff(justify=just + ['bottom']), ['uuid', u]])

    def power(self, net, pt, out):
        lib_id, style = POWER_NETS[net]
        # ground-style symbols hang below their pin, rail symbols stand above
        natural = (0, 1) if style == 'down' else (0, -1)
        rot = {((0, 1), (0, 1)): 0, ((0, 1), (1, 0)): 90, ((0, 1), (0, -1)): 180, ((0, 1), (-1, 0)): 270,
               ((0, -1), (0, -1)): 0, ((0, -1), (-1, 0)): 90, ((0, -1), (0, 1)): 180, ((0, -1), (1, 0)): 270
               }[(natural, out)]
        self.pwr += 1
        ref = ('#PWR%d%02d' if net != 'PWR_FLAG' else '#FLG%d%02d') % (self.page, self.pwr)
        self.lib_ids.add(lib_id)
        u = stable_uuid('pwr', self.stem, net, pt)
        sym = ['symbol', ['lib_id', Q(lib_id)], ['at', pt[0], pt[1], rot], ['unit', 1],
               ['exclude_from_sim', 'no'], ['in_bom', 'no'], ['on_board', 'yes'], ['dnp', 'no'],
               ['uuid', Q(u)],
               ['property', Q('Reference'), Q(ref), ['at', pt[0], pt[1], 0], eff(hide=True)],
               ['property', Q('Value'), Q(net), ['at', pt[0], pt[1], 0], eff(hide=(style == 'down'))],
               ['property', Q('Footprint'), Q(''), ['at', pt[0], pt[1], 0], eff(hide=True)],
               ['property', Q('Datasheet'), Q(''), ['at', pt[0], pt[1], 0], eff(hide=True)],
               ['pin', Q('1'), ['uuid', Q(stable_uuid('pwrpin', u))]],
               ['instances', ['project', Q(PROJECT), ['path', Q(self.path), ['reference', Q(ref)], ['unit', 1]]]]]
        self.items.append(sym)
        # rail name just beyond the symbol, on the side away from the pin; a sideways symbol keeps its name on its
        # own row (above it, the name would sit on the next pin's label)
        if out[1] == 0:
            vx, vy = pt[0] + out[0] * (3.3 + len(net) * 0.55), pt[1]
        else:
            vx, vy = pt[0], pt[1] + out[1] * 3.81
        value_prop = findall(sym, 'property')[1]
        # field angles are stored relative to the symbol: counter-rotate so rail names read horizontally
        value_prop[3] = ['at', round(vx, 3), snap(vy), rot if rot in (90, 270) else 0]

    def wire(self, a, b):
        self.items.append(['wire', ['pts', ['xy', a[0], a[1]], ['xy', b[0], b[1]]],
                           ['stroke', ['width', 0], ['type', 'default']],
                           ['uuid', Q(stable_uuid('wire', self.stem, a, b))]])

    def no_connect(self, pt):
        self.items.append(['no_connect', ['at', pt[0], pt[1]], ['uuid', Q(stable_uuid('nc', self.stem, pt))]])


def part_symbol(sheet, circuit, part, at, rot, global_nets, skip_pins=()):
    node = circuit.symbol_node(part.symbol)
    sheet.lib_ids.add(part.symbol)
    pins = kicadlib.symbol_pins(node)
    props = []
    bb = kicadlib.symbol_bbox(node)
    # Reference above/left of the body, value below
    rx, ry = rot_vec(bb[0], -bb[3], rot)
    vx, vy = rot_vec(bb[0], -bb[1], rot)
    passive = part.symbol in ('Device:R', 'Device:C', 'Device:C_Polarized', 'Device:L',
                              'Device:R_Small', 'Device:C_Small', 'Device:FerriteBead_Small',
                              'Device:Fuse', 'Device:Polyfuse', 'Device:LED', 'Device:D', 'Device:D_TVS',
                              'Device:D_Schottky')
    across = part.symbol.split(':')[1] in ('LED', 'D', 'D_TVS', 'D_Schottky')   # drawn across at 0 degrees
    upright = (rot in (0, 180)) != across
    if passive and upright:
        fa = 90 if across else 0             # field angles are relative to the symbol: text reads horizontally
        ref_at = ['at', snap(at[0] + 2.54), snap(at[1] - 1.27), fa]
        val_at = ['at', snap(at[0] + 2.54), snap(at[1] + 1.27), fa]
        jl = ['right'] if (rot + fa) % 360 == 180 else ['left']   # KiCad turns a 180-degree text upright and flips
                                                                     # its justification with it
    elif passive:
        # horizontal: reference above, value below, centred; field angles are relative to the
        # symbol, so 90 here reads horizontally on a symbol rotated by 90
        ref_at = ['at', at[0], snap(at[1] - 2.54), rot]
        val_at = ['at', at[0], snap(at[1] + 2.54), rot]
        jl = None
    else:
        # reference above the body's left edge, value beside its lower right corner: clear of the
        # labels and rail symbols hanging off top and bottom pins
        ref_at = ['at', snap(at[0] + min(rx, vx)), snap(at[1] + min(ry, vy) - 1.27), 0]
        val_at = ['at', snap(at[0] + max(rot_vec(bb[2], 0, rot)[0], rot_vec(bb[0], 0, rot)[0]) + 1.27),
                  snap(at[1] + max(ry, vy) + 1.27), 0]
        jl = ['left']
    fields = [('Reference', part.ref, ref_at, False), ('Value', part.value, val_at, False),
              ('Footprint', part.footprint, None, True), ('Datasheet', part.fields.get('Datasheet', ''), None, True),
              ('Description', part.fields.get('Description', ''), None, True),
              ('MPN', part.mpn, None, True), ('Manufacturer', part.mfr, None, True),
              ('LCSC', part.lcsc, None, True)]
    for k, v in part.fields.items():
        if k not in ('Datasheet', 'Description'):
            fields.append((k, v, None, True))
    for name, value, pos, hide in fields:
        props.append(['property', Q(name), Q(value), pos or ['at', at[0], at[1], 0],
                      eff(justify=jl if (jl and not hide) else None, hide=hide)])
    in_bom = 'no' if part.fields.get('Exclude from BOM') else 'yes'
    sym = (['symbol', ['lib_id', Q(part.symbol)], ['at', at[0], at[1], rot], ['unit', 1],
            ['exclude_from_sim', 'no'], ['in_bom', in_bom],
            ['on_board', 'yes'], ['dnp', 'yes' if part.dnp else 'no'], ['fields_autoplaced', 'yes'],
            ['uuid', Q(part.uuid)]] + props +
           [['pin', Q(num), ['uuid', Q(stable_uuid('pin', part.ref, num))]]
            for num in sorted({p[0] for p in pins})] +
           [['instances', ['project', Q(PROJECT),
                           ['path', Q(sheet.path), ['reference', Q(part.ref)], ['unit', 1]]]]])
    sheet.items.append(sym)

    # terminate every pin; stacked pins (same point) are handled once; a rail pin next to another pin of the
    # same rail on the same side joins it with a wire instead of a second symbol (no overprinted rail names)
    done = set()
    rails = []                              # (point, out, net) of rail symbols drawn for this part
    for pin in pins:
        num = pin[0]
        pt, out = pin_geometry(pin, at, rot)
        if (pt, num) in done:
            continue
        hidden_dupe = pt in {d[0] for d in done}
        done.add((pt, num))
        if hidden_dupe or num in skip_pins:
            continue
        net = part.pin_net.get(num)
        if net is None:
            sheet.no_connect(pt)
        elif net in POWER_NETS:
            mate = next((q for q, o, n in rails if n == net and o == out and
                         abs(abs(q[0] - pt[0]) + abs(q[1] - pt[1]) - 2.54) < 0.01 and
                         (q[0] - pt[0]) * out[0] + (q[1] - pt[1]) * out[1] == 0), None)
            if mate:
                sheet.wire(mate, pt)
            else:
                sheet.power(net, pt, out)
                rails.append((pt, out, net))
        else:
            sheet.label(net, pt, out, net in global_nets)


def symbol_extent(circuit, part, rot):
    """Bounding box of a placed symbol including its labels (relative)."""
    node = circuit.symbol_node(part.symbol)
    pins = kicadlib.symbol_pins(node)
    bb = kicadlib.symbol_bbox(node)
    pts = []
    for cx, cy in ((bb[0], bb[1]), (bb[2], bb[3]), (bb[0], bb[3]), (bb[2], bb[1])):
        pts.append(rot_vec(cx, -cy, rot))
    for pin in pins:
        (x, y), out = pin_geometry(pin, (0, 0), rot)
        net = part.pin_net.get(pin[0])
        l = 2.0 if net is None else (5.5 if net in POWER_NETS else label_len(net))
        pts.append((x + out[0] * l, y + out[1] * l))
        pts.append((x, y))
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0, y0, x1, y1 = min(xs) - 1.27, min(ys) - 2.54, max(xs) + 1.27, max(ys) + 2.54
    sym = part.symbol.split(':')[1]
    across = sym in ('LED', 'D', 'D_TVS', 'D_Schottky')
    if sym in ('R', 'C', 'L', 'C_Polarized', 'LED', 'D', 'D_TVS', 'D_Schottky') and (rot in (0, 180)) != across:
        # reference and value are written to the right of a vertical passive
        x1 = max(x1, 2.54 + max(len(part.ref), len(part.value)) * 1.05)
    elif sym in ('R', 'C', 'L', 'C_Polarized', 'LED', 'D', 'D_TVS', 'D_Schottky'):
        half = max(len(part.ref), len(part.value)) * 0.55
        x0, x1 = min(x0, -half), max(x1, half)
        y0, y1 = min(y0, -3.81), max(y1, 3.81)
    else:
        # IC reference above the body, value beside its lower right corner
        y0, y1 = y0 - 1.27, y1 + 2.54
        right = max(rot_vec(bb[2], 0, rot)[0], rot_vec(bb[0], 0, rot)[0]) + 1.27
        x1 = max(x1, right + len(part.value) * 1.05)
    return x0, y0, x1, y1


def passive_rot(part):
    """Vertical when one side is a power rail (reads like a decoupler),
    horizontal for series parts between two signals."""
    sym = part.symbol.split(':')[1]
    if sym in ('R', 'C', 'L', 'C_Polarized', 'FerriteBead_Small', 'R_Small',
               'C_Small', 'Polyfuse', 'Fuse', 'D', 'LED', 'D_TVS', 'D_Schottky'):
        nets = list(part.pin_net.values())
        vertical = any(n in POWER_NETS for n in nets if n)
        across = sym in ('D', 'LED', 'D_TVS', 'D_Schottky')     # these symbols are drawn across at 0 degrees
        return (90 if across else 0) if vertical else (0 if across else 90)
    return 0


PASSIVES = ('R', 'C', 'L', 'C_Polarized', 'FerriteBead_Small', 'R_Small', 'C_Small')


def _rect_hit(a, b, g=0.0):
    return a[0] < b[2] + g and b[0] < a[2] + g and a[1] < b[3] + g and b[1] < a[3] + g


def _term_len(net, horizontal=False):
    """How far a pin's terminal reaches out from the pin: a rail symbol drawn sideways carries its name beyond it."""
    if net is None:
        return 2.0
    if net in POWER_NETS:
        return 3.81 + len(net) * 1.05 + 0.5 if horizontal else 5.5
    return label_len(net)


def attach_plan(circuit, main, mrot, supports, nets):
    """Support parts wired to the main part's side pins: [(part, rot, (dx, dy), main_pin_pt, attach_pt, pin)].
    Coordinates are relative to the main part's origin."""
    if main.symbol.split(':')[1] in PASSIVES:
        return []
    node = circuit.symbol_node(main.symbol)
    mpins = kicadlib.symbol_pins(node)
    geo = {}
    for pin in mpins:
        pt, out = pin_geometry(pin, (0.0, 0.0), mrot)
        geo.setdefault(pin[0], (pt, out))
    bb = kicadlib.symbol_bbox(node)
    corners = [rot_vec(x, -y, mrot) for x in (bb[0], bb[2]) for y in (bb[1], bb[3])]
    obst = [(min(c[0] for c in corners), min(c[1] for c in corners), max(c[0] for c in corners), max(c[1] for c in corners))]
    labels = {}
    for num, (pt, out) in geo.items():         # every main pin's terminal (label, rail symbol, no-connect)
        net_ = main.pin_net.get(num)
        L = _term_len(net_, out[1] == 0)
        hw = max(1.0, len(net_) * 0.55) if (net_ in POWER_NETS and out[0] == 0) else 1.0
        x2, y2 = pt[0] + out[0] * L, pt[1] + out[1] * L
        labels[num] = (min(pt[0], x2) - (out[1] != 0) * hw, min(pt[1], y2) - (out[0] != 0) * 1.6,
                       max(pt[0], x2) + (out[1] != 0) * hw, max(pt[1], y2) + (out[0] != 0) * 1.0)
    cands = []
    for p in supports:
        if p.symbol.split(':')[1] not in PASSIVES or len(p.pin_net) != 2:
            continue
        for k, net in p.pin_net.items():
            if net is None or net in POWER_NETS:
                continue
            mem = nets.get(net, [])
            others = [m_ for m_ in mem if m_[0] != p.ref]
            if len(mem) != 2 or len(others) != 1 or others[0][0] != main.ref:
                continue
            mp = others[0][1]
            if mp not in geo or geo[mp][1][1] != 0:
                continue                        # side pins only
            far = next(n_ for k_, n_ in p.pin_net.items() if k_ != k)
            up = far in POWER_NETS and POWER_NETS[far][1] == 'up'
            cands.append((p, k, mp, far, up))
            break
    divs = []
    sup = {p.ref: p for p in supports if p.symbol.split(':')[1] in PASSIVES and len(p.pin_net) == 2}
    for net, mem in nets.items():
        if net in POWER_NETS or len(mem) != 3:
            continue
        mains = [m_ for m_ in mem if m_[0] == main.ref]
        rest = [m_ for m_ in mem if m_[0] != main.ref]
        if len(mains) != 1 or len(rest) != 2 or not all(r_[0] in sup for r_ in rest) or rest[0][0] == rest[1][0]:
            continue
        mp = mains[0][1]
        if mp not in geo or geo[mp][1][1] != 0:
            continue
        far = {r_[0]: next(n_ for k_, n_ in sup[r_[0]].pin_net.items() if k_ != r_[1]) for r_ in rest}
        ups = [r_ for r_ in rest if far[r_[0]] in POWER_NETS and POWER_NETS[far[r_[0]]][1] == 'up']
        if len(ups) != 1:
            continue
        dn = [r_ for r_ in rest if r_ is not ups[0]][0]
        divs.append((sup[ups[0][0]], ups[0][1], sup[dn[0]], dn[1], mp, far[ups[0][0]], far[dn[0]]))
    plan, wires, boxes, used = [], [], [], set()
    for pu, ku, pd, kd, mp, fu, fd in sorted(divs, key=lambda d_: -geo[d_[4]][0][1]):   # divider columns first
        (px, py), (ox, _) = geo[mp]
        rots = {}
        for part_, k_, want in ((pu, ku, 1), (pd, kd, -1)):
            ppins = kicadlib.symbol_pins(circuit.symbol_node(part_.symbol))
            for prot in (0, 180):
                apt = [pin_geometry(q, (0.0, 0.0), prot) for q in ppins if q[0] == k_][0]
                if apt[1][1] == want:            # the upper part meets the node with its bottom pin, the lower its top
                    rots[part_.ref] = (prot, apt[0])
        if len(rots) != 2:
            continue
        text_w = 2.54 + max(len(pu.ref), len(pu.value), len(pd.ref), len(pd.value)) * 1.05
        for j in range(16):
            cx = px + ox * (label_len(main.pin_net[mp]) + 3.81 + 2.54 * j)
            body = (cx - 1.6, py - 7.62 - _term_len(fu), cx + 1.6 + text_w, py + 7.62 + _term_len(fd))
            wire = (min(px, cx), py - 0.3, max(px, cx), py + 0.3)
            if any(_rect_hit(body, o) for o in obst + boxes + wires): continue
            if any(_rect_hit(body, r) for n_, r in labels.items()): continue
            if any(_rect_hit(wire, o) for o in boxes): continue
            if any(_rect_hit(wire, r) for n_, r in labels.items() if n_ != mp and geo[n_][0][1] != py): continue
            for part_, k_ in ((pu, ku), (pd, kd)):
                prot, (ax, ay) = rots[part_.ref]
                plan.append((part_, prot, (cx - ax, py - ay), (px, py), (cx, py), k_))
                used.add(part_.ref)
            boxes.append(body); wires.append(wire)
            break
    order = sorted([c_ for c_ in cands if not c_[4]], key=lambda c_: -geo[c_[2]][0][1]) + \
        sorted([c_ for c_ in cands if c_[4]], key=lambda c_: geo[c_[2]][0][1])      # hangers bottom-up, standers top-down
    for p, k, mp, far, up in order:
        if p.ref in used:
            continue
        (px, py), (ox, _) = geo[mp]
        pnode = circuit.symbol_node(p.symbol)
        ppins = kicadlib.symbol_pins(pnode)
        best = None
        for prot in (0, 180):
            apt = [pin_geometry(q, (0.0, 0.0), prot) for q in ppins if q[0] == k][0]
            if (apt[1][1] == -1) != (not up):  # a hanger meets the wire with its top pin, a stander with its bottom
                continue
            best = (prot, apt[0])
        if best is None:
            continue
        prot, (ax, ay) = best
        text_w = 2.54 + max(len(p.ref), len(p.value)) * 1.05
        span = _term_len(far)
        for j in range(16):
            cx = px + ox * (label_len(main.pin_net[mp]) + 3.81 + 2.54 * j)
            dx, dy = cx - ax, py - ay
            body = (dx - 1.6, py if not up else py - 7.62 - span, dx + 1.6 + text_w, py + 7.62 + span if not up else py)
            wire = (min(px, cx), py - 0.3, max(px, cx), py + 0.3)
            if any(_rect_hit(body, o) for o in obst + boxes + wires): continue
            if any(_rect_hit(body, r) for n_, r in labels.items()): continue
            if any(_rect_hit(wire, o) for o in boxes): continue
            if any(_rect_hit(wire, r) for n_, r in labels.items() if n_ != mp and geo[n_][0][1] != py): continue
            plan.append((p, prot, (dx, dy), (px, py), (cx, py), k))
            boxes.append(body); wires.append(wire); used.add(p.ref)
            break
    return plan


def layout_sheet(circuit, sheet, parts, global_nets, page='A3', nets=None, extra=None):
    W, H = PAGE[page]
    margin = 15.24
    x, y = margin, margin + 10.16
    row_h = 0.0
    blocks, index = [], {}
    for p in parts:                        # one frame per block name, whatever the capture order
        if p.block not in index:
            index[p.block] = len(blocks)
            blocks.append((p.block, []))
        blocks[index[p.block]][1].append(p)

    sheet.text(sheet.title.upper(), margin, margin + 2.54, size=2.54, bold=True)
    for bname, bparts in blocks:
        # inside a block: first part on the left, the rest packed in rows
        placements = []
        main = bparts[0]
        mrot = passive_rot(main)
        ext = symbol_extent(circuit, main, mrot)
        plan = attach_plan(circuit, main, mrot, bparts[1:], nets or {})
        attached = {a_[0].ref for a_ in plan}
        for p_, prot, (dx, dy), _, _, _ in plan:   # the main part's extent grows by what hangs off it
            e_ = symbol_extent(circuit, p_, prot)
            ext = (min(ext[0], dx + e_[0], dx - 1.6), min(ext[1], dy + e_[1]), max(ext[2], dx + e_[2]), max(ext[3], dy + e_[3]))
        placements.append((main, mrot, -ext[0], -ext[1]))
        col_x = ext[2] - ext[0] + 5.08
        cx, cy, line_h = col_x, 0.0, 0.0
        max_w = max(ext[2] - ext[0] + 90.0, 140.0)
        for p in [p_ for p_ in bparts[1:] if p_.ref not in attached]:
            r = passive_rot(p)
            e = symbol_extent(circuit, p, r)
            w, h = e[2] - e[0], e[3] - e[1]
            if cx + w > max_w and cx > col_x:
                cx = col_x
                cy += line_h + 2.54
                line_h = 0.0
            placements.append((p, r, cx - e[0], cy - e[1]))
            cx += w + 3.81
            line_h = max(line_h, h)
        # actual block size
        maxx = ext[2] - ext[0]
        maxy = ext[3] - ext[1]
        for p, r, ox, oy in placements[1:]:
            e = symbol_extent(circuit, p, r)
            maxx = max(maxx, ox + e[2])
            maxy = max(maxy, oy + e[3])
        bw = maxx + 5.08
        desc = circuit.blocks.get((sheet.stem, bname), '')
        lines = wrap(desc, max(40, int(bw / 0.95))) if desc else []
        bh = maxy + 5.08 + len(lines) * NOTE_PITCH + 2.54
        if x + bw > W - margin and x > margin:
            x = margin
            y += row_h + 12.7
            row_h = 0.0
        if y + bh > H - margin:
            print('WARNING: sheet %s overflows the page at block %s' % (sheet.stem, bname))
        sheet.rect(snap(x - 2.54), snap(y - 5.08), snap(x + bw), snap(y + bh))
        sheet.text(bname, x, y - 1.27, size=1.524, bold=True)
        for k, line in enumerate(lines):
            sheet.text(line, x, y + maxy + 6.35 + k * NOTE_PITCH, size=1.0, justify=('left', 'bottom'))
        for p, r, ox, oy in placements:
            part_symbol(sheet, circuit, p, (snap(x + ox), snap(y + 2.54 + oy)), r, global_nets)
        mx, my = snap(x + placements[0][2]), snap(y + 2.54 + placements[0][3])
        for p_, prot, (dx, dy), mpt, apt, k in plan:
            part_symbol(sheet, circuit, p_, (snap(mx + dx), snap(my + dy)), prot, global_nets, skip_pins={k})
            sheet.wire((snap(mx + mpt[0]), snap(my + mpt[1])), (snap(mx + apt[0]), snap(my + apt[1])))
        x += bw + 10.16
        row_h = max(row_h, bh)
    for bname, bw, bh, draw in (extra or []):      # extra frames (rail source declarations) in the same flow
        if x + bw > W - margin and x > margin:
            x = margin
            y += row_h + 12.7
            row_h = 0.0
        sheet.rect(snap(x - 2.54), snap(y - 5.08), snap(x + bw), snap(y + bh))
        sheet.text(bname, x, y - 1.27, size=1.524, bold=True)
        draw(x, y)
        x += bw + 10.16
        row_h = max(row_h, bh)


def sheet_file(circuit, sheet, page='A3', title_block=None):
    libs = ['lib_symbols']
    for lib_id in sorted(sheet.lib_ids):
        libs.append(circuit.symbol_node(lib_id))
    root = ['kicad_sch', ['version', 20250114], ['generator', Q('eeschema')],
            ['generator_version', Q('10.0')], ['uuid', Q(sheet.uuid)], ['paper', Q(page)]]
    if title_block:
        root.append(title_block)
    root.append(libs)
    root.extend(sheet.items)
    root.append(['embedded_fonts', 'no'])
    return dumps(root) + '\n'


def title_block(title, rev, date, comments=()):
    tb = ['title_block', ['title', Q(title)], ['date', Q(date)], ['rev', Q(rev)],
          ['company', Q('ODD JOBS')]]
    for i, c in enumerate(comments, 1):
        tb.append(['comment', i, Q(c)])
    return tb


def power_flags(sheet, nets, x0, y0):
    """A PWR_FLAG joined pin-to-pin with each rail symbol: declares the rail's source for ERC."""
    for i, net in enumerate(nets):
        x, y = snap(x0 + i * 12.7), snap(y0)
        sheet.power(net, (x, y), (0, 1) if POWER_NETS[net][1] == 'down' else (0, -1))
        sheet.power('PWR_FLAG', (x, y), (0, -1) if POWER_NETS[net][1] == 'down' else (0, 1))
        sheet.text(net, x + 1.27, y + 6.35, size=1.0)


def generate(circuit, outdir, rev, date, flags=()):
    import os
    root_uuid = stable_uuid('root')
    nets_by_sheet = {}
    for p in circuit.parts:
        for net in p.pin_net.values():
            if net:
                nets_by_sheet.setdefault(net, set()).add(p.sheet)
    # Every net is a global label: net names stay plain (LCD_SCLK, not /Sheet/LCD_SCLK) in the PCB,
    # the routing rules and the test documents, as in the KINO capture.
    global_nets = set(nets_by_sheet)

    sheets = []
    nets = circuit.nets()
    for i, (stem, title, desc) in enumerate(circuit.sheets, start=2):
        sh = Sheet(stem, title, root_uuid, i)
        parts = [p for p in circuit.parts if p.sheet == stem]
        mine = [n for n, where in dict(flags).items() if where == stem]
        extra = []
        if mine:                           # each source declaration sits on the sheet that uses the rail, in a frame
            extra.append(('RAIL SOURCES', 12.7 * len(mine) + 5.08, 22.86,
                          lambda x_, y_, mine=mine, sh=sh: power_flags(sh, mine, x_ + 2.54, y_ + 10.16)))
        layout_sheet(circuit, sh, parts, global_nets, nets=nets, extra=extra)
        sheets.append((sh, desc))
        with open(os.path.join(outdir, stem + '.kicad_sch'), 'w', encoding='utf-8') as f:
            f.write(sheet_file(circuit, sh, title_block=title_block(
                'MAO_MAIN ' + rev + ' - ' + title, rev, date, [desc])))

    # root: sheet symbols laid out as a block diagram
    items = []
    x0, y0 = 25.4, 50.8
    for idx, (sh, desc) in enumerate(sheets):
        col, row = idx % 3, idx // 3
        x, y = x0 + col * 127.0, y0 + row * 76.2
        items.append(['sheet', ['at', x, y], ['size', 101.6, 50.8], ['exclude_from_sim', 'no'],
                      ['in_bom', 'yes'], ['on_board', 'yes'], ['dnp', 'no'], ['fields_autoplaced', 'yes'],
                      ['stroke', ['width', 0.1524], ['type', 'solid']], ['fill', ['color', 0, 0, 0, 0.0]],
                      ['uuid', Q(sh.uuid)],
                      ['property', Q('Sheetname'), Q(sh.title), ['at', x, y - 0.7, 0], eff(justify=['left', 'bottom'])],
                      ['property', Q('Sheetfile'), Q(sh.stem + '.kicad_sch'), ['at', x, y + 51.4, 0],
                       eff(justify=['left', 'top'], hide=True)],
                      ['instances', ['project', Q(PROJECT), ['path', Q('/' + root_uuid), ['page', Q(str(sh.page))]]]]])
        # description inside the sheet box
        lines = desc.split('\n')
        for k, line in enumerate(lines):
            items.append(['text', Q(line), ['exclude_from_sim', 'no'], ['at', x + 2.54, y + 5.08 + k * 2.54, 0],
                          eff(justify=['left', 'bottom']), ['uuid', Q(stable_uuid('rtext', sh.stem, k))]])
    notes = circuit.root_notes if hasattr(circuit, 'root_notes') else []
    for k, line in enumerate(notes):
        items.append(['text', Q(line), ['exclude_from_sim', 'no'], ['at', 25.4, 215.9 + k * 3.81, 0],
                      eff(size=1.524 if k == 0 else 1.27, justify=['left', 'bottom']),
                      ['uuid', Q(stable_uuid('rnote', k))]])
    root = ['kicad_sch', ['version', 20250114], ['generator', Q('eeschema')],
            ['generator_version', Q('10.0')], ['uuid', Q(root_uuid)], ['paper', Q('A3')],
            title_block('MAO_MAIN ' + rev, rev, date, ['ODD JOBS / MAO main board', 'Design-as-code: hardware/mao/gen']),
            ['lib_symbols']] + items + [
        ['sheet_instances', ['path', Q('/'), ['page', Q('1')]]],
        ['embedded_fonts', 'no']]
    with open(os.path.join(outdir, 'mao-main.kicad_sch'), 'w', encoding='utf-8') as f:
        f.write(dumps(root) + '\n')
    return root_uuid, {sh.stem: sh for sh, _ in sheets}
