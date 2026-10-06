"""MAO_MAIN A1 electrical capture: the single source for schematic, PCB netlist, BOM and CPL.

A1 = the A0 puck (round board, ring dial with two Hall latches, face press, VL53L4CD) moved to the
M5 Gate C locked architecture (docs/hardware/m5_0_gate_c.md, docs/hardware/mao-a1-report.md):
ESP32-S3-MINI-1-N8, BQ25185 charger, TPS62840 buck, ICM-42670-P, DRV2605L on the 3.3 V rail,
constant-current backlight sink, TPS22916C load switches. Body touch, the microphone, the light
sensor, the I/O expander and the board-ID divider are gone.

Pin tables of custom symbols are transcribed from the datasheets named beside them.
Net names follow ODD JOBS 105/11: every rail and every important signal is named.

Rails:
  VBUS       USB-C VBUS (5 V, hostile: TVS, charger OVP 18.5 V)
  BAT_RAW    battery connector + (before the measurement link)
  BAT_IN     after LINK_BAT, the 0R measurement link (ODD JOBS 110)
  VBAT       after the reverse-polarity P-FET: charger BAT, fuel gauge
  VSYS       charger power-path output: 4.5 V on USB, ~VBAT on battery (BATFET off below 3.0 V)
  REG_IN     after LINK_REG, the 0R link that measures the regulator and everything on +3V3
  +3V3       TPS62840 output, 3.2 V (CORE_3V3 of Gate C): MCU, sensors, haptic driver (always on)
  3V3_LCD    switched panel rail (TPS22916C)
  AUX_3V3    switched IR receiver rail (TPS22916C)
"""
import kicadlib
import pinmap
from model import Circuit, NC
from parts import Builder

c = Circuit()
b = Builder(c)
G = pinmap.native_by_net()       # GPIO numbers in notes come from the pin map, so they cannot drift

c.sheets = [
    ('power', 'Power', 'USB-C entry, charger with power path, battery, fuel gauge, 3.2 V buck, display rail'),
    ('compute', 'Compute', 'ESP32-S3-MINI-1 module, boot/reset, USB, I2C, service and test access'),
    ('interface', 'Interface', 'Round display, backlight current sink, ring dial, face press'),
    ('sense', 'Sense', 'IMU, proximity'),
    ('feedback', 'Feedback', 'Speaker amplifier, haptic driver, IR transmit and receive'),
]
c.root_notes = [
    'MAO_MAIN A1 - ODD JOBS / MAO round puck main board (A0 moved to the M5 Gate C architecture)',
    'GPIO0 = BOOT strap only: 10k pull-up and test pad TP8 BOOT (Tag-Connect pin 6). Hold TP8 low during a reset '
    '(TP7 RST, Tag-Connect pin 2, or USB with no cell) to enter download mode. The face press is GPIO%d, not a strap.' % G['PRESS_N'],
    'GPIO45 (VDD_SPI strap) and GPIO46 are NC with their internal pull-downs: nothing may pull GPIO45 high '
    '(VDD_SPI 1.8 V would stop the 3.3 V flash). GPIO3 (JTAG-source strap) is NC.',
    'Every enable has a hardware pull-down and sits on a pin without a reset pull (pinmap.py rules): all off at reset.',
    'Pin map: hardware/mao/design/pinmap.py (also generates the firmware header and docs/hardware/mao-pin-map.md).',
]

# --------------------------------------------------------------------------------------------
# Custom symbols (pinouts transcribed from datasheets)
# --------------------------------------------------------------------------------------------
L, R, T, B_ = 'L', 'R', 'T', 'B'


def pin(num, name, typ, side):
    return {'num': num, 'name': name, 'type': typ, 'side': side}


c.custom_symbols['MAO:BQ25185'] = kicadlib.make_ic_symbol('MAO:BQ25185', 'U', [
    # TI SLUSF65B table 4-1, WSON-10 DLH0010A; pad 11 = exposed thermal pad (to GND)
    pin(10, 'IN', 'power_in', L), pin(4, '~{CE}', 'input', L), pin(7, 'ILIM/VSET', 'passive', L),
    pin(8, 'ISET', 'passive', L), pin(6, 'TS/MR', 'passive', L),
    pin(1, 'SYS', 'power_out', R), pin(2, 'BAT', 'power_in', R), pin(9, 'STAT1', 'open_collector', R),
    pin(3, 'STAT2', 'open_collector', R),
    pin(5, 'GND', 'power_in', B_), pin(11, 'EP', 'passive', B_),
], 'BQ25185 1-cell linear charger with power path, 1 A, I2C-free, WSON-10')

c.custom_symbols['MAO:TPS62840'] = kicadlib.make_ic_symbol('MAO:TPS62840', 'U', [
    # TI SLVSEC6D pin functions, VSON-HR-8 DLC (DLC0008B, no exposed pad)
    pin(2, 'VIN', 'power_in', L), pin(4, 'EN', 'input', L), pin(3, 'MODE', 'input', L), pin(6, 'STOP', 'input', L),
    pin(7, 'SW', 'passive', R), pin(8, 'VOS', 'input', R), pin(5, 'VSET', 'passive', R),
    pin(1, 'GND', 'power_in', B_),
], 'TPS62840 750 mA buck, 60 nA Iq, 100 % mode, VSET resistor-programmed, VSON-HR-8')

c.custom_symbols['MAO:MAX17048'] = kicadlib.make_ic_symbol('MAO:MAX17048', 'U', [
    # ADI MAX17048/MAX17049 datasheet pin description, TDFN-8 2x2 (T822)
    pin(3, 'VDD', 'power_in', L), pin(2, 'CELL', 'input', L), pin(1, 'CTG', 'passive', L),
    pin(6, 'QSTRT', 'input', L),
    pin(8, 'SDA', 'bidirectional', R), pin(7, 'SCL', 'input', R), pin(5, '~{ALRT}', 'open_collector', R),
    pin(4, 'GND', 'power_in', B_), pin(9, 'EP', 'passive', B_),
], 'MAX17048 1-cell fuel gauge, ModelGauge, I2C 0x36')

c.custom_symbols['MAO:TPS22916C'] = kicadlib.make_ic_symbol('MAO:TPS22916C', 'U', [
    # TI TPS22916 (SLVSDO5) pin functions, DSBGA-4 YFP: A1 VOUT, A2 VIN, B1 GND, B2 ON
    pin('A2', 'VIN', 'power_in', L), pin('B2', 'ON', 'input', L),
    pin('A1', 'VOUT', 'power_out', R),
    pin('B1', 'GND', 'power_in', B_),
], 'TPS22916C 2 A load switch, slow rise, 150 ohm quick output discharge, 10 nA off, DSBGA-4')

