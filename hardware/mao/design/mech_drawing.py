"""Dimensioned enclosure-interface drawing of MAO_MAIN A1 (plain Python 3, writes SVG).

Every number comes from mechanical.py and placement.py, so the drawing can't drift from the board
(ODD JOBS 72/73). Output: docs/hardware/mao-a1-mechanical-interface.svg (+ PNG when PyMuPDF exists).
"""
import math
import os
import subprocess
from xml.sax.saxutils import escape

import mechanical as m
from netrules import ROOT
from placement import PLACE

OUT = ROOT.parents[1] / 'docs' / 'hardware' / 'mao-a1-mechanical-interface.svg'   # A0's drawing stays as history
S = 9.0            # px per mm
W, H = 1250, 820
CX, CY = 360, 380   # board origin in px (top view)


def X(x):
    return CX + x * S


def Y(y):
    return CY + y * S


def circle(x, y, r, stroke='#111', fill='none', w=1.2, dash=''):
    d = ' stroke-dasharray="%s"' % dash if dash else ''
    return '<circle cx="%.1f" cy="%.1f" r="%.1f" stroke="%s" fill="%s" stroke-width="%.1f"%s/>' % (X(x), Y(y), r * S, stroke, fill, w, d)


def line(x1, y1, x2, y2, stroke='#111', w=1.0, dash=''):
    d = ' stroke-dasharray="%s"' % dash if dash else ''
    return '<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f"%s/>' % (X(x1), Y(y1), X(x2), Y(y2), stroke, w, d)


def text(x, y, s, size=12, anchor='start', color='#111', weight='normal'):
    return '<text x="%.1f" y="%.1f" font-family="Helvetica, Arial, sans-serif" font-size="%d" text-anchor="%s" fill="%s" font-weight="%s">%s</text>' % (X(x), Y(y), size, anchor, color, weight, escape(s))


def rect(x0, y0, x1, y1, stroke='#111', fill='none', w=1.0, dash=''):
    d = ' stroke-dasharray="%s"' % dash if dash else ''
    return '<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" stroke="%s" fill="%s" stroke-width="%.1f"%s/>' % (
        X(min(x0, x1)), Y(min(y0, y1)), abs(x1 - x0) * S, abs(y1 - y0) * S, stroke, fill, w, d)


