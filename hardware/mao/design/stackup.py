"""Write the ordered stackup into MAO_MAIN_A0.kicad_pcb (plain Python 3; idempotent).

JLCPCB JLC06161H-3313, 6 layers, 1.6 mm, as published on jlcpcb.com/impedance:
  L1 F.Cu 1 oz | 3313 prepreg 0.0994 | L2 In1.Cu 0.5 oz | core 0.55 | L3 In2.Cu 0.5 oz | 2116 prepreg 0.1088
  | L4 In3.Cu 0.5 oz | core 0.55 | L5 In4.Cu 0.5 oz | 3313 prepreg 0.0994 | L6 B.Cu 1 oz
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
    '(layer "dielectric 1" (type "prepreg") (color "FR4 natural") (thickness 0.0994) %s)' % FR4,
    '(layer "In1.Cu" (type "copper") (thickness 0.0152))',
    '(layer "dielectric 2" (type "core") (color "FR4 natural") (thickness 0.55) %s)' % FR4,
    '(layer "In2.Cu" (type "copper") (thickness 0.0152))',
    '(layer "dielectric 3" (type "prepreg") (color "FR4 natural") (thickness 0.1088) %s)' % FR4,
    '(layer "In3.Cu" (type "copper") (thickness 0.0152))',
    '(layer "dielectric 4" (type "core") (color "FR4 natural") (thickness 0.55) %s)' % FR4,
    '(layer "In4.Cu" (type "copper") (thickness 0.0152))',
    '(layer "dielectric 5" (type "prepreg") (color "FR4 natural") (thickness 0.0994) %s)' % FR4,
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
    print('stackup: JLC06161H-3313, %.4f mm dielectric+copper, black mask, ENIG' % (
        2 * 0.035 + 4 * 0.0152 + 2 * 0.0994 + 0.1088 + 2 * 0.55), flush=True)


if __name__ == '__main__':
    main()
