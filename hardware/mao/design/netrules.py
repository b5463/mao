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
# B signals + parts. In1 is solid GND and In2 solid +3V3 (no tracks): the grid router routes on F
# and B only; GND and +3V3 pads each get their own via to their plane.
ROUTE_LAYERS = ('F', 'B')
# ODD JOBS 17 (L3 = power + slower signals): these nets may cross on In2 inside the +3V3 plane where
# F and B are taken. Everything fast (USB, display SPI, I2S, PDM), every touch lead (parasitic C) and
# every power net stays on the outer layers.
INNER_OK = re.compile(r'(I2C_SDA|I2C_SCL|\w+_EN|\w+_N|TOF_XSHUT|IMU_INT\d|HALL_\w+|IR_RX|IR_TX|IR_RX_PWR|'
                      r'UART_TX|UART_RX|IO\d+_SPARE|MIC_PWR|LCD_BL_PWM|BOARD_ID)$')
INNER_FAST = re.compile(r'(USB_D[PN]|LCD_(SCLK|MOSI|CS|DC)\w*|AMP_(DIN|BCLK|LRCLK)|MIC_(CLK|DATA)|SPK_[PN]|BB_L\d)$')
PLANE_NETS = ('GND', '+3V3')     # In1 and In2: every pad reaches its plane through its own via

# Net classes. Power: carries load current. Switch: the buck-boost inductor nodes (ODD JOBS 8).
POWER = re.compile(r'(VBUS|VBUS_\w+|VSYS|VBAT|BAT_\w+|\+3V3|3V3_\w+|SPK_[PN]|LRA_[PN]|IR_LED_\w+|LCD_BL_K)$')
SWITCH = re.compile(r'(BB_L1|BB_L2)$')
DIFF_PAIRS = {('USB_DP', 'USB_DN'): 'USB 2.0 FS, 90 ohm target (ODD JOBS 23)'}


def width_for(net, ends=()):
    """Track widths to try, widest first (ODD JOBS 12/85: power wide, signals sensible). Every list
    ends narrow enough to leave a 0.5 mm-pitch pad: the router only falls back to it where nothing
    wider fits (KINO grid_router practice: 0.2 only where a fine-pitch pin allows nothing wider)."""
    if re.match(r'(VSYS|VBAT|BAT_\w+)$', net):
        return [0.6, 0.4, 0.25]
    if re.match(r'(VBUS|VBUS_\w+)$', net):
        return [0.5, 0.4, 0.25]
    if net == '+3V3':
        return [0.4, 0.3, 0.2]
    if re.match(r'(SPK_[PN]|IR_LED_\w+|LCD_BL_K)$', net):
        return [0.4, 0.3, 0.2]
    if re.match(r'(LRA_[PN]|3V3_\w+)$', net):
        return [0.3, 0.2]
    if re.match(r'(BB_L1|BB_L2)$', net):
        return [0.6, 0.4]
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
    'USB_DP', 'USB_DN', 'USB_CC1', 'USB_CC2',
    'LCD_SCLK_P', 'LCD_MOSI_P', 'LCD_CS', 'LCD_DC', 'LCD_SCLK', 'LCD_MOSI', 'LCD_RST_N', 'LCD_PWR_EN',
    'LCD_BL_PWM', 'LCD_BL_G', 'LCD_BL_K', 'LCD_BL_D', '3V3_LCD', 'LCD_SW_CT', 'LCD_SW_QOD',
    'I2C_SDA', 'I2C_SCL',
    'MIC_CLK', 'MIC_DATA', 'MIC_PWR', 'MIC_VDD',
    'HALL_A', 'HALL_B', 'HALL_FAST',
    'IR_TX', 'IR_TX_G', 'IR_LED_K', 'IR_LED_A1', 'IR_LED_A2', 'IR_RX', 'IR_RX_PWR', 'IR_RX_VCC',
    'UART_TX', 'UART_RX', 'MCU_EN', 'PRESS_N', 'EXP_RST_N', 'IO35_SPARE',
    'IMU_INT1', 'IMU_INT2', 'TOF_INT_N', 'TOF_XSHUT',
    'AMP_SD_N', 'HAPTIC_EN', 'HAP_REG', 'CHG_N', 'SENSE_ALRT_N', 'EXP_INT_N', 'USB_PRESENT_N',
    'TOUCH_LEFT', 'TOUCH_RIGHT', 'TOUCH_TOP', 'TOUCH_REAR',
    'TOUCH_LEFT_E', 'TOUCH_RIGHT_E', 'TOUCH_TOP_E', 'TOUCH_REAR_E',
]
