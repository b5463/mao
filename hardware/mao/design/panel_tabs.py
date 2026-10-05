"""Breakaway-tab spots on the rim (KiCad python, read-only). ODD JOBS 142, 144, 145.

A panelised order needs two tabs on the round edge. The spots are fixed datums (mechanical.TAB_ANGLES): build_pcb.py
keeps every copper layer TAB_CLEAR off the edge over TAB_ARC of rim there, plus TAB_CLEAR beyond each end. Over each
tab's arc this measures the room from the rim to the nearest copper of any layer (pads, tracks, vias, the touch arcs,
the filled pours and planes) and, separately, to the nearest part body (courtyard) and fastener ring. It writes
outputs/PANEL-TABS.json with the text fab.py puts in FAB-NOTES.txt and exits 1 if copper comes closer than TAB_CLEAR
or a part body closer than 1.0 mm (ODD JOBS 144: no part where the mouse bites flex the board).
"""
import json
import math
import os

import pcbnew as pcb

import mechanical as m
from board import ROOT, TARGET

BODY_CLEAR = 1.0


def main():
    b = pcb.LoadBoard(str(TARGET))
    pcb.ZONE_FILLER(b).Fill(b.Zones())
    at = lambda x, y: pcb.VECTOR2I(pcb.FromMM(x + 50), pcb.FromMM(y + 50))
    copper, bodies = [], []
    for z in b.Zones():
        if not z.GetIsRuleArea():
            copper += [z.GetFilledPolysList(l) for l in (pcb.F_Cu, pcb.In1_Cu, pcb.In2_Cu, pcb.B_Cu) if z.IsOnLayer(l)]
    for f in b.GetFootprints():
        cy = f.GetCourtyard(pcb.B_CrtYd if f.IsFlipped() else pcb.F_CrtYd)
        if cy.OutlineCount() and not f.GetReference().startswith('H'):
            bodies.append((f.GetReference(), cy))
        for p in f.Pads():
            for lay in (pcb.F_Cu, pcb.B_Cu):
                if p.IsOnLayer(lay):
                    copper.append(p.GetEffectivePolygon(lay))
    tracks = [t for t in b.GetTracks()]

    def dist(c, hit):                              # 0.05 mm steps to 4 mm
        for k in range(0, 81):
            if hit(c, pcb.FromMM(k * 0.05)):
                return k * 0.05
        return 4.0

    room = {}
    for a in m.TAB_ANGLES:
        half = math.degrees(m.TAB_ARC / 2 / m.PCB_R)
        cu, body, who = 99.0, 99.0, None
        for k in range(9):
            c = at(*m.polar(m.PCB_R, a - half + 2 * half * k / 8))
            cu = min(cu, dist(c, lambda c_, r_: any(s.Collide(c_, r_) for s in copper) or
                              any(t.GetEffectiveShape(pcb.F_Cu).Collide(c_, r_) for t in tracks)))
            for ref, cy in bodies:
                d = dist(c, cy.Collide)
                if d < body:
                    body, who = d, ref
            for sa in m.SCREW_ANGLES + (m.PEG_ANGLE,):
                x, y = m.polar(m.MOUNT_R, sa); p = m.polar(m.PCB_R, a - half + 2 * half * k / 8)
                d = math.hypot(p[0] - x, p[1] - y) - m.MOUNT_KEEPOUT_D / 2
                if d < body:
                    body, who = d, 'fastener'
        room[a] = {'copper_mm': round(cu, 2), 'body_mm': round(body, 2), 'nearest_body': who}
    clock = lambda deg: '%d o\'clock' % (round(deg / 30) % 12 or 12)
    text = ' and '.join('about %s (%d deg clockwise from the USB-C; no copper within %.2f mm of the edge and no part '
                        'within %.2f mm, over %.0f mm of rim)' % (clock(a), a, room[a]['copper_mm'], room[a]['body_mm'],
                                                                  m.TAB_ARC) for a in m.TAB_ANGLES)
    (ROOT / 'outputs' / 'PANEL-TABS.json').write_text(json.dumps({'tabs_deg': list(m.TAB_ANGLES), 'arc_mm': m.TAB_ARC,
                                                                  'room': {str(int(a)): room[a] for a in m.TAB_ANGLES},
                                                                  'text': text}, indent=1) + '\n')
    print('panel tabs:', text, flush=True)
    return 0 if all(v['copper_mm'] >= m.TAB_CLEAR - 0.05 and v['body_mm'] >= BODY_CLEAR for v in room.values()) else 1


if __name__ == '__main__':
    rc = main()
    os._exit(rc)
