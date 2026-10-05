"""Geometry for the assembly drawing (KiCad 10 python, read-only): python asm_dump.py

Writes .cache/mao-routing/asm-dump.json for assembly_drawing.py: the board outline (Edge.Cuts, arcs and circles
sampled), and per footprint its reference, value, side, position, DNP flag, Fab drawing (segments, circles and
polygons, arcs sampled), courtyard outline and pad outlines (on its own side; plated holes as drawn on F).
"""
import json
import math
import os

import pcbnew as pcb

from board import TARGET
from netrules import CACHE

b = pcb.LoadBoard(str(TARGET))
mm = lambda v: round(pcb.ToMM(v) - 50, 4)
P = lambda v: (mm(v.x), mm(v.y))


def shape_lines(g):
    """A drawing item as polylines in board mm."""
    s = g.GetShape()
    if s == pcb.SHAPE_T_SEGMENT:
        return [[P(g.GetStart()), P(g.GetEnd())]]
    if s == pcb.SHAPE_T_RECT:
        a, c = g.GetStart(), g.GetEnd()
        return [[(mm(a.x), mm(a.y)), (mm(c.x), mm(a.y)), (mm(c.x), mm(c.y)), (mm(a.x), mm(c.y)), (mm(a.x), mm(a.y))]]
    if s == pcb.SHAPE_T_CIRCLE:
        c, r = g.GetCenter(), pcb.ToMM(g.GetRadius())
        return [[(mm(c.x) + r * math.cos(2 * math.pi * k / 48), mm(c.y) + r * math.sin(2 * math.pi * k / 48)) for k in range(49)]]
    if s == pcb.SHAPE_T_ARC:
        c, r = g.GetCenter(), pcb.ToMM(g.GetRadius())
        a0 = g.GetArcAngleStart().AsRadians() if hasattr(g, 'GetArcAngleStart') else math.atan2(g.GetStart().y - c.y, g.GetStart().x - c.x)
        da = g.GetArcAngle().AsRadians()
        n = max(4, int(abs(da) / 0.1))
        return [[(mm(c.x) + r * math.cos(a0 + da * k / n), mm(c.y) + r * math.sin(a0 + da * k / n)) for k in range(n + 1)]]
    if s == pcb.SHAPE_T_POLY:
        ps = g.GetPolyShape()
        return [[P(ps.Outline(k).CPoint(i)) for i in range(ps.Outline(k).PointCount())] + [P(ps.Outline(k).CPoint(0))]
                for k in range(ps.OutlineCount())]
    return []


out = {'edge': [], 'parts': []}
for d in b.GetDrawings():
    if d.GetLayer() == pcb.Edge_Cuts and isinstance(d, pcb.PCB_SHAPE):
        out['edge'] += shape_lines(d)
for f in b.GetFootprints():
    side = 'B' if f.IsFlipped() else 'F'
    fab, cu, cy = (pcb.B_Fab, pcb.B_Cu, pcb.B_CrtYd) if side == 'B' else (pcb.F_Fab, pcb.F_Cu, pcb.F_CrtYd)
    rec = {'ref': f.GetReference(), 'value': f.GetValue(), 'side': side, 'x': mm(f.GetPosition().x),
           'y': mm(f.GetPosition().y), 'rot': round(f.GetOrientationDegrees(), 1), 'dnp': bool(f.IsDNP()),
           'fab': [], 'court': [], 'pads': []}
    for g in f.GraphicalItems():
        if isinstance(g, pcb.PCB_SHAPE) and g.GetLayer() == fab:
            rec['fab'] += shape_lines(g)
    c = f.GetCourtyard(cy)
    for k in range(c.OutlineCount()):
        o = c.Outline(k)
        rec['court'].append([P(o.CPoint(i)) for i in range(o.PointCount())])
    for p in f.Pads():
        lay = cu if p.IsOnLayer(cu) else pcb.F_Cu
        ps = p.GetEffectivePolygon(lay)
        for k in range(ps.OutlineCount()):
            o = ps.Outline(k)
            rec['pads'].append([P(o.CPoint(i)) for i in range(o.PointCount())])
    out['parts'].append(rec)
(CACHE / 'asm-dump.json').write_text(json.dumps(out))
print('asm dump: %d parts, %d edge lines' % (len(out['parts']), len(out['edge'])), flush=True)
os._exit(0)
