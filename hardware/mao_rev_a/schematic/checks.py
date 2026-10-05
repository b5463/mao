"""Gate C electrical rule checks on top of SKiDL's ERC.

Each check returns findings (category, severity, text). A finding matching a
WAIVERS entry is reported as waived with its reason; anything else fails.
"""
import os
import re

KICAD_FP = r'C:\Program Files\KiCad\10.0\share\kicad\footprints'
HERE = os.path.dirname(os.path.abspath(__file__))
LOCAL_FP = os.path.join(HERE, '..', 'footprints')


def _nets(circuit):
    return [n for n in circuit.get_nets() if n.pins]


def _is_gnd(net):
    return net is not None and net.name == 'GND'


def _other(r, net):
    """The net at the other end of a two-pin part from `net`."""
    a, b = r.pins[0].nets, r.pins[1].nets
    a = a[0] if a else None
    b = b[0] if b else None
    return b if a is net else a


def _resistors_on(net):
    return [p.part for p in net.pins if p.part.ref.startswith('R') and len(p.part.pins) == 2
            and not getattr(p.part, 'is_link', False)]


def check_connectivity(circuit):
    out = []
    for part in circuit.parts:
        for p in part.pins:
            if p.func == p.types.NOCONNECT:
                if p.nets and not p.nets[0].name.startswith('__NOCONNECT'):
                    out.append(('connectivity', 'error', f'{part.ref}.{p.name} is NC but connected to {p.nets[0].name}'))
                continue
            if not p.nets:
                out.append(('connectivity', 'error', f'{part.ref}.{p.num} {p.name} unconnected'))
    for n in _nets(circuit):
        if len(n.pins) < 2 and not any(getattr(p.part, 'is_tp', False) for p in n.pins):
            out.append(('connectivity', 'error', f'net {n.name} has one pin ({n.pins[0].part.ref}.{n.pins[0].name})'))
    return out


def check_domains(circuit, rails, never_above=()):
    """A pin bounded by a supply (meta lim) must not see a net above that supply + 0.3 V.

    never_above: (low, high) rail pairs where low <= high by topology (a buck
    output never exceeds its input), so the pair is safe at every battery level.
    """
    out = []
    for part in circuit.parts:
        pins = {p.name: p for p in part.pins}
        for p in part.pins:
            lim = p.meta.get('lim') if hasattr(p, 'meta') else None
            if not lim or not p.nets:
                continue
            sup = pins[lim].nets[0] if pins[lim].nets else None
            net = p.nets[0]
            dom = net.name if getattr(net, 'is_rail', False) else getattr(net, 'dom', None)
            if sup is None or dom is None or sup.name not in rails or dom not in rails:
                continue
            svmin, dvmax = rails[sup.name][0], rails[dom][1]
            if dom == sup.name or (dom, sup.name) in never_above:
                continue
            if dvmax > svmin + 0.3:
                out.append(('domain', 'error',
                            f'{part.ref}.{p.name} on {net.name} ({dom} up to {dvmax} V) exceeds {lim} '
                            f'({sup.name} down to {svmin} V) + 0.3 V'))
    return out


def check_backpower(circuit, gated):
    """Every signal from an always-on domain that reaches a part powered from a gated rail.

    These are the back-power paths: each must be driven low or isolated while that rail is
    off (the firmware power-down rule). Reported as 'backpower' info rows for the audit table.
    """
    out = []
    for part in circuit.parts:
        rails = {p.nets[0].name for p in part.pins if p.nets and p.nets[0].name in gated
                 and p.func in (p.types.PWRIN,)}
        if not rails:
            continue
        for p in part.pins:
            if not p.nets or p.func in (p.types.PWRIN, p.types.PWROUT, p.types.NOCONNECT):
                continue
            net = p.nets[0]
            if getattr(net, 'is_rail', False):
                continue
            nets_ = [net]
            # follow one series resistor (damping / data resistors)
            for q in net.pins:
                r = q.part
                if r is not part and r.ref.startswith('R') and len(r.pins) == 2 and not getattr(r, 'dnp', False):
                    o = _other(r, net)
                    if o is not None and not getattr(o, 'is_rail', False):
                        nets_.append(o)
            for n in nets_:
                dom = getattr(n, 'dom', None)
                if dom and dom not in gated and not getattr(n, 'is_rail', False):
                    out.append(('backpower', 'info', f'{part.ref} {part.name}.{p.name} <- {n.name} ({dom}) '
                                f'while {"/".join(sorted(rails))} off'))
    # the panel connector is passive: list its pins by the net domains it carries
    return out


