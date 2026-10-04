"""MAO_MAIN A0 authoritative pin map (ODD JOBS 199).

One table drives three things: the schematic nets of the module and the expander (circuit.py),
the firmware board header (hardware/mao/design/gen_pinmap.py writes
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
    (2,  'HALL_FAST',   'out', 'Hall sensors: high = fast sampling, low = low-power', 'R (held in deep sleep); 100k pull-down; right column beside the Hall pair'),
    (3,  'USB_PRESENT_N', 'in', 'charger PGOOD (open-drain), low = USB power valid', 'R; 100k pull-up; deep-sleep wake. S: JTAG-source strap, inert unless EFUSE_STRAP_JTAG_SEL is burnt (never on MAO). Top-row corner pin: drops through a via between the pad rows to L3'),
    (4,  'TOUCH_LEFT',  'analog', 'touch T4, left rim electrode', 'T R; 510R series; the three touch pins (4-6) leave in the order of their electrodes: left rim (outermost lead), top spring, rear spring'),
    (5,  'TOUCH_TOP',   'analog', 'touch T5, window-border electrode (spring)', 'T R; 510R series'),
    (6,  'TOUCH_REAR',  'analog', 'touch T6, base electrode (spring at 10 o\'clock)', 'T R; 510R series'),
    (7,  'I2C_SDA',     'io',  'I2C data (all sensors, expander, gauge, haptic)', 'R; 2.2k pull-up to +3V3; left-column pin below the I2S group: the bus reaches the haptic driver under the I2S lanes'),
    (8,  'BOARD_ID',    'analog', 'board revision divider (ADC1_CH7): A0 = 1M/1M + 100 nF = 1.65 V', 'read once at boot; the static line runs on L3 to its divider'),
    (9,  'LCD_TE',      'in',  'display tearing-effect output: frame sync for tear-free animation', 'R; the display group (pins 17-21: TE, DC, MOSI, SCLK, CS) is in the connector\'s own pin order: no crossing'),
    (10, 'LCD_DC',      'out', 'display data/command', 'R'),
    (11, 'LCD_MOSI',    'out', 'display data (FSPID IO_MUX)', 'R; 22R series (ODD JOBS 29)'),
    (12, 'LCD_SCLK',    'out', 'display clock (FSPICLK IO_MUX)', 'R; 22R series (ODD JOBS 29)'),
    (13, 'LCD_CS',      'out', 'display chip select (GPIO matrix; SCLK/MOSI stay on IO_MUX)', 'R'),
    (14, 'IMU_INT1',    'in',  'IMU INT1: wake-on-motion, tap', 'R; deep-sleep wake (ext1, active low); top row beside the IMU'),
    (15, 'I2C_SCL',     'out', 'I2C clock', 'R; 2.2k pull-up to +3V3'),
    (16, 'AMP_DIN',     'out', 'I2S1 data (left column, with BCLK/LRCLK on the next two pins)', 'R'),
    (17, 'AMP_BCLK',    'out', 'I2S1 bit clock to the amplifier', 'R'),
    (18, 'AMP_LRCLK',   'out', 'I2S1 word select', 'R'),
    (19, 'USB_DN',      'io',  'USB D-', 'native USB-Serial/JTAG'),
    (20, 'USB_DP',      'io',  'USB D+', 'native USB-Serial/JTAG'),
    (21, 'EXP_INT_N',   'in',  'expander interrupt (charger status, gauge/light alerts)', 'R; 100k pull-up; deep-sleep wake'),
    (35, 'MIC_PWR',     'out', 'microphone supply (GPIO-powered through 100R/1uF)', '100k pull-down: mic off at reset and in deep sleep (pad high-Z). SPH0641 draws 80 uA even with the clock stopped, so it is powered only while listening. Right column: the mic group (pins 28-30) leaves together towards MK401'),
    (36, 'MIC_DATA',    'in',  'I2S0 PDM data from the microphone', 'mic group'),
    (37, 'MIC_CLK',     'out', 'I2S0 PDM clock to the microphone', 'mic group'),
    (38, 'EXP_RST_N',   'out', 'expander RESET: pulse low to recover a wedged TCA6408A without a power cycle', '10k pull-up: released from power-on; drive open-drain'),
    (39, 'IR_TX',       'out', 'IR LED driver gate (RMT carrier)', '100k pull-down: LED off at reset (ODD JOBS 117)'),
    (40, 'IR_RX',       'in',  'IR receiver output (RMT)', '10k pull-up R506 to the receiver supply'),
    (41, 'HALL_A',      'in',  'ring dial channel A', 'push-pull from the Hall latch'),
    (42, 'HALL_B',      'in',  'ring dial channel B', ''),
    (43, 'UART_TX',     'out', 'UART0 TX (service)', 'service pad'),
    (44, 'UART_RX',     'in',  'UART0 RX (service)', 'service pad'),
    (45, 'LCD_BL_PWM',  'out', 'backlight PWM (LEDC)', 'S (VDD_SPI strap, must read 0 at reset): the 100k gate pull-down R305 (through 100R) holds it low, which also keeps the backlight off. Top-row end pin: the line leaves on L3 clear of the display bus'),
    (46, None,          'nc',  'spare', 'S (must read 0 for download boot): left NC with its internal pull-down; boxed in by the display bus, so not used'),
    (47, 'IMU_INT2',    'in',  'IMU INT2: orientation, free-fall', ''),
    (48, 'TOF_INT_N',   'in',  'proximity GPIO1: threshold interrupt', '10k pull-up (ST application circuit)'),
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
    (7, 'SENSE_ALRT_N', 'in',  'fuel-gauge ALRT + light-sensor INT (wired-OR, open-drain)', '100k pull-up'),
]

I2C_ADDRESSES = {
    0x20: 'TCA6408A expander (ADDR low)',
    0x29: 'VL53L4CD proximity (default)',
    0x36: 'MAX17048 fuel gauge',
    0x44: 'OPT3004 ambient light (ADDR to GND)',
    0x5A: 'DRV2605L haptic driver',
    0x6A: 'IMU (SA0 low)',
}


def native_by_net():
    return {net: gpio for gpio, net, *_ in NATIVE if net}


def expander_by_net():
    return {net: bit for bit, net, *_ in EXPANDER}
