"""MAO custom footprints (KiCad 10 Python). Writes hardware/mao/lib/MAO.pretty (A1).

Only parts with no KiCad library footprint live here. Every land pattern is transcribed from the
manufacturer drawing named in its docstring; dimensions are in millimetres, origin at the
package centre, pin 1 top-left, viewed from the top.

    <KiCad python> hardware/mao/design/footprints.py
"""
import os

import pcbnew as pcb

from board import LOCAL_FP

MM = pcb.FromMM


def vec(x, y):
    return pcb.VECTOR2I(MM(x), MM(y))


def new(name, descr, tags, smd=True):
    fp = pcb.FOOTPRINT(None)
    fp.SetReference('REF**')
    fp.SetValue(name)
    fp.SetFPID(pcb.LIB_ID('MAO', name))
    fp.SetLibDescription(descr)
    fp.SetKeywords(tags)
    fp.SetAttributes(pcb.FP_SMD if smd else pcb.FP_THROUGH_HOLE)
    fp.Reference().SetPosition(vec(0, -2.5))
    fp.Reference().SetLayer(pcb.F_SilkS)
    fp.Value().SetPosition(vec(0, 2.5))
    fp.Value().SetLayer(pcb.F_Fab)
    for t, size in ((fp.Reference(), 0.8), (fp.Value(), 0.5)):
        t.SetTextSize(vec(size, size))
        t.SetTextThickness(MM(size * 0.15))
    return fp


def smd_pad(fp, num, x, y, w, h, rr=0.0, paste_ratio=0.0, shape=None):
    p = pcb.PAD(fp)
    p.SetNumber(str(num))
    p.SetAttribute(pcb.PAD_ATTRIB_SMD)
    ls = pcb.LSET()
    for l in (pcb.F_Cu, pcb.F_Paste, pcb.F_Mask):
        ls.AddLayer(l)
    p.SetLayerSet(ls)
    p.SetPosition(vec(x, y))
    p.SetSize(pcb.F_Cu, vec(w, h))
    if shape == 'circle':
        p.SetShape(pcb.F_Cu, pcb.PAD_SHAPE_CIRCLE)
    elif rr > 0:
        p.SetShape(pcb.F_Cu, pcb.PAD_SHAPE_ROUNDRECT)
        p.SetRoundRectRadiusRatio(pcb.F_Cu, min(0.5, rr / min(w, h)))
    else:
        p.SetShape(pcb.F_Cu, pcb.PAD_SHAPE_RECTANGLE)
    if paste_ratio:
        p.SetLocalSolderPasteMarginRatio(paste_ratio)
    fp.Add(p)
    return p


def line(fp, layer, a, b, w):
    s = pcb.PCB_SHAPE(fp)
    s.SetShape(pcb.SHAPE_T_SEGMENT)
    s.SetStart(vec(*a))
    s.SetEnd(vec(*b))
    s.SetLayer(layer)
    s.SetWidth(MM(w))
    fp.Add(s)


def rect(fp, layer, x0, y0, x1, y1, w):
    for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        line(fp, layer, a, b, w)


def dot(fp, layer, x, y, r):
    s = pcb.PCB_SHAPE(fp)
    s.SetShape(pcb.SHAPE_T_CIRCLE)
    s.SetCenter(vec(x, y))
    s.SetEnd(vec(x + r, y))
    s.SetFilled(True)
    s.SetWidth(0)
    s.SetLayer(layer)
    fp.Add(s)


