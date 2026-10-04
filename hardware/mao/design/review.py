"""Checker + visual review pass for MAO_MAIN (plain Python 3; calls kicad-cli).

    python3 review.py drc            # DRC with schematic parity -> outputs/DRC.json, summary
    python3 review.py plots [dir]    # per-side layer plots as PNG (needs PyMuPDF in the router venv)
    python3 review.py render [dir]   # 3D renders: top, bottom, iso

Visual review is part of every pass (ODD JOBS 194/195): the PNGs are looked at, not just produced.
"""
import collections
import json
import os
import subprocess
import sys

from netrules import DRC_JSON, NAME, OUTPUTS, ROOT, ORIGIN

KICAD_CLI = os.environ.get('KICAD_CLI', os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'))
PCB = str(ROOT / (NAME + '.kicad_pcb'))


def cli(*args):
    r = subprocess.run([KICAD_CLI] + list(args), capture_output=True, text=True)
    return r.returncode, '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if 'Fontconfig' not in l)


def drc(refill=True, show=40):
    import project
    project.write()        # any pcbnew save may have reset the project rules
    args = ['pcb', 'drc', '--schematic-parity', '--severity-all', '--format', 'json', '-o', str(DRC_JSON)]
    if refill:
        args += ['--refill-zones', '--save-board']
    cli(*(args + [PCB]))
    d = json.loads(DRC_JSON.read_text())
    kinds = collections.Counter(v['type'] for v in d['violations'])
    print('violations:', sum(kinds.values()), dict(kinds))
    print('unconnected:', len(d.get('unconnected_items', [])), '| parity:', len(d.get('schematic_parity', [])))
    shown = 0
    for v in d['violations']:
        if v['type'] in ('unconnected_items',):
            continue
        if shown < show:
            items = '; '.join('%s @(%.2f,%.2f)' % (i['description'][:60], i['pos']['x'] - ORIGIN, i['pos']['y'] - ORIGIN)
                              for i in v['items'])
            print('  [%s] %s | %s' % (v['type'], v['description'][:60], items))
            shown += 1
    for v in d.get('schematic_parity', [])[:20]:
        print('  [parity] %s | %s' % (v['description'], [i['description'] for i in v['items']]))
    return d


def to_png(svg, png, zoom=4.0):
    py = os.environ.get('PYMUPDF_PY')
    code = ('import pymupdf,sys;d=pymupdf.open(sys.argv[1]);p=d[0];'
            'p.get_pixmap(matrix=pymupdf.Matrix(%f,%f)).save(sys.argv[2])' % (zoom, zoom))
    subprocess.run([py, '-c', code, svg, png], check=True)


def plots(outdir):
    os.makedirs(outdir, exist_ok=True)
    sets = {
        'front': 'F.Cu,F.SilkS,F.Fab,F.CrtYd,Edge.Cuts,F.Mask',
        'back': 'B.Cu,B.SilkS,B.Fab,B.CrtYd,Edge.Cuts,B.Mask',
        'inner': 'In1.Cu,In2.Cu,In3.Cu,In4.Cu,Edge.Cuts',
        'silk-front': 'F.SilkS,F.Mask,Edge.Cuts',
        'silk-back': 'B.SilkS,B.Mask,Edge.Cuts',
    }
    for name, layers in sets.items():
        svg = os.path.join(outdir, name + '.svg')
        extra = ['--mirror'] if name in ('back', 'silk-back') else []
        cli('pcb', 'export', 'svg', '--layers', layers, '--mode-single', '--exclude-drawing-sheet',
            '--fit-page-to-board', *extra, '-o', svg, PCB)
        to_png(svg, svg[:-4] + '.png')
    print('plots in', outdir)


def render(outdir):
    os.makedirs(outdir, exist_ok=True)
    for side, rot in (('top', ''), ('bottom', ''), ('iso', '-40,0,30')):
        args = ['pcb', 'render', '--quality', 'high', '--width', '1800', '--height', '1800', '--background', 'opaque',
                '--use-board-stackup-colors', '-o', os.path.join(outdir, 'render-%s.png' % side)]
        if side == 'iso':
            args += ['--rotate', rot, '--perspective']
        else:
            args += ['--side', side]
        cli(*(args + [PCB]))
    print('renders in', outdir)


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'drc'
    out = sys.argv[2] if len(sys.argv) > 2 else str(OUTPUTS / 'review')
    {'drc': lambda: drc(), 'plots': lambda: plots(out), 'render': lambda: render(out)}[what]()