c.custom_symbols['MAO:ICM-42670-P'] = kicadlib.make_ic_symbol('MAO:ICM-42670-P', 'U', [
    # TDK DS-000451 r1.0 table 9, LGA-14 2.5 x 3.0
    pin(8, 'VDD', 'power_in', L), pin(5, 'VDDIO', 'power_in', L), pin(12, 'AP_CS', 'input', L),
    pin(1, 'AP_AD0', 'input', L), pin(7, 'FSYNC', 'input', L),
    pin(14, 'AP_SDA', 'bidirectional', R), pin(13, 'AP_SCL', 'input', R), pin(4, 'INT1', 'open_collector', R),
    pin(9, 'INT2', 'open_collector', R),
    pin(2, 'RESV', 'no_connect', T), pin(3, 'RESV', 'no_connect', T), pin(10, 'RESV', 'no_connect', T),
    pin(11, 'RESV', 'no_connect', T),
    pin(6, 'GND', 'power_in', B_),
], 'ICM-42670-P 6-axis IMU, I2C 0x68 with AP_AD0 low')

# --------------------------------------------------------------------------------------------
# POWER
# --------------------------------------------------------------------------------------------
b.at('power', 'USB-C ENTRY',
     'Sink only: 5.1k Rd on CC1/CC2 (ODD JOBS 25): Default USB power, 500 mA. ESD at the connector (119): TPD2E2U06 '
     'on D+/D-, SMF15A on VBUS. Shell to GND (plastic enclosure, 26). The 22R series resistors sit at the module '
     '(compute sheet). VBUS_SENSE 100k/150k (5.0 V -> 3.0 V, 0 uA without USB) drives the gate of {Q2}: its drain is '
     'USB_PRESENT_N, active low, with a 100k pull-up to +3V3 ({R97}, 33 uA only while USB is present), on the RTC pad '
     'GPIO%d, so plugging USB wakes A1 from deep sleep (ext1 any-low; design review 2026-10-06). 2N7002: VGS(th) 2.5 V '
     'max at 250 uA against >= 2.85 V of gate at the USB minimum 4.75 V; VGS 20 V max, so a faulty source up to 33 V '
     'leaves the gate intact.' % G['USB_PRESENT_N'])
b.part('J1', 'Connector:USB_C_Receptacle_USB2.0_16P', 'Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12',
       'USB-C', {'A1': 'GND', 'A12': 'GND', 'B1': 'GND', 'B12': 'GND', 'A4': 'VBUS', 'A9': 'VBUS', 'B4': 'VBUS',
                 'B9': 'VBUS', 'A5': 'USB_CC1', 'B5': 'USB_CC2', 'A6': 'USB_C_DP', 'B6': 'USB_C_DP',
                 'A7': 'USB_C_DN', 'B7': 'USB_C_DN', 'A8': NC, 'B8': NC, 'SH': 'GND'},
       lcsc='C165948', mpn='TYPE-C-31-M-12', mfr='Korean Hroparts', note='USB 2.0 16P receptacle, 4 THT shell legs')
b.R('R1', '5.1k', 'USB_CC1', 'GND', note='Rd: sink, default USB power')
b.R('R2', '5.1k', 'USB_CC2', 'GND', note='Rd')
b.part('D1', 'Device:D_Zener', 'Diode_SMD:D_SMF', 'SMF15A', {'1': 'VBUS', '2': 'GND'}, lcsc='C123802', mpn='SMF15A',
       mfr='MDD', note='VBUS TVS: 15 V standoff, under the charger 18.5 V OVP trip in normal use, clamps surges')
b.part('U1', 'Power_Protection:TPD2E2U06DRL', 'Package_TO_SOT_SMD:SOT-553', 'TPD2E2U06',
       {'3': 'USB_C_DN', '5': 'USB_C_DP', '4': 'GND', '1': NC, '2': NC}, lcsc='C1972959', mpn='TPD2E2U06DRLR', mfr='TI',
       note='2-ch ESD, 1.5 pF, IEC 61000-4-2 level 4; the two identical channels take D- (pin 3) and D+ (pin 5) so the '
       'pair passes through them in the order the module wants (design review 2026-10-06)')

b.at('power', 'CHARGER',
     'BQ25185 linear charger with power path: runs MAO from USB while charging, SYS 4.5 V on USB. '
     'ILIM/VSET {R4} 18k = 4.2 V, 500 mA input limit (SLUSF65B table 6-1). ISET {R3} 1.43k 1%% = 300 AOhm / 1.43k '
     '= 210 mA charge (199-220 mA), as A0 (the LP503035 cell allows 250 mA, 0.5C). TS/MR to the cell\'s 10k NTC '
     '(DNP 10k fallback {R11}); the fixture enters factory (ship) mode by holding TS/MR low 10 s with USB present '
     '(TP TSMR). /CE from GPIO%d (CHG_CE_N, 100k pull-down: charging on from reset): firmware pauses charging outside '
     'the cell\'s 0-45 C. STAT1/STAT2 open-drain, 10k pull-ups to +3V3, to GPIO%d/%d: high-Z on battery, 0 uA. '
     'BATFET off below 3.0 V (BUVLO): the hardware floor that replaces A0\'s UVLO divider. '
     'Caps per TI: IN and BAT 2.2 uF 25 V 0402 (>= 1 uF left after DC bias, SLUSF65B 7.2.2.3), SYS 10 uF 25 V.' % (G['CHG_CE_N'], G['CHG_STAT1'], G['CHG_STAT2']))
b.part('U2', 'MAO:BQ25185', 'Package_DFN_QFN:Texas_DLH0010A_WSON-10-1EP_2.2x2mm_P0.4mm_EP0.9x1.5mm',
       'BQ25185DLHR', {'10': 'VBUS', '1': 'VSYS', '2': 'VBAT', '9': 'CHG_STAT1', '3': 'CHG_STAT2', '4': 'CHG_CE_N',
                       '5': 'GND', '11': 'GND', '6': 'BAT_NTC', '7': 'CHG_ILIM', '8': 'CHG_ISET'},
       lcsc='C19725033', mpn='BQ25185DLHR', mfr='TI',
       note='1 A linear charger, power path, 3.0 V battery UVLO, factory mode on TS/MR')
b.C('C1', '2.2u', 'VBUS', voltage='25V', note='charger IN: 2.2 uF 25 V 0402 keeps >= 1 uF at 5 V DC bias (SLUSF65B 7.2.2.3)')
b.C('C2', '10u', 'VSYS', pkg='0603', voltage='25V', note='charger SYS (TI: 10 uF, 25 V rating)')
b.C('C3', '2.2u', 'VBAT', voltage='25V', note='charger BAT: 2.2 uF 25 V 0402 keeps >= 1 uF at 4.2 V DC bias (SLUSF65B 7.2.2.3)')
b.R('R3', '1.43k', 'CHG_ISET', 'GND', note='ISET: 300 AOhm / 1.43k = 210 mA (199-220 mA with KISET 285-315); cell max 250 mA')
b.R('R4', '18k', 'CHG_ILIM', 'GND', note='ILIM/VSET: 4.2 V, 500 mA input limit (SLUSF65B table 6-1)')