def main():
    e = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (W, H, W, H),
         '<rect width="100%" height="100%" fill="#fff"/>']
    # board outline with notch
    r = m.PCB_R
    hw = m.NOTCH_W / 2
    yc = math.sqrt(r * r - hw * hw)
    e.append(text(0, -m.PUCK_OD / 2 - 1.5, 'MAO_MAIN A1: enclosure interface (top view, from the face)', 16, 'middle', weight='bold'))
    path = 'M %.1f %.1f A %.1f %.1f 0 1 0 %.1f %.1f L %.1f %.1f L %.1f %.1f L %.1f %.1f Z' % (
        X(hw), Y(yc), r * S, r * S, X(-hw), Y(yc), X(-hw), Y(m.NOTCH_Y), X(hw), Y(m.NOTCH_Y), X(hw), Y(yc))
    e.append('<path d="%s" fill="#f4f4f0" stroke="#111" stroke-width="1.6"/>' % path)
    # enclosure, ring, window
    e.append(circle(0, 0, m.PUCK_OD / 2, '#999', dash='6,4'))
    e.append(circle(0, 0, m.RING_ID / 2, '#c80', dash='3,3'))
    e.append(circle(0, 0, m.DISPLAY_ACTIVE_D / 2, '#36c', '#eef3ff', 1.0))
    e.append(circle(0, 0, m.DISPLAY_OUTLINE_R, '#36c', dash='4,3'))
    e.append(text(0, -m.DISPLAY_ACTIVE_D / 2 + 2.2, 'display active area', 10, 'middle', '#36c'))
    e.append(text(0, -m.RING_ID / 2 + 1.6, 'ring ID', 10, 'middle', '#c80'))
    # antenna
    e.append(rect(-m.MODULE_W / 2, m.ANTENNA_EDGE_Y, m.MODULE_W / 2, m.MODULE_OUTER_R, '#c00', '#fde', 1.0))
    e.append(text(0, m.ANTENNA_EDGE_Y + 2.6, 'ANTENNA', 11, 'middle', '#c00'))
    e.append(text(0, m.ANTENNA_EDGE_Y + 4.4, 'no metal within 15 mm', 10, 'middle', '#c00'))
    lx, ly = m.polar(m.LRA_R, m.LRA_ANGLE)          # the one exception: the LRA's steel can in the base
    e.append(circle(lx, ly, 4.0, '#c00', dash='2,2'))
    e.append(text(lx + 4.6, ly + 0.4, 'LRA (base): 7.4 mm from the antenna, RSSI A/B check', 9, 'start', '#c00'))
    # battery
    bw, bd, bh = m.BATTERY_ENVELOPE
    bx, by = m.BATTERY_CENTRE
    e.append(rect(bx - bw / 2, by - bd / 2, bx + bw / 2, by + bd / 2, '#777', 'none', 1.0, '2,3'))
    e.append(text(bx - 2.0, by + bd / 2 - 1.0, 'cell envelope %g x %g x %g (below the PCB)' % (bw, bd, bh), 10, 'middle', '#777'))
    sw, sl, sh = m.SPEAKER_SIZE                       # speaker beside it, also below the PCB
    sx, sy = m.SPEAKER_CENTRE
    e.append(rect(sx - sw / 2, sy - sl / 2, sx + sw / 2, sy + sl / 2, '#c80', 'none', 1.0))
    # display-tail slot and the tail's path under the board to J301
    sl = m.TAIL_SLOT
    e.append(rect(sl[0], sl[1], sl[2], sl[3], '#36c', '#36c', 1.0))
    e.append(rect(m.TAIL_CORRIDOR[0], -m.TAIL_W / 2, PLACE['J301'][0] - 0.8, m.TAIL_W / 2, '#36c', '#eef3ff', 0.8, '3,2'))
    w_ = m.TAIL_WELL                       # where the tail's loop turns, in the carrier's 2.4 mm well
    e.append(rect(w_[0], w_[1], w_[2], w_[3], '#36c', '#eef3ff', 0.8, '3,2'))
    e.append(text((w_[0] + w_[2]) / 2, w_[1] - 0.8, 'tail loop well', 9, 'middle', '#36c'))
    e.append(text(sl[2] + 3.6, sl[1] - 1.0, 'tail slot 1.0 x 11.5', 10, 'middle', '#36c'))
    # fasteners
    for a in m.SCREW_ANGLES:
        x, y = m.polar(m.MOUNT_R, a)
        e.append(circle(x, y, m.MOUNT_HOLE_D / 2, '#111', '#fff', 1.2))
        e.append(circle(x, y, m.MOUNT_KEEPOUT_D / 2, '#111', dash='2,2'))
        e.append(text(x + 2.8, y + 0.4, 'M2 %.2f, %.2f' % (x, y), 10))
    x, y = m.polar(m.MOUNT_R, m.PEG_ANGLE)
    e.append(circle(x, y, m.PEG_HOLE_D / 2, '#111', '#fff', 1.2))
    e.append(text(x + 2.8, y + 0.4, 'peg Ø%.1f (plastic) %.2f, %.2f' % (m.PEG_HOLE_D, x, y), 10))
    # interfaces from the placement
    marks = [('J101', 'USB-C (B), face at y %.1f' % m.USB_FRONT_Y, '#111'),
             ('D501', 'IR out (B)', '#a0a'), ('D502', 'IR out (B)', '#a0a'), ('J301', 'display FPC (B), 70 mm tail through the slot', '#36c'),
             ('U402', 'ToF window', '#0a5'), ('U503', 'IR receive window', '#0a5'),
             ('SW301', 'face-press stem (tripod)', '#111'), ('SW302', 'face-press stem (tripod)', '#111'),
             ('SW303', 'face-press stem (tripod)', '#111'),
             ('LS501', 'speaker below the PCB, outline; contacts on B', '#c80'), ('J501', 'LRA leads (B)', '#c80'), ('U301', 'Hall A', '#36c'), ('U302', 'Hall B', '#36c'),
             ('J201', 'Tag-Connect (B)', '#111'), ('J102', 'battery plug (B), opening to 6 o\'clock', '#c00')]
    legend = []
    for i, (ref, label, color) in enumerate(marks, 1):
        x, y = PLACE[ref][0], PLACE[ref][1]
        e.append(circle(x, y, 0.9, color, '#fff', 1.2))
        e.append(text(x, y + 0.45, str(i), 9, 'middle', color, 'bold'))
        legend.append((i, ref, label, x, y, color))
    # key dimensions (right column)
    rows = [('Board', 'Ø%.1f mm disc, 1.6 mm, 4 layers' % (2 * m.PCB_R)),
            ('Origin', 'puck axis = display centre; +y towards 6 o\'clock'),
            ('Enclosure', 'Ø%.0f mm, wall %.1f mm, board-to-wall %.1f mm' % (m.PUCK_OD, m.WALL, m.PUCK_OD / 2 - m.WALL - m.PCB_R)),
            ('Ring', 'ID %.0f / OD %.0f mm, 30-pole ferrite strip at r %.1f mm, gap %.1f mm' % (m.RING_ID, m.RING_OD, m.RING_MAGNET_R, m.HALL_GAP)),
            ('Notch', '%.1f x %.1f mm, inner edge y %.2f (antenna boundary)' % (m.NOTCH_W, m.PCB_R - m.NOTCH_Y, m.NOTCH_Y)),
            ('Display', 'active Ø%.1f, outline Ø%.1f, panel %.1f / window %.1f mm above PCB' % (
                m.DISPLAY_ACTIVE_D, 2 * m.DISPLAY_OUTLINE_R, m.DISPLAY_STANDOFF, m.WINDOW_Z)),
            ('Zone A', 'F.Cu under the panel: parts <= %.1f mm' % m.ZONE_A_MAX_H),
            ('Zone B', 'B.Cu over the cell: parts <= %.1f mm, 0.3 mm insulator' % m.ZONE_B_MAX_H),
            ('Press', 'tripod: 3 stems at %s deg, r %s mm; 0.25 mm, %.2f N each' % (
                '/'.join('%.0f' % v[0] for v in m.PRESS_TRIPOD.values()),
                '/'.join('%.1f' % v[1] for v in m.PRESS_TRIPOD.values()), m.PRESS_FORCE_N)),
            ('Window', 'sensor band r %.1f-%.1f mm: IR-clear at 11 and 3 o\'clock' % m.WINDOW_ANNULUS),
            ('Fixing', '2 x M2 into heat-set inserts, 1 plastic peg'),
            ('Tail', 'stock 70.1 mm FPC, one loop under the panel turning in a 2.4 mm well'),
            ('', 'of the carrier (x %.0f..%.0f), through the slot (x %.1f..%.1f) to J301 on B' % (m.TAIL_WELL[0], m.TAIL_WELL[2], m.TAIL_SLOT[0], m.TAIL_SLOT[2])),
            ('Panel tabs', '%s deg clockwise from the USB-C: no copper within %.2f mm of the rim' % (' and '.join('%.0f' % a_ for a_ in m.TAB_ANGLES), m.TAB_CLEAR + 0.05)),
            ]
    y = -28.0
    for k, v in rows:
        e.append('<text x="%d" y="%.1f" font-family="Helvetica, Arial, sans-serif" font-size="12"><tspan font-weight="bold">%s</tspan>  %s</text>' % (720, Y(y), k, escape(v)))
        y += 2.4
    y += 1.6
    e.append('<text x="%d" y="%.1f" font-family="Helvetica, Arial, sans-serif" font-size="13" font-weight="bold">Interfaces (board mm, top view)</text>' % (720, Y(y)))
    y += 2.4
    for i, ref, label, x, yy, color in legend:
        e.append('<text x="%d" y="%.1f" font-family="Helvetica, Arial, sans-serif" font-size="11" fill="%s"><tspan font-weight="bold">%2d</tspan>  %s  %s  (%.2f, %.2f)</text>' % (
            720, Y(y), color, i, ref, escape(label), x, yy))
        y += 1.9
    e.append('</svg>')
    OUT.write_text('\n'.join(e), encoding='utf-8')
    py = os.environ.get('PYMUPDF_PY')
    if py:
        subprocess.run([py, '-c', 'import pymupdf,sys;d=pymupdf.open(sys.argv[1]);d[0].get_pixmap(matrix=pymupdf.Matrix(1.5,1.5)).save(sys.argv[2])',
                        str(OUT), str(OUT.with_suffix('.png'))])
    print('wrote', OUT)


if __name__ == '__main__':
    main()
