"""Write MAO_MAIN_A1.kicad_pro: JLCPCB-safe design rules and net classes (ODD JOBS 84-86, 180).

Base settings come from project_base.json (the KINO D4 carrier project, stripped of its own net
classes), so every KiCad 10 key exists; MAO-specific rules are applied on top.

Stackup assumption: JLCPCB standard 4-layer 1.6 mm (JLC04161H-1080): outer 1 oz / inner 0.5 oz,
L1-L2 and L3-L4 1080 prepreg 0.076 mm (Er ~4.1). F sits on the L2 ground plane, B on the L3 power layer.
USB D+/D- runs as an edge-coupled pair on F over the L2 ground at w 0.15 / gap 0.25 (design review 2026-10-06):
about 91 ohm differential bare, ~88 under the mask (Hammerstad-Jensen microstrip with the IPC-2141 coupling term,
h 0.0764 mm, er 4.1, 35 um copper); full-speed USB (12 Mbit/s, edges of several ns over ~45 mm of track,
electrically short) does not need more; no impedance control is ordered.
"""
import json
from pathlib import Path

from netrules import NAME, ROOT

BASE = Path(__file__).with_name('project_base.json')

CLASSES = [
    # name, track, clearance, via dia, via drill, nets
    ('Battery', 0.4, 0.15, 0.6, 0.3, ['VBAT', 'BAT_RAW', 'BAT_IN', 'VSYS', 'REG_IN']),
    ('USBPower', 0.4, 0.15, 0.6, 0.3, ['VBUS']),
    ('Rail3V3', 0.4, 0.15, 0.6, 0.3, ['+3V3']),
    ('Switched', 0.3, 0.15, 0.6, 0.3, ['3V3_LCD', 'AUX_3V3', 'IR_RX_VCC', 'LCD_BL_K', 'BL_SENSE']),
    ('Speaker', 0.4, 0.15, 0.6, 0.3, ['SPK_P', 'SPK_N']),
    ('Actuator', 0.3, 0.15, 0.6, 0.3, ['LRA_P', 'LRA_N', 'IR_LED_K', 'IR_LED_A1', 'IR_LED_A2']),
    ('Switch', 0.4, 0.15, 0.6, 0.3, ['REG_SW']),
    ('USB', 0.15, 0.15, 0.6, 0.3, ['USB_DP', 'USB_DN', 'USB_C_DP', 'USB_C_DN']),
    # fine-pitch IC pads sit 0.15 mm apart, so every class keeps the 0.15 mm rule at the pads;
    # wider spacing for power and switch nets is enforced by the router (netrules / grid_router)
]


def write():
    pro = json.loads(BASE.read_text())
    pro['meta']['filename'] = NAME + '.kicad_pro'
    rules = pro['board']['design_settings']['rules']
    rules.update(min_clearance=0.15, min_track_width=0.15, min_via_diameter=0.5, min_via_annular_width=0.13,
                 min_through_hole_diameter=0.2, min_hole_to_hole=0.25, min_hole_clearance=0.25,
                 min_copper_edge_clearance=0.3, min_silk_clearance=0.15, min_text_height=0.8,
                 min_text_thickness=0.12, solder_mask_to_copper_clearance=0.0)
    default = [c for c in pro['net_settings']['classes'] if c['name'] == 'Default'][0]
    default.update(clearance=0.15, track_width=0.2, via_diameter=0.6, via_drill=0.3,
                   diff_pair_width=0.15, diff_pair_gap=0.25, diff_pair_via_gap=0.25)
    classes = [default]
    patterns = []
    for i, (name, track, clr, vd, vdr, nets) in enumerate(CLASSES):
        c = dict(default)
        c.update(name=name, track_width=track, clearance=clr, via_diameter=vd, via_drill=vdr, priority=i)
        if name == 'USB':
            c.update(diff_pair_width=0.15, diff_pair_gap=0.25)
        classes.append(c)
        patterns += [{'netclass': name, 'pattern': n} for n in nets]
    pro['net_settings']['classes'] = classes
    pro['net_settings']['netclass_patterns'] = patterns
    ds = pro['board']['design_settings']
    # Footprints that differ from their library copy by design, pads identical (electrical audit 3 compared them):
    # build_pcb.trim_silk cuts the silk of the parts that reach the rim back from the milled edge (U201, J101, LS501),
    # and silk.py moves every test pad's silk ring to Fab, since its function name is the cue (TP1-TP16)
    ds['rule_severities']['lib_footprint_mismatch'] = 'ignore'
    ds['track_widths'] = [0.0, 0.15, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.8]
    ds['via_dimensions'] = [{'diameter': 0.0, 'drill': 0.0}, {'diameter': 0.6, 'drill': 0.3}]
    ds['diff_pair_dimensions'] = [{'gap': 0.0, 'via_gap': 0.0, 'width': 0.0}, {'gap': 0.25, 'via_gap': 0.25, 'width': 0.15}]
    (ROOT / (NAME + '.kicad_pro')).write_text(json.dumps(pro, indent=2) + '\n')


if __name__ == '__main__':
    write()
