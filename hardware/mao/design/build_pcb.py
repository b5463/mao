"""Build MAO_MAIN_A1.kicad_pcb from the exported netlist and placement.py (KiCad 10 Python).

The netlist written by build_sch.py is the authority for nets, footprints, fields and symbol paths,
so schematic parity holds by construction. This script rebuilds the board from scratch: outline,
stackup, footprints and nets, keep-outs and pours. It does NOT route (route_*.py) and does NOT
place silkscreen artwork (silk.py).

    <KiCad python> hardware/mao/design/build_pcb.py
"""
import math
import os
import sys

import pcbnew as pcb

import mechanical as m
from board import ORIGIN, ROOT, LOCAL_FP, STOCK_FP, TARGET, at, pt
from netrules import NAME, OUTPUTS
from sexp import find, findall, parse

MM = pcb.FromMM
NETLIST = OUTPUTS / (NAME + '.net')


def netlist():
    root = parse(NETLIST.read_text())
    comps = {}
    for comp in findall(find(root, 'components'), 'comp'):
        ref = str(find(comp, 'ref')[1])
        fields = {}
        fl = find(comp, 'fields')
        for f in findall(fl, 'field') if fl else []:
            fields[str(find(f, 'name')[1])] = str(f[2]) if len(f) > 2 and not isinstance(f[2], list) else ''
        sp = find(comp, 'sheetpath')
        props = {str(find(p, 'name')[1]): str(find(p, 'value')[1]) if find(p, 'value') else ''
                 for p in findall(comp, 'property')}
        comps[ref] = dict(value=str(find(comp, 'value')[1]), footprint=str(find(comp, 'footprint')[1]),
                          fields=fields, sheet_tstamps=str(find(sp, 'tstamps')[1]),
                          tstamp=str(find(comp, 'tstamps')[1]), props=props, pins={})
    for net in findall(find(root, 'nets'), 'net'):
        name = str(find(net, 'name')[1])
        for node in findall(net, 'node'):
            comps[str(find(node, 'ref')[1])]['pins'][str(find(node, 'pin')[1])] = name
    return comps


def load_fp(fpid):
    lib, name = fpid.split(':', 1)
    path = str(LOCAL_FP) if lib == 'MAO' else os.path.join(STOCK_FP, lib + '.pretty')
    fp = pcb.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit('footprint not found: ' + fpid)
    fp.SetFPID(pcb.LIB_ID(lib, name))
    return fp


def seg(board, a, b, layer, w):
    s = pcb.PCB_SHAPE(board)
    s.SetShape(pcb.SHAPE_T_SEGMENT)
    s.SetStart(at(*a))
    s.SetEnd(at(*b))
    s.SetLayer(layer)
    s.SetWidth(MM(w))
    board.Add(s)


def arc(board, start, mid, end, layer, w):
    s = pcb.PCB_SHAPE(board)
    s.SetShape(pcb.SHAPE_T_ARC)
    s.SetArcGeometry(at(*start), at(*mid), at(*end))
    s.SetLayer(layer)
    s.SetWidth(MM(w))
    board.Add(s)


def outline_points(step_deg=2.0, inset=0.0):
    """Board outline as a polygon (for zones): disc with the antenna notch, optionally inset."""
    r = m.PCB_R - inset
    hw = m.NOTCH_W / 2 + inset
    ny = m.NOTCH_Y - inset
    a0 = math.degrees(math.asin(hw / r))          # notch edge angle from 6 o'clock
    pts = []
    # clockwise from 12 o'clock: angle a (cw from 12) -> (r sin a, -r cos a)
    a = 0.0
    end = 180.0 - a0
    while a < end:
        pts.append((r * math.sin(math.radians(a)), -r * math.cos(math.radians(a))))
        a += step_deg
    pts.append((hw, math.sqrt(r * r - hw * hw)))
    pts += [(hw, ny), (-hw, ny), (-hw, math.sqrt(r * r - hw * hw))]
    a = 180.0 + a0
    while a < 360.0:
        pts.append((r * math.sin(math.radians(a)), -r * math.cos(math.radians(a))))
        a += step_deg
    return pts


