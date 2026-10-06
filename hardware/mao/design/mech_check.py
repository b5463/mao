"""Enclosure collision check of the placed board (KiCad python, read-only). ODD JOBS 74, 75, 135, 194, 198.

Every part's height is read from the 3D model its footprint shows (STEP: the highest CARTESIAN_POINT; VRML: the
highest vertex or box, 0.1 inch units), with the footprint's model scale, offset and rotation. Each part's body
(its Fab outline) is tested against the height envelopes and its courtyard against the keep-outs in mechanical.py:

  F, under the panel        r < DISPLAY_OUTLINE_R + 0.5, plus the glass ledge under the tail at 9 o'clock:
                            height <= ZONE_A_MAX_H (SW301 excepted: its stem is what the face presses)
  F, under the window       up to the ring: height <= WINDOW_Z - PRESS_TRAVEL - 0.3 (pressed, worst tolerance)
  F, under the ring lip     r >= RING_ID / 2: height <= RING_LIP_MAX
  B, everywhere             height <= ZONE_B_MAX_H (the cell's insulator sits on the tallest part)
  B, under the speaker      no part but its contact pads LS501
  B, the tail corridor      no part between the slot and J301 (the folded FPC lies there)
  F, under the tail         no part beside the slot (TAIL_F_CLEAR) nor under the carrier well where the loop turns
  fasteners                 no part in the insert boss (F) or screw head (B) keep-outs, nor at the peg
  wall                      every body inside the board circle (r <= PCB_R: 1 mm to the wall at r 30), except the
                            parts that reach into a wall opening by design (USB-C, the IR LEDs); the speaker's
                            outline (mechanical.py, it has no footprint body) >= 0.8 mm from the wall
The face switch (its stem touches the carrier boss by design) and the parts with no body (pads, holes,
fiducials, electrodes, test pads) are listed, not tested. Writes outputs/MECH-CHECK.json; exits 1 on a finding.
"""
import json
import math
import os
import re
import sys

import pcbnew as pcb

import mechanical as m
from board import ROOT, TARGET, courtyard_boxes

