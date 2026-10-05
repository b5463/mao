"""MAO Rev A schematic: the part and net vocabulary shared by every sheet.

The schematic is written as a SKiDL netlist, sheet by sheet (s01..s11).
Every IC pin carries its number and name from the vendor datasheet cited in
its template, and its electrical type, so SKiDL's ERC and the Gate C checks
in checks.py can reason about it. Pin metadata used by the checks:

  lim   the supply pin that bounds this pin's voltage (abs max = that + 0.3 V)
  pd    an internal pull-down on an enable input (default-off evidence)
  pu    an internal pull-up
"""
import builtins

from skidl import TEMPLATE, SKIDL, Net, Part, Pin

NCNET = builtins.NC  # SKiDL's no-connect net

T = Pin.types
PWR, PAS, IN, OUT, IO, OC, NC = T.PWRIN, T.PASSIVE, T.INPUT, T.OUTPUT, T.BIDIR, T.OPENCOLL, T.NOCONNECT
POUT = T.PWROUT

# ---------------------------------------------------------------- rails
RAILS = {}      # name -> (vmin, vmax, note)


def rail(name, vmin, vmax, note=''):
    """A power net with its operating range; drive=POWER for SKiDL's ERC."""
    n = Net(name)
    n.drive = Pin.drives.POWER
    n.vmin, n.vmax, n.is_rail = vmin, vmax, True
    RAILS[name] = (vmin, vmax, note)
    return n


def sig(name, dom, note=''):
    """A signal net at the logic level of rail `dom` (a RAILS name, or None)."""
    n = Net(name)
    n.dom, n.note, n.is_rail = dom, note, False
    return n


# ---------------------------------------------------------------- parts
def ic(name, ref, pins, fp, mpn, mfr, src, value=None, **kw):
    """An IC template. pins: (num, name, type[, meta dict])."""
    ps = []
    for num, nm, ty, *meta in pins:
        p = Pin(num=str(num), name=nm, func=ty)
        p.meta = meta[0] if meta else {}
        ps.append(p)
    part = Part(tool=SKIDL, name=name, ref_prefix=ref, dest=TEMPLATE, pins=ps,
                footprint=fp, value=value or name, **kw)
    part.mpn, part.mfr, part.src = mpn, mfr, src
    return part


def _two(ref, name, fp, value, mpn, mfr, spec, **kw):
    p = Part(tool=SKIDL, name=name, ref_prefix=ref, dest=TEMPLATE,
             pins=[Pin(num='1', name='1', func=PAS), Pin(num='2', name='2', func=PAS)],
             footprint=fp, value=value)
    p.mpn, p.mfr, p.spec, p.src = mpn, mfr, spec, 'generic passive'
    for k, v in kw.items():
        setattr(p, k, v)
    for q in p.pins:
        q.meta = {}
    return p()


FP_R = {'0402': 'Resistor_SMD:R_0402_1005Metric', '0603': 'Resistor_SMD:R_0603_1608Metric',
        '0805': 'Resistor_SMD:R_0805_2012Metric'}
FP_C = {'0201': 'Capacitor_SMD:C_0201_0603Metric', '0402': 'Capacitor_SMD:C_0402_1005Metric',
        '0603': 'Capacitor_SMD:C_0603_1608Metric', '0805': 'Capacitor_SMD:C_0805_2012Metric'}


# Murata part numbers by (value, size, dielectric) -> (MPN, rated volts)
C_MPN = {
    ('0.1uF', '0402', 'X5R'): ('GRM155R61A104KA01D', 10), ('0.1uF', '0402', 'X7R'): ('GRM155R71A104KA01D', 10),
    ('1uF', '0402', 'X5R'): ('GRM155R61A105KE15D', 10), ('4.7uF', '0402', 'X5R'): ('GRM155R61A475MEAAD', 10),
    ('10uF', '0603', 'X5R'): ('GRM188R61A106KE69D', 10), ('22uF', '0805', 'X5R'): ('GRM21BR61A226ME51L', 10),
    ('1uF', '0603', 'X5R'): ('GRM188R61E105KA12D', 25), ('10uF', '0805', 'X5R'): ('GRM21BR61E106KA73L', 25),
    ('2.2uF', '0603', 'X7R'): ('GRM188R71A225KE15D', 10), ('10nF', '0402', 'X7R'): ('GRM155R71C103KA01D', 16),
    ('1nF', '0402', 'X7R'): ('GRM155R71H102KA01D', 50), ('10pF', '0402', 'C0G'): ('GRM1555C1H100JA01D', 50),
    ('100pF', '0402', 'C0G'): ('GRM1555C1H101JA01D', 50),
}


