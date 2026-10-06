"""STL export of the MAO wheel-stone parts (binary STL, mm), plus a solid 'feel model'.

Meshing and the watertight check are the rock's (../rock/export_rock.py); each mesh is then decimated (quadric,
50-70 %, about 0.2 mm deviation) as far as the result stays watertight. The window is a laser-cut 1 mm PMMA
disc (D 45.8) and is not exported.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'rock'))
from export_rock import mesh, write_stl  # noqa: E402

from wheelstone import WHEEL_STEP_R, Z_LIP1, WheelStone  # noqa: E402


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
        # one solid piece to print and hold: the stone with the face well and the wheel's grip grooves
        'mao-wheelstone-feel-model': np.maximum(np.minimum(F['stone'], F['wheel']), -well),
    }
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
