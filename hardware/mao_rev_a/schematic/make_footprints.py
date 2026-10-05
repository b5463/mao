"""Generate the Rev A custom footprints (hardware/mao_rev_a/footprints/MAO_RevA.pretty).

Only for parts whose KiCad 10 library footprint doesn't match the vendor's
land pattern. Every dimension here is from the drawing cited in its docstring.
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'footprints', 'MAO_RevA.pretty')


def _pad(num, x, y, w, h, shape='roundrect', layers='"F.Cu" "F.Paste" "F.Mask"'):
    rr = ' (roundrect_rratio 0.25)' if shape == 'roundrect' else ''
    return (f'  (pad "{num}" smd {shape} (at {x:.4f} {y:.4f}) (size {w:.4f} {h:.4f}) '
            f'(layers {layers}){rr})\n')


def _rect(layer, x0, y0, x1, y1, width=0.05):
    return (f'  (fp_rect (start {x0:.3f} {y0:.3f}) (end {x1:.3f} {y1:.3f}) '
            f'(stroke (width {width}) (type solid)) (fill none) (layer "{layer}"))\n')


def _write(name, descr, body, bx, by, court):
    text = (f'(footprint "{name}" (version 20240108) (generator "mao_make_footprints")\n'
            f'  (layer "F.Cu")\n  (descr "{descr}")\n  (attr smd)\n'
            f'  (property "Reference" "REF**" (at 0 {-by/2 - 0.6:.3f}) (layer "F.SilkS") '
            f'(effects (font (size 0.5 0.5) (thickness 0.08))))\n'
            f'  (property "Value" "{name}" (at 0 {by/2 + 0.6:.3f}) (layer "F.Fab") '
            f'(effects (font (size 0.4 0.4) (thickness 0.06))))\n'
            + _rect('F.Fab', -bx/2, -by/2, bx/2, by/2, 0.1)
            + _rect('F.CrtYd', -court[0]/2, -court[1]/2, court[0]/2, court[1]/2)
            + body + ')\n')
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name + '.kicad_mod'), 'w', encoding='utf8') as f:
        f.write(text)


def dlc0008b():
    """TI DLC0008B VSON-HR-8, 1.5 x 2.0 mm (TPS62840 SLVSEC6D, drawing 4224310/A,
    land pattern example): 8x 0.25 x 0.6 mm pads, 0.5 mm pitch, rows (1.3) mm
    centre to centre, no exposed pad. Pin 1 top left, 1-4 down the left side."""
    body = ''
    for i in range(4):
        y = -0.75 + 0.5 * i
        body += _pad(i + 1, -0.65, y, 0.6, 0.25)
        body += _pad(8 - i, 0.65, y, 0.6, 0.25)
    body += '  (fp_circle (center -1.05 -1.15) (end -1.0 -1.15) (stroke (width 0.1) (type solid)) (fill solid) (layer "F.SilkS"))\n'
    _write('TI_DLC0008B_VSON-HR-8_1.5x2mm_P0.5mm', 'TI DLC0008B, TPS62840DLC land pattern example (4224310/A)',
           body, 1.5, 2.0, (2.4, 2.5))


if __name__ == '__main__':
    dlc0008b()
    print('written to', os.path.normpath(OUT))