def draw_outline(board):
    r, hw, ny, f = m.PCB_R, m.NOTCH_W / 2, m.NOTCH_Y, m.NOTCH_FILLET
    yc = math.sqrt(r * r - hw * hw)
    # main arc: from the right notch edge, round through 12 o'clock, to the left notch edge
    arc(board, (hw, yc), (0, -r), (-hw, yc), pcb.Edge_Cuts, 0.05)
    # notch sides and floor with filleted inner corners (a milled cut-out has them anyway)
    seg(board, (hw, yc), (hw, ny + f), pcb.Edge_Cuts, 0.05)
    seg(board, (-hw, yc), (-hw, ny + f), pcb.Edge_Cuts, 0.05)
    seg(board, (hw - f, ny), (-hw + f, ny), pcb.Edge_Cuts, 0.05)
    c45 = f * (1 - math.sqrt(0.5))
    arc(board, (hw, ny + f), (hw - c45, ny + c45), (hw - f, ny), pcb.Edge_Cuts, 0.05)
    arc(board, (-hw + f, ny), (-hw + c45, ny + c45), (-hw, ny + f), pcb.Edge_Cuts, 0.05)
    # display tail slot at 9 o'clock: a routed 1.0 mm slot with round ends, just outside the panel edge
    x0, y0, x1, y1 = m.TAIL_SLOT
    r = (x1 - x0) / 2
    xc = (x0 + x1) / 2
    seg(board, (x0, y0 + r), (x0, y1 - r), pcb.Edge_Cuts, 0.05)
    seg(board, (x1, y0 + r), (x1, y1 - r), pcb.Edge_Cuts, 0.05)
    arc(board, (x0, y1 - r), (xc, y1), (x1, y1 - r), pcb.Edge_Cuts, 0.05)
    arc(board, (x1, y0 + r), (xc, y0), (x0, y0 + r), pcb.Edge_Cuts, 0.05)


def zone(board, net, layers, pts, priority=0, clearance=0.2, name='', thermal=True, min_w=0.2):
    z = pcb.ZONE(board)
    ls = pcb.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    if net:
        z.SetNet(board.FindNet(net))
    chain = pcb.SHAPE_LINE_CHAIN()
    for x, y in pts:
        chain.Append(MM(x + ORIGIN), MM(y + ORIGIN))
    chain.SetClosed(True)
    z.Outline().AddOutline(chain)
    z.SetAssignedPriority(priority)
    z.SetLocalClearance(MM(clearance))
    z.SetMinThickness(MM(min_w))
    z.SetThermalReliefGap(MM(0.25))
    z.SetThermalReliefSpokeWidth(MM(0.3))
    z.SetPadConnection(pcb.ZONE_CONNECTION_THERMAL if thermal else pcb.ZONE_CONNECTION_FULL)
    z.SetIslandRemovalMode(pcb.ISLAND_REMOVAL_MODE_ALWAYS)
    if name:
        z.SetZoneName(name)
    board.Add(z)
    return z


def keepout(board, layers, pts, name, tracks=True, vias=True, pads=True, pours=True, footprints=False):
    z = pcb.ZONE(board)
    z.SetIsRuleArea(True)
    ls = pcb.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    chain = pcb.SHAPE_LINE_CHAIN()
    for x, y in pts:
        chain.Append(MM(x + ORIGIN), MM(y + ORIGIN))
    chain.SetClosed(True)
    z.Outline().AddOutline(chain)
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(vias)
    z.SetDoNotAllowPads(pads)
    z.SetDoNotAllowZoneFills(pours)
    z.SetDoNotAllowFootprints(footprints)
    z.SetZoneName(name)
    board.Add(z)
    return z


