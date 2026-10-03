"""Write MAO_MAIN_A0.kicad_pro: JLCPCB-safe design rules and net classes (ODD JOBS 84-86, 180).

Base settings come from project_base.json (the KINO D4 carrier project, stripped of its own net
classes), so every KiCad 10 key exists; MAO-specific rules are applied on top.

Stackup assumption: JLCPCB JLC04161H-7628, 1.6 mm, outer 1 oz / inner 0.5 oz, L1-L2 prepreg
0.2104 mm (Er ~4.4). USB D+/D- edge-coupled microstrip over the L2 ground: w 0.25, gap 0.15 ->
Z0 ~61 ohm, Zdiff ~92 ohm (IPC-2141 approximation); USB full speed tolerates the residual error.
"""
import json
from pathlib import Path

from netrules import NAME, ROOT

BASE = Path(__file__).with_name('project_base.json')

CLASSES = [
    # name, track, clearance, via dia, via drill, nets
    ('Battery', 0.6, 0.2, 0.6, 0.3, ['VBAT', 'BAT_RAW', 'BAT_IN', 'VSYS']),
    ('USBPower', 0.5, 0.2, 0.6, 0.3, ['VBUS']),
    ('Rail3V3', 0.4, 0.15, 0.6, 0.3, ['+3V3']),
    ('Switched', 0.3, 0.15, 0.6, 0.3, ['3V3_LCD', 'LCD_BL_K', 'LCD_BL_D', 'MIC_VDD', 'IR_RX_VCC']),
    ('Speaker', 0.4, 0.2, 0.6, 0.3, ['SPK_P', 'SPK_N']),
    ('Actuator', 0.3, 0.2, 0.6, 0.3, ['LRA_P', 'LRA_N', 'IR_LED_K', 'IR_LED_A1', 'IR_LED_A2']),
    ('Switch', 0.5, 0.25, 0.6, 0.3, ['BB_L1', 'BB_L2']),
    ('USB', 0.25, 0.15, 0.6, 0.3, ['USB_DP', 'USB_DN']),
    ('Touch', 0.15, 0.3, 0.6, 0.3, ['TOUCH_LEFT_E', 'TOUCH_RIGHT_E', 'TOUCH_TOP_E', 'TOUCH_REAR_E',
                                    'TOUCH_LEFT', 'TOUCH_RIGHT', 'TOUCH_TOP', 'TOUCH_REAR']),
]


def write():
    pro = json.loads(BASE.read_text())
    pro['meta']['filename'] = NAME + '.kicad_pro'
    rules = pro['board']['design_settings']['rules']
    rules.update(min_clearance=0.15, min_track_width=0.15, min_via_diameter=0.6, min_via_annular_width=0.13,
                 min_through_hole_diameter=0.3, min_hole_to_hole=0.25, min_hole_clearance=0.25,
                 min_copper_edge_clearance=0.3, min_silk_clearance=0.15, min_text_height=0.8,
                 min_text_thickness=0.12, solder_mask_to_copper_clearance=0.0)
    default = [c for c in pro['net_settings']['classes'] if c['name'] == 'Default'][0]
    default.update(clearance=0.15, track_width=0.2, via_diameter=0.6, via_drill=0.3,
                   diff_pair_width=0.25, diff_pair_gap=0.15, diff_pair_via_gap=0.25)
    classes = [default]
    patterns = []
    for i, (name, track, clr, vd, vdr, nets) in enumerate(CLASSES):
        c = dict(default)
        c.update(name=name, track_width=track, clearance=clr, via_diameter=vd, via_drill=vdr, priority=i)
        if name == 'USB':
            c.update(diff_pair_width=0.25, diff_pair_gap=0.15)
        classes.append(c)
        patterns += [{'netclass': name, 'pattern': n} for n in nets]
    pro['net_settings']['classes'] = classes
    pro['net_settings']['netclass_patterns'] = patterns
    ds = pro['board']['design_settings']
    ds['track_widths'] = [0.0, 0.15, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.8]
    ds['via_dimensions'] = [{'diameter': 0.0, 'drill': 0.0}, {'diameter': 0.6, 'drill': 0.3}]
    ds['diff_pair_dimensions'] = [{'gap': 0.0, 'via_gap': 0.0, 'width': 0.0}, {'gap': 0.15, 'via_gap': 0.25, 'width': 0.25}]
    (ROOT / (NAME + '.kicad_pro')).write_text(json.dumps(pro, indent=2) + '\n')


if __name__ == '__main__':
    write()
