"""Build the MAO Rev A schematic: sheets 01-11 -> netlist, BOM, ERC report.

usage (SKiDL 2.3 in a venv):  python build.py
outputs: ../out/mao_rev_a.net (KiCad netlist), ../out/bom.csv, ../out/erc.txt
Exit status 1 if any unwaived error remains.
"""
import collections
import csv
import io
import os
import sys
from contextlib import redirect_stderr, redirect_stdout

os.environ.setdefault('KICAD10_SYMBOL_DIR', r'C:\Program Files\KiCad\10.0\share\kicad\symbols')
import skidl  # noqa: E402
from skidl import ERC, KICAD8, SKIDL, generate_netlist, set_default_tool  # noqa: E402
import builtins  # noqa: E402

default_circuit = builtins.default_circuit

set_default_tool(SKIDL)
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', 'out'))
sys.path.insert(0, HERE)

import checks  # noqa: E402
import nets  # noqa: E402
import s01_usb, s02_charger, s03_core, s04_mcu, s05_display, s06_s09_io, s10_s11  # noqa: E402,E401
from lib import RAILS  # noqa: E402

SHEETS = [
    ('01 USB-C input', s01_usb.build),
    ('02 Battery and charger', s02_charger.build),
    ('03 CORE_3V3 and fuel gauge', s03_core.build),
    ('04 MCU', s04_mcu.build),
    ('05 Display and backlight', s05_display.build),
    ('06 Encoder', s06_s09_io.encoder),
    ('07 IMU', s06_s09_io.imu),
    ('08 Haptics', s06_s09_io.haptics),
    ('09 Audio', s06_s09_io.audio),
    ('10 IR and RGB', s10_s11.ir_rgb),
    ('11 Service', s10_s11.service),
]