def circle_pts(cx, cy, r, n=24):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def sector_pts(r1, r2, a1, a2, n=24):
    out = []
    for i in range(n + 1):
        a = math.radians(a1 + (a2 - a1) * i / n)
        out.append((r2 * math.sin(a), -r2 * math.cos(a)))
    for i in range(n + 1):
        a = math.radians(a2 - (a2 - a1) * i / n)
        out.append((r1 * math.sin(a), -r1 * math.cos(a)))
    return out


SILK_MIN_W = 0.15


def trim_silk(board, margin=0.25):
    """Footprint silk that would cross the board edge (the module's antenna outline over the notch,
    the USB-C body lines at the rim) is cut back to `margin` inside it: the fab would clip it anyway,
    and a clipped line reads as a mistake (ODD JOBS 94). Pieces shorter than 0.3 mm go."""
    sx0, sy0, sx1, sy1 = m.TAIL_SLOT

    def inside(x, y):
        return (math.hypot(x, y) <= m.PCB_R - margin
                and not (abs(x) < m.NOTCH_W / 2 + margin and y > m.NOTCH_Y - margin)
                and not (sx0 - margin < x < sx1 + margin and sy0 - margin < y < sy1 + margin))
    n = 0
    for f in board.GetFootprints():
        for g in list(f.GraphicalItems()):
            if not isinstance(g, pcb.PCB_SHAPE) or g.GetLayer() not in (pcb.F_SilkS, pcb.B_SilkS):
                continue
            if 0 < g.GetWidth() < pcb.FromMM(SILK_MIN_W):       # library outlines and pin-1 dots at 0.10-0.12 mm
                g.SetWidth(pcb.FromMM(SILK_MIN_W))              # print as 0.15 mm (ODD JOBS 95)
            if g.GetShape() != pcb.SHAPE_T_SEGMENT:
                bb = g.GetBoundingBox()
                xs = [pcb.ToMM(v) - ORIGIN for v in (bb.GetLeft(), bb.GetRight())]
                ys = [pcb.ToMM(v) - ORIGIN for v in (bb.GetTop(), bb.GetBottom())]
                if not all(inside(x, y) for x in xs for y in ys):
                    f.Remove(g)
                    n += 1
                continue
            a, b = g.GetStart(), g.GetEnd()
            ax, ay, bx, by = (pcb.ToMM(v) - ORIGIN for v in (a.x, a.y, b.x, b.y))
            ts = [i / 200 for i in range(201)]
            ok = [inside(ax + (bx - ax) * t, ay + (by - ay) * t) for t in ts]
            if all(ok):
                continue
            runs, start = [], None
            for i, o in enumerate(ok + [False]):
                if o and start is None:
                    start = i
                elif not o and start is not None:
                    runs.append((start, i - 1))
                    start = None
            n += 1
            best = max(runs, key=lambda r: r[1] - r[0], default=None)
            seglen = math.hypot(bx - ax, by - ay)
            if best is None or (ts[best[1]] - ts[best[0]]) * seglen < 0.3:
                f.Remove(g)
                continue
            t0, t1 = ts[best[0]], ts[best[1]]
            g.SetStart(at(ax + (bx - ax) * t0, ay + (by - ay) * t0))
            g.SetEnd(at(ax + (bx - ax) * t1, ay + (by - ay) * t1))
    return n


def stackup(board):
    """JLCPCB standard 4-layer 1.6 mm (JLC04161H-1080), matte black mask, white silk, ENIG.
    L1 F parts + signals | L2 In1 solid GND | L3 In2 power regions + slow signals | L4 B parts + signals.
    L1 sits 0.076 mm over the ground plane, L4 0.076 mm under the power layer (ODD JOBS 17-19)."""
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(MM(1.6))
    board.SetCopperLayerCount(4)
    ds.SetCopperLayerCount(4)


