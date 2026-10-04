"""Carry value and field changes from the netlist into the routed board (KiCad python).

A BOM-only edit in circuit.py (value text, LCSC / MPN / manufacturer, description, DNP) must not cost a
re-placement and re-route. This updates those properties on the existing footprints and refuses to run
when anything electrical changed: a different footprint, a different pad net or a missing part means
the board has to be rebuilt by the pipeline (place onwards).
"""
import os
import sys

import pcbnew as pcb
from board import TARGET
from build_pcb import netlist


def main():
    comps = netlist()
    b = pcb.LoadBoard(str(TARGET))
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    assert set(fps) == set(comps), ('parts differ: rebuild', set(fps) ^ set(comps))
    changed = []
    for ref, c in comps.items():
        f = fps[ref]
        assert str(f.GetFPID().GetUniStringLibId()) == c['footprint'], ('footprint changed: rebuild', ref)
        for p in f.Pads():
            want = c['pins'].get(p.GetNumber())
            if want:
                assert p.GetNetname() == want, ('net changed: rebuild', ref, p.GetNumber(), p.GetNetname(), want)
        if f.GetValue() != c['value']:
            changed.append('%s value %s -> %s' % (ref, f.GetValue(), c['value']))
            f.SetValue(c['value'])
        have = {fl.GetName(): fl.GetText() for fl in f.GetFields()}
        for k, v in c['fields'].items():
            if k in ('Footprint', 'Reference', 'Value') or have.get(k) == v:
                continue
            changed.append('%s %s %r -> %r' % (ref, k, have.get(k), v))
            f.SetField(k, v)
            for fl in f.GetFields():
                if fl.GetName() == k:
                    fl.SetVisible(False)
                    fl.SetLayer(pcb.B_Fab if f.IsFlipped() else pcb.F_Fab)
    pcb.SaveBoard(str(TARGET), b)
    import project
    project.write()
    print('sync_fields: %d changes' % len(changed))
    for line in changed:
        print('  ' + line)


if __name__ == '__main__':
    main()
    sys.stdout.flush()
    os._exit(0)