def body(fp, w, h, pin1=True, crt_margin=0.25, pads_extent=None):
    """Fab outline with chamfered pin-1 corner, courtyard round body+pads, silk pin-1 cues."""
    x0, y0, x1, y1 = -w / 2, -h / 2, w / 2, h / 2
    c = min(w, h) * 0.2
    pts = [(x0 + c, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0 + c), (x0 + c, y0)]
    for a, b in zip(pts, pts[1:]):
        line(fp, pcb.F_Fab, a, b, 0.1)
    ex = pads_extent or (x0, y0, x1, y1)
    cx0, cy0 = min(x0, ex[0]) - crt_margin, min(y0, ex[1]) - crt_margin
    cx1, cy1 = max(x1, ex[2]) + crt_margin, max(y1, ex[3]) + crt_margin
    rect(fp, pcb.F_CrtYd, round(cx0, 2), round(cy0, 2), round(cx1, 2), round(cy1, 2), 0.05)
    if pin1:
        # two cues (ODD JOBS 93): chamfered fab corner and a silk dot outside the pad field
        dot(fp, pcb.F_SilkS, round(cx0 + 0.02, 2), round(cy0 + 0.02, 2), 0.1)


def model(fp, name, boxes):
    """Attach lib/MAO.3dshapes/<name>.wrl to the footprint and write it from datasheet boxes, so every
    part has its real height in the 3D review and the enclosure check (ODD JOBS 74, 194)."""
    m = pcb.FP_3DMODEL()
    m.m_Filename = '${KIPRJMOD}/lib/MAO.3dshapes/%s.wrl' % name
    fp.Models().push_back(m)
    wrl_box_model(os.path.join(os.path.dirname(LOCAL_FP), 'MAO.3dshapes', name + '.wrl'), boxes)


def save(fp):
    os.makedirs(LOCAL_FP, exist_ok=True)
    pcb.PCB_IO_MGR.FindPlugin(pcb.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(LOCAL_FP), fp)
    print('saved', fp.GetValue())


def ti_dlc0008b():
    """TPS62840 VSON-HR-8 (DLC0008B), 1.5 x 2.0 mm, no exposed pad. TI SLVSEC6D drawing 4224310/A, 'Example
    board layout': 8 pads 0.25 x 0.60 on 0.5 mm pitch, rows (1.3) mm centre to centre; pin 1 top-left, 1-4 down
    the left side, 5-8 up the right. Same geometry as the Gate C generator
    (hardware/mao_rev_a/schematic/make_footprints.py dlc0008b), here with MAO's silk, courtyard and body.
    KiCad's Texas_VSON-HR-8 is the TPS62823 land (0.8 mm pads, 1.45 mm rows): a mismatch (Gate C audit)."""
    fp = new('TI_DLC0008B_VSON-HR-8_1.5x2mm_P0.5mm', 'TI DLC0008B VSON-HR-8 (TPS62840), land pattern example 4224310/A',
             'VSON HotRod DLC TPS62840')
    for i in range(4):
        y = -0.75 + 0.5 * i
        smd_pad(fp, i + 1, -0.65, y, 0.60, 0.25, rr=0.0625)
        smd_pad(fp, 8 - i, 0.65, y, 0.60, 0.25, rr=0.0625)
    body(fp, 1.5, 2.0, pads_extent=(-0.95, -0.875, 0.95, 0.875))
    for sx in (-1, 1):                      # silk: short marks above and below the body, clear of the pads
        line(fp, pcb.F_SilkS, (sx * 0.45, -1.15), (sx * 0.15, -1.15), 0.12)
        line(fp, pcb.F_SilkS, (sx * 0.45, 1.15), (sx * 0.15, 1.15), 0.12)
    fp.Reference().SetPosition(vec(0, -1.9))
    fp.Value().SetPosition(vec(0, 1.9))
    model(fp, 'TI_DLC0008B_VSON-HR-8', [(-0.75, -1.0, 0.0, 0.75, 1.0, 1.0, (0.10, 0.10, 0.10))])   # 1 mm max height
    save(fp)


def text(fp, layer, s, x, y, size=0.4):
    t = pcb.PCB_TEXT(fp)
    t.SetText(s)
    t.SetPosition(vec(x, y))
    t.SetLayer(layer)
    t.SetTextSize(vec(size, size))
    t.SetTextThickness(MM(size * 0.15))
    fp.Add(t)


