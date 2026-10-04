"""Datasheet-height 3D bodies for library footprints whose KiCad model is not installed (KiCad 10 python).

The 3D review and the enclosure check (ODD JOBS 74, 194, 198) need every part at its real height. KiCad's
footprints name STEP models that this KiCad 10 install does not ship (QFNs, the USB-C receptacle, the
microphone, ToF, IR receiver, Hall latches, touch ESD). For those, a box body is written to
lib/MAO.3dshapes from the package drawing below and the board footprint is pointed at it. A footprint whose
library model exists is left alone. Idempotent; geometry and connectivity are untouched.

    <KiCad python> hardware/mao/design/models3d.py
"""
import os

import pcbnew as pcb

from board import TARGET
from footprints import wrl_box_model, LOCAL_FP

K3D = os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels')
SHAPES = os.path.join(os.path.dirname(LOCAL_FP), 'MAO.3dshapes')
BLACK, LID, SHELL, DARK = (0.10, 0.10, 0.10), (0.78, 0.78, 0.76), (0.82, 0.82, 0.82), (0.05, 0.05, 0.05)

# footprint -> (model name, boxes (x0, y0, z0, x1, y1, z1, rgb) in footprint mm); heights from the drawings
BODIES = {
    # TI DPY0002A (touch ESD): 1.0 x 0.6, 0.4 max
    'Texas_DPY0002A_0.6x1mm_P0.65mm': ('TI_DPY0002A_X1SON-2', [(-0.5, -0.3, 0, 0.5, 0.3, 0.4, BLACK)]),
    # TI DMR0004A (DRV5012): 1.1 x 1.4, 0.4 max
    'Texas_X2SON-4-1EP_1.1x1.4mm_P0.5mm_EP0.8x0.6mm': ('TI_DMR0004A_X2SON-4', [(-0.55, -0.7, 0, 0.55, 0.7, 0.4, BLACK)]),
    # VL53L4CD: 4.4 x 2.4 x 1.0, emitter and receiver windows
    'ST_VL53L0X': ('ST_VL53L4CD', [(-2.2, -1.2, 0, 2.2, 1.2, 1.0, BLACK),
                                   (-1.6, -0.35, 1.0, -0.9, 0.35, 1.02, DARK), (0.9, -0.35, 1.0, 1.6, 0.35, 1.02, DARK)]),
    # Everlight IRM-H6XXT/TR2 rev 3 p.6: body 5.0 x 4.0 x 2.5, dome R1.5, 4.0 overall
    'Everlight_IRM-H6xxT': ('Everlight_IRM-H6xxT', [(-2.0, -2.5, 0, 2.0, 2.5, 2.5, BLACK),
                                                    (-1.3, -1.3, 2.5, 1.3, 1.3, 4.0, DARK)]),
    # MAX17048 T822 TDFN: 2 x 2, 0.8 max
    'TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm': ('TDFN-8_2x2_0.8', [(-1.0, -1.0, 0, 1.0, 1.0, 0.8, BLACK)]),
    # TCA6408A / BQ24073 RGT VQFN: 3 x 3, 1.0 max
    'VQFN-16-1EP_3x3mm_P0.5mm_EP1.45x1.45mm_ThermalVias': ('VQFN-16_3x3_1.0', [(-1.5, -1.5, 0, 1.5, 1.5, 1.0, BLACK)]),
    'VQFN-16-1EP_3x3mm_P0.5mm_EP1.68x1.68mm_ThermalVias': ('VQFN-16_3x3_1.0', [(-1.5, -1.5, 0, 1.5, 1.5, 1.0, BLACK)]),
    # MAX98357A T1633 TQFN: 3 x 3, 0.8 max
    'TQFN-16-1EP_3x3mm_P0.5mm_EP1.23x1.23mm_ThermalVias': ('TQFN-16_3x3_0.8', [(-1.5, -1.5, 0, 1.5, 1.5, 0.8, BLACK)]),
    # SPH0641LU4H-1: 3.50 x 2.65 x 0.98, metal lid
    'Knowles_LGA-5_3.5x2.65mm': ('Knowles_SPH0641', [(-1.325, -1.75, 0, 1.325, 1.75, 0.98, LID)]),
    # HRO TYPE-C-31-M-12: shell 8.94 x 7.3, about 3.2 high, mouth at +y
    'USB_C_Receptacle_HRO_TYPE-C-31-M-12': ('HRO_TYPE-C-31-M-12', [(-4.47, -3.65, 0, 4.47, 3.65, 3.2, SHELL),
                                                                   (-4.1, 3.6, 0.32, 4.1, 3.7, 2.88, DARK)]),
}


def missing(fp):
    for m in fp.Models():
        p = m.m_Filename.replace('${KICAD10_3DMODEL_DIR}', K3D)
        if '${' in p or os.path.exists(p) or os.path.exists(os.path.splitext(p)[0] + '.wrl'):
            return False
    return True


def main():
    b = pcb.LoadBoard(str(TARGET))
    written, set_on = set(), []
    for fp in b.GetFootprints():
        name = str(fp.GetFPID().GetLibItemName())
        if name not in BODIES or not missing(fp):
            continue
        model, boxes = BODIES[name]
        if model not in written:
            wrl_box_model(os.path.join(SHAPES, model + '.wrl'), boxes)
            written.add(model)
        fp.Models().clear()
        m = pcb.FP_3DMODEL()
        m.m_Filename = '${KIPRJMOD}/lib/MAO.3dshapes/%s.wrl' % model
        fp.Models().push_back(m)
        set_on.append(fp.GetReference())
    if set_on:
        pcb.SaveBoard(str(TARGET), b)
    print('3D bodies: %d footprints -> %s' % (len(set_on), ', '.join(sorted(set_on))), flush=True)


if __name__ == '__main__':
    main()
    os._exit(0)
