"""Plane continuity of the 4-layer stack (KiCad python, read-only). Brief: L2 uninterrupted, L3 power coherent.

Fills every zone, then reports for each plane zone how many separate copper pieces it fills on its layer,
each piece's area and how many vias of its own net land in it:
  In1 GND PLANE   must be one piece (L2: no signals, no splits; only the touch cuts and the antenna keep-out
                  shape its outline)
  In2 3V3 / VSYS / VBUS   one piece each (L3 slow lines may cross a region but never cut a piece off)
Then it refills the inner planes with a NECK mm minimum width: a plane that falls into more pieces has a neck
narrower than that somewhere (a 0.27 mm VSYS neck at a tab keep-out was found by hand in audit 3). That fails the
check for the GND plane and for the rail regions of power_regions.py (VSYS, VBUS: one band, one path to each load).
The +3V3 default fill is a mesh round the L3 slow lines, its paths in parallel: its pieces at NECK are reported,
and its resistance is what matters (about 22 mOhm buck-boost to module, electrical audit 3).
Writes outputs/PLANES.json and exits 1 when any plane is split or necked. Run after routing and stitching.
"""
import json
import os
import sys

import pcbnew as pcb

from board import TARGET
from netrules import OUTPUTS

b = pcb.LoadBoard(str(TARGET))
pcb.ZONE_FILLER(b).Fill(b.Zones())
mm = lambda v: round(pcb.ToMM(v) - 50, 2)
vias = [t for t in b.GetTracks() if isinstance(t, pcb.PCB_VIA)]
report, split = [], False
for z in b.Zones():
    if z.GetIsRuleArea():
        continue
    for layer in (pcb.In1_Cu, pcb.In2_Cu):
        if not z.IsOnLayer(layer):
            continue
        polys = z.GetFilledPolysList(layer)
        pieces = []
        for i in range(polys.OutlineCount()):
            o = polys.Outline(i)
            bb = o.BBox()
            n = sum(1 for v in vias if v.GetNetCode() == z.GetNetCode() and polys.Contains(v.GetPosition(), i))
            pieces.append({'area_mm2': round(o.Area() / 1e12, 1), 'vias': n,
                           'bbox': [mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom())]})
        report.append({'zone': z.GetZoneName(), 'net': z.GetNetname(), 'layer': b.GetLayerName(layer),
                       'pieces': pieces})
        if len(pieces) != 1:
            split = True
        print('%-10s %-6s %-7s %d piece(s): %s' % (z.GetZoneName(), z.GetNetname(), b.GetLayerName(layer), len(pieces),
              ', '.join('%.0f mm2 / %d vias' % (p['area_mm2'], p['vias']) for p in pieces)), flush=True)
NECK = 0.5
inner = [z for z in b.Zones() if not z.GetIsRuleArea() and (z.IsOnLayer(pcb.In1_Cu) or z.IsOnLayer(pcb.In2_Cu))]
count = lambda z: sum(z.GetFilledPolysList(l).OutlineCount() for l in (pcb.In1_Cu, pcb.In2_Cu) if z.IsOnLayer(l))
before = {z.GetZoneName(): count(z) for z in inner}
for z in inner:
    z.SetMinThickness(pcb.FromMM(NECK))
pcb.ZONE_FILLER(b).Fill(inner)
import power_regions
STRICT = {'GND'} | {n for n, _, _, _ in power_regions.zones()}
necked_all = [z for z in inner if count(z) > before[z.GetZoneName()]]
necked = [z.GetZoneName() for z in necked_all if z.GetNetname() in STRICT]
for z in necked_all:
    if z.GetNetname() not in STRICT:
        print('plane necks: %s (mesh) falls into %d pieces at %.1f mm minimum width: parallel paths, reported only'
              % (z.GetZoneName(), count(z), NECK), flush=True)
for r in report:
    r['neck_%.1f_mm' % NECK] = ('NECKED' if r['zone'] in necked else
                                'mesh, %d pieces' % count(next(z for z in inner if z.GetZoneName() == r['zone']))
                                if any(z.GetZoneName() == r['zone'] for z in necked_all) else 'ok')
print('plane necks (GND and rail regions refilled at %.1f mm minimum width): %s' % (NECK, ('NECKED ' + ', '.join(necked)) if necked else 'none'), flush=True)
(OUTPUTS / 'PLANES.json').write_text(json.dumps(report, indent=1))
print('planes:', 'SPLIT' if split else ('NECKED' if necked else 'all continuous'), flush=True)
os._exit(1 if split or necked else 0)