def everlight_ir12():
    """Everlight IR12-21C/TR8 right-angle 940 nm LED. Datasheet DIR-0000971 rev 8 p.2 'For reflow
    soldering (propose)': two 0.8 x 1.4 pads, centres 3.0 apart (0.8|0.65|0.9|0.65|0.8). Datasheet
    pin 2 = cathode is pad '1' here (KiCad LED symbol: 1 = K, 2 = A). The lens emits towards -y:
    place that side at the board edge. The 0.9 x 0.6 centre area under the lens stays copper-free."""
    fp = new('Everlight_IR12-21C_RightAngle_3x1mm', 'Everlight IR12-21C side-emitting IR LED, lens towards -y',
             'IR LED side looking right angle')
    smd_pad(fp, 1, -1.5, 0, 0.8, 1.4, rr=0.05)   # cathode
    smd_pad(fp, 2, 1.5, 0, 0.8, 1.4, rr=0.05)    # anode
    rect(fp, pcb.F_Fab, -1.5, -0.4, 1.5, 0.6, 0.1)                 # body 3.0 x 1.0
    line(fp, pcb.F_Fab, (-0.5, -0.4), (-0.5, -1.4), 0.1)            # lens dome (side view extent)
    line(fp, pcb.F_Fab, (0.5, -0.4), (0.5, -1.4), 0.1)
    line(fp, pcb.F_Fab, (-0.5, -1.4), (0.5, -1.4), 0.1)
    rect(fp, pcb.F_CrtYd, -2.15, -1.65, 2.15, 0.95, 0.05)
    # cathode cue: silk bar beside pad 1 (two cues with the Fab K mark, ODD JOBS 92/93)
    line(fp, pcb.F_SilkS, (-2.05, -0.6), (-2.05, 0.6), 0.15)
    text(fp, pcb.F_Fab, 'K', -1.5, 1.2, 0.5)
    fp.Reference().SetPosition(vec(0, 1.8))
    fp.Value().SetPosition(vec(0, 2.6))
    model(fp, 'Everlight_IR12-21C', [                                # body 3.0 x 1.1, 1.0 high; lens 0.88 wide
        (-1.5, -0.5, 0.0, 1.5, 0.6, 1.0, (0.70, 0.78, 0.86)),
        (-0.44, -1.4, 0.06, 0.44, -0.5, 0.94, (0.80, 0.86, 0.92)),
    ])
    save(fp)


