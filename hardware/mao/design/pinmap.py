"""MAO_MAIN A1 authoritative pin map (ODD JOBS 199).

One table drives three things: the schematic nets of the module (circuit.py), the firmware board
header (hardware/mao/design/gen_pinmap.py writes
components/mao_board/boards/main_a1/mao_board_pins.h) and docs/hardware/mao-pin-map.md.
Change a pin here and regenerate; never edit the generated outputs.

A1 = A0 moved to the M5 Gate C locked architecture (docs/hardware/m5_0_gate_c.md):
ESP32-S3-MINI-1-N8 (no PSRAM; IO22-25 and IO27-32 are not brought out), no I/O expander (its
lines are native GPIOs now), no touch, no microphone, no ambient-light sensor, no board-ID divider
(the revision is written to NVS at manufacturing).

Rules this table keeps (Gate A/B.1/C):
  * the face press and one dial line are RTC wake inputs, and the press is NOT on a strap
    (GPIO0 is only the BOOT pad now);
  * every default-off enable sits on GPIO1-21 or 38, which have no pull at reset (S3 datasheet
    v2.2 Table 2-1); GPIO39-42 (JTAG pads) and GPIO33/34 have reset pull-ups and carry only
    inputs or I2S lines whose consumer is held off;
  * nothing may pull GPIO45 high at reset (VDD_SPI = 1.8 V would stop the 3.3 V flash);
  * >= 4 spare pins: GPIO26, GPIO39, GPIO43/44 (UART0 service pads).

Routing swaps (2026-10-06, A1 layout): the first A1 layout could not route the board with the pin map as
committed: the face press (GPIO1) sat at the module's far corner from the switch, the dial's wake line (GPIO2)
on the side away from the Hall pair, the IR receiver's enable (GPIO16) away from its switch and the ToF
interrupt (GPIO38) away from the sensor. Swapped, every rule above kept: PRESS_N GPIO1 -> GPIO14 (RTC, not a
strap), HAPTIC_EN GPIO14 -> GPIO1 (reset-quiet enable pin), HALL_A GPIO2 -> GPIO21 (RTC: ext0 wake), the spare
GPIO21 -> GPIO2, AUX_PWR_EN GPIO16 -> GPIO38 (reset-quiet), TOF_INT_N GPIO38 -> GPIO16.

USB wake (design review 2026-10-06, finding 1): VBUS_SENSE on GPIO39 could not wake A1 from deep sleep (GPIO39 is
not an RTC pad). The VBUS divider now drives the gate of a 2N7002 whose drain, USB_PRESENT_N (100k pull-up to +3V3,
33 uA only while USB is present), is the spare RTC pad GPIO2: an ext1 any-low wake source. GPIO39 becomes a spare
with its own test pad.

Module pin numbers are ESP32-S3-MINI-1 pads (datasheet v1.7 Table 3-1).
"""

