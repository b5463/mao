"""MAO_MAIN board constants shared by every design script (plain Python, no pcbnew).

The routing toolchain is ported from the KINO D4 carrier (see the header of each ported file);
this module replaces KINO's board-specific constants: paths, net classes, track widths.
Coordinates in the scripts are board millimetres; the board itself sits at +ORIGIN in KiCad,
the same convention as the KINO scripts, so their coordinate arithmetic carries over unchanged.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # hardware/mao
NAME = 'MAO_MAIN_A1'
CACHE = ROOT.parents[1] / '.cache' / 'mao-routing'     # repo-root/.cache (git-ignored)
CACHE.mkdir(parents=True, exist_ok=True)
OUTPUTS = ROOT / 'outputs'
DRC_JSON = OUTPUTS / 'DRC.json'
ORIGIN = 50.0

# Layer roles: JLC04161H-1080, 4 layers (ODD JOBS 17, brief "MAO must be 4-layer"):
#   L1 F.Cu   parts + short fan-out and buses, 0.076 mm above L2
#   L2 In1.Cu uninterrupted solid GND: no tracks at all, only vias pass
#   L3 In2.Cu power regions (+3V3, VSYS, VBAT, VBUS, switched rails) + selected slow signals
#   L4 B.Cu   parts + signals, 0.076 mm below L3
# Router layer names, in stack order; index = position in the stack.
COPPER = ('F', 'I1', 'I2', 'B')
PLANE_LAYERS = ('I1',)           # never routed
SLOW_LAYER = 'I2'                # L3: only SLOW_OK nets, never inside a power-region core
ROUTE_LAYERS = ('F', 'I2', 'B')
# L3 policy: enables, shutdowns, status, interrupts, static or slow GPIO, and I2C where it leaves the power copper
# whole. Never USB, display SPI, I2S, power, switch nodes or the backlight sink's analog loop.
SLOW_OK = {
    'I2C_SDA', 'I2C_SCL',
    'LCD_PWR_EN', 'HAPTIC_EN', 'TOF_XSHUT', 'AUX_PWR_EN', 'LCD_RST_N', 'AMP_SD', 'CHG_CE_N',
    'CHG_STAT1', 'CHG_STAT2', 'VBUS_SENSE', 'TOF_INT_N', 'IMU_INT1', 'LCD_TE', 'LCD_BL',
    'HALL_A', 'HALL_B', 'HALL_FAST', 'IR_RX', 'IR_TX', 'UART_TX', 'UART_RX', 'MCU_EN', 'PRESS_N', 'BOOT',
    'GPIO21', 'GPIO26', 'BAT_NTC', 'CHG_ILIM', 'CHG_ISET',
}
INNER_OK = re.compile(r'(%s)$' % '|'.join(sorted(SLOW_OK)))
INNER_FAST = re.compile(r'(USB_(C_)?D[PN]|LCD_(SCLK|MOSI|CS|DC)\w*|AMP_(DIN|BCLK|LRCLK)|SPK_[PN]|REG_SW|BL_\w+)$')
PLANE_NETS = ('GND', '+3V3')     # GND: L2 plane; +3V3: the L3 +3V3 region. Every pad reaches its copper by a via

# Net classes. Power: carries load current. Switch: the buck-boost inductor nodes (ODD JOBS 8).
POWER = re.compile(r'(VBUS|VSYS|VBAT|BAT_RAW|BAT_IN|REG_IN|\+3V3|3V3_\w+|AUX_3V3|IR_RX_VCC|SPK_[PN]|LRA_[PN]|IR_LED_\w+|LCD_BL_K|BL_SENSE)$')
SWITCH = re.compile(r'(REG_SW)$')
DIFF_PAIRS = {('USB_DP', 'USB_DN'): 'USB 2.0 FS, 90 ohm target (ODD JOBS 23)',
              ('USB_C_DP', 'USB_C_DN'): 'USB 2.0 FS, connector side of the 22R'}


def width_for(net, ends=()):
    """Track widths to try, widest first (ODD JOBS 12/85: power wide, signals sensible). Every list
    ends narrow enough to leave a 0.5 mm-pitch pad: the router only falls back to it where nothing
    wider fits (KINO grid_router practice: 0.2 only where a fine-pitch pin allows nothing wider)."""
    if re.match(r'(VSYS|VBAT|BAT_RAW|BAT_IN|REG_IN)$', net):
        return [0.6, 0.4, 0.25]
    if re.match(r'(VBUS|VBUS_\w+)$', net):
        return [0.5, 0.4, 0.25]
    if net == '+3V3':
        return [0.4, 0.3, 0.2]
    if re.match(r'(SPK_[PN]|IR_LED_\w+|LCD_BL_K|BL_SENSE)$', net):
        return [0.4, 0.3, 0.2]
    if re.match(r'(LRA_[PN]|3V3_\w+|AUX_3V3|IR_RX_VCC)$', net):
        return [0.3, 0.2]
    if re.match(r'(REG_SW)$', net):
        return [0.4, 0.3]
    return [0.2, 0.15]


def priority(net):
    """Routing order: power first, then clocks, then everything else by length."""
    if POWER.match(net):
        return 0
    if re.search(r'(SCLK|_CLK|BCLK)$', net):
        return 1
    return 2


# Designer's routing order for the grid router (GR_PRIORITY): constrained buses first, so they take
# the direct paths; everything else follows by the default priority above.
ROUTE_ORDER = [
    'USB_C_DP', 'USB_C_DN', 'USB_DP', 'USB_DN', 'USB_CC1', 'USB_CC2',
    'LCD_SCLK_P', 'LCD_MOSI_P', 'LCD_CS', 'LCD_DC', 'LCD_SCLK', 'LCD_MOSI', 'LCD_RST_N', 'LCD_TE',
    'LCD_BL_K', 'BL_SENSE', 'BL_FB', 'BL_DRIVE', 'BL_GATE', 'BL_REF', 'LCD_BL', '3V3_LCD', 'LCD_PWR_EN',
    'VBUS', 'VBAT', 'VSYS', 'REG_IN', 'BAT_NTC', 'CHG_ISET', 'CHG_ILIM', 'CHG_CE_N', 'CHG_STAT1', 'CHG_STAT2',
    'AMP_BCLK', 'AMP_LRCLK', 'AMP_DIN', 'AMP_SD', 'SPK_P', 'SPK_N',
    'I2C_SDA', 'I2C_SCL',
    'IR_TX', 'IR_TX_G', 'IR_LED_K', 'IR_RX', 'AUX_3V3', 'IR_RX_VCC', 'AUX_PWR_EN',
    'HALL_A', 'HALL_B', 'HALL_FAST', 'PRESS_N', 'IMU_INT1', 'TOF_INT_N', 'TOF_XSHUT', 'HAPTIC_EN', 'HAP_REG',
    'UART_TX', 'UART_RX', 'MCU_EN', 'BOOT', 'VBUS_SENSE', 'GPIO21', 'GPIO26',
]