b.at('power', 'BATTERY',
     'JST SH 3-pin: 1 = BAT-, 2 = NTC, 3 = BAT+ (same order as A0 and KINO J1100, ODD JOBS 41/167). Kept from A0: the '
     'Gate C JST PH (2 A) is 4.8 mm tall and does not fit the 3.2 mm zone over the cell (owner decision, A1 report). '
     'Cell MUST carry its own protection PCM and a 10k NTC. {R10} = LINK_BAT, 0R to measure battery current (110). '
     '{Q1} blocks a reversed cell (51); its drop is in the low-battery budget (A1 report). '
     '{R11} fits only for cells without NTC.')
b.part('J2', 'Connector_Generic:Conn_01x03', 'Connector_JST:JST_SH_SM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal',
       'BAT', {'1': 'GND', '2': 'BAT_NTC', '3': 'BAT_RAW'}, lcsc='C7430445', mpn='ZX-SH1.0-3PWT (JST SM03B-SRSS-TB compatible)',
       mfr='Megastar', note='battery: 1 BAT-, 2 NTC, 3 BAT+. 2.9 mm tall: fits over the cell. 1 A per contact')
b.R('R10', '0R', 'BAT_RAW', 'BAT_IN', pkg='1206', note='LINK_BAT: remove to insert an ammeter (ODD JOBS 110)')
b.part('Q1', 'Transistor_FET:AO3401A', 'Package_TO_SOT_SMD:SOT-23', 'AO3401A',
       {'1': 'BAT_RPP_G', '3': 'BAT_IN', '2': 'VBAT'}, lcsc='C15127', mpn='AO3401A', mfr='AOS',
       note='reverse-polarity P-FET: drain to cell, source to VBAT, gate to GND; conducts both ways when the cell is right')
b.R('R12', '10k', 'BAT_RPP_G', 'GND', note='Q1 gate')
b.R('R11', '10k', 'BAT_NTC', 'GND', note='DNP with an NTC cell; fit for a 2-wire cell', dnp=True)

b.at('power', 'FUEL GAUGE', 'MAX17048 ModelGauge on VBAT, I2C 0x36. ALRT not connected: firmware polls SOC (Gate C).')
b.part('U3', 'MAO:MAX17048', 'Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm', 'MAX17048G+T10',
       {'3': 'VBAT', '2': 'VBAT', '1': 'GND', '6': 'GND', '8': 'I2C_SDA', '7': 'I2C_SCL', '5': NC,
        '4': 'GND', '9': 'GND'}, lcsc='C2682616', mpn='MAX17048G+T10', mfr='Analog Devices',
       note='fuel gauge; CTG and QSTRT to GND, ALRT unused')
b.C('C4', '100n', 'VBAT', note='gauge VDD')

b.at('power', 'CORE 3V3 BUCK',
     'TPS62840 from REG_IN (= VSYS through LINK_REG {R40}): 750 mA, 60 nA Iq, PFM (MODE low), STOP low, EN to VIN. '
     'VSET {R13} 102k 1 % = 3.2 V (SLVSEC6D table 1, TPS62840: 97.92-106.08k): 0.1 V under the panel VCI 3.3 V '
     'maximum. Below ~3.25 V in it runs in 100 % mode (output = input - I x (RDS_on + DCR)). '
     'L 2.2 uH DFE201612E-2R2M, CIN 4.7 uF, COUT 10 uF; with the loads the +3V3 rail stays under 40 uF effective. '
     'No UVLO divider: the charger\'s 3.0 V BATFET cut-off and a firmware floor replace it.')
b.part('U4', 'MAO:TPS62840', 'MAO:TI_DLC0008B_VSON-HR-8_1.5x2mm_P0.5mm', 'TPS62840DLCR',
       {'2': 'REG_IN', '4': 'REG_IN', '3': 'GND', '6': 'GND', '7': 'REG_SW', '8': '+3V3', '5': 'REG_VSET',
        '1': 'GND'}, lcsc='C2071859', mpn='TPS62840DLCR', mfr='TI', note='3.2 V buck, 750 mA')
b.part('L1', 'Device:L', 'Inductor_SMD:L_Murata_DFE201610P', '2.2uH', {'1': 'REG_SW', '2': '+3V3'},
       lcsc='C337893', mpn='DFE201612E-2R2M=P2', mfr='Murata', note='2.2 uH, 116 mOhm, 2016, 1.2 mm')
b.C('C5', '4.7u', 'REG_IN', pkg='0603', note='buck CIN (TI: 4.7 uF), at VIN/GND')
b.C('C7', '10u', '+3V3', pkg='0603', note='buck COUT (TI: 10 uF)')
b.R('R13', '102k', 'REG_VSET', 'GND', note='VSET: 3.2 V (table 1, E96, 1 %); no capacitance on VSET (< 100 pF)')

b.at('power', 'DISPLAY RAIL',
     'TPS22916C switches 3V3_LCD (panel logic and the backlight op-amp; the LEDs run from VSYS). ON from GPIO%d with '
     'its 750k smart pull-down and a 100k: off until firmware enables it. Slow rise (0.9 ms at 3.6 V), 150 ohm '
     'quick output discharge: the rail really reaches 0 V when off. 10 nA off.' % G['LCD_PWR_EN'])
b.part('U5', 'MAO:TPS22916C', 'Package_BGA:Texas_PicoStar_BGA-4_0.758x0.758mm_Layout2x2_P0.4mm', 'TPS22916CYFPR',
       {'A2': '+3V3', 'B1': 'GND', 'B2': 'LCD_PWR_EN', 'A1': '3V3_LCD'},
       lcsc='C2680319', mpn='TPS22916CYFPR', mfr='TI', note='load switch, slow rise, QOD')
b.C('C10', '1u', '3V3_LCD', note='switch output / panel VCI bulk (Gate C: 1 uF + 0.1 uF)')
b.R('R17', '100k', 'LCD_PWR_EN', 'GND', note='default off')

# --------------------------------------------------------------------------------------------
# COMPUTE
# --------------------------------------------------------------------------------------------
b.at('compute', 'MCU',
     'ESP32-S3-MINI-1-N8 (8 MB quad flash, no PSRAM, 85 C) on B.Cu at 6 o\'clock, antenna over the board-edge notch '
     '(no copper beneath, Espressif keep-out). 22 uF + 100 nF at 3V3 pad 3. EN: 10k/1 uF RC. GPIO0 = BOOT pad only. '
     'GPIO3/45/46 NC. Spares GPIO26/39 on test pads, GPIO43/44 on the Tag-Connect.')
mod = {'GND': 'GND', '3V3': '+3V3', 'EN': 'MCU_EN'}
names = {0: 'IO0', 43: 'TXD0', 44: 'RXD0', 19: 'USB_D-', 20: 'USB_D+'}
special = {0: 'BOOT', 26: 'GPIO26', 39: 'GPIO39'}    # pads without a firmware signal: named for their test pads
for gpio, net, d, fn, note in pinmap.NATIVE:
    name = names.get(gpio, 'IO%d' % gpio)
    mod[name] = net if net else special.get(gpio, NC)
