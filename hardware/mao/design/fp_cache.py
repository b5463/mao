"""Cache every footprint's pads and courtyard for plain-Python placement work (KiCad 10 python).

Writes fp-geometry.json next to this file: per footprint, the pad centres, sizes and numbers and the
courtyard strips as KiCad places them on F.Cu and on B.Cu at rotation 0 (the same Flip/SetOrientation
calls build_pcb.py makes). placement.py turns these into board positions of any pad (pin-relative
placement of support parts) and the placement review (place_view.py) tests courtyards for overlap.
Rotation convention, verified here on every footprint: offset (x, y) at orientation a degrees becomes
(x cos a + y sin a, -x sin a + y cos a) (KiCad: counter-clockwise on screen, y down).

    <KiCad python> hardware/mao/design/fp_cache.py
"""
import json, math, os, sys
import pcbnew as pcb
from build_pcb import netlist, load_fp
from board import courtyard_boxes

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fp-geometry.json')
mm = pcb.ToMM


def grab(fp):
    o = fp.GetPosition()
    kind = {pcb.PAD_ATTRIB_SMD: 'SMD', pcb.PAD_ATTRIB_PTH: 'PTH', pcb.PAD_ATTRIB_NPTH: 'NPTH'}
    pads = [{'n': p.GetNumber(), 'x': round(mm(p.GetPosition().x - o.x), 4), 'y': round(mm(p.GetPosition().y - o.y), 4),
             'w': round(mm(p.GetBoundingBox().GetWidth()), 3), 'h': round(mm(p.GetBoundingBox().GetHeight()), 3),
             'kind': kind.get(p.GetAttribute(), 'SMD'), 'drill': round(mm(p.GetDrillSize().x), 3)}
            for p in fp.Pads()]
    ox, oy = mm(o.x) - 50, mm(o.y) - 50
    crt = [[round(b[0] - ox, 3), round(b[1] - oy, 3), round(b[2] - ox, 3), round(b[3] - oy, 3)] for b in courtyard_boxes(fp)]
    return pads, crt


def rot(x, y, a):
    c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
    return x * c + y * s, -x * s + y * c


def main():
    out = {}
    for ref, c in sorted(netlist().items()):
        fpid = c['footprint']
        if fpid in out:
            continue
        rec = {}
        for side in ('F', 'B'):
            board = pcb.BOARD()               # one board per placement: no Remove() (it breaks SWIG lists)
            fp = load_fp(fpid)
            board.Add(fp)
            if side == 'B':
                fp.Flip(fp.GetPosition(), pcb.FLIP_DIRECTION_LEFT_RIGHT)
            fp.SetPosition(pcb.VECTOR2I(pcb.FromMM(50), pcb.FromMM(50)))
            fp.SetOrientationDegrees(0)
            pads, crt = grab(fp)
            fp.SetOrientationDegrees(90)
            p90, _ = grab(fp)
            for a, b in zip(pads, p90):          # the convention above, checked on every pad
                ex, ey = rot(a['x'], a['y'], 90)
                assert abs(ex - b['x']) < 1e-3 and abs(ey - b['y']) < 1e-3, (fpid, side, a, b)
            fp.SetOrientationDegrees(0)
            rec[side] = {'pads': pads, 'crt': crt}
        out[fpid] = rec
    with open(OUT, 'w') as f:
        json.dump(out, f, indent=0, sort_keys=True)
    print('fp-geometry: %d footprints' % len(out), flush=True)


if __name__ == '__main__':
    main()
    sys.stdout.flush()
    os._exit(0)