def check_caps(circuit, rails):
    """Capacitor rating vs the rail it sits on: >= the rail max, and >= 1.5x for class II ceramics."""
    out = []
    for part in circuit.parts:
        v = getattr(part, 'volts', None)
        if v is None:
            continue
        for p in part.pins:
            n = p.nets[0] if p.nets else None
            if n is None or n.name not in rails:
                continue
            vmax = rails[n.name][1]
            if v < vmax:
                out.append(('derating', 'error', f'{part.ref} {part.value} {v} V on {n.name} (up to {vmax} V)'))
            elif v < 1.5 * vmax and part.diel in ('X5R', 'X7R', 'X6S'):
                out.append(('derating', 'warning', f'{part.ref} {part.value} {v} V on {n.name} (up to {vmax} V): '
                            f'< 1.5x, check DC-bias capacitance'))
    return out


def check_default_off(circuit, enables):
    """Every hardware enable must be held off through reset: a resistor to GND, or an internal pull-down."""
    out = []
    nets = {n.name: n for n in _nets(circuit)}
    for name in enables:
        n = nets.get(name)
        if n is None:
            out.append(('default-off', 'error', f'enable net {name} missing'))
            continue
        ext = [r for r in _resistors_on(n) if _is_gnd(_other(r, n))]
        internal = [f'{p.part.ref}.{p.name}' for p in n.pins if getattr(p, 'meta', {}).get('pd')]
        if not ext and not internal:
            out.append(('default-off', 'error', f'{name}: no pull-down (external or internal)'))
        ups = [r for r in _resistors_on(n) if getattr(_other(r, n), 'is_rail', False) and not _is_gnd(_other(r, n))]
        if ups:
            out.append(('default-off', 'error', f'{name}: has a pull-up ({ups[0].ref})'))
    return out


def check_i2c(circuit, sda, scl, rail, addrs):
    out = []
    nets = {n.name: n for n in _nets(circuit)}
    for name in (sda, scl):
        ups = [r for r in _resistors_on(nets[name]) if _other(r, nets[name]).name == rail]
        if len(ups) != 1:
            out.append(('i2c', 'error', f'{name}: {len(ups)} pull-ups to {rail} (want exactly 1)'))
    seen = {}
    for dev, a in addrs.items():
        if a in seen:
            out.append(('i2c', 'error', f'address 0x{a:02X} shared by {seen[a]} and {dev}'))
        seen[a] = dev
    return out


def check_footprints(circuit):
    out = []
    for part in circuit.parts:
        fp = part.footprint
        if not fp or ':' not in fp:
            out.append(('footprint', 'error', f'{part.ref}: no footprint'))
            continue
        lib, name = fp.split(':', 1)
        paths = [os.path.join(KICAD_FP, lib + '.pretty', name + '.kicad_mod'),
                 os.path.join(LOCAL_FP, lib + '.pretty', name + '.kicad_mod')]
        hit = next((p for p in paths if os.path.exists(p)), None)
        if not hit:
            out.append(('footprint', 'error', f'{part.ref}: footprint {fp} not found'))
            continue
        pads = set(re.findall(r'\(pad "([^"]*)"', open(hit, encoding='utf8').read()))
        pads.discard('')
        missing = [p.num for p in part.pins if p.num not in pads]
        if missing:
            out.append(('footprint', 'error', f'{part.ref} ({fp}): pins {missing} have no pad'))
    return out


def apply_waivers(findings, waivers):
    """waivers: list of (category, regex, reason). Returns (open, waived)."""
    open_, waived = [], []
    for cat, sev, text in findings:
        w = next((w for w in waivers if w[0] == cat and re.search(w[1], text)), None)
        if w:
            waived.append((cat, sev, text, w[2]))
        else:
            open_.append((cat, sev, text))
    return open_, waived