# (category, regex on the finding, reason)
WAIVERS = [
    ('skidl', r'OPEN-COLLECTOR .* BIDIRECTIONAL|BIDIRECTIONAL .* OPEN-COLLECTOR',
     'OPEN-DRAIN TO MCU INPUT: the S3 pin is configured as an input; pull-up on the net'),
    ('domain', r'LED1\.DIN on RGB_DIN .*AUX_SYS down to 0 V',
     'BACK-POWER, FIRMWARE-SEQUENCED: RGB_DATA (IO33) is driven low before AUX_PWR_EN goes low and '
     'until after it goes high; 330 R limits the current to < 8 mA if the rule is ever broken'),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    os.chdir(OUT)  # SKiDL drops its .erc/.log/_sklib.py next to the working directory
    seen = set()
    for name, fn in SHEETS:
        fn()
        for p in default_circuit.parts:
            if id(p) not in seen:
                p.sheet = name
                seen.add(id(p))

    log = io.StringIO()
    with redirect_stdout(log), redirect_stderr(log):
        ERC()
    import logging
    for lg in [logging.getLogger()] + [l for l in logging.Logger.manager.loggerDict.values()
                                        if isinstance(l, logging.Logger)]:
        for h in lg.handlers:
            h.flush()
    erc_file = os.path.join(HERE, 'build.erc')  # SKiDL's ERC log, written beside the script
    raw = log.getvalue().splitlines() + (open(erc_file, encoding='utf8').read().splitlines()
                                         if os.path.exists(erc_file) else [])
    skidl_erc = sorted({l for l in raw if l.startswith('ERC ') and 'INFO' not in l})
    skidl_erc = [l for l in skidl_erc if 'Missing tag' not in l and 'Random tag' not in l
                 and 'SYMBOL_DIR' not in l and 'fp-lib-table' not in l]

    c = default_circuit
    findings = []
    findings += checks.check_connectivity(c)
    findings += checks.check_domains(c, RAILS, nets.NEVER_ABOVE)
    findings += checks.check_caps(c, RAILS)
    findings += checks.check_default_off(c, nets.ENABLES)
    findings += checks.check_i2c(c, 'I2C_SDA', 'I2C_SCL', 'CORE_3V3', nets.I2C_ADDR)
    findings += checks.check_footprints(c)
    backpower = checks.check_backpower(c, nets.GATED)
    open_, waived = checks.apply_waivers(findings, WAIVERS)

    os.makedirs(OUT, exist_ok=True)
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        generate_netlist(file_=os.path.join(OUT, 'mao_rev_a.net'), tool=KICAD8)
    write_bom(c)

    errors = [f for f in open_ if f[1] == 'error']
    errors += [l for l in skidl_erc if 'ERROR' in l and not any(
        w[0] == 'skidl' and __import__('re').search(w[1], l) for w in WAIVERS)]
    with open(os.path.join(OUT, 'erc.txt'), 'w', encoding='utf8') as f:
        f.write(f'MAO Rev A ERC: {len(c.parts)} parts, {len([n for n in c.get_nets() if n.pins])} nets\n\n')
        s_open, s_waived = checks.apply_waivers([('skidl', 'warning' if 'WARNING' in l else 'error', l)
                                                 for l in skidl_erc], WAIVERS)
        f.write('SKiDL ERC, open:\n' + ''.join(f'  {t}\n' for _, _, t in s_open) + '\n')
        f.write('SKiDL ERC, waived:\n' + ''.join(f'  {t}  -- {w}\n' for _, _, t, w in s_waived) + '\n')
        f.write('Gate C checks, open:\n' + ''.join(f'  [{s}] {cat}: {t}\n' for cat, s, t in open_) + '\n')
        f.write('Gate C checks, waived:\n' + ''.join(f'  [{s}] {cat}: {t}  -- {w}\n' for cat, s, t, w in waived))
        f.write('\nFitted capacitance per rail (nominal; DC-bias derating not applied):\n')
        for rail, (uf, parts) in rail_caps(c).items():
            f.write(f'  {rail:<9} {uf:7.2f} uF  {" ".join(parts)}\n')
        f.write('\nBack-power exposure (each line is held low / isolated while its rail is off):\n'
                + ''.join(f'  {t}\n' for _, _, t in backpower))
    print(open(os.path.join(OUT, 'erc.txt'), encoding='utf8').read())
    return 1 if errors else 0


def rail_caps(c):
    units = {'pF': 1e-6, 'nF': 1e-3, 'uF': 1.0}
    out = collections.OrderedDict((r, [0.0, []]) for r in RAILS if r != 'GND')
    for p in c.parts:
        if getattr(p, 'volts', None) is None or getattr(p, 'dnp', False):
            continue
        v = str(p.value)
        uf = next(float(v[:-2]) * k for u, k in units.items() if v.endswith(u))
        for q in p.pins:
            n = q.nets[0].name if q.nets else None
            if n in out:
                out[n][0] += uf
                out[n][1].append(f'{p.ref}={v}')
    return out


def write_bom(c):
    groups = collections.OrderedDict()
    for p in sorted(c.parts, key=lambda p: (p.ref_prefix, int(''.join(ch for ch in p.ref if ch.isdigit()) or 0))):
        if getattr(p, 'is_tp', False):
            key = ('TP', 'test pad', p.footprint, '', '', False)
        else:
            key = (p.name, p.value, p.footprint, getattr(p, 'mpn', ''), getattr(p, 'spec', ''),
                   bool(getattr(p, 'dnp', False)))
        groups.setdefault(key, []).append(p)
    with open(os.path.join(OUT, 'bom.csv'), 'w', newline='', encoding='utf8') as f:
        w = csv.writer(f)
        w.writerow(['Refs', 'Qty', 'Part', 'Value', 'Spec / tolerance', 'MPN', 'Manufacturer', 'Footprint',
                    'DNP', 'Sheet', 'Why / source'])
        for (name, value, fp, mpn, spec, dnp), ps in groups.items():
            p0 = ps[0]
            w.writerow([' '.join(p.ref for p in ps), len(ps), name, value, spec, mpn, getattr(p0, 'mfr', ''), fp,
                        'DNP' if dnp else '', '; '.join(sorted({p.sheet for p in ps})),
                        getattr(p0, 'why', '') or getattr(p0, 'src', '')])


if __name__ == '__main__':
    sys.exit(main())
