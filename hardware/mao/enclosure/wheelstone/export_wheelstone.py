"""STL export of the MAO wheel-stone parts (binary STL, mm), plus a solid 'feel model'.

Meshing and the watertight check are the rock's (../rock/export_rock.py); each mesh is then decimated (quadric,
50-70 %, about 0.2 mm deviation) as far as the result stays watertight. The window is a laser-cut 1 mm PMMA
disc (D 45.8) and is not exported. The press finger is 1.5 mm stainless, laser cut with a 0.3 mm dimple under the
switch stem: its STL is for fit only, the part is made from mao-wheelstone-finger.dxf (outline and the two M2 holes).
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rock'))
from export_rock import mesh, write_stl  # noqa: E402

from wheelstone import FINGER_POLY, FINGER_ROOT, PRESS, WHEEL_STEP_R, Z_LIP1, WheelStone  # noqa: E402


def write_finger_dxf(path):
    """The finger's flat pattern (R12 DXF, mm, +y up as cut): outline, two M2 clearance holes, the dimple's centre."""
    out = ['0', 'SECTION', '2', 'ENTITIES']
    pts = FINGER_POLY + FINGER_POLY[:1]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        out += ['0', 'LINE', '8', 'CUT', '10', '%.3f' % x0, '20', '%.3f' % -y0, '11', '%.3f' % x1, '21', '%.3f' % -y1]
    for x, y in FINGER_ROOT:
        out += ['0', 'CIRCLE', '8', 'CUT', '10', '%.3f' % x, '20', '%.3f' % -y, '40', '1.100']
    out += ['0', 'POINT', '8', 'DIMPLE_0.3', '10', '%.3f' % PRESS[0], '20', '%.3f' % -PRESS[1]]
    out += ['0', 'ENDSEC', '0', 'EOF']
    open(path, 'w').write('\n'.join(out) + '\n')



def main(res=0.25, out='stl'):
    os.makedirs(out, exist_ok=True)
    ws = WheelStone(res)
    F = ws.fields()
    X, Y, Z = ws.grid()
    well = np.maximum(np.hypot(X, Y) - WHEEL_STEP_R, Z_LIP1 - Z)
    parts = {
        'mao-wheelstone-base': F['base'],
        'mao-wheelstone-frame': F['frame'],
        'mao-wheelstone-wheel': F['wheel'],
        'mao-wheelstone-bezel': F['bezel'],
        'mao-wheelstone-carrier': F['carrier'],
        'mao-wheelstone-finger': F['finger'],                   # fit model; made from the DXF
        # one solid piece to print and hold: the stone with the face well and the wheel's grip grooves
        'mao-wheelstone-feel-model': np.maximum(np.minimum(F['stone'], F['wheel']), -well),
    }
    write_finger_dxf(os.path.join(out, 'mao-wheelstone-finger.dxf'))
    import trimesh
    for name, fld in parts.items():
        v, f = mesh(fld, ws)
        m = trimesh.Trimesh(v, f, process=True)
        bodies = m.split(only_watertight=False)
        keep = [b for b in bodies if abs(b.volume) > 2.0]          # drop sub-voxel slivers (< 2 mm3)
        m = trimesh.util.concatenate(keep)
        if m.volume < 0:
            m.invert()
        try:                                                        # fewer triangles (fast-simplification), the
            for pct in (0.7, 0.6, 0.5):                             # strongest cut that keeps the mesh closed
                d = m.simplify_quadric_decimation(percent=pct)
                if d.is_watertight and abs(d.volume - m.volume) < 1e-3 * abs(m.volume):
                    m = d
                    break
        except ImportError:
            pass
        write_stl(os.path.join(out, name + '.stl'), np.asarray(m.vertices), np.asarray(m.faces), name)
        print('%s: %d bodies kept of %d, watertight=%s, volume %.1f cm3, %d triangles, bbox %s mm'
              % (name, len(keep), len(bodies), m.is_watertight, m.volume / 1000, len(m.faces),
                 np.round(np.ptp(np.asarray(m.vertices), 0), 1)))


if __name__ == '__main__':
    main(float(sys.argv[1]) if len(sys.argv) > 1 else 0.25)