# (gpio, net, direction, function, notes)
#   direction: in / out / io / od (open-drain input) / analog
#   flags in notes: R = RTC GPIO (deep-sleep wake/hold), S = strapping pin
NATIVE = [
    (0,  None,          'nc',  'BOOT strap (pad only)', 'S R; 10k pull-up, TP BOOT: the fixture holds it low for download. Nothing else on it'),
    (1,  'HAPTIC_EN',   'out', 'haptic driver enable', 'R; 100k pull-down (+ the DRV2605L\'s internal 2M): off'),
    (2,  'USB_PRESENT_N', 'od', 'USB present, active low (VBUS divider -> 2N7002 inverter)', 'R; 100k pull-up at the FET drain (33 uA only while USB is present, 0 uA on battery); deep-sleep wake (ext1, any-low; armed only while it idles high)'),
    (3,  None,          'nc',  'JTAG-source strap', 'S; NC (inert unless EFUSE_STRAP_JTAG_SEL is burnt, never on MAO)'),
    (4,  'IMU_INT1',    'od',  'IMU INT1: wake-on-motion (ICM-42670-P, open-drain, active low, latched)', 'R; 100k pull-up; deep-sleep wake (ext1, any-low)'),
    (5,  'HALL_FAST',   'out', 'Hall sensors: high = fast sampling, low = low-power', 'R; 100k pull-down: low-power from reset and in deep sleep'),
    (6,  'LCD_PWR_EN',  'out', 'display logic rail switch (TPS22916C)', 'R; switch\'s smart pull-down + 100k: panel unpowered from reset'),
    (7,  'LCD_RST_N',   'out', 'display reset', 'R; 100k pull-down: panel held in reset'),
    (8,  'LCD_BL',      'out', 'backlight current-sink reference (LEDC ~30 kHz)', 'R; 100k pull-down + divider: dark from reset'),
    (9,  'AMP_SD',      'out', 'amplifier SD_MODE (high = on, left channel)', 'R; 100k pull-down + the MAX98357A\'s internal 100k: silent from reset'),
    (10, 'LCD_CS',      'out', 'display chip select (FSPICS0 IO_MUX)', 'R'),
    (11, 'LCD_MOSI',    'out', 'display data (FSPID IO_MUX)', 'R; 22R series'),
    (12, 'LCD_SCLK',    'out', 'display clock (FSPICLK IO_MUX)', 'R; 22R series'),
    (13, 'LCD_DC',      'out', 'display data/command', 'R'),
    (14, 'PRESS_N',     'in',  'face-press switch (to GND)', 'R; 100k pull-up (draws only while pressed); deep-sleep wake (ext1, any-low)'),
    (15, 'TOF_XSHUT',   'out', 'proximity sensor shutdown (low = off)', 'R; 100k pull-down: off'),
    (16, 'TOF_INT_N',   'od',  'proximity GPIO1: threshold interrupt', 'R; 10k pull-up (ST application circuit)'),
    (17, 'IR_TX',       'out', 'IR LED driver gate (RMT carrier)', 'R; 100k pull-down: LEDs off at reset'),
    (18, 'CHG_CE_N',    'out', 'charger /CE: high pauses charging (firmware thermal limit, cell 0-45 C)', 'R; 100k pull-down: charging enabled from reset'),
    (19, 'USB_DN',      'io',  'USB D-', 'native USB-Serial/JTAG; 22R series + DNP 10 pF'),
    (20, 'USB_DP',      'io',  'USB D+', 'native USB-Serial/JTAG; 22R series + DNP 10 pF'),
    (21, 'HALL_A',      'in',  'ring dial channel A', 'R; push-pull from the Hall latch; deep-sleep wake (ext0, armed at the opposite level)'),
    (26, None,          'nc',  'spare', 'free on the -N8 (SPICS1 only on -N4R2); test pad'),
    (33, 'CHG_STAT1',   'od',  'charger STAT1 (open-drain)', '10k pull-up; both STAT pins high-Z on battery: 0 uA'),
    (34, 'CHG_STAT2',   'od',  'charger STAT2 (open-drain)', '10k pull-up'),
    (35, 'LCD_TE',      'in',  'display tearing-effect output: frame sync', 'driven by the panel; isolated while the panel is off'),
    (36, 'IR_RX',       'in',  'IR receiver output (RMT)', 'receiver supply switched by AUX_PWR_EN; isolated while it is off. IMU INT2 is not wired on A1: INT1 carries wake-on-motion, the rest is polled'),
    (37, 'HALL_B',      'in',  'ring dial channel B', 'push-pull from the Hall latch'),
    (38, 'AUX_PWR_EN',  'out', 'IR receiver supply switch (TPS22916C on +3V3)', 'no reset pull; switch\'s smart pull-down + 100k: off'),
    (39, None,          'nc',  'spare', 'test pad (TP26); JTAG MTCK pad with a reset pull-up'),
    (40, 'AMP_BCLK',    'out', 'I2S bit clock to the amplifier', ''),
    (41, 'AMP_LRCLK',   'out', 'I2S word select', ''),
    (42, 'AMP_DIN',     'out', 'I2S data', ''),
    (43, 'UART_TX',     'out', 'UART0 TX (service / spare)', 'service pad; the ROM prints here at reset'),
    (44, 'UART_RX',     'in',  'UART0 RX (service / spare)', 'service pad'),
    (45, None,          'nc',  'VDD_SPI strap', 'S; NC with its internal pull-down: 3.3 V flash. Nothing may pull it high'),
    (46, None,          'nc',  'boot-mode strap', 'S; NC with its internal pull-down'),
    (47, 'I2C_SDA',     'io',  'I2C data (IMU, ToF, gauge, haptic)', '4.7k pull-up to +3V3; VDD_SPI/VDD3P3_CPU domain = 3.3 V on the N8'),
    (48, 'I2C_SCL',     'out', 'I2C clock', '4.7k pull-up to +3V3'),
]

# A1 has no I/O expander (its lines are native above).
EXPANDER = []

I2C_ADDRESSES = {
    0x29: 'VL53L4CD proximity (default)',
    0x36: 'MAX17048 fuel gauge',
    0x5A: 'DRV2605L haptic driver',
    0x68: 'ICM-42670-P IMU (AP_AD0 low)',
}

# MINI-1 pads that are GPIOs (Table 3-1): the checks below make sure every one is accounted for.
MODULE_GPIOS = list(range(0, 22)) + [26] + list(range(33, 49))
SPARES_MIN = 4


def native_by_net():
    return {net: gpio for gpio, net, *_ in NATIVE if net}


def expander_by_net():
    return {}


def _self_check():
    gpios = [g for g, *_ in NATIVE]
    assert sorted(gpios) == MODULE_GPIOS, 'every MINI-1 GPIO exactly once'
    nets = [n for _, n, *_ in NATIVE if n]
    assert len(nets) == len(set(nets)), 'a net on two pins'
    assert native_by_net().get('PRESS_N') not in (0, 3, 45, 46), 'press on a strap'
    for net in ('PRESS_N', 'HALL_A', 'IMU_INT1', 'USB_PRESENT_N'):
        assert native_by_net()[net] <= 21, net + ' must be an RTC GPIO (wake)'
    enables = ('HALL_FAST', 'LCD_PWR_EN', 'LCD_RST_N', 'LCD_BL', 'AMP_SD', 'HAPTIC_EN', 'TOF_XSHUT',
               'AUX_PWR_EN', 'IR_TX', 'CHG_CE_N')
    for net in enables:
        g = native_by_net()[net]
        assert 1 <= g <= 21 or g == 38, net + ' needs a pin without a reset pull'
    spare = [g for g, n, *_ in NATIVE if n is None and g not in (0, 3, 45, 46)]
    spare += [native_by_net()[n] for n in ('UART_TX', 'UART_RX')]
    assert len(spare) >= SPARES_MIN, 'fewer than %d spares' % SPARES_MIN


_self_check()