K3D = os.path.join(os.environ.get('KICAD_SHARE', os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/SharedSupport')),
                   '3dmodels')
PRJ = str(ROOT)
RING_LIP_MAX = 0.95          # mao-mechanical.md section 1: the ring's lower lip is 1.9 mm above F.Cu at r > 24 mm
WINDOW_MAX = m.WINDOW_Z - m.PRESS_TRAVEL - 0.3
DESIGNED_CONTACT = {'SW301': 'face switch: its stem meets the carrier boss'}
NO_BODY = ('H', 'FID', 'TP', 'E', 'LS', 'J201', 'J501')   # pads, holes, marks, electrodes, Tag-Connect, LRA lead pads
WALL_R = m.PUCK_OD / 2 - m.WALL
WALL_OPENING = {'J101': 'USB-C: mating face in the wall opening', 'D501': 'IR LED: fires through the wall',
                'D502': 'IR LED: fires through the wall'}

_cache = {}


def model_points(path):
    """Vertices (mm) of a model file, cached."""
    if path in _cache:
        return _cache[path]
    pts = []
    try:
        txt = open(path, errors='ignore').read()
    except OSError:
        _cache[path] = None
        return None
    if path.lower().endswith(('.step', '.stp')):
        for x, y, z in re.findall(r"CARTESIAN_POINT\s*\(\s*'[^']*'\s*,\s*\(\s*([-\d.Ee+]+)\s*,\s*([-\d.Ee+]+)\s*,"
                                  r"\s*([-\d.Ee+]+)\s*\)", txt):
            pts.append((float(x), float(y), float(z)))
    else:                                            # VRML, 1 unit = 0.1 inch
        u = 2.54
        for block in re.findall(r'point\s*\[([^\]]*)\]', txt):
            v = [float(t) for t in re.findall(r'[-\d.Ee+]+', block)]
            pts += [(v[i] * u, v[i + 1] * u, v[i + 2] * u) for i in range(0, len(v) - 2, 3)]
        for tr, sz in re.findall(r'translation\s+([-\d.Ee+\s]+?)\s+children.*?Box\s*\{\s*size\s+([-\d.Ee+\s]+?)\s*\}', txt):
            t = [float(a) for a in tr.split()]
            s = [float(a) for a in sz.split()]
            for dx in (-0.5, 0.5):
                for dy in (-0.5, 0.5):
                    for dz in (-0.5, 0.5):
                        pts.append(((t[0] + dx * s[0]) * u, (t[1] + dy * s[1]) * u, (t[2] + dz * s[2]) * u))
    _cache[path] = pts
    return pts


def rot(p, rx, ry, rz):
    x, y, z = p
    for ax, a in (('x', rx), ('y', ry), ('z', rz)):
        if not a:
            continue
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        if ax == 'x':
            y, z = y * c - z * s, y * s + z * c
        elif ax == 'y':
            x, z = x * c + z * s, -x * s + z * c
        else:
            x, y = x * c - y * s, x * s + y * c
    return x, y, z


def height(fp):
    """Highest point of the footprint's shown models above its board face (mm), or None without a model."""
    best = None
    for md in fp.Models():
        if not md.m_Show:
            continue
        path = md.m_Filename.replace('${KICAD10_3DMODEL_DIR}', K3D).replace('${KIPRJMOD}', PRJ)
        pts = model_points(path)
        if not pts and not path.lower().endswith('.wrl'):
            pts = model_points(os.path.splitext(path)[0] + '.wrl')
        if not pts:
            continue
        s, o, r = md.m_Scale, md.m_Offset, md.m_Rotation
        top = max(rot((x * s.x, y * s.y, z * s.z), r.x, r.y, r.z)[2] for x, y, z in pts) + o.z
        best = top if best is None else max(best, top)
    return best


def body_box(fp):
    """The part's body as one board-mm box: its Fab outline (the package drawing), else its courtyard."""
    fab = (pcb.B_Fab, pcb.F_Fab)
    rs = [g.GetBoundingBox() for g in fp.GraphicalItems() if g.GetLayer() in fab and not isinstance(g, pcb.PCB_TEXT)]
    if not rs:
        return courtyard_boxes(fp)
    mm = lambda v: pcb.ToMM(v) - 50
    return [(min(mm(r.GetLeft()) for r in rs), min(mm(r.GetTop()) for r in rs),
             max(mm(r.GetRight()) for r in rs), max(mm(r.GetBottom()) for r in rs))]


def corners(boxes):
    return [(x, y) for b in boxes for x in (b[0], b[2]) for y in (b[1], b[3])]


def in_box(b, q):
    return b[0] < q[2] and q[0] < b[2] and b[1] < q[3] and q[1] < b[3]


def near_circle(boxes, c, r, round_part=False):
    if round_part:                                   # a round part (fiducial): its courtyard is a circle
        b = boxes[0]
        return math.hypot((b[0] + b[2]) / 2 - c[0], (b[1] + b[3]) / 2 - c[1]) - (b[2] - b[0]) / 2 < r
    for b in boxes:
        nx, ny = min(max(c[0], b[0]), b[2]), min(max(c[1], b[1]), b[3])
        if math.hypot(nx - c[0], ny - c[1]) < r:
            return True
    return False


def main():
    b = pcb.LoadBoard(sys.argv[1] if len(sys.argv) > 1 else str(TARGET))     # optional: a snapshot board
    panel_r = m.DISPLAY_OUTLINE_R + 0.5
    ledge = (-(m.DISPLAY_OUTLINE_R + 2.14 + 0.5), -13.05 / 2 - 0.5, -m.DISPLAY_OUTLINE_R + 1.0, 13.05 / 2 + 0.5)
    sx, sy = m.SPEAKER_CENTRE
    speaker = (sx - m.SPEAKER_SIZE[0] / 2, sy - m.SPEAKER_SIZE[1] / 2, sx + m.SPEAKER_SIZE[0] / 2, sy + m.SPEAKER_SIZE[1] / 2)
    mounts = [(m.polar(m.MOUNT_R, a), m.BOSS_KEEPOUT_D_F / 2, m.HEAD_KEEPOUT_D_B / 2) for a in m.SCREW_ANGLES]
    mounts.append((m.polar(m.MOUNT_R, m.PEG_ANGLE), m.PEG_KEEPOUT_D / 2, m.PEG_KEEPOUT_D_B / 2))
    found, rows, unlisted = [], [], []
    for fp in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
        ref = fp.GetReference()
        side = 'B' if fp.IsFlipped() else 'F'
        boxes = courtyard_boxes(fp)
        body = body_box(fp)                          # heights are tested where the body is, keep-outs by courtyard
        h = height(fp)
        rmin = min(math.hypot(x, y) for x, y in corners(body))
        rmax = max(math.hypot(x, y) for x, y in corners(body))
        rows.append({'ref': ref, 'side': side, 'height_mm': None if h is None else round(h, 2),
                     'r_mm': [round(rmin, 2), round(rmax, 2)]})
        if rmax > m.PCB_R + 1e-3 and ref not in WALL_OPENING:
            found.append({'ref': ref, 'rule': 'wall clearance', 'side': side, 'r_max_mm': round(rmax, 2),
                          'wall_clearance_mm': round(WALL_R - rmax, 2), 'limit_r_mm': m.PCB_R})
        elif ref in WALL_OPENING:
            rows[-1]['note'] = WALL_OPENING[ref]
        for c, rf, rb in mounts:
            if not ref.startswith('H') and near_circle(boxes, c, rf if side == 'F' else rb, ref.startswith('FID')):
                found.append({'ref': ref, 'rule': 'fastener keep-out', 'side': side, 'at': [round(v, 2) for v in c]})
        if side == 'B':
            if any(in_box(q, speaker) for q in boxes) and not ref.startswith('LS'):
                found.append({'ref': ref, 'rule': 'under the speaker', 'side': side})
            if any(in_box(q, m.TAIL_CORRIDOR) for q in boxes) and ref != 'J301':
                found.append({'ref': ref, 'rule': 'in the tail corridor', 'side': side})
        elif any(in_box(q, m.TAIL_WELL) or in_box(q, m.TAIL_F_CLEAR) for q in boxes):
            found.append({'ref': ref, 'rule': 'under the tail (slot side or the loop\'s well)', 'side': side})
        if ref.startswith(NO_BODY) and ref not in DESIGNED_CONTACT:
            continue
        if ref in DESIGNED_CONTACT:
            rows[-1]['note'] = DESIGNED_CONTACT[ref]
            continue
        if h is None:
            unlisted.append(ref)
            found.append({'ref': ref, 'rule': 'no 3D model (height unknown)', 'side': side})
            continue
        if side == 'B':
            if h > m.ZONE_B_MAX_H + 1e-3:
                found.append({'ref': ref, 'rule': 'B zone height', 'height_mm': round(h, 2), 'limit_mm': m.ZONE_B_MAX_H})
            continue
        under_panel = rmin < panel_r or any(in_box(q, ledge) for q in body)
        limit, zone = (m.ZONE_A_MAX_H, 'under the panel') if under_panel else \
            (RING_LIP_MAX, 'under the ring lip') if rmax >= m.RING_ID / 2 else (WINDOW_MAX, 'under the window')
        rows[-1]['zone'] = zone
        rows[-1]['limit_mm'] = round(limit, 2)
        if h > limit + 1e-3:
            found.append({'ref': ref, 'rule': zone, 'height_mm': round(h, 2), 'limit_mm': round(limit, 2)})
    spk_wall = WALL_R - max(math.hypot(x, y) for x, y in corners([speaker]))
    if spk_wall < 0.8 - 1e-3:
        found.append({'ref': 'speaker', 'rule': 'wall clearance', 'wall_clearance_mm': round(spk_wall, 2), 'limit_mm': 0.8})
    out = {'board': TARGET.name, 'parts': len(rows), 'findings': found, 'speaker_wall_clearance_mm': round(spk_wall, 2),
           'max_body_r_mm': max((r_['r_mm'][1], r_['ref']) for r_ in rows if r_['ref'] not in WALL_OPENING),
           'limits_mm': {'wall: body r max': m.PCB_R, 'under the panel': m.ZONE_A_MAX_H, 'under the window (pressed)': round(WINDOW_MAX, 2),
                         'under the ring lip': RING_LIP_MAX, 'B side': m.ZONE_B_MAX_H},
           'parts_detail': rows}
    (ROOT / 'outputs' / 'MECH-CHECK.json').write_text(json.dumps(out, indent=1) + '\n')
    tall = sorted((r_ for r_ in rows if r_['height_mm'] is not None), key=lambda r_: -r_['height_mm'])[:8]
    print('mech check: %d parts, %d findings; tallest: %s' % (len(rows), len(found),
          ', '.join('%s %s %.2f' % (r_['ref'], r_['side'], r_['height_mm']) for r_ in tall)), flush=True)
    for f_ in found:
        print(' ', f_, flush=True)
    return 1 if found else 0


if __name__ == '__main__':
    rc = main()
    os._exit(rc)
