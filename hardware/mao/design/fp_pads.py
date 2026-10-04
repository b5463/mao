"""Print pad centres of footprints on the current board (KiCad python): fp_pads.py REF [REF ...].

Board millimetres (centre origin). Used while designing the hand placement and routes of the
power section: positions come from the real footprints, never from memory.
"""
import sys
import pcbnew as pcb
from board import TARGET

b = pcb.LoadBoard(str(TARGET))
mm = lambda v: pcb.ToMM(v) - 50
want = set(sys.argv[1:])
for f in b.GetFootprints():
    if f.GetReference() not in want:
        continue
    pos = f.GetPosition()
    cy = f.GetCourtyard(pcb.B_CrtYd if f.IsFlipped() else pcb.F_CrtYd).BBox()
    print('%s at (%.3f, %.3f) rot %g %s  courtyard x %.2f..%.2f y %.2f..%.2f' % (
        f.GetReference(), mm(pos.x), mm(pos.y), f.GetOrientationDegrees(), 'B' if f.IsFlipped() else 'F',
        mm(cy.GetLeft()), mm(cy.GetRight()), mm(cy.GetTop()), mm(cy.GetBottom())))
    for p in f.Pads():
        bb = p.GetBoundingBox()
        print('   %-4s %-16s (%.3f, %.3f)  %.2f x %.2f' % (p.GetNumber(), p.GetNetname(), mm(p.GetPosition().x),
              mm(p.GetPosition().y), pcb.ToMM(bb.GetWidth()), pcb.ToMM(bb.GetHeight())))
sys.stdout.flush()
import os; os._exit(0)
