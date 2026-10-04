"""KiCad-side board helpers for the MAO design scripts (run with KiCad 10's Python).

Replaces the KINO `rework.TARGET` / `routing.pt|CACHE|restore_through_hole_mask` imports of the
ported scripts. Board millimetres + ORIGIN = KiCad coordinates.
"""
import os

import pcbnew as pcb

from netrules import CACHE, NAME, ORIGIN, OUTPUTS, ROOT, DRC_JSON  # noqa: F401  (re-exported)

TARGET = ROOT / (NAME + '.kicad_pcb')
_KS = os.environ.get('KICAD_SHARE', os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/SharedSupport'))
STOCK_FP = os.path.join(_KS, 'footprints')
LOCAL_FP = ROOT / 'lib' / 'MAO.pretty'


def pt(x, y):
    return pcb.VECTOR2I(round(x * 1e6), round(y * 1e6))


def at(x, y):
    """Board millimetres -> KiCad position."""
    return pt(ORIGIN + x, ORIGIN + y)


def mm(v):
    return pcb.ToMM(v) - ORIGIN


def restore_through_hole_mask(board):
    """KiCad 10 can drop wildcard mask layers of through-hole pads when a board is rebuilt or a
    Specctra session is imported; restore them from the library footprint (from KINO routing.py)."""
    originals = {}
    for f in board.GetFootprints():
        if not any(p.GetAttribute() in (pcb.PAD_ATTRIB_PTH, pcb.PAD_ATTRIB_NPTH) for p in f.Pads()):
            continue
        lib = str(f.GetFPID().GetLibNickname())
        name = str(f.GetFPID().GetLibItemName())
        folder = str(LOCAL_FP) if lib == 'MAO' else os.path.join(STOCK_FP, lib + '.pretty')
        if (lib, name) not in originals:
            originals[(lib, name)] = pcb.FootprintLoad(folder, name)
        original = originals[(lib, name)]
        for p in f.Pads():
            if p.GetAttribute() not in (pcb.PAD_ATTRIB_PTH, pcb.PAD_ATTRIB_NPTH):
                continue
            matches = [q for q in original.Pads() if q.GetNumber() == p.GetNumber()
                       and q.GetAttribute() == p.GetAttribute() and q.GetDrillSize() == p.GetDrillSize()]
            assert matches, (f.GetReference(), p.GetNumber())
            q = matches[0]
            layers = p.GetLayerSet()
            for mask in (pcb.F_Mask, pcb.B_Mask):
                source = (pcb.B_Mask if mask == pcb.F_Mask else pcb.F_Mask) if f.IsFlipped() else mask
                if q.IsOnLayer(source):
                    layers.AddLayer(mask)
                else:
                    layers.RemoveLayer(mask)
            p.SetLayerSet(layers)


def courtyard_boxes(fp, step=0.5):
    """A part's body as board-mm boxes (x0, y0, x1, y1). Rectangular courtyards give one box; others
    (the module's courtyard includes its antenna keep-out, a T shape) are cut into `step` mm strips, so
    silkscreen checks see the real outline instead of its bounding box. Falls back to the footprint box."""
    c = fp.GetCourtyard(pcb.B_CrtYd if fp.IsFlipped() else pcb.F_CrtYd)
    if not c.OutlineCount():
        r = fp.GetBoundingBox(False, False)
        return [(mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom()))]
    r = c.BBox()
    x0, y0, x1, y1 = mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom())
    if c.Area() >= 0.95 * (x1 - x0) * (y1 - y0) * 1e12:
        return [(x0, y0, x1, y1)]
    out = []
    y = y0
    while y < y1 - 1e-6:
        yb = min(y + step, y1)
        ym = (y + yb) / 2
        xs = [x0 + (x1 - x0) * k / 200 for k in range(201)]
        inside = [x for x in xs if c.Contains(pt(ORIGIN + x, ORIGIN + ym))]
        if inside:
            out.append((min(inside), y, max(inside), yb))
        y = yb
    return out or [(x0, y0, x1, y1)]
