"""Write the ordered stackup into MAO_MAIN_A0.kicad_pcb (plain Python 3; idempotent).

JLCPCB JLC04161H-1080, 4 layers, 1.6 mm, as published on jlcpcb.com/impedance:
  L1 F.Cu 1 oz | 1080 prepreg 0.0764 | L2 In1.Cu 0.5 oz | core 1.265 | L3 In2.Cu 0.5 oz | 1080 prepreg 0.0764
  | L4 B.Cu 1 oz
The thinnest of JLC's standard 4-layer prepregs: L1 sits 0.076 mm over the L2 ground plane and L4 0.076 mm
under the L3 power layer (brief: outer layers as close to their reference as the standard options allow).
Black solder mask, white legend, ENIG (FAB-NOTES). The Gerber job file and the 3D view take these from the
board, so the release package and the renders show the board as ordered. Rewrites any existing stackup block.
"""
import re

from board import TARGET

FR4 = '(material "FR4") (epsilon_r 4.1) (loss_tangent 0.02)'
LAYERS = [
    '(layer "F.SilkS" (type "Top Silk Screen") (color "White"))',
    '(layer "F.Paste" (type "Top Solder Paste"))',
    '(layer "F.Mask" (type "Top Solder Mask") (color "Black") (thickness 0.01))',
    '(layer "F.Cu" (type "copper") (thickness 0.035))',
    '(layer "dielectric 1" (type "prepreg") (color "FR4 natural") (thickness 0.0764) %s)' % FR4,
    '(layer "In1.Cu" (type "copper") (thickness 0.0152))',
    '(layer "dielectric 2" (type "core") (color "FR4 natural") (thickness 1.265) %s)' % FR4,
    '(layer "In2.Cu" (type "copper") (thickness 0.0152))',
    '(layer "dielectric 3" (type "prepreg") (color "FR4 natural") (thickness 0.0764) %s)' % FR4,
    '(layer "B.Cu" (type "copper") (thickness 0.035))',
    '(layer "B.Mask" (type "Bottom Solder Mask") (color "Black") (thickness 0.01))',
    '(layer "B.Paste" (type "Bottom Solder Paste"))',
    '(layer "B.SilkS" (type "Bottom Silk Screen") (color "White"))',
    '(copper_finish "ENIG")',
    '(dielectric_constraints no)',
]


def block(indent='\t\t'):
    inner = indent + '\t'
    return indent + '(stackup\n' + ''.join(inner + l + '\n' for l in LAYERS) + indent + ')\n'


def end_of(text, start):
    """Index just past the s-expression opening at `start`."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError('unbalanced')


def main():
    s = TARGET.read_text(encoding='utf-8')
    m = re.search(r'\n(\t*)\(stackup\b', s)
    if m:                                             # replace the old block, line and all
        e = end_of(s, m.start() + 1 + len(m.group(1)))
        s = s[:m.start() + 1] + s[e + 1:] if s[e] == '\n' else s[:m.start() + 1] + s[e:]
    m = re.search(r'\n(\t*)\(setup\n', s)
    assert m, 'no setup section'
    s = s[:m.end()] + block(m.group(1) + '\t') + s[m.end():]
    TARGET.write_text(s, encoding='utf-8')
    print('stackup: JLC04161H-1080, %.4f mm dielectric+copper, black mask, ENIG' % (
        2 * 0.035 + 2 * 0.0152 + 2 * 0.0764 + 1.265), flush=True)


if __name__ == '__main__':
    main()
