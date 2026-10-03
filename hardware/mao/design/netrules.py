"""MAO_MAIN board constants shared by every design script (plain Python, no pcbnew).

The routing toolchain is ported from the KINO D4 carrier (see the header of each ported file);
this module replaces KINO's board-specific constants: paths, net classes, track widths.
Coordinates in the scripts are board millimetres; the board itself sits at +ORIGIN in KiCad,
the same convention as the KINO scripts, so their coordinate arithmetic carries over unchanged.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # hardware/mao
NAME = 'MAO_MAIN_A0'
CACHE = ROOT.parents[1] / '.cache' / 'mao-routing'     # repo-root/.cache (git-ignored)
CACHE.mkdir(parents=True, exist_ok=True)
OUTPUTS = ROOT / 'outputs'
DRC_JSON = OUTPUTS / 'DRC.json'
ORIGIN = 50.0

# Layer roles (ODD JOBS 17): F signals + parts, In1 solid GND, In2 power pours + slow signals,
# B signals + parts. The grid router routes on F, In2 and B; In1 takes vias only.
ROUTE_LAYERS = ('F', 'I2', 'B')

# Net classes. Power: carries load current. Switch: the buck-boost inductor nodes (ODD JOBS 8).
POWER = re.compile(r'(VBUS|VBUS_\w+|VSYS|VBAT|BAT_\w+|\+3V3|3V3_\w+|SPK_[PN]|LRA_[PN]|IR_LED_\w+|LCD_BL_K)$')
SWITCH = re.compile(r'(BB_L1|BB_L2)$')
DIFF_PAIRS = {('USB_DP', 'USB_DN'): 'USB 2.0 FS, 90 ohm target (ODD JOBS 23)'}


def width_for(net, ends=()):
    """Track widths to try, widest first (ODD JOBS 12/85: power wide, signals sensible)."""
    if re.match(r'(VSYS|VBAT|BAT_\w+)$', net):
        return [0.6, 0.5, 0.4]
    if re.match(r'(VBUS|VBUS_\w+)$', net):
        return [0.5, 0.4]
    if net == '+3V3':
        return [0.4, 0.3, 0.25]
    if re.match(r'(SPK_[PN]|IR_LED_\w+|LCD_BL_K)$', net):
        return [0.4, 0.3]
    if re.match(r'(LRA_[PN]|3V3_\w+)$', net):
        return [0.3, 0.25]
    return [0.2, 0.15]


def priority(net):
    """Routing order: power first, then clocks, then everything else by length."""
    if POWER.match(net):
        return 0
    if re.search(r'(SCLK|_CLK|BCLK)$', net):
        return 1
    return 2
