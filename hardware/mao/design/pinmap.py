"""MAO_MAIN A0 authoritative pin map (ODD JOBS 199).

One table drives three things: the schematic nets of the module and the expander (circuit.py),
the firmware board header (tools/gen_board_header.py writes
components/mao_board/boards/main_a0/mao_board_pins.h) and docs/hardware/mao-pin-map.md.
Change a pin here and regenerate; never edit the generated outputs.

Module pin numbers are ESP32-S3-WROOM-1 pads. Module on B.Cu at 6 o'clock, antenna outwards:
pads 1-14 face 9 o'clock (left), 15-26 face the board centre, 27-40 face 3 o'clock (right).
"""

# (gpio, net, direction, function, notes)
#   direction: in / out / io / od (open-drain input) / analog
#   flags: R = RTC GPIO (deep-sleep wake/hold), T = touch channel, S = strapping pin
NATIVE = [
    (0,  'PRESS_N',     'in',  'face-press switch (to GND), BOOT strap', 'S R; 10k pull-up; hold at reset = download mode, like the LCDkit knob'),
    (1,  'TOUCH_RIGHT', 'analog', 'touch T1, right rim electrode', 'T R; 510R series at the module'),
    (2,  'IMU_INT1',    'in',  'IMU INT1: wake-on-motion, tap', 'R; deep-sleep wake (ext1, active low)'),
    (3,  'BOARD_ID',    'analog', 'board revision divider (ADC1_CH2): A0 = 100k/100k = 1.65 V', 'S: JTAG-source strap, inert unless EFUSE_STRAP_JTAG_SEL is burnt (never on MAO); read once at boot'),
    (4,  'TOUCH_LEFT',  'analog', 'touch T4, left rim electrode', 'T R; 510R series'),
    (5,  'TOUCH_TOP',   'analog', 'touch T5, window-border electrode (spring)', 'T R; 510R series'),
    (6,  'TOUCH_REAR',  'analog', 'touch T6, base electrode (spring)', 'T R; 510R series'),
    (7,  'IMU_INT2',    'in',  'IMU INT2: orientation, free-fall', 'R'),
    (8,  'USB_PRESENT_N', 'in', 'charger PGOOD (open-drain), low = USB power valid', 'R; 100k pull-up; deep-sleep wake'),
    (9,  'LCD_DC',      'out', 'display data/command', ''),
    (10, 'LCD_CS',      'out', 'display chip select (FSPICS0 IO_MUX)', ''),
    (11, 'LCD_MOSI',    'out', 'display data (FSPID IO_MUX)', '22R series option (ODD JOBS 29)'),
    (12, 'LCD_SCLK',    'out', 'display clock (FSPICLK IO_MUX)', '22R series option (ODD JOBS 29)'),
    (13, 'MIC_PWR',     'out', 'microphone supply (GPIO-powered through 100R/1uF)', 'R; 100k pull-down: mic off at reset. SPH0641 draws 80 uA even with the clock stopped, so it is powered only while listening'),
    (14, 'HALL_FAST',   'out', 'Hall sensors: high = fast sampling, low = low-power', 'R (held in deep sleep); 100k pull-down'),
    (15, 'I2C_SDA',     'io',  'I2C data (all sensors, expander, gauge, haptic)', 'R; 2.2k pull-up to +3V3'),
    (16, 'I2C_SCL',     'out', 'I2C clock', 'R; 2.2k pull-up to +3V3'),
    (17, 'EXP_INT_N',   'in',  'expander interrupt (charger status, gauge/light alerts)', 'R; 100k pull-up; deep-sleep wake'),
    (18, 'TOF_INT_N',   'in',  'proximity GPIO1: threshold interrupt', 'R; 100k pull-up'),
    (19, 'USB_DN',      'io',  'USB D-', 'native USB-Serial/JTAG'),
    (20, 'USB_DP',      'io',  'USB D+', 'native USB-Serial/JTAG'),
    (21, 'AMP_BCLK',    'out', 'I2S1 bit clock to the amplifier', 'R'),
    (35, 'IR_TX',       'out', 'IR LED driver gate (RMT carrier)', '100k pull-down: LED off at reset (ODD JOBS 117)'),
    (36, 'IR_RX',       'in',  'IR receiver output (RMT)', ''),
    (37, None,          'nc',  'spare', 'to a test pad'),
    (38, None,          'nc',  'spare', 'to a test pad; GPIO46 drives the backlight because its boot pull-down is also the hardware "backlight off" default'),
    (39, 'HALL_A',      'in',  'ring dial channel A', 'push-pull from the Hall latch'),
    (40, 'HALL_B',      'in',  'ring dial channel B', ''),
    (41, 'MIC_CLK',     'out', 'I2S0 PDM clock to the microphone', ''),
    (42, 'MIC_DATA',    'in',  'I2S0 PDM data from the microphone', ''),
    (43, 'UART_TX',     'out', 'UART0 TX (service)', 'service pad'),
    (44, 'UART_RX',     'in',  'UART0 RX (service)', 'service pad'),
    (45, None,          'nc',  'spare', 'S (VDD_SPI strap, must read 0): left NC with its internal pull-down'),
    (46, 'LCD_BL_PWM',  'out', 'backlight PWM (LEDC)', 'S (must read 0 for download boot): 100k pull-down keeps the backlight off'),
    (47, 'AMP_LRCLK',   'out', 'I2S1 word select', ''),
    (48, 'AMP_DIN',     'out', 'I2S1 data', ''),
]

# TCA6408A-class 8-bit expander, I2C 0x20. All pins are inputs (high-Z, no internal pulls) from
# power-on until firmware configures them: external resistors define every default.
EXPANDER = [
    # (port bit, net, direction, function, default/pull)
    (0, 'LCD_RST_N',    'out', 'display reset', '100k pull-down: panel held in reset'),
    (1, 'LCD_PWR_EN',   'out', 'display + backlight rail load switch', '100k pull-down: off'),
    (2, 'AMP_SD_N',     'out', 'amplifier enable (SD_MODE)', '100k pull-down: amplifier shut down'),
    (3, 'HAPTIC_EN',    'out', 'haptic driver enable', '100k pull-down: off'),
    (4, 'TOF_XSHUT',    'out', 'proximity sensor shutdown (low = off)', '100k pull-down: off'),
    (5, 'IR_RX_PWR',    'out', 'IR receiver supply (via RC filter)', '100k pull-down: off'),
    (6, 'CHG_N',        'in',  'charger CHG (open-drain): low = charging', '100k pull-up'),
    (7, 'SENSE_ALRT_N', 'in',  'fuel-gauge ALRT + light-sensor INT (wired-OR, open-drain)', '10k pull-up'),
]

I2C_ADDRESSES = {
    0x20: 'TCA6408A expander (ADDR low)',
    0x29: 'VL53L4CD proximity (default)',
    0x36: 'MAX17048 fuel gauge',
    0x44: 'OPT3001 ambient light (ADDR to GND)',
    0x5A: 'DRV2605L haptic driver',
    0x6A: 'IMU (SA0 low)',
}


def native_by_net():
    return {net: gpio for gpio, net, *_ in NATIVE if net}


def expander_by_net():
    return {net: bit for bit, net, *_ in EXPANDER}