def add_footprint(board, ref, c, place):
    """One part from the netlist (c = netlist()[ref]) onto the board at place = (x, y, rotation, side): library
    footprint, reference, value, symbol path (schematic parity), fields, BOM/DNP attributes and pad nets. Also used
    by sync_fields.py --add to put a part added to circuit.py on the routed board."""
    fp = load_fp(c['footprint'])
    fp.SetReference(ref)
    fp.SetValue(c['value'])
    fp.SetPath(pcb.KIID_PATH(c['sheet_tstamps'] + c['tstamp']))
    for k, v in c['fields'].items():
        if k in ('Footprint', 'Reference', 'Value'):
            continue
        fp.SetField(k, v)
        for f in fp.GetFields():
            if f.GetName() == k:
                f.SetVisible(False)
                f.SetLayer(pcb.F_Fab)
    attrs = fp.GetAttributes()
    if c['fields'].get('Exclude from BOM') == 'yes' or 'Exclude from BOM' in c['props']:
        attrs |= pcb.FP_EXCLUDE_FROM_BOM | pcb.FP_EXCLUDE_FROM_POS_FILES
    if 'dnp' in c['props'] or c['fields'].get('DNP') == 'yes':
        attrs |= pcb.FP_DNP
    fp.SetAttributes(attrs)
    if 'dnp' in c['props']:
        fp.SetDNP(True)
    board.Add(fp)
    x, y, rot, side = place
    if side == 'B':
        fp.Flip(fp.GetPosition(), pcb.FLIP_DIRECTION_LEFT_RIGHT)
    fp.SetPosition(at(x, y))
    fp.SetOrientationDegrees(rot)
    for p in fp.Pads():
        net = c['pins'].get(p.GetNumber())
        if net:
            p.SetNet(board.FindNet(net))
    # references to Fab until silk.py places the ones that earn a silk position (ODD JOBS 177)
    fp.Reference().SetLayer(pcb.B_Fab if side == 'B' else pcb.F_Fab)
    fp.Value().SetVisible(False)
    return fp


