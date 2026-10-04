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

from netrules import CACHE, NAME, ROOT, ROUTE_ORDER

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

STEPS = {
    'capture': lambda: run(sys.executable, 'build_sch.py'),
    'footprints': lambda: run(KIPY, 'footprints.py'),
    'place': lambda: (run(KIPY, 'build_pcb.py'), run(KIPY, 'stackup.py'), snapshot('placed')),
    'power': lambda: (run(KIPY, 'route_power.py', 'add'), run(KIPY, 'route_local.py', 'add'),
                      run(sys.executable, 'project.py'), snapshot('power')),
    'planevias': lambda: (run(KIPY, 'gnd_pad_vias.py', *EP_LINKS, log='gnd-links.log'),
                          run(KIPY, 'gnd_pad_vias.py', '--all', log='gnd-vias.log'),
                          run(KIPY, 'gnd_pad_vias.py', '--all', '--net', '+3V3', log='3v3-vias.log'),
                          run(sys.executable, 'project.py'), snapshot('planevias')),
    'drc': lambda: run(sys.executable, 'review.py', 'drc'),
    'gridroute': lambda: (run(KIPY, 'grid_dump.py'), os.environ.setdefault('GR_PRIORITY', ','.join(ROUTE_ORDER)),
                          run(VENV, 'grid_router.py', log='grid-router.log'),
                          run(KIPY, 'apply_routes.py'), run(sys.executable, 'project.py'), snapshot('gridrouted')),
    'freeroute': lambda: (run(KIPY, 'route_freerouting.py', 'export'), run(sys.executable, 'route_freerouting.py', 'run'),
                          run(KIPY, 'route_freerouting.py', 'import'), run(sys.executable, 'project.py'),
                          snapshot('freerouted')),
    'tidy': lambda: (run(KIPY, 'tidy_routes.py'), run(sys.executable, 'project.py')),
    'stitch': lambda: (run(KIPY, 'gnd_stitch.py'), run(sys.executable, 'project.py')),
    'prune': lambda: (run(KIPY, 'prune_stubs.py'), run(sys.executable, 'project.py')),
    'silk': lambda: (run(KIPY, 'silk.py'), run(sys.executable, 'project.py')),
    'labels': lambda: (run(KIPY, 'mao_labels.py'), run(sys.executable, 'project.py')),
    'checksilk': lambda: run(KIPY, 'check_silk_text.py'),
    'stackup': lambda: run(KIPY, 'stackup.py'),
    'fab': lambda: run(KIPY, 'fab.py'),
}
SEQ = {
    'all': ['capture', 'footprints', 'place', 'power', 'planevias', 'drc', 'gridroute', 'drc',
            'prune', 'drc', 'prune', 'drc', 'stitch', 'drc', 'tidy', 'drc', 'prune', 'drc', 'prune', 'drc'],
    'route': ['planevias', 'drc', 'gridroute', 'drc', 'prune', 'drc', 'prune', 'drc', 'stitch', 'drc',
              'tidy', 'drc', 'prune', 'drc', 'prune', 'drc'],
    'silk': ['silk', 'labels', 'checksilk', 'drc'],
}

if __name__ == '__main__':
    for arg in sys.argv[1:]:
        for step in SEQ.get(arg, [arg]):
            if step.startswith('restore:'):
                restore(step.split(':', 1)[1])
            else:
                STEPS[step]()