b.part('U6', 'RF_Module:ESP32-S3-MINI-1', 'RF_Module:ESP32-S2-MINI-1', 'ESP32-S3-MINI-1-N8', mod,
       lcsc='C2913206', mpn='ESP32-S3-MINI-1-N8', mfr='Espressif',
       note='8 MB flash, no PSRAM, -40..85 C; S2-MINI-1 land = S3-MINI-1 Fig 11-1 (Gate C audit)')
b.C('C11', '22u', '+3V3', pkg='0603', note='module bulk (MINI-1 Fig 9-1)')
b.C('C12', '100n', '+3V3', note='module HF')
b.R('R18', '10k', '+3V3', 'MCU_EN', note='EN pull-up')
b.C('C13', '1u', 'MCU_EN', note='EN delay (10k x 1 uF)')
b.R('R19', '10k', '+3V3', 'BOOT', note='GPIO0 pull-up: normal boot; TP8 BOOT low at reset = download')

b.at('compute', 'DEFAULTS',
     'Hardware pull-downs that hold every peripheral off from reset and in deep sleep (ODD JOBS 116, 117). '
     'Each sits at the pin it serves.')
b.R('R24', '100k', 'LCD_RST_N', 'GND', note='panel held in reset (GPIO%d)' % G['LCD_RST_N'])
b.R('R25', '100k', 'AMP_SD', 'GND', note='amplifier shut down (GPIO%d; plus its internal 100k)' % G['AMP_SD'])
b.R('R26', '100k', 'HAPTIC_EN', 'GND', note='haptic driver off (GPIO%d; plus its internal 2M)' % G['HAPTIC_EN'])
b.R('R27', '100k', 'TOF_XSHUT', 'GND', note='proximity sensor off (GPIO%d)' % G['TOF_XSHUT'])
b.R('R28', '100k', 'AUX_PWR_EN', 'GND', note='IR receiver switch off (GPIO%d; plus its 750k smart pull-down)' % G['AUX_PWR_EN'])

b.at('compute', 'I2C', 'One 400 kHz bus, four devices (IMU, ToF, gauge, haptic), all on always-on rails. '
                       '4.7k pull-ups to +3V3 (Gate C), one pair only.')
b.R('R30', '4.7k', '+3V3', 'I2C_SDA')
b.R('R31', '4.7k', '+3V3', 'I2C_SCL')

b.at('compute', 'SERVICE',
     'Tag-Connect TC2030-NL (no part fitted): 1 GND, 2 EN, 3 TXD0, 4 3V3, 5 RXD0, 6 GPIO0 BOOT - recovery '
     'without USB (ODD JOBS 33-35, 157). Probe pads: rails beside the power section, comms and wake lines in one '
     'field, switched rails at their sources (36-39).')
b.part('J3', 'Connector:TC2030', 'Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical', 'TAG',
       {'1': 'GND', '2': 'MCU_EN', '3': 'UART_TX', '4': '+3V3', '5': 'UART_RX', '6': 'BOOT'},
       mpn='PCB feature, no part', note='Tag-Connect TC2030-IDC-NL footprint', **{'Exclude from BOM': 'yes'})
TEST_PADS = [('TP1', 'GND', 'GND'), ('TP2', 'GND', 'GND'), ('TP3', '+3V3', '3V3'),
             ('TP4', 'VSYS', 'SYS'), ('TP5', 'VBAT', 'BAT'), ('TP6', 'VBUS', 'VBUS'),
             ('TP7', 'MCU_EN', 'RST'), ('TP8', 'BOOT', 'BOOT'), ('TP9', 'I2C_SDA', 'SDA'),
             ('TP10', 'I2C_SCL', 'SCL'), ('TP13', '3V3_LCD', 'LCDV'),
             ('TP15', 'IR_RX_VCC', 'IRV'), ('TP16', 'GND', 'GND'),
             # A1 (Gate C fixture pads): backlight reference, amplifier enable, frame sync, both wake inputs,
             # charger factory mode and /CE, USB presence (the third wake input), the two spare GPIOs
             ('TP17', 'LCD_BL', 'BL'), ('TP18', 'AMP_SD', 'AMP'), ('TP19', 'LCD_TE', 'TE'),
             ('TP20', 'PRESS_N', 'PRS'), ('TP21', 'HALL_A', 'HLA'), ('TP22', 'BAT_NTC', 'TSMR'),
             ('TP23', 'CHG_CE_N', 'CE'), ('TP24', 'USB_PRESENT_N', 'USB'), ('TP25', 'GPIO26', 'IO26'),
             ('TP26', 'GPIO39', 'IO39')]
for ref, net, label in TEST_PADS:
    # switched rails (TP13, TP15): the fixture proves each one really switches; signal pads 1.0 mm, rails and
    # supplies 1.2 mm (ODD JOBS 37: 1-1.5 mm)
    b.TP(ref, net, label, size='1.2' if label in ('GND', '3V3', 'SYS', 'BAT', 'VBUS') else '1.0')

for i in (1, 2, 3):
    b.part('FID%d' % i, 'Mechanical:Fiducial', 'Fiducial:Fiducial_1mm_Mask2mm', 'FID', {},
           mpn='PCB feature, no part', note='assembly fiducial, top', **{'Exclude from BOM': 'yes'})
for i, side in ((4, 'bottom'), (5, 'bottom'), (6, 'bottom')):
    b.part('FID%d' % i, 'Mechanical:Fiducial', 'Fiducial:Fiducial_1mm_Mask2mm', 'FID', {},
           mpn='PCB feature, no part', note='assembly fiducial, ' + side, **{'Exclude from BOM': 'yes'})
for i, (fp, what) in enumerate([('MountingHole:MountingHole_2.2mm_M2', 'M2 screw to the top chassis (heat-set insert)'),
                                ('MountingHole:MountingHole_2.2mm_M2', 'M2 screw to the top chassis (heat-set insert)'),
                                ('MountingHole:MountingHole_2mm', 'locating peg, plastic only (antenna side, ODD JOBS 69)')], 1):
    b.part('H%d' % i, 'Mechanical:MountingHole', fp, 'MOUNT', {}, mpn='PCB feature, no part', note=what,
           **{'Exclude from BOM': 'yes'})

# --------------------------------------------------------------------------------------------
# Custom symbols for the remaining parts
# --------------------------------------------------------------------------------------------
c.custom_symbols['MAO:DRV5012'] = kicadlib.make_ic_symbol('MAO:DRV5012', 'U', [
    # TI SLVSDD5 pin functions, X2SON-4 DMR; thermal pad is a no-connect, tied to GND
    pin(1, 'VCC', 'power_in', L), pin(4, 'SEL', 'input', L),
    pin(3, 'OUT', 'output', R),
    pin(2, 'GND', 'power_in', B_), pin(5, 'PAD', 'passive', B_),
], 'DRV5012 low-power bipolar Hall latch, SEL selects 20 Hz / 2.5 kHz sampling')

