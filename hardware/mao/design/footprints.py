"""MAO custom footprints (KiCad 10 Python). Writes hardware/mao/lib/MAO.pretty.

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


def ti_dla0010a():
    """TPS63802 VSON-HR-10 (DLA0010A). TI SLVSEU9D p.36 'Example board layout' 4223750/D:
    pads 1-5 0.60 x 0.25 at x -0.90; pads 6,7,9,10 0.90 x 0.25 at x +0.75; pad 8 (GND, HotRod
    power pad) 1.30 x 0.25 at x +0.55; pitch 0.50, pin 1 top-left, R0.05 corners. Body 2.0 x 3.0.
    Stencil (p.37) prints pad 8 at ~83 %; modelled as a -8 % paste ratio on pad 8."""
    fp = new('TI_DLA0010A_VSON-HR-10_2x3mm_P0.5mm', 'TI VSON-HR DLA0010A (TPS63802), land pattern per SLVSEU9D',
             'VSON HotRod DLA TPS63802')
    ys = [-1.0, -0.5, 0.0, 0.5, 1.0]
    for i, y in enumerate(ys):
        smd_pad(fp, i + 1, -0.90, y, 0.60, 0.25, rr=0.05)
    for num, y in zip((10, 9, 8, 7, 6), ys):
        if num == 8:
            smd_pad(fp, 8, 0.55, y, 1.30, 0.25, rr=0.05, paste_ratio=-0.08)
        else:
            smd_pad(fp, num, 0.75, y, 0.90, 0.25, rr=0.05)
    body(fp, 2.0, 3.0, pads_extent=(-1.2, -1.125, 1.2, 1.125))
    # silk: short corner marks clear of pads
    for sx in (-1, 1):
        line(fp, pcb.F_SilkS, (sx * 0.5, -1.62), (sx * 0.2, -1.62), 0.12)
        line(fp, pcb.F_SilkS, (sx * 0.5, 1.62), (sx * 0.2, 1.62), 0.12)
    fp.Reference().SetPosition(vec(0, -2.3))
    fp.Value().SetPosition(vec(0, 2.3))
    model(fp, 'TI_DLA0010A_VSON-HR-10', [(-1.0, -1.5, 0.0, 1.0, 1.5, 1.0, (0.10, 0.10, 0.10))])   # 1 mm max height
    save(fp)


def text(fp, layer, s, x, y, size=0.4):
    t = pcb.PCB_TEXT(fp)
    t.SetText(s)
    t.SetPosition(vec(x, y))
    t.SetLayer(layer)
    t.SetTextSize(vec(size, size))
    t.SetTextThickness(MM(size * 0.15))
    fp.Add(t)


def ti_dnp0006a():
    """OPT3004/OPT3001 USON-6 2x2 (DNP0006A). TI SBOS681 'Example board layout' 4221434/C:
    six 0.50 x 0.25 obround pads at x +-0.95, y -0.65/0/+0.65 (1-3 left top->bottom, 4-6 right
    bottom->top), exposed pad 0.65 x 1.35. TI's optional EP vias are omitted (no via in pad,
    ODD JOBS 82); the EP connects to GND by copper."""
    fp = new('TI_DNP0006A_USON-6_2x2mm_P0.65mm_EP0.65x1.35mm', 'TI USON-6 DNP0006A (OPT3004), per SBOS681',
             'USON DNP OPT3004 OPT3001')
    for num, (x, y) in {1: (-0.95, -0.65), 2: (-0.95, 0), 3: (-0.95, 0.65),
                        4: (0.95, 0.65), 5: (0.95, 0), 6: (0.95, -0.65)}.items():
        smd_pad(fp, num, x, y, 0.50, 0.25, rr=0.125)
    smd_pad(fp, 7, 0, 0, 0.65, 1.35, rr=0.05, paste_ratio=-0.15)
    body(fp, 2.0, 2.0, pads_extent=(-1.2, -1.0, 1.2, 1.0))
    # the optical aperture is the package centre: mark it on Fab so the window is placed over it
    rect(fp, pcb.F_Fab, -0.3, -0.3, 0.3, 0.3, 0.05)
    model(fp, 'TI_DNP0006A_USON-6', [                                # 0.65 mm max height
        (-1.0, -1.0, 0.0, 1.0, 1.0, 0.62, (0.18, 0.16, 0.12)),
        (-0.3, -0.3, 0.62, 0.3, 0.3, 0.65, (0.55, 0.45, 0.30)),     # optical aperture
    ])
    save(fp)


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


def bw0019_spring():
    """BAT WIRELESS BW0019BG-L3.5W1.5H3.8 SMD spring contact, gold. Drawing 681-0019B-2: solder pad
    1.50 x 2.10 (+-0.05), body 3.5 x 1.5, free height 3.8, working height 3.0, limit 2.5. The pad is
    the copper land; the arm extends 1.4 mm towards +y. The contact point sits over +y."""
    fp = new('BAT_BW0019BG_SpringContact_3.5x1.5mm', 'SMD gold spring contact, working height 3.0 mm',
             'spring contact finger')
    smd_pad(fp, 1, 0, 0, 1.60, 2.20, rr=0.1)
    rect(fp, pcb.F_Fab, -0.75, -1.05, 0.75, 2.45, 0.08)
    rect(fp, pcb.F_CrtYd, -1.05, -1.35, 1.05, 2.75, 0.05)
    fp.Reference().SetPosition(vec(0, -2.0))
    fp.Value().SetPosition(vec(0, 3.4))
    model(fp, 'BAT_BW0019BG', [                                      # free height 3.8
        (-0.75, -1.05, 0.0, 0.75, 1.05, 0.15, (0.85, 0.68, 0.30)),  # solder base
        (-0.6, 0.6, 0.15, 0.6, 2.45, 3.8, (0.85, 0.68, 0.30)),      # arm and contact
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


def touch_arc():
    """Rim touch electrode for the LEFT / RIGHT body zones: an annular sector at the board edge,
    copper on F.Cu and B.Cu stitched by three plated holes, sensing a finger on the ring through
    the plastic. Geometry from mechanical.py. Origin at the arc mid-point; the board centre is at
    (0, +r_mid) in footprint coordinates, so the placer rotates it about its own origin."""
    import math
    import mechanical as m
    r1, r2 = m.TOUCH_ARC_R
    span = m.TOUCH_ARC_SPAN
    rmid = (r1 + r2) / 2
    name = 'TouchArc_R%.1f-%.1f_%ddeg' % (r1, r2, span)
    fp = new(name, 'MAO rim touch electrode, annular sector, F+B copper', 'capacitive touch electrode')
    fp.SetAttributes(pcb.FP_THROUGH_HOLE | pcb.FP_EXCLUDE_FROM_BOM | pcb.FP_EXCLUDE_FROM_POS_FILES)

    def local(r, deg):
        a = math.radians(deg)
        return (r * math.sin(a), rmid - r * math.cos(a))

    steps = 40
    pts = [local(r2, -span / 2 + span * i / steps) for i in range(steps + 1)]
    pts += [local(r1, span / 2 - span * i / steps) for i in range(steps + 1)]
    for layer, mask in ((pcb.F_Cu, pcb.F_Mask), (pcb.B_Cu, pcb.B_Mask)):
        p = pcb.PAD(fp)
        p.SetNumber('1')
        p.SetAttribute(pcb.PAD_ATTRIB_SMD)
        ls = pcb.LSET()
        ls.AddLayer(layer)          # covered by mask: the electrode is never exposed
        p.SetLayerSet(ls)
        p.SetPosition(vec(0, 0))
        p.SetShape(layer, pcb.PAD_SHAPE_CUSTOM)
        p.SetAnchorPadShape(layer, pcb.PAD_SHAPE_CIRCLE)
        p.SetSize(layer, vec(0.6, 0.6))
        chain = pcb.SHAPE_LINE_CHAIN()
        for x, y in pts:
            chain.Append(MM(x), MM(y))
        chain.SetClosed(True)
        poly = pcb.SHAPE_POLY_SET()
        poly.AddOutline(chain)
        p.AddPrimitivePoly(layer, poly, 0, True)
        fp.Add(p)
    for deg in (-span / 3, 0, span / 3):
        x, y = local(rmid, deg)
        p = pcb.PAD(fp)
        p.SetNumber('1')
        p.SetAttribute(pcb.PAD_ATTRIB_PTH)
        p.SetLayerSet(pcb.PAD.PTHMask())
        p.SetPosition(vec(x, y))
        p.SetDrillSize(vec(0.3, 0.3))
        p.SetSize(pcb.F_Cu, vec(0.6, 0.6))
        p.SetShape(pcb.F_Cu, pcb.PAD_SHAPE_CIRCLE)
        ls = p.GetLayerSet()
        ls.RemoveLayer(pcb.F_Mask)
        ls.RemoveLayer(pcb.B_Mask)
        p.SetLayerSet(ls)            # tented
        fp.Add(p)
    # courtyard: the sector inflated by 0.25 mm
    cp = [local(r2 + 0.25, -span / 2 - 0.5 + (span + 1) * i / steps) for i in range(steps + 1)]
    cp += [local(r1 - 0.25, span / 2 + 0.5 - (span + 1) * i / steps) for i in range(steps + 1)]
    for a, b in zip(cp, cp[1:] + cp[:1]):
        line(fp, pcb.F_CrtYd, a, b, 0.05)
    fp.Reference().SetPosition(vec(0, 2.0))
    fp.Value().SetPosition(vec(0, 3.0))
    save(fp)
    return name


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


ALL = [ti_dla0010a, ti_dnp0006a, everlight_ir12, hdgc_fpc18, bw0019_spring, wire_pads_2, touch_arc]

if __name__ == '__main__':
    for f in ALL:
        f()
    os._exit(0)
