"""pcbnew helpers (run with KiCad's bundled Python)."""
import os
import subprocess

import pcbnew

FP_DIR = os.path.join(os.environ.get(
    'KICAD_SHARE', os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/SharedSupport')), 'footprints')

MM = pcbnew.FromMM


def vec(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


def load_footprint(fpid, local_dirs=()):
    lib, name = fpid.split(':', 1)
    for d in list(local_dirs) + [FP_DIR]:
        path = os.path.join(d, lib + '.pretty')
        if os.path.isdir(path) and os.path.exists(os.path.join(path, name + '.kicad_mod')):
            fp = pcbnew.FootprintLoad(path, name)
            fp.SetFPID(pcbnew.LIB_ID(lib, name))
            return fp
    raise SystemExit('footprint not found: ' + fpid)


def get_net(board, name):
    ni = board.FindNet(name)
    if ni is None:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
    return ni


def set_layer_count(board, n):
    board.SetCopperLayerCount(n)
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(n)


def add_segment(board, layer, pts, width=0.1, closed=False):
    pts = list(pts)
    if closed:
        pts = pts + [pts[0]]
    for a, b in zip(pts, pts[1:]):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(vec(*a))
        s.SetEnd(vec(*b))
        s.SetLayer(layer)
        s.SetWidth(MM(width))
        board.Add(s)


def add_circle(board, layer, c, r, width=0.1, filled=False):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_CIRCLE)
    s.SetCenter(vec(*c))
    s.SetEnd(vec(c[0] + r, c[1]))
    s.SetLayer(layer)
    s.SetWidth(MM(width))
    if filled:
        s.SetFilled(True)
    board.Add(s)
    return s


def add_arc(board, layer, start, mid, end, width=0.1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetArcGeometry(vec(*start), vec(*mid), vec(*end))
    s.SetLayer(layer)
    s.SetWidth(MM(width))
    board.Add(s)
    return s


def add_text(board, text, x, y, layer, size=1.0, thickness=0.15, angle=0, justify='center',
             mirror=False, bold=False, font=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(vec(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(thickness))
    t.SetTextAngleDegrees(angle)
    t.SetMirrored(mirror)
    t.SetBold(bold)
    t.SetHorizJustify({'left': pcbnew.GR_TEXT_H_ALIGN_LEFT, 'center': pcbnew.GR_TEXT_H_ALIGN_CENTER,
                       'right': pcbnew.GR_TEXT_H_ALIGN_RIGHT}[justify])
    board.Add(t)
    return t


def polygon(points):
    chain = pcbnew.SHAPE_LINE_CHAIN()
    for x, y in points:
        chain.Append(MM(x), MM(y))
    chain.SetClosed(True)
    return chain


def add_zone(board, net, layers, points, priority=0, clearance=0.2, min_width=0.2,
             thermal_gap=0.25, thermal_spoke=0.3, name='', solid_pads=False):
    z = pcbnew.ZONE(board)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    if net:
        z.SetNet(get_net(board, net))
    z.Outline().AddOutline(polygon(points))
    z.SetAssignedPriority(priority)
    z.SetLocalClearance(MM(clearance))
    z.SetMinThickness(MM(min_width))
    z.SetThermalReliefGap(MM(thermal_gap))
    z.SetThermalReliefSpokeWidth(MM(thermal_spoke))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if solid_pads else pcbnew.ZONE_CONNECTION_THERMAL)
    if name:
        z.SetZoneName(name)
    board.Add(z)
    return z


def add_keepout(board, layers, points, name, tracks=True, vias=True, pads=True, pours=True, footprints=False):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    z.Outline().AddOutline(polygon(points))
    z.SetDoNotAllowTracks(tracks)
    z.SetDoNotAllowVias(vias)
    z.SetDoNotAllowPads(pads)
    z.SetDoNotAllowZoneFills(pours)
    z.SetDoNotAllowFootprints(footprints)
    z.SetZoneName(name)
    board.Add(z)
    return z


def add_track(board, net, layer, pts, width):
    n = get_net(board, net)
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(vec(*a))
        t.SetEnd(vec(*b))
        t.SetLayer(layer)
        t.SetWidth(MM(width))
        t.SetNet(n)
        board.Add(t)


def add_via(board, net, x, y, drill=0.3, size=0.6):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(vec(x, y))
    v.SetDrill(MM(drill))
    v.SetWidth(MM(size))
    v.SetNet(get_net(board, net))
    board.Add(v)
    return v


def fill_zones(board):
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())


def freeroute(dsn, ses, jar, passes=40, threads=6, docker_image='eclipse-temurin:25-jre'):
    d = os.path.dirname(os.path.abspath(dsn))
    jd = os.path.dirname(os.path.abspath(jar))
    cmd = ['docker', 'run', '--rm', '-v', d + ':/w', '-v', jd + ':/fr', docker_image,
           'java', '-Djava.awt.headless=true', '-jar', '/fr/' + os.path.basename(jar),
           '-de', '/w/' + os.path.basename(dsn), '-do', '/w/' + os.path.basename(ses),
           '-mp', str(passes), '-mt', str(threads), '--gui.enabled=false']
    print(' '.join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    tail = '\n'.join((r.stdout + r.stderr).splitlines()[-25:])
    print(tail)
    if not os.path.exists(ses):
        raise SystemExit('freerouting produced no session file')
