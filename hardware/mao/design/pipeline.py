"""MAO_MAIN board pipeline: one command from circuit.py to a routed board (plain Python 3 driver).

    python3 pipeline.py all          # capture -> placement -> plane vias -> route -> check
    python3 pipeline.py route        # from the placed board: plane vias, grid router, apply, check
    python3 pipeline.py <step> ...   # any single step below

Steps run in their own processes: KiCad's bindings become unreliable after a Remove(), so every
board-editing script loads, edits and saves once (KINO practice).
"""
import os
import shutil
import subprocess
import sys

from netrules import CACHE, NAME, OUTPUTS, ROOT, ROUTE_ORDER

HERE = os.path.dirname(os.path.abspath(__file__))
KIPY = os.environ.get('KICAD_PY', os.path.expanduser(
    '~/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3'))
VENV = os.environ.get('ROUTER_PY', '')          # Python with numpy/scipy/numba/Pillow for grid_router
BOARD = ROOT / (NAME + '.kicad_pcb')


def run(py, *args, log=None):
    cmd = [py] + list(args)
    print('$', os.path.basename(py), ' '.join(args), flush=True)
    r = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True)
    out = '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if 'wxApp' not in l and 'assert' not in l)
    if log:
        (CACHE / log).write_text(out)
        out = '\n'.join(out.splitlines()[-6:])
    print(out, flush=True)
    if r.returncode not in (0,):
        raise SystemExit('step failed: %s' % ' '.join(args))


def snapshot(name):
    shutil.copy(BOARD, CACHE / (name + '.kicad_pcb'))


def restore(name):
    shutil.copy(CACHE / (name + '.kicad_pcb'), BOARD)


# ground pins beside an exposed pad join it on the part's own layer (ODD JOBS 16: shortest return)
EP_LINKS = ['U501.3>U501.17', 'U501.11>U501.17', 'U501.15>U501.17', 'U202.6>U202.17', 'U202.16>U202.17',
            'U103.6>U103.9']

# 4 layers: neighbouring plane pins share one via to In1 / In2 (brief: fewer vias than the 6-layer A0)
SHARE = ['--share', '1.6']
# pour-joined resistors whose GND pad the routing boxes in (DRC: starved thermal / island): own via up front
POUR_BOXED = ['R105.2', 'R114.2', 'R116.2', 'R207.2', 'R208.2', 'R211.2', 'R504.2', 'SW301.2', 'TP2.1']

def starved_pads():
    """GND pads DRC reports as starved thermals (pour-joined resistors boxed in by routing): each gets its own via."""
    import json, re
    drc = json.loads((OUTPUTS / 'DRC.json').read_text(encoding='utf-8'))
    out = []
    for v in drc['violations']:
        if v['type'] != 'starved_thermal':
            continue
        for it in v['items']:
            m = re.match(r'Pad (\S+) \[GND\] of (\S+)', it['description'])
            if m:
                out.append('%s.%s' % (m.group(2), m.group(1)))
    return sorted(set(out))


STEPS = {
    'capture': lambda: run(sys.executable, 'build_sch.py'),
    'footprints': lambda: run(KIPY, 'footprints.py'),
    'place': lambda: (run(KIPY, 'fp_cache.py'), run(KIPY, 'build_pcb.py'), run(KIPY, 'stackup.py'), run(KIPY, 'models3d.py'),
                      snapshot('placed')),
    'power': lambda: (run(KIPY, 'route_power.py', 'add'), run(KIPY, 'route_local.py', 'add'),
                      run(sys.executable, 'project.py'), snapshot('power')),
    'planevias': lambda: (run(KIPY, 'gnd_pad_vias.py', *EP_LINKS, log='gnd-links.log'),
                          run(KIPY, 'gnd_pad_vias.py', '--all', *SHARE, '--pour', 'R,TP,SW', *POUR_BOXED, log='gnd-vias.log'),
                          run(KIPY, 'gnd_pad_vias.py', '--all', *SHARE, '--net', '+3V3', log='3v3-vias.log'),
                          run(sys.executable, 'project.py'), snapshot('planevias')),
    'drc': lambda: run(sys.executable, 'review.py', 'drc'),
    'planes': lambda: run(KIPY, 'plane_check.py'),
    'offpad': lambda: (run(KIPY, 'via_off_pad.py'), run(sys.executable, 'project.py')),
    'snap45': lambda: (run(KIPY, 'snap45.py'), run(sys.executable, 'project.py')),
    'gridroute': lambda: (run(KIPY, 'grid_dump.py'), os.environ.setdefault('GR_PRIORITY', ','.join(ROUTE_ORDER)),
                          os.environ.setdefault('GR_VIACOST', '160'),    # 4 layers: a via is worth ~4 mm of track
                          run(VENV, 'grid_router.py', log='grid-router.log'),
                          run(KIPY, 'apply_routes.py'), run(sys.executable, 'project.py'), snapshot('gridrouted')),
    'freeroute': lambda: (run(KIPY, 'route_freerouting.py', 'export'), run(sys.executable, 'route_freerouting.py', 'run'),
                          run(KIPY, 'route_freerouting.py', 'import'), run(sys.executable, 'project.py'),
                          snapshot('freerouted')),
    'tidy': lambda: (run(KIPY, 'tidy_routes.py'), run(sys.executable, 'project.py')),
    'stitch': lambda: (run(KIPY, 'gnd_stitch.py'), run(sys.executable, 'project.py')),
    'starved': lambda: (run(KIPY, 'gnd_pad_vias.py', *SHARE, *starved_pads()) if starved_pads() else None,
                        run(sys.executable, 'project.py')),
    'prune': lambda: (run(KIPY, 'prune_stubs.py'), run(sys.executable, 'project.py')),
    'silk': lambda: (run(KIPY, 'silk.py'), run(sys.executable, 'project.py')),
    'labels': lambda: (run(KIPY, 'mao_labels.py'), run(sys.executable, 'project.py')),
    'checksilk': lambda: run(KIPY, 'check_silk_text.py'),
    'stackup': lambda: run(KIPY, 'stackup.py'),
    'models': lambda: run(KIPY, 'models3d.py'),
    'fab': lambda: run(KIPY, 'fab.py'),
}
SEQ = {
    'all': ['capture', 'footprints', 'place', 'power', 'planevias', 'drc', 'gridroute', 'drc',
            'prune', 'drc', 'prune', 'drc', 'stitch', 'drc', 'starved', 'drc', 'tidy', 'drc', 'prune', 'drc', 'prune', 'drc', 'starved', 'drc',
            'prune', 'drc', 'offpad', 'drc', 'snap45', 'drc', 'planes'],
    'route': ['planevias', 'drc', 'gridroute', 'drc', 'prune', 'drc', 'prune', 'drc', 'stitch', 'drc',
              'starved', 'drc', 'tidy', 'drc', 'prune', 'drc', 'prune', 'drc', 'starved', 'drc', 'prune', 'drc',
              'offpad', 'drc', 'snap45', 'drc', 'planes'],
    'silk': ['silk', 'labels', 'checksilk', 'drc'],
}

if __name__ == '__main__':
    for arg in sys.argv[1:]:
        for step in SEQ.get(arg, [arg]):
            if step.startswith('restore:'):
                restore(step.split(':', 1)[1])
            else:
                STEPS[step]()