c.custom_symbols['MAO:MAX98357A'] = kicadlib.derive_symbol(
    'Audio:MAX98357A', 'MAO:MAX98357A', value='MAX98357A',
    pin_types={'17': 'passive'})   # exposed pad: not internally connected, soldered to GND

# Panel pin k lands on connector pad 19 - k: J301 sits on B.Cu (mirrored), and the tail keeps its lateral order
# through every fold, so panel pin 1 meets the connector end at 12 o'clock, which is pad 18 on the flipped part.
PANEL_PINS = [(1, 'TP_INT', L), (2, 'TP_SDA', L), (3, 'TP_SCL', L), (4, 'TP_RST', L), (5, 'TP_GND', L),
              (6, 'TP_VDD', L), (7, 'VLED+', R), (8, 'VLED-', R), (9, 'GND', R), (10, 'CS', R), (11, 'SCL', R),
              (12, 'SDA', R), (13, 'RS', R), (14, 'TE', R), (15, '~{RESET}', R), (16, 'VCI', R), (17, 'NC', R),
              (18, 'GND', R)]
c.custom_symbols['MAO:ROUND_LCD_FPC18'] = kicadlib.make_ic_symbol('MAO:ROUND_LCD_FPC18', 'J', [
    # 18-pin round-panel standard (Winstar WF0128BTYAA4DNN0 / -DNF10 pin definition): 1-6 the touch
    # controller of touch variants (NC on plain panels), 7-18 the GC9A01 panel. Symbol pin = connector pad.
    *[pin(19 - k, '%s/P%d' % (name, k), 'passive', side) for k, name, side in PANEL_PINS],
    pin('MP', 'MP', 'passive', B_),
], 'FPC connector for 1.28" round GC9A01 panels with the 18-pin 0.5 mm tail')

# --------------------------------------------------------------------------------------------
# INTERFACE
# --------------------------------------------------------------------------------------------
b.at('interface', 'DISPLAY',
     'Plug-in display: 1.28" round GC9A01 panel (Winstar WF0128BTYAA4DNN0, 240x240 IPS) on an 18-pin 0.5 mm '
     'FPC tail, folded under the panel into a 1.0 mm high back-flip connector with top and bottom contacts, '
     'so any panel of this 18-pin standard plugs in either way up and can be swapped without solder. '
     'Pins 1-6 serve the touch controller of touch variants: not wired, TP_GND to GND. TE to GPIO%d for '
     'tear-free frames. 22R on SCLK/MOSI damps the edges (ODD JOBS 29). ' % G['LCD_TE'] +
     'Panel logic on the switched 3V3_LCD rail, LEDs from VSYS through the current sink; RST held '
     'low by 100k until firmware. Every line to the panel is low or Hi-Z while 3V3_LCD is off (firmware, Gate C). '
     "The stock 70 mm tail lies as one loop under the panel, drops through the board slot at 9 o'clock and "
     'plugs into J301 on B.Cu.')
b.part('J301', 'MAO:ROUND_LCD_FPC18', 'MAO:HDGC_0.5K-HX-18PWB_1x18-1MP_P0.5mm_Horizontal', 'LCD',
       dict({str(19 - k): net for k, net in ((1, NC), (2, NC), (3, NC), (4, NC), (5, 'GND'), (6, NC), (7, 'VSYS'),
                                             (8, 'LCD_BL_K'), (9, 'GND'), (10, 'LCD_CS'), (11, 'LCD_SCLK_P'),
                                             (12, 'LCD_MOSI_P'), (13, 'LCD_DC'), (14, 'LCD_TE'), (15, 'LCD_RST_N'),
                                             (16, '3V3_LCD'), (17, NC), (18, 'GND'))}, MP='GND'),
       lcsc='C2919497', mpn='0.5K-HX-18PWB', mfr='HDGC',
       note='0.5 mm 18P FPC connector, 1.0 mm high, back flip, top and bottom contacts, on B.Cu: panel pin k on pad '
       '19-k. Panel pinout: 1 TP_INT, 2 TP_SDA, 3 TP_SCL, 4 TP_RST, 5 TP_GND, 6 TP_VDD, 7 VLED+, 8 VLED-, 9 GND, '
       '10 CS, 11 SCL, 12 SDA, 13 RS, 14 TE, 15 RESET, 16 VCI, 17 NC, 18 GND')
b.R('R301', '22R', 'LCD_SCLK', 'LCD_SCLK_P', note='series damping, at the module')
b.R('R302', '22R', 'LCD_MOSI', 'LCD_MOSI_P', note='series damping, at the module')
b.C('C301', '100n', '3V3_LCD', note='panel VCI HF, at the land')

b.at('interface', 'RING DIAL',
     'Two DRV5012 Hall latches under the ring\'s 30-pole ferrite strip, 6 deg apart (half a pole = 90 deg electrical): '
     'A/B quadrature identical to the LCDkit EC11 (2 transitions per detent, 30 detents). HALL_A on GPIO%d (RTC: '
     'deep-sleep wake, ext0 armed at the opposite level), HALL_B on GPIO%d. HALL_FAST (GPIO%d) low = 20 Hz '
     'sampling, 1.3 uA (sleep); high = 2.5 kHz (awake); 100k pull-down.' % (G['HALL_A'], G['HALL_B'], G['HALL_FAST']))
b.part('U301', 'MAO:DRV5012', 'Package_SON:Texas_X2SON-4-1EP_1.1x1.4mm_P0.5mm_EP0.8x0.6mm', 'DRV5012AEDMRR',
       {'1': '+3V3', '4': 'HALL_FAST', '3': 'HALL_A', '2': 'GND', '5': 'GND'},
       lcsc='C2655038', mpn='DRV5012AEDMRR', mfr='TI', note='ring channel A (wake)')
b.part('U302', 'MAO:DRV5012', 'Package_SON:Texas_X2SON-4-1EP_1.1x1.4mm_P0.5mm_EP0.8x0.6mm', 'DRV5012AEDMRR',
       {'1': '+3V3', '4': 'HALL_FAST', '3': 'HALL_B', '2': 'GND', '5': 'GND'},
       lcsc='C2655038', mpn='DRV5012AEDMRR', mfr='TI', note='ring channel B')
b.C('C303', '100n', '+3V3', note='U301 VCC')
b.C('C304', '100n', '+3V3', note='U302 VCC')
b.R('R306', '100k', 'HALL_FAST', 'GND', note='low-power sampling until firmware')

b.at('interface', 'FACE PRESS',
     'ALPS SKQG on F.Cu at the centre, under the panel: the floating face presses it. PRESS_N on GPIO%d (RTC: '
     'deep-sleep wake, ext1 any-low), not a strap. 100k pull-up draws only while pressed; DNP 1 nF makes an '
     'optional 100 us RC.' % G['PRESS_N'])
b.part('SW301', 'Switch:SW_Push', 'Button_Switch_SMD:SW_SPST_SKQG_WithStem', 'SKQGADE010',
       {'1': 'PRESS_N', '2': 'GND'}, lcsc='C116647', mpn='SKQGADE010', mfr='ALPS', note='2.55 N, 0.25 mm travel')