def samesky_cms150803_pads():
    """Same Sky CMS-150803-088S-X8 micro speaker, 15 x 8 x 3 mm, 8 ohm 0.8 W, with its own spring
    contacts (datasheet 09/11/2024 p.2, 'Recommended PCB Layout'): two 2.0 x 2.0 mm pads 3.09 mm off
    the long axis and 6.95 mm either side of the centre. The speaker is bought separately and held by
    the base with its contacts pressed on these pads, its back about 0.25 mm under the board, so the
    courtyard (body + 0.2 mm tolerance + 0.25 mm) keeps every other part out from under it. Pad 1 is
    SPK+, pad 2 SPK-; with one speaker the polarity only sets the phase."""
    fp = new('SameSky_CMS-150803_SpringPads', 'Same Sky CMS-150803-088S-X8 15x8 mm speaker contact pads',
             'speaker spring contact pads')
    fp.SetAttributes(pcb.FP_SMD | pcb.FP_EXCLUDE_FROM_BOM | pcb.FP_EXCLUDE_FROM_POS_FILES)
    smd_pad(fp, 1, 3.09, -6.95, 2.0, 2.0, rr=0.2)
    smd_pad(fp, 2, 3.09, 6.95, 2.0, 2.0, rr=0.2)
    rect(fp, pcb.F_Fab, -4.0, -7.5, 4.0, 7.5, 0.1)
    rect(fp, pcb.F_Fab, -2.6, -5.0, 2.6, 5.0, 0.08)                   # membrane opening, faces away
    rect(fp, pcb.F_CrtYd, -4.45, -7.95, 4.45, 7.95, 0.05)
    # silk: the body outline, so the speaker's place reads on the bare board, open round the two pads;
    # '+' beside pad 1
    line(fp, pcb.F_SilkS, (-4.35, -7.85), (-4.35, 7.85), 0.12)
    line(fp, pcb.F_SilkS, (-4.35, -7.85), (1.75, -7.85), 0.12)
    line(fp, pcb.F_SilkS, (-4.35, 7.85), (1.75, 7.85), 0.12)
    line(fp, pcb.F_SilkS, (4.35, -5.55), (4.35, 5.55), 0.12)
    line(fp, pcb.F_SilkS, (0.9, -6.95), (1.7, -6.95), 0.12)
    line(fp, pcb.F_SilkS, (1.3, -7.35), (1.3, -6.55), 0.12)
    text(fp, pcb.F_Fab, '+', 3.09, -4.9, 0.6)
    fp.Reference().SetPosition(vec(0, 0))
    fp.Value().SetPosition(vec(0, 1.2))
    model(fp, 'SameSky_CMS-150803', [
        (-4.0, -7.5, 0.25, 4.0, 7.5, 3.25, (0.16, 0.16, 0.17)),         # body, back towards the board
        (-2.6, -5.0, 3.25, 2.6, 5.0, 3.3, (0.55, 0.47, 0.30)),          # membrane (PEEK)
        (2.5, -7.55, 0.0, 3.7, -6.35, 0.25, (0.85, 0.68, 0.30)),       # spring contacts
        (2.5, 6.35, 0.0, 3.7, 7.55, 0.25, (0.85, 0.68, 0.30)),
    ])
    save(fp)


def wire_pads_2():
    """Two 1.0 x 1.8 mm SMD lands at 2.5 mm pitch for the LRA's AWG32 leads (hand soldered).
    Pad 1 is the positive lead; a silk '+' sits beside it."""
    fp = new('WirePads_1x02_P2.5mm_1.0x1.8mm', 'Solder lands for two AWG32 actuator leads', 'wire pad LRA')
    smd_pad(fp, 1, -1.25, 0, 1.0, 1.8, rr=0.15)
    smd_pad(fp, 2, 1.25, 0, 1.0, 1.8, rr=0.15)
    rect(fp, pcb.F_CrtYd, -2.0, -1.15, 2.0, 1.15, 0.05)
    line(fp, pcb.F_SilkS, (-2.5, -0.35), (-2.5, 0.35), 0.15)
    line(fp, pcb.F_SilkS, (-2.85, 0), (-2.15, 0), 0.15)
    fp.Reference().SetPosition(vec(0, -1.8))
    fp.Value().SetPosition(vec(0, 1.8))
    save(fp)


