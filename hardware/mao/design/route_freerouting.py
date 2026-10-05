"""Freerouting pass for MAO_MAIN, adapted from the KINO carrier's route_secondary.py.

    <KiCad python> route_freerouting.py export    -> .cache/mao-routing/mao.dsn
    python3        route_freerouting.py run       (Freerouting in Docker, eclipse-temurin:25-jre)
    <KiCad python> route_freerouting.py import    <- .cache/mao-routing/mao.ses

Every existing track and via is exported locked, so designed routes are never moved. In1/In4 (GND) and
In3 (+3V3) are solid planes: they are declared power layers and kept as planes, so their nets count
as connected through the existing pad vias and no track uses either layer; routing is on F and B
only (netrules.ROUTE_LAYERS). The outer GND pours are removed from the exchange (Freerouting treats
them as obstacles) and refilled afterwards.
Autorouter output is a draft: every net class is reviewed afterwards (ODD JOBS 191).
"""
import os
import re
import subprocess
import sys

DSN_NAME, SES_NAME = 'mao.dsn', 'mao.ses'
JAR = os.environ.get('FREEROUTING_JAR', '')


def export():
    import pcbnew as pcb
    from board import CACHE, TARGET
    b = pcb.LoadBoard(str(TARGET))
    for t in list(b.GetTracks()):
        t.SetLocked(True)                 # in memory only
    dsn = CACHE / DSN_NAME
    assert pcb.ExportSpecctraDSN(b, str(dsn)), 'DSN export failed'
    text = dsn.read_text()
    text = text.replace('(layer In1.Cu\n      (type signal)', '(layer In1.Cu\n      (type power)', 1)
    text = text.replace('(layer In3.Cu\n      (type signal)', '(layer In3.Cu\n      (type power)', 1)
    text = text.replace('(layer In4.Cu\n      (type signal)', '(layer In4.Cu\n      (type power)', 1)
    settings = '''(autoroute_settings
      (autoroute on) (postroute on) (vias on)
      (via_costs 50) (plane_via_costs 5) (start_ripup_costs 100)
      (layer_rule F.Cu (active on) (preferred_direction horizontal)
        (preferred_direction_trace_costs 1.0) (against_preferred_direction_trace_costs 2.0))
      (layer_rule In1.Cu (active off) (preferred_direction horizontal)
        (preferred_direction_trace_costs 99.0) (against_preferred_direction_trace_costs 99.0))
      (layer_rule In2.Cu (active on) (preferred_direction vertical)
        (preferred_direction_trace_costs 1.2) (against_preferred_direction_trace_costs 2.0))
      (layer_rule In3.Cu (active off) (preferred_direction vertical)
        (preferred_direction_trace_costs 99.0) (against_preferred_direction_trace_costs 99.0))
      (layer_rule In4.Cu (active off) (preferred_direction vertical)
        (preferred_direction_trace_costs 99.0) (against_preferred_direction_trace_costs 99.0))
      (layer_rule B.Cu (active on) (preferred_direction vertical)
        (preferred_direction_trace_costs 1.0) (against_preferred_direction_trace_costs 2.0)))'''
    text = text.replace('(boundary', settings + '\n    (boundary', 1)
    # GND pours on F/In2/B leave the exchange; the In1/In4 GND and In3 +3V3 planes stay
    text, n_outer = re.subn(r'\(plane GND \(polygon (?:F|B|In2)\.Cu [^)]*\)\)\s*', '', text)
    dsn.write_text(text)
    print('exported', dsn, '| removed outer GND pours:', n_outer)
    os._exit(0)


def run(passes=60):
    from netrules import CACHE
    jar = JAR
    assert jar and os.path.exists(jar), 'set FREEROUTING_JAR'
    d, jd = str(CACHE), os.path.dirname(os.path.abspath(jar))
    cmd = ['docker', 'run', '--rm', '-v', d + ':/w', '-v', jd + ':/fr', 'eclipse-temurin:25-jre',
           'java', '-Djava.awt.headless=true', '-jar', '/fr/' + os.path.basename(jar),
           '-de', '/w/' + DSN_NAME, '-do', '/w/' + SES_NAME, '-mp', str(passes), '-mt', '6',
           '--gui.enabled=false']
    ses = CACHE / SES_NAME
    if ses.exists():
        ses.unlink()
    r = subprocess.run(cmd, capture_output=True, text=True)
    lines = (r.stdout + r.stderr).splitlines()
    for line in lines:
        if 'pass #' in line or 'completed' in line or 'unrouted' in line.lower() and 'final' in line.lower():
            pass
    print('\n'.join(l for l in lines if 'Auto-routing stage completed' in l or 'Optimization stage completed' in l
                    or 'ERROR' in l or 'Exception' in l)[-3000:])
    assert ses.exists(), 'no session file'


def import_():
    import pcbnew as pcb
    from board import CACHE, TARGET, restore_through_hole_mask
    b = pcb.LoadBoard(str(TARGET))
    assert pcb.ImportSpecctraSES(b, str(CACHE / SES_NAME)), 'SES import failed'
    bad = [t for t in b.GetTracks() if not isinstance(t, pcb.PCB_VIA) and t.GetLayer() in (pcb.In1_Cu, pcb.In3_Cu, pcb.In4_Cu)]
    assert not bad, 'router used a plane layer'
    for t in list(b.GetTracks()):
        t.SetLocked(False)
    restore_through_hole_mask(b)
    pcb.SaveBoard(str(TARGET), b)
    import project
    project.write()
    print('imported: %d tracks/vias' % len(list(b.GetTracks())))
    os._exit(0)


if __name__ == '__main__':
    {'export': export, 'run': run, 'import': import_}[sys.argv[1]]()
