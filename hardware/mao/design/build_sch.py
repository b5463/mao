"""Build the MAO_MAIN capture: symbol library, library tables, schematic sheets, then ERC, netlist,
PDF and per-sheet PNG renders for visual review. Plain Python 3 (no pcbnew needed).

    python3 hardware/mao/design/build_sch.py
"""
import json
import os
import subprocess
import sys

import gen_sch
import kicadlib
from circuit import c
from netrules import NAME, OUTPUTS, ROOT

KICAD_CLI = os.environ.get('KICAD_CLI', os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'))
REV, DATE = 'A0', '2026-10-04'
RAILS = {'GND': 'down', '+3V3': 'up', 'VBUS': 'up', 'VSYS': 'up', 'VBAT': 'up', '3V3_LCD': 'up',
         'MIC_VDD': 'up', 'IR_RX_VCC': 'up', 'PWR_FLAG': 'up'}
# Rails fed through passive parts (connector, RC filters from GPIO/expander pins): ERC cannot see
# their source, so it is declared. Rails with a regulator or switch output need no flag.
FLAGS = {'GND': 'power', 'VBUS': 'power', 'MIC_VDD': 'sense', 'IR_RX_VCC': 'feedback'}


def run(*args):
    r = subprocess.run([KICAD_CLI] + list(args), capture_output=True, text=True)
    out = '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if 'Fontconfig' not in l)
    return r.returncode, out


def main():
    gen_sch.PROJECT = NAME
    gen_sch.setup_power_symbols(c, RAILS)
    c.resolve()
    c.check_nets()
    OUTPUTS.mkdir(exist_ok=True)
    (ROOT / 'lib').mkdir(exist_ok=True)
    kicadlib.write_symbol_lib(ROOT / 'lib' / 'MAO.kicad_sym',
                              [n for k, n in sorted(c.custom_symbols.items()) if k.startswith('MAO:')])
    (ROOT / 'sym-lib-table').write_text(
        '(sym_lib_table\n  (version 7)\n  (lib (name "MAO")(type "KiCad")(uri "${KIPRJMOD}/lib/MAO.kicad_sym")'
        '(options "")(descr "MAO_MAIN custom symbols"))\n)\n')
    (ROOT / 'fp-lib-table').write_text(
        '(fp_lib_table\n  (version 7)\n  (lib (name "MAO")(type "KiCad")(uri "${KIPRJMOD}/lib/MAO.pretty")'
        '(options "")(descr "MAO_MAIN custom footprints"))\n)\n')

    gen_sch.generate(c, str(ROOT), REV, DATE, flags=FLAGS)
    os.replace(ROOT / 'mao-main.kicad_sch', ROOT / (NAME + '.kicad_sch'))
    import project
    project.write()

    sch = str(ROOT / (NAME + '.kicad_sch'))
    code, out = run('sch', 'erc', '--severity-all', '--format', 'json', '-o', str(OUTPUTS / 'ERC.json'), sch)
    erc = json.loads((OUTPUTS / 'ERC.json').read_text())
    viol = [(s['path'], v['type'], v['description'], [i['description'] for i in v['items']])
            for s in erc['sheets'] for v in s['violations']]
    print('ERC violations:', len(viol))
    for v in viol[:60]:
        print('  ', v)
    run('sch', 'export', 'netlist', '--format', 'kicadsexpr', '-o', str(OUTPUTS / (NAME + '.net')), sch)
    run('sch', 'export', 'pdf', '-o', str(OUTPUTS / (NAME + '-schematic.pdf')), sch)
    svgdir = OUTPUTS / 'sch-svg'
    svgdir.mkdir(exist_ok=True)
    run('sch', 'export', 'svg', '--exclude-drawing-sheet', '-o', str(svgdir), sch)
    print('BOM parts:', sum(1 for p in c.parts if not p.fields.get('Exclude from BOM')),
          'of', len(c.parts), '| nets:', len(c.nets()))
    return 1 if viol else 0


if __name__ == '__main__':
    sys.exit(main())