b.at('interface', 'BACKLIGHT',
     'Constant-current low-side sink from VSYS (Gate C): LCD_BL (GPIO%d, LEDC ~30 kHz) -> 32.4k / 1.0k -> BL_REF '
     '(0 / 95.8 mV at 3.2 V: 3.2 V x 1.0 / 33.4); TLV9061 on 3V3_LCD (0 uA in deep sleep) drives DMG2302UKQ so that BL_SENSE follows '
     'BL_REF: Rs 3.3 ohm 1%% = 29.0 mA at 100 %% (Gate C 30 mA at 3.3 V). Regulates for VSYS >= Vf (3.4 V max) + '
     '0.1 V. Off from reset: LCD_BL 100k, BL_REF 1.0k, gate 100k to GND, op-amp unpowered with the panel. '
     'DNP 100 pF loop compensation (fit if the sink rings). Rs is re-set when Winstar\'s backlight data arrives.'
     % G['LCD_BL'])

# --------------------------------------------------------------------------------------------
# SENSE
# --------------------------------------------------------------------------------------------
b.at('sense', 'IMU',
     'ICM-42670-P at 0x68 (AP_AD0 low), I2C (AP_CS to VDDIO), FSYNC to GND, RESV pins open (DS-000451 table 9). '
     'INT1 (GPIO%d, RTC) = wake-on-motion: open-drain active-low, 100k pull-up. INT2 not wired (pin map). '
     'VDD 0.1 uF + 2.2 uF X7R, VDDIO 10 nF X7R (table 10). On F.Cu: +Z out of the face.' % G['IMU_INT1'])
b.part('U401', 'MAO:ICM-42670-P', 'Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y', 'ICM-42670-P',
       {'1': 'GND', '2': NC, '3': NC, '4': 'IMU_INT1', '5': '+3V3', '6': 'GND', '7': 'GND',
        '8': '+3V3', '9': NC, '10': NC, '11': NC, '12': '+3V3', '13': 'I2C_SCL', '14': 'I2C_SDA'},
       lcsc='C3288646', mpn='ICM-42670-P', mfr='TDK InvenSense', note='6-axis IMU, wake-on-motion')
b.C('C401', '100n', '+3V3', note='IMU VDD HF (X7R)')
b.C('C402', '10n', '+3V3', note='IMU VDDIO (10 nF X7R, table 10)')

b.at('sense', 'PROXIMITY',
     'VL53L4CD time-of-flight, 0-1.3 m, under the window border. XSHUT from GPIO%d, 100k pull-down (off by default); '
     'GPIO1 open-drain to GPIO%d with the 10k pull-up ST recommends. AVDD decoupled 4.7 uF + 100 nF per ST.'
     % (G['TOF_XSHUT'], G['TOF_INT_N']))
b.part('U402', 'Sensor_Distance:VL53L0CXV0DH1', 'OptoDevice:ST_VL53L0X', 'VL53L4CDV0DH/1',
       {'1': '+3V3', '2': 'GND', '3': 'GND', '4': 'GND', '5': 'TOF_XSHUT', '6': 'GND', '7': 'TOF_INT_N',
        '8': NC, '9': 'I2C_SDA', '10': 'I2C_SCL', '11': '+3V3', '12': 'GND'},
       lcsc='C3178291', mpn='VL53L4CDV0DH/1', mfr='ST', note='ToF; VL53L0X-family land, pin-identical')
b.C('C403', '100n', '+3V3', note='ToF AVDD')
b.C('C404', '4.7u', '+3V3', pkg='0603', note='ToF VCSEL bulk')
b.R('R401', '10k', '+3V3', 'TOF_INT_N', note='GPIO1 pull-up (ST: 10k)')

# --------------------------------------------------------------------------------------------
# FEEDBACK
# --------------------------------------------------------------------------------------------
b.at('feedback', 'SPEAKER AMP',
     'MAX98357A I2S class-D from VSYS (Gate C). SD_MODE driven directly from GPIO%d (AMP_SD): high = left channel, '
     'low = 0.6 uA shutdown; 100k pull-down plus the internal 100k: silent from reset (ODD JOBS 116). A0\'s 2.2k '
     'series resistor is gone: its datasheet reason (logic high above VDD) cannot occur, +3V3 <= VSYS by topology. '
     'I2S on GPIO%d/%d/%d. GAIN_SLOT open = 9 dB. Speaker: Same Sky CMS-150803-088S-X8 (15 x 8 x 3 mm, 8 ohm, '
     '0.8 W) in the base at 9 o\'clock; its own spring contacts press on two pads on B.Cu (LS501).'
     % (G['AMP_SD'], G['AMP_BCLK'], G['AMP_LRCLK'], G['AMP_DIN']))
b.part('U501', 'MAO:MAX98357A', 'Package_DFN_QFN:TQFN-16-1EP_3x3mm_P0.5mm_EP1.23x1.23mm_ThermalVias', 'MAX98357AETE+T',
       {'1': 'AMP_DIN', '2': NC, '3': 'GND', '4': 'AMP_SD', '5': NC, '6': NC, '7': 'VSYS', '8': 'VSYS',
        '9': 'SPK_P', '10': 'SPK_N', '11': 'GND', '12': NC, '13': NC, '14': 'AMP_LRCLK', '15': 'GND',
        '16': 'AMP_BCLK', '17': 'GND'},
       lcsc='C910544', mpn='MAX98357AETE+T', mfr='Analog Devices', note='3.2 W mono I2S class-D')
b.C('C501', '10u', 'VSYS', pkg='0603', note='amp bulk (ODD JOBS 15); with C503 the datasheet 10 uF + 0.1 uF')
b.C('C503', '100n', 'VSYS', note='amp HF')
b.part('LS501', 'Device:Speaker', 'MAO:SameSky_CMS-150803_SpringPads', 'CMS-150803-088S-X8',
       {'1': 'SPK_P', '2': 'SPK_N'}, mpn='CMS-150803-088S-X8', mfr='Same Sky',
       note='15 x 8 x 3 mm speaker, bought separately: its spring contacts press on these pads',
       **{'Exclude from BOM': 'yes'})

b.at('feedback', 'HAPTIC',
     'DRV2605L closed-loop LRA driver on +3V3 (Gate C: its SDA/SCL/EN/TRIG absolute maximum is VDD + 0.3 V, so it '
     'shares the always-on rail of the I2C pull-ups and is never supply-gated). VDD pin 10 and VDD/NC pin 6 to +3V3. '
     'I2C 0x5A, EN from GPIO%d with 100k pull-down (4 uA off). IN/TRIG low: playback by I2C. REG 1 uF, VDD 1 uF. '
     'LRA LD0832AA (235 Hz, 1.8 Vrms) glued to the base, leads to J501.' % G['HAPTIC_EN'])
