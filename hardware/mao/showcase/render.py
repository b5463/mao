"""Render helper for the showcase (plain Python): one KiCad raytraced frame per call, parallel batches.

A frame is a dict:
    parts   {name: (dx, dy, dz) offset in mm} for the enclosure parts to show (absent parts are hidden)
    opacity {name: 0..1} optional
    swap    {name: other model name} optional (e.g. display -> a blinking face)
    cam     kicad-cli camera args: rotate 'x,y,z', zoom, pan 'x,y,z', pivot 'x,y,z', perspective, side
    board   False hides the PCB by using the enclosure-only scene (not implemented: the board always renders)
"""
import os
import re
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCENE = HERE / 'build' / 'scene.kicad_pcb'
KC = os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli')
_TEXT = None
_BLOCK = re.compile(r'\(model "([^"]*?/models/([a-z_0-9+\-]+)\.wrl)"\s*\(offset\s*\(xyz [^)]*\)\s*\)\s*\(scale\s*\(xyz [^)]*\)\s*\)\s*'
                    r'\(rotate\s*\(xyz [^)]*\)\s*\)\s*\)', re.S)


def scene_text():
    global _TEXT
    if _TEXT is None:
        _TEXT = SCENE.read_text()
    return _TEXT


def variant(frame):
    parts = frame.get('parts', {})
    opac = frame.get('opacity', {})
    swap = frame.get('swap', {})
    rot = frame.get('rot', {})

    def sub(mo):
        path, name = mo.group(1), mo.group(2)
        if name not in parts:
            return ''
        dx, dy, dz = parts[name]
        p = path.replace('/models/%s.wrl' % name, '/models/%s.wrl' % swap.get(name, name))
        op = '(opacity %.3f)' % opac[name] if name in opac else ''
        return ('(model "%s" (offset (xyz %.4f %.4f %.4f)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 %.3f)) %s)'
                % (p, dx, -dy, dz, rot.get(name, 0.0), op))
    # project models (USB-C, FPC connector, mic) are ${KIPRJMOD}-relative, and the frame renders from a temp folder
    return _BLOCK.sub(sub, scene_text()).replace('${KIPRJMOD}/', str(HERE.parent) + '/')


def render(frame, out, w=1920, h=1080, quality='high', floor=False, timeout=7200):
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cam = frame.get('cam', {})
    with tempfile.TemporaryDirectory(dir=HERE / 'build') as td:
        pcbf = Path(td) / 'f.kicad_pcb'
        pcbf.write_text(variant(frame))
        (Path(td) / 'f.kicad_pro').write_text((HERE / 'build' / 'scene.kicad_pro').read_text())
        args = [KC, 'pcb', 'render', '--quality', quality, '--width', str(w), '--height', str(h),
                '--background', frame.get('background', 'transparent'), '--use-board-stackup-colors', '-o', str(out)]
        if cam.get('side'): args += ['--side', cam['side']]
        if cam.get('rotate'): args += ['--rotate', cam['rotate']]
        if cam.get('zoom'): args += ['--zoom', '%.4f' % cam['zoom']]
        if cam.get('pan'): args += ['--pan', cam['pan']]
        if cam.get('pivot'): args += ['--pivot', cam['pivot']]
        if cam.get('perspective', True): args += ['--perspective']
        for k in ('light_top', 'light_bottom', 'light_side', 'light_camera', 'light_side_elevation'):
            if k in cam: args += ['--' + k.replace('_', '-'), str(cam[k])]
        if floor or frame.get('floor'): args += ['--floor']
        args.append(str(pcbf))
        r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        if r.returncode != 0 or not out.exists():
            raise RuntimeError('render failed %s: %s' % (out, r.stderr[-400:]))
    return out


def batch(jobs, workers=3, **kw):
    """jobs: [(frame, out_path)]; skips frames already rendered."""
    todo = [(f, o) for f, o in jobs if not Path(o).exists()]
    with ThreadPoolExecutor(workers) as ex:
        list(ex.map(lambda j: render(j[0], j[1], **kw), todo))
    return len(todo)