def build():
    from placement import AUTO, KEEP_F, PLACE    # here, not at import: fp_cache.py needs netlist() before
                                                 # placement.py can resolve a new footprint's geometry
    comps = netlist()
    missing = sorted(set(comps) - set(PLACE))
    extra = sorted(set(PLACE) - set(comps))
    if missing or extra:
        raise SystemExit('placement mismatch: missing %s, extra %s' % (missing, extra))
    board = pcb.BOARD()
    stackup(board)
    for name in sorted({n for c in comps.values() for n in c['pins'].values()}):
        board.Add(pcb.NETINFO_ITEM(board, name))
    for ref, c in sorted(comps.items()):
        add_footprint(board, ref, c, PLACE[ref])
    draw_outline(board)

    # support parts: closest free spot to the pads they serve (place_auto.py)
    import place_auto
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    autos = {a[0] for a in AUTO}
    keep = []
    hw, ny = m.NOTCH_W / 2 + m.ANTENNA_COPPER_SETBACK, m.NOTCH_Y - 0.6
    keep.append((-hw, ny, hw, m.PCB_R + 1))
    for a in m.SCREW_ANGLES + (m.PEG_ANGLE,):
        cx, cy = m.polar(m.MOUNT_R, a)
        r = m.MOUNT_KEEPOUT_D / 2
        keep.append((cx - r, cy - r, cx + r, cy + r))
    occ = place_auto.Occupancy(board, keep, {'F': KEEP_F})
    for ref, fp in fps.items():
        if ref not in autos:
            occ.add_fp(fp)
    for line in place_auto.place(board, fps, AUTO, occ):
        print(line)
    print('silk trimmed at the edge: %d items' % trim_silk(board))

    allcu = [pcb.F_Cu, pcb.In1_Cu, pcb.In2_Cu, pcb.B_Cu]
    # Antenna: no track, via or pour on any layer beside the notch (3 mm past each side) and from 0.6 mm
    # inside the antenna boundary outwards: the part of the board the module's own keep-out covers (ODD JOBS 2/3).
    # The module's own pads are allowed.
    hw, ny = m.NOTCH_W / 2 + m.ANTENNA_COPPER_SETBACK, m.NOTCH_Y - 0.6
    keepout(board, allcu, [(-hw, ny), (hw, ny), (hw, m.PCB_R + 1), (-hw, m.PCB_R + 1)], 'ANTENNA KEEP-OUT',
            pads=False)
    # Module underside on B.Cu: no tracks between the pin rows, where the module body would hide them
    # and its bottom ground pad sits on them (vias and pour allowed; ODD JOBS 2, 94).
    mx0, my0 = 7.0 - 0.4 - 0.25, m.MODULE_PIN_ROW_Y + 0.4 + 0.25     # inside the MINI-1's pad columns and pin row
    keepout(board, [pcb.B_Cu], [(-mx0, my0), (mx0, my0), (mx0, m.ANTENNA_EDGE_Y), (-mx0, m.ANTENNA_EDGE_Y)],
            'MODULE UNDERSIDE', tracks=True, vias=False, pads=False, pours=False)
    # Fasteners: copper-free rings around the screws and the peg, both faces (ODD JOBS 68, 69), and part-free
    # areas for the chassis boss on F and the screw head on B (ODD JOBS 68, 70; mechanical.py).
    for a in m.SCREW_ANGLES + (m.PEG_ANGLE,):
        cx, cy = m.polar(m.MOUNT_R, a)
        keepout(board, allcu, circle_pts(cx, cy, m.MOUNT_KEEPOUT_D / 2), 'FASTENER %d' % int(a),
                pads=False, footprints=False)
        df, db = ((m.PEG_KEEPOUT_D, m.PEG_KEEPOUT_D_B) if a == m.PEG_ANGLE else
                  (m.BOSS_KEEPOUT_D_F, m.HEAD_KEEPOUT_D_B))
        for layer, d, name in ((pcb.F_Cu, df, 'BOSS'), (pcb.B_Cu, db, 'SCREW HEAD')):
            z = keepout(board, [layer], circle_pts(cx, cy, d / 2, 32), '%s %d' % (name, int(a)),
                        tracks=False, vias=False, pads=False, pours=False, footprints=True)
            hole = pcb.SHAPE_LINE_CHAIN()
            for x, y in reversed(circle_pts(cx, cy, 2.55, 32)):     # the hole's own footprint stays out of it
                hole.Append(MM(x + ORIGIN), MM(y + ORIGIN))
            hole.SetClosed(True)
            z.Outline().AddHole(hole)
    # Breakaway tab spots: laminate only near the edge (ODD JOBS 142)
    for a in m.TAB_ANGLES:
        half = math.degrees((m.TAB_ARC / 2 + m.TAB_CLEAR) / m.PCB_R)   # the clearance at the arc's ends too
        keepout(board, allcu, sector_pts(m.PCB_R - m.TAB_CLEAR, m.PCB_R + 1.0, a - half, a + half, 8),
                'TAB %d' % int(a), pads=False)
    # Service-field names: no via where a probe pad's name goes (vias only; tracks may pass under silk)
    for ref, spec in m.FIELD_NAMES.items():
        x0, y0, x1, y1 = m.field_name_box(PLACE[ref][0], PLACE[ref][1], *spec)
        keepout(board, allcu, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], 'NAME %s' % ref,
                tracks=False, vias=True, pads=False, pours=False)
    # The back's maker mark and identity: their spot stays via-free (silk.py places them there)
    for k, (x0, y0, x1, y1) in enumerate(m.B_IDENT_KEEPOUT):
        keepout(board, allcu, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], 'B IDENT %d' % k,
                tracks=False, vias=True, pads=False, pours=False)
    if m.VSYS_CORNER_KEEPOUT:                    # A0: the L3 VSYS branch's turn into the backlight bar: no via antipad
        x0, y0, x1, y1 = m.VSYS_CORNER_KEEPOUT
        keepout(board, allcu, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], 'VSYS CORNER',
                tracks=False, vias=True, pads=False, pours=False)
    # Display tail: no part between the slot and J301 on B, none on the slot's inboard side on F.
    for layer, (x0, y0, x1, y1), name in ((pcb.B_Cu, m.TAIL_CORRIDOR, 'TAIL CORRIDOR'),
                                         (pcb.F_Cu, m.TAIL_F_CLEAR, 'TAIL CLEAR'),
                                         (pcb.F_Cu, m.TAIL_WELL, 'TAIL WELL')):
        keepout(board, [layer], [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], name,
                tracks=False, vias=False, pads=False, pours=False, footprints=True)
    # Antenna fringe: no tracks along the notch boundary on the outer layers (ODD JOBS 2, 3, 125); vias for
    # ground stitching stay allowed.
    hwf = m.NOTCH_W / 2 + m.ANTENNA_COPPER_SETBACK
    keepout(board, [pcb.F_Cu, pcb.B_Cu], [(-hwf, ny - 0.6), (hwf, ny - 0.6), (hwf, ny), (-hwf, ny)],
            'ANTENNA FRINGE', tracks=True, vias=False, pads=False, pours=False)
    # Fiducials need 0.6 mm of bare laminate around the 1 mm dot, and the Tag-Connect and mic-port
    # holes need the 0.25 mm hole clearance; the routers model only the 0.15 mm board rule, so these
    # rules become keep-outs they can see (USB-C pegs and screw holes are already covered).
    for f in board.GetFootprints():
        ref = f.GetReference()
        for p in f.Pads():
            c = p.GetPosition()
            x, y = pcb.ToMM(c.x) - ORIGIN, pcb.ToMM(c.y) - ORIGIN
            if ref.startswith('FID'):
                keepout(board, [f.GetLayer()], circle_pts(x, y, 1.15, 16), ref + ' CLEAR', pads=False, pours=False)
            elif ref in ('J201',) and p.GetAttribute() == pcb.PAD_ATTRIB_NPTH:
                keepout(board, allcu, circle_pts(x, y, pcb.ToMM(p.GetDrillSize().x) / 2 + 0.3, 16),
                        ref + ' HOLE CLEAR', pads=False, pours=False)
    # Solid pour connection where thermal spokes make no sense: exposed pads and their thermal vias
    # (heat path), the module ground pads and the USB-C ground pins (return current, ODD JOBS 13).
    for f in board.GetFootprints():
        solid = 'ThermalVias' in str(f.GetFPID().GetLibItemName()) or f.GetReference() in ('U201', 'J101', 'J301')
        if not solid:
            continue
        for p in f.Pads():
            if p.GetNetname() == 'GND':
                (p.SetLocalZoneConnection if hasattr(p, 'SetLocalZoneConnection') else p.SetZoneConnection)(
                    pcb.ZONE_CONNECTION_FULL)

    edge = outline_points(inset=m.POUR_EDGE)
    zone(board, 'GND', [pcb.In1_Cu], edge, priority=0, name='GND PLANE', thermal=False)
    zone(board, 'GND', [pcb.F_Cu, pcb.B_Cu], edge, priority=0, name='GND POUR')
    # L3: +3V3 everywhere by default, the travelling rails in their own regions above it (power_regions.py)
    import power_regions
    zone(board, '+3V3', [pcb.In2_Cu], edge, priority=0, name='3V3 L3')
    for net, prio, outline, _ in power_regions.zones():
        zone(board, net, [pcb.In2_Cu], outline, priority=prio, name='%s L3' % net)

    pcb.SaveBoard(str(TARGET), board)
    import project
    project.write()        # SaveBoard rewrites the .kicad_pro with defaults: restore MAO's rules
    print('board: %d footprints, %d nets' % (len(board.GetFootprints()), board.GetNetCount()))


if __name__ == '__main__':
    build()
    sys.stdout.flush()
    os._exit(0)
