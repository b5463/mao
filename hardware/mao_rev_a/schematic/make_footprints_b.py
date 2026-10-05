"""Generate the second batch of Rev A custom footprints (footprints/MAO_RevA.pretty).

Same rules as make_footprints.py: only parts whose KiCad 10 library footprint
doesn't exist or doesn't match the vendor land pattern, and every dimension is
from the drawing cited in the docstring. Pad numbers = the part's pin numbers.
"""
from make_footprints import OUT, _pad, _rect, _write  # noqa: F401  (OUT re-exported for callers)


def _dot(x, y, r=0.05, layer='F.SilkS'):
    return (f'  (fp_circle (center {x:.3f} {y:.3f}) (end {x + r:.3f} {y:.3f}) '
            f'(stroke (width 0.1) (type solid)) (fill solid) (layer "{layer}"))\n')


def _line(layer, x0, y0, x1, y1, width=0.1):
    return (f'  (fp_line (start {x0:.3f} {y0:.3f}) (end {x1:.3f} {y1:.3f}) '
            f'(stroke (width {width}) (type solid)) (layer "{layer}"))\n')


def tsop75xxx_heimdall():
    """Vishay TSOP752../TSOP754.. Heimdall side-looking SMD IR receiver (doc 82494,
    Rev. 2.4, 27-May-2025, p7, drawing 6.550-5297.01-4 issue 4).
    Proposed pad layout (component side): 4x 0.8 x 1.8 mm, pitch 1.27 (3 x 1.27 = 3.81).
    Pinning 1, 4 = GND, 2 = VS, 3 = OUT; front view (looking into the lens) reads
    GND Vs Out GND left to right, so with the lens facing +y pad 1 is at -x.
    Body 6.8 (incl. mold residue) x 3.2 mm (2.5 body + lens). The drawing does not
    dimension pad-to-body; the leads bend under the rear face with a (1) mm foot, so
    the pad centre is placed 0.5 mm in from the rear face (estimate, affects only the
    Fab/courtyard outline, i.e. where the lens sits relative to the pads)."""
    body = ''
    rear = -1.6                      # body centred on origin, rear face at -1.6, lens tip +1.6
    py = rear + 0.5
    for i in range(4):
        body += _pad(i + 1, -1.905 + 1.27 * i, py, 0.8, 1.8)
    body += _line('F.Fab', -3.4, rear + 2.5, 3.4, rear + 2.5, 0.05)   # body/lens boundary
    body += _dot(-2.75, py - 0.9, 0.08)                               # pin 1 (GND) marker
    _write('Vishay_TSOP75xxx_Heimdall_SMD',
           'Vishay TSOP752xx/754xx Heimdall SMD, proposed pad layout doc 82494 Rev 2.4 p7; lens toward +Y',
           body, 6.8, 3.2, (7.4, 4.6))


def vsmb2943_gullwing():
    """Vishay VSMB2943GX01 gull-wing 2.3 x 2.3 mm IR emitter (doc 83486, Rev. 1.7,
    24-Mar-2025, p5 'VSMB2943G', drawing 6.544-5408.01-4). Solder pad proposal acc.
    IPC 7351: 2x 0.75 (y) pads, inner gap 2.45, outer 5.15 -> 1.35 x 0.75 at +-1.9.
    Cathode on the pin-ID (chamfer) side. The datasheet gives no pin numbers; this
    uses the KiCad Device:LED convention 1 = K (cathode), 2 = A (anode)."""
    body = _pad(1, -1.9, 0, 1.35, 0.75) + _pad(2, 1.9, 0, 1.35, 0.75)
    for y0, y1 in ((-1.35, -0.6), (0.6, 1.35)):                        # cathode bar, clear of pad 1
        body += _line('F.SilkS', -1.15, y0, -1.15, y1)
    _write('Vishay_VSMB2943_GullWing',
           'Vishay VSMB2943GX01 gull wing, solder pad proposal doc 83486 Rev 1.7 p5; pad 1 = cathode',
           body, 2.3, 2.3, (5.7, 2.9))


def sk6805_ec20():
    """OPSCO SK6805-EC20 2.0 x 2.0 mm addressable RGB LED (spec SK6805-EC20-001 Rev A1,
    2025-08-25, p5 'PCB recommended pad size'): 4x 0.8 x 0.7 mm, x gap 0.5, y gap 0.5
    -> centres (+-0.65, +-0.6). Top view: 1 GND top-left, 2 DIN bottom-left,
    3 VDD bottom-right, 4 DOUT top-right (p4 table 5)."""
    body = ''
    for num, x, y in ((1, -0.65, -0.6), (2, -0.65, 0.6), (3, 0.65, 0.6), (4, 0.65, -0.6)):
        body += _pad(num, x, y, 0.8, 0.7)
    body += _dot(-1.3, -1.15, 0.06)
    _write('LED_SK6805-EC20_2.0x2.0mm',
           'OPSCO SK6805-EC20, recommended pad size spec SK6805-EC20-001 Rev A1 p5; 1 GND 2 DIN 3 VDD 4 DOUT',
           body, 2.0, 2.0, (2.7, 2.6))


if __name__ == '__main__':
    tsop75xxx_heimdall()
    vsmb2943_gullwing()
    sk6805_ec20()
    import os
    print('written to', os.path.normpath(OUT))