def yageo(value, size, tol):
    """Yageo RC-series code, e.g. 4.7k 1 % 0402 -> RC0402FR-074K7L, 0 R -> RC0402JR-070RL."""
    v = value.replace('R', '').replace('.0k', 'k').replace('.0M', 'M')
    if v in ('0', ''):
        return f'RC{size}JR-070RL'
    if v.endswith('k'):
        code = v[:-1].replace('.', 'K') + ('K' if '.' not in v else '')
    elif v.endswith('M'):
        code = v[:-1].replace('.', 'M') + ('M' if '.' not in v else '')
    else:
        code = v.replace('.', 'R') + ('R' if '.' not in v else '')
    return f'RC{size}{"F" if tol == "1%" else "J"}R-07{code}L'


def R(value, size='0402', tol='1%', mpn='', power='1/16 W', dnp=False, why=''):
    mpn = mpn or yageo(value, size, tol)
    r = _two('R', 'R', FP_R[size], value, mpn, 'Yageo', f'{tol} {power} thick film {size}',
             tol=tol, dnp=dnp, why=why)
    return r


def C(value, volts, size='0402', diel='X5R', tol='10%', mpn='', dnp=False, why=''):
    """A capacitor. volts is its rating; checks.py derates against the rail."""
    if not mpn:
        mpn, volts = C_MPN[(value, size, diel)]
    c = _two('C', 'C', FP_C[size], value, mpn, 'Murata', f'{volts} V {diel} {tol} {size}',
             volts=volts, diel=diel, tol=tol, dnp=dnp, why=why)
    return c


def L(value, fp, mpn, mfr, spec):
    return _two('L', 'L', fp, value, mpn, mfr, spec)


def FB(value, mpn, mfr='Murata', size='0402', spec=''):
    return _two('FB', 'FB', f'Inductor_SMD:L_{size}_1005Metric' if size == '0402' else f'Inductor_SMD:L_{size}_1608Metric',
                value, mpn, mfr, spec)


def TP(net, name=''):
    """A 1 mm test pad on `net` (a single pin, so it's exempt from the 2-pin check)."""
    t = Part(tool=SKIDL, name='TP', ref_prefix='TP', dest=TEMPLATE,
             pins=[Pin(num='1', name='1', func=PAS)], footprint='TestPoint:TestPoint_Pad_D1.0mm',
             value=name or net.name)
    t.mpn, t.mfr, t.spec, t.src = 'n/a (copper pad)', '-', 'test pad', ''
    t.pins[0].meta = {}
    tp = t()
    tp[1] += net
    tp.is_tp = True
    return tp


def link(a, b, name, why):
    """A 0 Ω measurement link (populated) between two rails."""
    r = _two('R', 'R', 'Resistor_SMD:R_0603_1608Metric', '0R', 'RC0603JR-070RL', 'Yageo',
             '0 Ω 1 A 0603 jumper', why=why, dnp=False, tol='jumper')
    r.is_link = True
    r[1] += a
    r[2] += b
    return r


def pullup(net, rail_net, value, why='', size='0402'):
    r = R(value, size, why=why)
    r[1] += net
    r[2] += rail_net
    return r


def pulldown(net, gnd, value, why='', size='0402'):
    r = R(value, size, why=why)
    r[1] += net
    r[2] += gnd
    return r


def decouple(rail_net, gnd, value, volts, size='0402', diel='X5R', why=''):
    c = C(value, volts, size, diel, why=why)
    c[1] += rail_net
    c[2] += gnd
    return c
