"""Build the showcase scene: a copy of the final board with the concept enclosure attached (KiCad 10 python).

    <KiCad python> scene.py       -> showcase/build/scene.kicad_pcb

The enclosure parts (showcase/models, enclosure.py) hang as 3D models on one board-only footprint "SHOW" at the
puck axis, so KiCad's raytracer renders board and enclosure together. The repository board is not touched.
"""
import os
import shutil
import sys
from pathlib import Path

import pcbnew as pcb

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / 'MAO_MAIN_A0.kicad_pcb'
OUT = HERE / 'build'
PARTS = ['base', 'cell', 'lra', 'speaker', 'carrier', 'display', 'window', 'bezel', 'ring']

OUT.mkdir(parents=True, exist_ok=True)
b = pcb.LoadBoard(str(SRC))
fp = pcb.FOOTPRINT(b)
fp.SetReference('SHOW')
fp.SetValue('concept enclosure')
fp.SetPosition(pcb.VECTOR2I(pcb.FromMM(50), pcb.FromMM(50)))
fp.Reference().SetVisible(False)
fp.Value().SetVisible(False)
fp.SetAttributes(pcb.FP_BOARD_ONLY | pcb.FP_EXCLUDE_FROM_BOM | pcb.FP_EXCLUDE_FROM_POS_FILES)
for name in PARTS:
    mdl = pcb.FP_3DMODEL()
    mdl.m_Filename = str(HERE / 'models' / (name + '.wrl'))
    fp.Add3DModel(mdl)
b.Add(fp)
pcb.SaveBoard(str(OUT / 'scene.kicad_pcb'), b)
for ext in ('.kicad_pro',):
    shutil.copy(SRC.with_suffix(ext), OUT / ('scene' + ext))
print('scene:', OUT / 'scene.kicad_pcb', flush=True)
os._exit(0)