def hdgc_fpc18():
    """HDGC 0.5K-HX-18PWB (LCSC C2919497): 0.5 mm FPC/FFC connector, 18 contacts, 1.0 mm high, front
    insertion with a back-flip actuator, contacts top and bottom (the tail may face either way).
    HDGC drawing 0.5K-HX-xxPWB rev A0 (2016-03-12), dimensions for n = 18 from its table formulas
    (A = 0.5n + 2, B = 0.5(n - 1), E = B + 1.6, F = B + 2.4): signal lands 0.30 x 0.80 on 0.50 pitch,
    retention lands 0.40 x 0.80 between E = 10.1 and F = 10.9, 3.30 from the signal land's far edge to the
    retention land's far edge. Body A = 11.0 wide, housing front 3.25 from the contact tips.
    Origin: centre of the signal row; pin 1 at -x; the FPC enters from +y. Mating FPC: 0.30 mm thick at
    the stiffener, contacts 0.30 wide, G = 9.5 wide (round 1.28" GC9A01 panels with the 18-pin tail,
    e.g. Winstar WF0128BTYAA4DNN0)."""
    name = 'HDGC_0.5K-HX-18PWB_1x18-1MP_P0.5mm_Horizontal'
    fp = new(name, 'HDGC 0.5K-HX-18PWB FPC connector, 18 pins, 0.5 mm pitch, 1.0 mm high, top/bottom contact',
             'FPC FFC ZIF 0.5mm 18P flip HDGC')
    for n in range(1, 19):
        smd_pad(fp, n, -4.25 + 0.5 * (n - 1), 0.0, 0.30, 0.80)
    for x in (-5.25, 5.25):
        smd_pad(fp, 'MP', x, 2.5, 0.40, 0.80)
    # housing (Fab), the FPC entry edge and the inserted tail outline (G = 9.5 wide)
    rect(fp, pcb.F_Fab, -5.5, -0.3, 5.5, 2.85, 0.1)
    line(fp, pcb.F_Fab, (-4.75, 2.85), (-4.75, 4.6), 0.08)
    line(fp, pcb.F_Fab, (4.75, 2.85), (4.75, 4.6), 0.08)
    text(fp, pcb.F_Fab, 'FPC', 0.0, 3.8, 0.5)
    rect(fp, pcb.F_CrtYd, -5.8, -0.7, 5.8, 3.15, 0.05)
    # pin-1 cue on silk: a dot beyond pad 1, outside the land; housing ends as short ticks
    dot(fp, pcb.F_SilkS, -4.25, -0.95, 0.12)
    line(fp, pcb.F_SilkS, (-5.6, 1.0), (-5.6, 1.9), 0.12)
    line(fp, pcb.F_SilkS, (5.6, 1.0), (5.6, 1.9), 0.12)
    text(fp, pcb.F_Fab, '1', -4.25, -1.0, 0.4)
    m = pcb.FP_3DMODEL()
    m.m_Filename = '${KIPRJMOD}/lib/MAO.3dshapes/HDGC_0.5K-HX-18PWB.wrl'
    fp.Models().push_back(m)
    fp.Reference().SetPosition(vec(0, -1.8))
    fp.Value().SetPosition(vec(0, 5.2))
    save(fp)
    wrl_box_model(os.path.join(os.path.dirname(LOCAL_FP), 'MAO.3dshapes', 'HDGC_0.5K-HX-18PWB.wrl'), [
        # (x0, y0, z0, x1, y1, z1, rgb) in mm, board coordinates of the footprint (y down)
        (-5.5, 0.35, 0.0, 5.5, 2.85, 0.75, (0.86, 0.80, 0.66)),     # housing, natural thermoplastic
        (-5.1, -0.3, 0.55, 5.1, 1.0, 1.0, (0.12, 0.12, 0.12)),      # actuator, closed, at the rear
        (-4.4, -0.4, 0.0, 4.4, 0.35, 0.12, (0.85, 0.68, 0.30)),    # contact tails
    ])


def wrl_box_model(path, boxes):
    """Minimal VRML 2.0 body: one coloured box per entry (KiCad VRML unit = 0.1 inch)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    u = 1 / 2.54
    out = ['#VRML V2.0 utf8']
    for x0, y0, z0, x1, y1, z1, rgb in boxes:
        cx, cy, cz = (x0 + x1) / 2 * u, -(y0 + y1) / 2 * u, (z0 + z1) / 2 * u
        out.append('Transform { translation %.4f %.4f %.4f children [ Shape { appearance Appearance { material '
                   'Material { diffuseColor %.2f %.2f %.2f } } geometry Box { size %.4f %.4f %.4f } } ] }'
                   % (cx, cy, cz, rgb[0], rgb[1], rgb[2], (x1 - x0) * u, (y1 - y0) * u, (z1 - z0) * u))
    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


ALL = [ti_dlc0008b, everlight_ir12, hdgc_fpc18, samesky_cms150803_pads, wire_pads_2]

if __name__ == '__main__':
    for f in ALL:
        f()
    os._exit(0)
