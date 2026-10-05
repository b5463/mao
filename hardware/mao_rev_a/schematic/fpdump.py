"""Print a KiCad footprint's pads (number, shape, centre, size) for the footprint audit.

usage: python fpdump.py Lib:Footprint [...]
"""
import os
import re
import sys

from checks import KICAD_FP, LOCAL_FP


def pads(fp):
    lib, name = fp.split(':', 1)
    for root in (KICAD_FP, LOCAL_FP):
        path = os.path.join(root, lib + '.pretty', name + '.kicad_mod')
        if os.path.exists(path):
            break
    else:
        raise FileNotFoundError(fp)
    text = open(path, encoding='utf8').read()
    out = []
    for m in re.finditer(r'\(pad\s+"([^"]*)"\s+(\w+)\s+(\w+)(.*?)\(layers', text, re.S):
        body = m.group(4)
        at = re.search(r'\(at\s+([-\d.]+)\s+([-\d.]+)', body)
        size = re.search(r'\(size\s+([\d.]+)\s+([\d.]+)', body)
        out.append((m.group(1), m.group(2), m.group(3), float(at.group(1)), float(at.group(2)),
                    float(size.group(1)), float(size.group(2))))
    descr = re.search(r'\(descr\s+"([^"]*)"', text)
    return (descr.group(1) if descr else ''), out


if __name__ == '__main__':
    for fp in sys.argv[1:]:
        d, ps = pads(fp)
        print(f'== {fp}\n   {d}')
        for num, kind, shape, x, y, w, h in ps:
            print(f'   {num:>4} {kind:<6} {shape:<9} at ({x:7.3f},{y:7.3f}) size {w:.3f} x {h:.3f}')