b.part('U502', 'Driver:DRV2605LDGS', 'Package_SO:MSOP-10_3x3mm_P0.5mm', 'DRV2605LDGST',
       {'1': 'HAP_REG', '2': 'I2C_SCL', '3': 'I2C_SDA', '4': 'GND', '5': 'HAPTIC_EN', '6': '+3V3',
        '7': 'LRA_P', '8': 'GND', '9': 'LRA_N', '10': '+3V3'},
       lcsc='C425927', mpn='DRV2605LDGST', mfr='TI', note='haptic driver, auto-resonance (DGST: the DGSR part on a 250-piece reel)')
b.C('C504', '1u', '+3V3', note='haptic VDD (table 32: 1 uF)')
b.C('C505', '1u', 'HAP_REG', note='DRV2605L internal regulator (required, 1 uF)')
b.part('J501', 'Connector_Generic:Conn_01x02', 'MAO:WirePads_1x02_P2.5mm_1.0x1.8mm', 'LRA',
       {'1': 'LRA_P', '2': 'LRA_N'}, mpn='PCB feature: LD0832AA-0099F leads', note='LRA lead pads',
       **{'Exclude from BOM': 'yes'})

b.at('feedback', 'IR TRANSMIT',
     'Two side-emitting 940 nm LEDs at the back edge (B.Cu), each with its own 56R from VSYS (26-59 mA pulses at '
     '38 kHz, 33 %% duty: under the 65 mA rating at any VSYS up to 4.5 V and any Vf). AO3400A low side driven from '
     'GPIO%d through 100R; the 100k at the gate is also IR_TX\'s pull-down: dark at reset (117).' % G['IR_TX'])
b.part('D501', 'Device:LED', 'MAO:Everlight_IR12-21C_RightAngle_3x1mm', 'IR12-21C', {'1': 'IR_LED_K', '2': 'IR_LED_A1'},
       lcsc='C53672', mpn='IR12-21C/TR8', mfr='Everlight', note='940 nm side-emitting IR LED')
b.part('D502', 'Device:LED', 'MAO:Everlight_IR12-21C_RightAngle_3x1mm', 'IR12-21C', {'1': 'IR_LED_K', '2': 'IR_LED_A2'},
       lcsc='C53672', mpn='IR12-21C/TR8', mfr='Everlight', note='940 nm side-emitting IR LED')
b.R('R501', '56R', 'VSYS', 'IR_LED_A1', pkg='0603', note='IR LED current')
b.R('R502', '56R', 'VSYS', 'IR_LED_A2', pkg='0603', note='IR LED current')
b.part('Q501', 'Transistor_FET:AO3400A', 'Package_TO_SOT_SMD:SOT-23', 'AO3400A',
       {'1': 'IR_TX_G', '2': 'GND', '3': 'IR_LED_K'}, lcsc='C20917', mpn='AO3400A', mfr='AOS', note='IR LED switch')
b.R('R503', '100R', 'IR_TX', 'IR_TX_G', note='gate resistor')
b.R('R504', '100k', 'IR_TX_G', 'GND', note='IR off at reset: gate and (through 100R) IR_TX pull-down')
b.C('C506', '10u', 'VSYS', pkg='0603', note='IR pulse reservoir')

b.at('feedback', 'IR RECEIVE',
     'IRM-H638T (as LCDkit), top-view, looking up through the window border, on GPIO%d. Powered from AUX_3V3, a '
     'TPS22916C on +3V3 switched by AUX_PWR_EN (GPIO%d), through 100R/4.7 uF only while listening (0.4 mA). '
     'OUT pull-up goes to the switched supply, never to +3V3, so an unpowered receiver is never back-powered and '
     'reads a defined low while it is off.' % (G['IR_RX'], G['AUX_PWR_EN']))
b.part('U503', 'Interface_Optical:IRM-H6xxT', 'OptoDevice:Everlight_IRM-H6xxT', 'IRM-H638T/TR2',
       {'1': 'GND', '2': 'GND', '3': 'IR_RX', '4': 'IR_RX_VCC'}, lcsc='C91447', mpn='IRM-H638T/TR2',
       mfr='Everlight', note='38 kHz IR receiver')
b.R('R505', '100R', 'AUX_3V3', 'IR_RX_VCC', note='receiver supply filter (datasheet application circuit)')
b.C('C507', '4.7u', 'IR_RX_VCC', pkg='0603', note='receiver supply filter')
b.R('R506', '10k', 'IR_RX_VCC', 'IR_RX', note='OUT pull-up to the switched supply (defined level, no back-powering)')

# --------------------------------------------------------------------------------------------
# A1 additions: parts added at the end of their sheets, so every part kept from A0 keeps its A0 number
# (procedures and the A0 docs name them). Each joins its block.
# --------------------------------------------------------------------------------------------
b.at('power', 'DISPLAY RAIL')
b.C('C90', '1u', '+3V3', note='TPS22916C VIN, at the ball (datasheet CIN)')
b.at('power', 'CHARGER')
b.R('R90', '100k', 'CHG_CE_N', 'GND', note='/CE default low: charging on from reset and in deep sleep')
b.R('R91', '10k', '+3V3', 'CHG_STAT1', note='STAT1 pull-up (SLUSF65B: 1-20k); off on battery: 0 uA')
b.R('R92', '10k', '+3V3', 'CHG_STAT2', note='STAT2 pull-up')
b.at('power', 'USB-C ENTRY')
b.R('R93', '100k', 'VBUS', 'VBUS_SENSE', note='VBUS_SENSE divider top')
b.R('R94', '150k', 'VBUS_SENSE', 'GND', note='divider bottom: 5.0 V -> 3.0 V at the inverter gate; holds it off without USB')
b.at('power', 'CORE 3V3 BUCK')
b.R('R40', '0R', 'VSYS', 'REG_IN', pkg='0603', note='LINK_REG: remove to measure the regulator and all of +3V3')

b.at('compute', 'MCU')
b.R('R95', '22R', 'USB_C_DP', 'USB_DP', note='USB D+ series, at the module (MINI-1 HDG)')
b.R('R96', '22R', 'USB_C_DN', 'USB_DN', note='USB D- series, at the module')
b.C('C95', '10p', 'USB_DP', note='USB D+ EMI option (Espressif HDG), DNP', dnp=True)
b.C('C96', '10p', 'USB_DN', note='USB D- EMI option, DNP', dnp=True)

b.at('interface', 'BACKLIGHT')
b.R('R311', '100k', 'LCD_BL', 'GND', note='LCD_BL default off through reset')
b.R('R312', '32.4k', 'LCD_BL', 'BL_REF', note='reference divider top: 3.2 V x 1.0 / 33.4 = 95.8 mV')
b.R('R313', '1.0k', 'BL_REF', 'GND', note='reference divider bottom')
b.C('C306', '100n', '3V3_LCD', note='TLV9061 supply')
b.part('U304', 'Amplifier_Operational:TLV9061xDBV', 'Package_TO_SOT_SMD:SOT-23-5', 'TLV9061IDBVR',
       {'1': 'BL_DRIVE', '2': 'GND', '3': 'BL_REF', '4': 'BL_FB', '5': '3V3_LCD'},
       lcsc='C398358', mpn='TLV9061IDBVR', mfr='TI', note='10 MHz RRIO op-amp, current-sink control')
