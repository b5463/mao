"""Plane continuity of the 4-layer stack (KiCad python, read-only). Brief: L2 uninterrupted, L3 power coherent.

Fills every zone, then reports for each plane zone how many separate copper pieces it fills on its layer,
each piece's area and how many vias of its own net land in it:
  In1 GND PLANE   must be one piece (L2: no signals, no splits; only the touch cuts and the antenna keep-out
                  shape its outline)
  In2 3V3 / VSYS / VBUS   one piece each (L3 slow lines may cross a region but never cut a piece off)
Writes outputs/PLANES.json and exits 1 when any plane is split. Run after routing and stitching.
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
(OUTPUTS / 'PLANES.json').write_text(json.dumps(report, indent=1))
print('planes:', 'SPLIT' if split else 'all continuous', flush=True)
os._exit(1 if split else 0)
