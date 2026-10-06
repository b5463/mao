"""STL export of the MAO rock parts (binary STL, mm), plus a solid 'feel model'.

Each part is meshed by marching cubes on its field (zero level), padded so every
mesh is closed, and checked for watertightness with trimesh.
"""
import os
import struct
import sys

import numpy as np
from skimage import measure

from rock import Rock


def write_stl(path, verts, faces, name):
    tri = verts[faces]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    with open(path, 'wb') as f:
        f.write(name.encode()[:80].ljust(80, b' '))
        f.write(struct.pack('<I', len(faces)))
        rec = np.zeros(len(faces), dtype=[('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')])
        rec['n'] = n
        rec['v'] = tri
        f.write(rec.tobytes())


def mesh(field, rk):
    pad = np.pad(field, 2, constant_values=10.0)
    v, f, _, _ = measure.marching_cubes(pad, level=0.0, spacing=(rk.res,) * 3)
    v -= 2 * rk.res
    # (z, y, x) -> (x, y, z), and flip y so +y is 12 o'clock in the STL (slicers expect a right-handed
    # frame with the part's top up; the board frame has +y towards 6 o'clock)
    xyz = np.stack([v[:, 2] + rk.xs[0], -(v[:, 1] + rk.ys[0]), v[:, 0] + rk.zs[0]], 1)
    return xyz, f


def main(res=0.3, out='stl'):
    os.makedirs(out, exist_ok=True)
    rk = Rock(res)
    F = rk.fields()
    parts = {
        'mao-rock-top-shell': F['top_shell'],
        'mao-rock-base': F['base'],
        'mao-rock-ring': F['ring'],
        # one solid piece to print and hold: stone + ring + window, no inside
        'mao-rock-feel-model': np.minimum(np.minimum(F['stone'], F['ring']), F['window']),
    }
    try:
        import trimesh
    except ImportError:
        trimesh = None
    for name, fld in parts.items():
        v, f = mesh(fld, rk)
        msg = name
        if trimesh:
            m = trimesh.Trimesh(v, f, process=True)
            bodies = m.split(only_watertight=False)
            keep = [b for b in bodies if abs(b.volume) > 2.0]          # drop sub-voxel slivers (< 2 mm3)
            m = trimesh.util.concatenate(keep)
            if m.volume < 0:
                m.invert()
            v, f = m.vertices, m.faces
            msg += ': %d bodies kept of %d, watertight=%s, volume %.1f cm3' % (len(keep), len(bodies), m.is_watertight, m.volume / 1000)
        write_stl(os.path.join(out, name + '.stl'), np.asarray(v), np.asarray(f), name)
        print(msg + ', %d triangles, bbox %s mm' % (len(f), np.round(np.ptp(np.asarray(v), 0), 1)))


if __name__ == '__main__':
    main(float(sys.argv[1]) if len(sys.argv) > 1 else 0.3)