b.R('R314', '1.0k', 'BL_SENSE', 'BL_FB', note='feedback isolation')
b.C('C307', '100p', 'BL_DRIVE', b='BL_FB', note='loop compensation, C0G, DNP: fit if the sink rings', dnp=True)
b.R('R315', '100R', 'BL_DRIVE', 'BL_GATE', note='gate isolation from the op-amp output')
b.part('Q302', 'Transistor_FET:DMG2302U', 'Package_TO_SOT_SMD:SOT-23', 'DMG2302UKQ',
       {'1': 'BL_GATE', '2': 'BL_SENSE', '3': 'LCD_BL_K'}, lcsc='C5224573', mpn='DMG2302UKQ-7', mfr='Diodes',
       note='backlight sink FET (pins 1 G, 2 S, 3 D); DMG2302UKQ-7 = the automotive-qualified DMG2302UK, same pinout')
b.R('R316', '100k', 'BL_GATE', 'GND', note='gate held off with the op-amp unpowered')
b.R('R317', '3.3R', 'BL_SENSE', 'GND', pkg='0603', note='Rs: 95.8 mV / 3.3 ohm = 29.0 mA LED current')
b.C('C305', '100n', 'VSYS', note='VLED+ bypass at J301 pin 7')
b.at('interface', 'FACE PRESS')
b.R('R318', '100k', '+3V3', 'PRESS_N', note='press pull-up (draws 32 uA only while pressed)')
b.C('C308', '1n', 'PRESS_N', note='optional RC with the 100k (100 us), DNP', dnp=True)

b.at('sense', 'IMU')
b.C('C408', '2.2u', '+3V3', pkg='0603', voltage='10V', note='IMU VDD bulk (2.2 uF X7R, table 10)')
b.R('R404', '100k', '+3V3', 'IMU_INT1', note='INT1 pull-up (open-drain, active low)')

b.at('feedback', 'IR RECEIVE')
b.part('U504', 'MAO:TPS22916C', 'Package_BGA:Texas_PicoStar_BGA-4_0.758x0.758mm_Layout2x2_P0.4mm', 'TPS22916CYFPR',
       {'A2': '+3V3', 'B1': 'GND', 'B2': 'AUX_PWR_EN', 'A1': 'AUX_3V3'},
       lcsc='C2680319', mpn='TPS22916CYFPR', mfr='TI', note='IR receiver supply switch')
b.C('C508', '1u', '+3V3', note='TPS22916C VIN')

# Design review 2026-10-06 (finding 1): USB presence on an RTC pad, so USB wakes A1 from deep sleep
b.at('power', 'USB-C ENTRY')
b.part('Q2', 'Transistor_FET:2N7002', 'Package_TO_SOT_SMD:SOT-23', '2N7002',
       {'1': 'VBUS_SENSE', '2': 'GND', '3': 'USB_PRESENT_N'}, lcsc='C8545', mpn='2N7002', mfr='CJ',
       note='USB presence inverter: gate from the VBUS divider, drain = USB_PRESENT_N (active low, RTC wake pad)')
b.R('R97', '100k', '+3V3', 'USB_PRESENT_N', note='USB_PRESENT_N pull-up: 33 uA only while USB is present')

# Parts removed: their numbers stay retired so later references do not move (A0 removals included).
RETIRED = {'Q301', 'R303', 'R304', 'R305', 'R107', 'C108',
           # A1: panel 4.7 uF bulk (the 1 uF at the switch output and C301 replace it, Gate C)
           'C302',
           # A1: BQ24073 TD/PGOOD, TPS63802 second COUT, FB and UVLO divider, TPS22919 QOD resistor
           'R105', 'R106', 'C107', 'R112', 'R113', 'R114', 'R115', 'C111',
           # A1: board-ID divider, TCA6408A and its parts, light-sensor/gauge ALRT pull-up
           'R203', 'R204', 'C204', 'U202', 'C205', 'R205', 'R206', 'R212',
           # A1: body touch (series R, ESD, electrodes, springs), AW9364 backlight driver
           'R307', 'R308', 'R309', 'R310', 'D301', 'D302', 'D303', 'D304', 'E301', 'E302', 'J302', 'J303',
           'U303',
           # A1: OPT3004, microphone
           'U403', 'C405', 'MK401', 'R402', 'C406', 'C407', 'R403',
           # A1: amplifier SD_MODE series resistor
           'R507'}
# A0 references that are still numbered the same way are pinned here so renumber() keeps them
KEEP_REF = {'C305': 'C305', 'R311': 'R311', 'R312': 'R312', 'R313': 'R313', 'C306': 'C306', 'U304': 'U304',
            'R314': 'R314', 'C307': 'C307', 'R315': 'R315', 'Q302': 'Q302', 'R316': 'R316', 'R317': 'R317',
            'R318': 'R318', 'C308': 'C308', 'C408': 'C408', 'R404': 'R404', 'U504': 'U504', 'C508': 'C508'}


# --------------------------------------------------------------------------------------------
# Reference designators: one hundred-block per sheet (power 1xx ... feedback 5xx), in capture order.
# Notes may name a part by its capture reference in braces, e.g. {R10}; renumber() rewrites them to
# the final reference, so a note can never drift from the board (ODD JOBS 189).
# --------------------------------------------------------------------------------------------
def renumber():
    import re
    base = {'power': 100, 'compute': 200, 'interface': 300, 'sense': 400, 'feedback': 500}
    keep = ('TP', 'FID', 'H')
    counters = {}
    final = {}
    pinned = set(KEEP_REF.values())
    for p in c.parts:
        prefix = ''.join(ch for ch in p.ref if ch.isalpha())
        if prefix in keep:
            continue
        if p.ref in KEEP_REF:
            final[p.ref] = KEEP_REF[p.ref]
            continue
        k = (p.sheet, prefix)
        while True:
            counters[k] = counters.get(k, 0) + 1
            ref = '%s%d' % (prefix, base[p.sheet] + counters[k])
            if ref not in RETIRED and ref not in pinned:
                break
        final[p.ref] = ref
    for p in c.parts:
        p.ref = final.get(p.ref, p.ref)
    refs = [p.ref for p in c.parts]
    assert len(refs) == len(set(refs)), 'duplicate references after renumbering'
    assert not set(refs) & RETIRED, ('retired reference reused', set(refs) & RETIRED)

    def resolve(text):
        def one(m):
            assert m.group(1) in final, ('note names an unknown part', m.group(1))
            return final[m.group(1)]
        return re.sub(r'\{([A-Z]+[0-9]+)\}', one, text)
    for k in list(c.blocks):
        c.blocks[k] = resolve(c.blocks[k])
    for p in c.parts:
        p.note = resolve(p.note)
        for f in list(p.fields):
            if isinstance(p.fields[f], str):
                p.fields[f] = resolve(p.fields[f])
    c.root_notes = [resolve(n) for n in c.root_notes]


renumber()
