"""MAO_MAIN A0 electrical capture: the single source for schematic, PCB netlist, BOM and CPL.

Pin tables of custom symbols are transcribed from the datasheets named beside them.
Net names follow ODD JOBS 105/11: every rail and every important signal is named.

Rails:
  VBUS       USB-C VBUS (5 V, hostile: TVS, charger OVP 6.6 V, 28 V abs max)
  BAT_RAW    battery connector + (before the measurement link)
  BAT_IN     after the 0R measurement link (ODD JOBS 110)
  VBAT       after the reverse-polarity P-FET: charger BAT, fuel gauge
  VSYS       charger power-path output: 4.4 V on USB, ~VBAT on battery
  +3V3       buck-boost output: MCU, sensors, expander (always on)
  3V3_LCD    switched display rail (TPS22919)
"""
import kicadlib
import pinmap
from model import Circuit, NC
from parts import Builder

c = Circuit()
b = Builder(c)
G = pinmap.native_by_net()       # GPIO numbers in notes come from the pin map, so they cannot drift

c.sheets = [
    ('power', 'Power', 'USB-C entry, charger with power path, battery, fuel gauge, 3.3 V buck-boost, display rail'),
    ('compute', 'Compute', 'ESP32-S3 module, GPIO expander, boot/reset, service and test access'),
    ('interface', 'Interface', 'Round display, backlight, ring dial, face press, body touch'),
    ('sense', 'Sense', 'IMU, proximity, ambient light, microphone'),
    ('feedback', 'Feedback', 'Speaker amplifier, haptic driver, IR transmit and receive'),
]
c.root_notes = [
    'MAO_MAIN A0 - ODD JOBS / MAO round puck main board',
    'GPIO0 = face-press switch = BOOT strap: holding the face while plugging USB enters download mode.',
    'GPIO45 (VDD_SPI strap) reads 0 at reset through the backlight driver EN pull-down; on this PSRAM module the '
    'flash voltage is fixed by eFuse (WROOM-1 datasheet section 8), so the strap only has to be defined. '
    'GPIO46 (download-boot strap) is NC with its internal pull-down.',
    'Expander TCA6408A outputs are high-Z until firmware writes them: every default is set by the resistors shown.',
    'Pin map: hardware/mao/design/pinmap.py (also generates the firmware header and docs/hardware/mao-pin-map.md).',
]

# --------------------------------------------------------------------------------------------
# Custom symbols (pinouts transcribed from datasheets)
# --------------------------------------------------------------------------------------------
L, R, T, B_ = 'L', 'R', 'T', 'B'


def pin(num, name, typ, side):
    return {'num': num, 'name': name, 'type': typ, 'side': side}


c.custom_symbols['MAO:TPS63802'] = kicadlib.make_ic_symbol('MAO:TPS63802', 'U', [
    # TI SLVSEU9D table 7-1, VSON-HR-10 DLA
    pin(10, 'VIN', 'power_in', L), pin(1, 'EN', 'input', L), pin(2, 'MODE', 'input', L),
    pin(9, 'L1', 'passive', T), pin(7, 'L2', 'passive', T),
    pin(6, 'VOUT', 'power_out', R), pin(4, 'FB', 'input', R), pin(5, 'PG', 'open_collector', R),
    pin(3, 'AGND', 'power_in', B_), pin(8, 'GND', 'power_in', B_),
], 'TPS63802 buck-boost, 2 A, VSON-HR-10')

c.custom_symbols['MAO:MAX17048'] = kicadlib.make_ic_symbol('MAO:MAX17048', 'U', [
    # ADI MAX17048/MAX17049 datasheet pin description, TDFN-8 2x2 (T822)
    pin(3, 'VDD', 'power_in', L), pin(2, 'CELL', 'input', L), pin(1, 'CTG', 'passive', L),
    pin(6, 'QSTRT', 'input', L),
    pin(8, 'SDA', 'bidirectional', R), pin(7, 'SCL', 'input', R), pin(5, '~{ALRT}', 'open_collector', R),
    pin(4, 'GND', 'power_in', B_), pin(9, 'EP', 'passive', B_),
], 'MAX17048 1-cell fuel gauge, ModelGauge, I2C 0x36')

# --------------------------------------------------------------------------------------------
# POWER
# --------------------------------------------------------------------------------------------
b.at('power', 'USB-C ENTRY',
     'Sink only: 5.1k Rd on CC1/CC2 (ODD JOBS 25). ESD at the connector (119): TPD2E2U06 on D+/D-, SMF15A on VBUS. '
     'Shell to GND (plastic enclosure, 26).')
b.part('J1', 'Connector:USB_C_Receptacle_USB2.0_16P', 'Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12',
       'USB-C', {'A1': 'GND', 'A12': 'GND', 'B1': 'GND', 'B12': 'GND', 'A4': 'VBUS', 'A9': 'VBUS', 'B4': 'VBUS',
                 'B9': 'VBUS', 'A5': 'USB_CC1', 'B5': 'USB_CC2', 'A6': 'USB_DP', 'B6': 'USB_DP',
                 'A7': 'USB_DN', 'B7': 'USB_DN', 'A8': NC, 'B8': NC, 'SH': 'GND'},
       lcsc='C165948', mpn='TYPE-C-31-M-12', mfr='Korean Hroparts', note='USB 2.0 16P receptacle, 4 THT shell legs')
b.R('R1', '5.1k', 'USB_CC1', 'GND', note='Rd: sink, default USB power')
b.R('R2', '5.1k', 'USB_CC2', 'GND', note='Rd')
b.part('D1', 'Device:D_TVS', 'Diode_SMD:D_SMF', 'SMF15A', {'1': 'VBUS', '2': 'GND'}, lcsc='C123802', mpn='SMF15A',
       mfr='MDD', note='VBUS TVS: 15 V standoff, clamps below the charger 28 V abs max')
b.part('U1', 'Power_Protection:TPD2E2U06DRL', 'Package_TO_SOT_SMD:SOT-553', 'TPD2E2U06',
       {'3': 'USB_DP', '5': 'USB_DN', '4': 'GND', '1': NC, '2': NC}, lcsc='C1972959', mpn='TPD2E2U06DRLR', mfr='TI',
       note='2-ch ESD, 1.5 pF, IEC 61000-4-2 level 4')

b.at('power', 'CHARGER',
     'BQ24073 linear charger with power path (DPPM): runs MAO from USB while charging. USB500 input limit '
     '(EN2=0, EN1=+3V3), ISET 4.3k = 207 mA charge (cell maximum 250 mA = 0.5C), ILIM 1.5k, default timers. '
     'Pack NTC on TS: the charger stops outside 0-50 C; the cell allows 0-45 C, so firmware pauses charging '
     'through /CE (expander P6) above 43 C board temperature; charging state comes from the gauge (CRATE) and PGOOD, '
     'so /CHG is not used. On F under the panel: its heat stays off the cell.')
b.part('U2', 'Battery_Management:BQ24073RGT', 'Package_DFN_QFN:VQFN-16-1EP_3x3mm_P0.5mm_EP1.68x1.68mm_ThermalVias',
       'BQ24073RGTR', {'13': 'VBUS', '10': 'VSYS', '11': 'VSYS', '2': 'VBAT', '3': 'VBAT', '1': 'BAT_NTC',
                       '4': 'CHG_CE_N', '5': 'GND', '6': '+3V3', '7': 'USB_PRESENT_N', '9': NC, '8': 'GND',
                       '17': 'GND', '12': 'CHG_ILIM', '14': NC, '15': 'CHG_TD', '16': 'CHG_ISET'},
       lcsc='C15220', mpn='BQ24073RGTR', mfr='TI',
       note='/CE from expander P6, pulled low = charge enabled; EN2=GND, EN1=+3V3 -> USB500 (USB100 until the rail is up); TMR open = 30 min / 5 h '
            'timers; /CHG unused; TD via {R7} to GND = termination on. BQ24074 drop-in: {R7} becomes RITERM (DNP = 10 %)')
b.C('C1', '1u', 'VBUS', voltage='25V', note='charger IN')
b.C('C2', '10u', 'VSYS', pkg='0603', note='charger OUT')
b.C('C3', '10u', 'VBAT', pkg='0603', note='charger BAT')
b.R('R5', '4.3k', 'CHG_ISET', 'GND', note='ICHG = 890/4.3k = 207 mA (185-227 mA); the LP503035 cell allows 250 mA (0.5C)')
b.R('R6', '1.5k', 'CHG_ILIM', 'GND', note='ILIM 1.07 A (unused in USB500, but ILIM open disables charging)')
b.R('R7', '0R', 'CHG_TD', 'GND', note='BQ24073 TD low: termination and timers on')
b.R('R8', '100k', 'USB_PRESENT_N', '+3V3', note='PGOOD pull-up')

b.at('power', 'BATTERY',
     'JST SH 3-pin: 1 = BAT-, 2 = NTC, 3 = BAT+ (same order as KINO J1100, ODD JOBS 41/167). Cell MUST carry '
     'its own protection PCM and a 10k NTC. {R10} = 0R link to measure battery current (110). '
     '{Q1} blocks a reversed cell (51); with USB in, the charger stays in its 4-11 mA short probe. '
     '{R11} fits only for cells without NTC.')
b.part('J2', 'Connector_Generic:Conn_01x03', 'Connector_JST:JST_SH_SM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal',
       'BAT', {'1': 'GND', '2': 'BAT_NTC', '3': 'BAT_RAW'}, lcsc='C7430445', mpn='ZX-SH1.0-3PWT (JST SM03B-SRSS-TB compatible)',
       mfr='Megastar', note='battery: 1 BAT-, 2 NTC, 3 BAT+. 2.9 mm tall: fits over the cell. 1 A (peaks ~0.6 A)')
b.R('R10', '0R', 'BAT_RAW', 'BAT_IN', pkg='1206', note='BAT LINK: remove to insert an ammeter (ODD JOBS 110)')
b.part('Q1', 'Transistor_FET:AO3401A', 'Package_TO_SOT_SMD:SOT-23', 'AO3401A',
       {'1': 'BAT_RPP_G', '3': 'BAT_IN', '2': 'VBAT'}, lcsc='C15127', mpn='AO3401A', mfr='AOS',
       note='reverse-polarity P-FET: drain to cell, source to VBAT, gate to GND; conducts both ways when the cell is right')
b.R('R12', '10k', 'BAT_RPP_G', 'GND', note='Q1 gate')
b.R('R11', '10k', 'BAT_NTC', 'GND', note='DNP with an NTC cell; fit for a 2-wire cell', dnp=True)

b.at('power', 'FUEL GAUGE', 'MAX17048 ModelGauge on VBAT, I2C 0x36, ALRT wired-OR with the light sensor INT.')
b.part('U3', 'MAO:MAX17048', 'Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm', 'MAX17048G+T10',
       {'3': 'VBAT', '2': 'VBAT', '1': 'GND', '6': 'GND', '8': 'I2C_SDA', '7': 'I2C_SCL', '5': 'SENSE_ALRT_N',
        '4': 'GND', '9': 'GND'}, lcsc='C2682616', mpn='MAX17048G+T10', mfr='Analog Devices',
       note='fuel gauge; CTG and QSTRT to GND')
b.C('C4', '100n', 'VBAT', note='gauge VDD')

b.at('power', '3V3 BUCK-BOOST',
     'TPS63802: VSYS 2.9-4.4 V to +3V3, 2 A, 11 uA Iq. EN divider = hardware UVLO: on at 3.25 V, off at 2.96 V '
     '(protects the cell even if firmware fails), 100 nF so load steps cannot trip it. MODE low = PFM. '
     'FB 536k/100k = 3.18 V (3.10-3.28 V worst case: under the panel VCI 3.3 V maximum). L = 0.47 uH only.')
b.part('U4', 'MAO:TPS63802', 'MAO:TI_DLA0010A_VSON-HR-10_2x3mm_P0.5mm', 'TPS63802DLAR',
       {'10': 'VSYS', '1': 'BB_EN', '2': 'GND', '9': 'BB_L1', '7': 'BB_L2', '6': '+3V3', '4': 'BB_FB', '5': NC,
        '3': 'GND', '8': 'GND'}, lcsc='C2845237', mpn='TPS63802DLAR', mfr='TI',
       note='buck-boost; PG not used')
b.part('L1', 'Device:L', 'Inductor_SMD:L_Murata_DFE201610P', '0.47uH', {'1': 'BB_L1', '2': 'BB_L2'},
       lcsc='C668312', mpn='DFE201612E-R47M=P2', mfr='Murata', note='0.47 uH, Isat 5.5 A, 26 mOhm, 2016')
b.C('C5', '10u', 'VSYS', pkg='0603', note='buck-boost CIN, about 1 mm from VIN (TI layout: no separate HF cap)')
b.C('C7', '22u', '+3V3', pkg='0603', note='COUT')
b.C('C8', '22u', '+3V3', pkg='0603', note='COUT')
b.R('R13', '470k', 'VSYS', 'BB_EN', note='UVLO divider top (470k, not 1M: the 0.2 uA EN leakage moves the trip only +/-0.1 V)')
b.R('R14', '240k', 'BB_EN', 'GND', note='UVLO divider bottom: 1.1 V rising / 1.0 V falling at EN')
b.R('R15', '536k', '+3V3', 'BB_FB', note='FB top')
b.R('R16', '100k', 'BB_FB', 'GND', note='FB bottom: 0.5 V x 6.36 = 3.18 V')

b.at('power', 'DISPLAY RAIL',
     'TPS22919 switches 3V3_LCD (panel logic; the backlight runs from VSYS). ON pulled down: off until firmware enables it. '
     'Fixed 1.6 ms soft start (datasheet 1.75 ms at 3.6 V), so the ~20 mA panel inrush cannot brown out the MCU. '
     'QOD to VOUT through 100R: clean power-down. SC70-6, 1.1 mm: it sits under the panel (zone A <= 1.2 mm).')
b.part('U5', 'Power_Management:TPS22919DCK', 'Package_TO_SOT_SMD:SOT-363_SC-70-6', 'TPS22919DCKR',
       {'1': '+3V3', '2': 'GND', '3': 'LCD_PWR_EN', '4': NC, '5': 'LCD_SW_QOD', '6': '3V3_LCD'},
       lcsc='C2149796', mpn='TPS22919DCKR', mfr='TI', note='load switch, 8 uA Iq, fixed rise time')
b.C('C10', '1u', '3V3_LCD', note='switch output')
b.R('R32', '100R', 'LCD_SW_QOD', '3V3_LCD', note='QOD output discharge through 100R (datasheet option): clean panel power-down')
b.R('R17', '100k', 'LCD_PWR_EN', 'GND', note='default off')

# --------------------------------------------------------------------------------------------
# COMPUTE
# --------------------------------------------------------------------------------------------
b.at('compute', 'MCU',
     'ESP32-S3-WROOM-1-N8R2 on B.Cu at 6 o\'clock, antenna over the board-edge notch (no copper beneath). '
     '22 uF + 100 nF at 3V3. EN: 10k/1 uF RC. GPIO0 = face press (BOOT). Unused pins NC or test pads.')
mod = {'GND': 'GND', '3V3': '+3V3', 'EN': 'MCU_EN'}
names = {0: 'IO0', 43: 'TXD0', 44: 'RXD0', 19: 'USB_D-', 20: 'USB_D+'}
for gpio, net, d, fn, note in pinmap.NATIVE:
    name = names.get(gpio, 'IO%d' % gpio)
    mod[name] = net if net else NC
b.part('U6', 'RF_Module:ESP32-S3-WROOM-1', 'RF_Module:ESP32-S3-WROOM-1', 'ESP32-S3-WROOM-1-N8R2', mod,
       lcsc='C2913204', mpn='ESP32-S3-WROOM-1-N8R2', mfr='Espressif', note='8 MB flash, 2 MB quad PSRAM, -40..85 C')
b.C('C11', '22u', '+3V3', pkg='0603', note='module bulk (Espressif HDG)')
b.C('C12', '100n', '+3V3', note='module HF')
b.R('R18', '10k', '+3V3', 'MCU_EN', note='EN pull-up')
b.C('C13', '1u', 'MCU_EN', note='EN delay (10k x 1 uF)')
b.R('R19', '10k', '+3V3', 'PRESS_N', note='GPIO0 pull-up: normal boot')
b.R('R20', '1M', '+3V3', 'BOARD_ID', note='board ID divider top (1M: 1.65 uA, ODD JOBS 112)')
b.R('R21', '1M', 'BOARD_ID', 'GND', note='board ID bottom: A0 = half of +3V3, 1.59 V')
b.C('C15', '100n', 'BOARD_ID', note='holds the divider for the ADC sample (1M source)')

b.at('compute', 'EXPANDER',
     'TCA6408A at 0x20. Outputs default off through 100k pull-downs; inputs pulled up. '
     'Firmware writes the output register before switching pins to outputs (register resets to 0xFF).')
exp = {'1': 'EXP_RST_N', '6': 'GND', '17': 'GND', '11': 'EXP_INT_N', '12': 'I2C_SCL', '13': 'I2C_SDA',
       '14': '+3V3', '15': '+3V3', '16': 'GND'}
port_pin = {0: '2', 1: '3', 2: '4', 3: '5', 4: '7', 5: '8', 6: '9', 7: '10'}
for bit, net, d, fn, pull in pinmap.EXPANDER:
    exp[port_pin[bit]] = net
b.part('U7', 'Interface_Expansion:TCA6408ARGT', 'Package_DFN_QFN:VQFN-16-1EP_3x3mm_P0.5mm_EP1.45x1.45mm_ThermalVias',
       'TCA6408ARGTR', exp, lcsc='C181499', mpn='TCA6408ARGTR', mfr='TI', note='8-bit I2C GPIO expander')
b.C('C14', '100n', '+3V3', note='expander')
b.R('R22', '10k', '+3V3', 'EXP_RST_N', note='expander reset released; GPIO%d can pull it low (recovery)' % G['EXP_RST_N'])
b.R('R23', '100k', '+3V3', 'EXP_INT_N', note='INT pull-up')
b.R('R24', '100k', 'LCD_RST_N', 'GND', note='P0 default: panel held in reset')
# R17 (LCD_PWR_EN) sits with the load switch
b.R('R25', '100k', 'AMP_SD', 'GND', note='P2 default: amplifier shut down (silent at power-up, ODD JOBS 116); at the amp pin')
b.R('R26', '100k', 'HAPTIC_EN', 'GND', note='P3 default: haptic off')
b.R('R27', '100k', 'TOF_XSHUT', 'GND', note='P4 default: proximity sensor off')
b.R('R28', '100k', 'IR_RX_PWR', 'GND', note='P5 default: IR receiver unpowered')
b.R('R29', '100k', '+3V3', 'SENSE_ALRT_N', note='P7 wired-OR pull-up (gauge ALRT + light INT); 100k: a latched alert in deep sleep costs 33 uA, not 330')

b.at('compute', 'I2C', 'One 400 kHz bus. 2.2k pull-ups (ODD JOBS 30): ~6 devices, short traces.')
b.R('R30', '2.2k', '+3V3', 'I2C_SDA')
b.R('R31', '2.2k', '+3V3', 'I2C_SCL')

b.at('compute', 'SERVICE',
     'Tag-Connect TC2030-NL (no part fitted): 1 GND, 2 EN, 3 TXD0, 4 3V3, 5 RXD0, 6 GPIO0 - recovery '
     'without USB (ODD JOBS 33-35, 157). Pinout chosen so TXD0/RXD0 run straight from the module pins. '
     'Probe pads: rails beside the charger, comms in one field, switched rails at their sources (36-39).')
b.part('J3', 'Connector:TC2030', 'Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical', 'TAG',
       {'1': 'GND', '2': 'MCU_EN', '3': 'UART_TX', '4': '+3V3', '5': 'UART_RX', '6': 'PRESS_N'},
       mpn='PCB feature, no part', note='Tag-Connect TC2030-IDC-NL footprint', **{'Exclude from BOM': 'yes'})
for ref, net, label in [('TP1', 'GND', 'GND'), ('TP2', 'GND', 'GND'), ('TP3', '+3V3', '3V3'),
                        ('TP4', 'VSYS', 'SYS'), ('TP5', 'VBAT', 'BAT'), ('TP6', 'VBUS', 'VBUS'),
                        ('TP7', 'MCU_EN', 'RST'), ('TP8', 'PRESS_N', 'BOOT'), ('TP9', 'I2C_SDA', 'SDA'),
                        ('TP10', 'I2C_SCL', 'SCL'), ('TP11', 'EXP_RST_N', 'XRST'),
                        ('TP13', '3V3_LCD', 'LCDV'), ('TP14', 'MIC_VDD', 'MIC'),
                        ('TP15', 'IR_RX_VCC', 'IRV'), ('TP16', 'GND', 'GND')]:
    # switched rails (TP13-15): the fixture proves each one really switches (firmware reads back only
    # the enables); signal pads 1.0 mm, rails and supplies 1.2 mm (ODD JOBS 37: 1-1.5 mm; 1.2 leaves each
    # pad's name room between the rows of the 2.7 mm service field)
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

c.custom_symbols['MAO:OPT3004'] = kicadlib.make_ic_symbol('MAO:OPT3004', 'U', [
    # TI SBOS681 USON-6 DNP: 1 VDD, 2 ADDR, 3 GND, 4 SCL, 5 INT, 6 SDA, EP
    pin(1, 'VDD', 'power_in', L), pin(2, 'ADDR', 'input', L),
    pin(6, 'SDA', 'bidirectional', R), pin(4, 'SCL', 'input', R), pin(5, 'INT', 'open_collector', R),
    pin(3, 'GND', 'power_in', B_), pin(7, 'EP', 'passive', B_),
], 'OPT3004 ambient light sensor, I2C 0x44 with ADDR to GND')

c.custom_symbols['MAO:LSM6DSOX'] = kicadlib.derive_symbol(
    'Sensor_Motion:LSM6DSM', 'MAO:LSM6DSOX', value='LSM6DSOX',
    # SA0, SDx, SCx and CS are hard-wired configuration inputs here (DS12814 table 2)
    pin_types={'1': 'input', '2': 'input', '3': 'input', '12': 'input'})
c.custom_symbols['MAO:MAX98357A'] = kicadlib.derive_symbol(
    'Audio:MAX98357A', 'MAO:MAX98357A', value='MAX98357A',
    pin_types={'17': 'passive'})   # exposed pad: not internally connected, soldered to GND

c.custom_symbols['MAO:AW9364'] = kicadlib.make_ic_symbol('MAO:AW9364', 'U', [
    # Awinic AW9364 V2.3 pin definition, DFN2x2-8L: 1 PGND, 2 EN, 3 VIN, 4 AGND, 5-8 LED4..LED1, 9 EP
    pin(3, 'VIN', 'power_in', L), pin(2, 'EN', 'input', L),
    pin(8, 'LED1', 'passive', R), pin(7, 'LED2', 'passive', R), pin(6, 'LED3', 'passive', R), pin(5, 'LED4', 'passive', R),
    pin(1, 'PGND', 'power_in', B_), pin(4, 'AGND', 'power_in', B_), pin(9, 'EP', 'passive', B_),
], 'AW9364 4-channel low-dropout LED current sink, 20 mA per channel, 1-wire 16-step dimming')

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

c.custom_symbols['MAO:Electrode'] = kicadlib.make_ic_symbol('MAO:Electrode', 'E', [
    pin(1, 'E', 'passive', L),
], 'capacitive touch electrode (PCB copper)', body_width=7.62)

c.custom_symbols['MAO:Spring'] = kicadlib.make_ic_symbol('MAO:Spring', 'J', [
    pin(1, 'C', 'passive', L),
], 'SMD spring contact to an enclosure part', body_width=7.62)

SPRING = dict(lcsc='C2826516', mpn='BW0019BG-L3.5W1.5H3.8', mfr='BAT WIRELESS')
SPRING_FP = 'MAO:BAT_BW0019BG_SpringContact_3.5x1.5mm'

# --------------------------------------------------------------------------------------------
# INTERFACE
# --------------------------------------------------------------------------------------------
b.at('interface', 'DISPLAY',
     'Plug-in display: 1.28" round GC9A01 panel (Winstar WF0128BTYAA4DNN0, 240x240 IPS) on an 18-pin 0.5 mm '
     'FPC tail, folded under the panel into a 1.0 mm high back-flip connector with top and bottom contacts, '
     'so any panel of this 18-pin standard plugs in either way up and can be swapped without solder. '
     'Pins 1-6 serve the touch controller of touch variants: not wired (MAO\'s face is a pressed window), '
     'TP_GND to GND. TE to GPIO%d for tear-free frames. 22R on SCLK/MOSI damps 80 MHz edges (ODD JOBS 29). ' % G['LCD_TE'] +
     'Panel logic on the switched 3V3_LCD rail, backlight anode on VSYS through the constant-current driver; RST held '
     'low until firmware. The stock 70 mm tail S-folds in the face carrier, drops through the board slot at 9 '
     "o'clock and plugs into J301 on B.Cu.")
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
b.C('C301', '100n', '3V3_LCD', note='panel VDD, at the land')
b.C('C302', '4.7u', '3V3_LCD', pkg='0603', note='panel bulk (ODD JOBS 15)')

b.at('interface', 'BACKLIGHT',
     'Panel LEDs (one group of two) need VLED+ 3.0/3.2/3.4 V at 40 mA (Winstar spec 4.2): more than 3V3 can drive. '
     'AW9364 constant-current sinks from VSYS (40-50 mV dropout): LED1+LED2 on VLED- = 2 x 20 mA = 40 mA nominal '
     '(33-47 mA: +-17.5 %% part tolerance), set by the part, not by firmware. 16 steps by EN pulses on GPIO%d; its 150k EN '
     'pull-down keeps the panel dark from reset (ODD JOBS 116). Full 40 mA down to VSYS 3.4 V on the typical 3.2 V '
     'bin, 3.6 V on the 3.4 V bin (sim S3); dimmer below.' % G['LCD_BL_CTRL'])

b.at('interface', 'RING DIAL',
     'Two DRV5012 Hall latches under the ring\'s 30-pole ferrite strip, 6 deg apart (half a pole = 90 deg electrical): '
     'A/B quadrature identical to the LCDkit EC11 (2 transitions per detent, 30 detents). '
     'HALL_FAST low = 20 Hz sampling, 1.6 uA (sleep); high = 2.5 kHz (awake).')
b.part('U301', 'MAO:DRV5012', 'Package_SON:Texas_X2SON-4-1EP_1.1x1.4mm_P0.5mm_EP0.8x0.6mm', 'DRV5012AEDMRR',
       {'1': '+3V3', '4': 'HALL_FAST', '3': 'HALL_A', '2': 'GND', '5': 'GND'},
       lcsc='C2655038', mpn='DRV5012AEDMRR', mfr='TI', note='ring channel A')
b.part('U302', 'MAO:DRV5012', 'Package_SON:Texas_X2SON-4-1EP_1.1x1.4mm_P0.5mm_EP0.8x0.6mm', 'DRV5012AEDMRR',
       {'1': '+3V3', '4': 'HALL_FAST', '3': 'HALL_B', '2': 'GND', '5': 'GND'},
       lcsc='C2655038', mpn='DRV5012AEDMRR', mfr='TI', note='ring channel B')
b.C('C303', '100n', '+3V3', note='U301 VCC')
b.C('C304', '100n', '+3V3', note='U302 VCC')
b.R('R306', '100k', 'HALL_FAST', 'GND', note='low-power sampling until firmware')

b.at('interface', 'FACE PRESS',
     'ALPS SKQG on F.Cu at the centre, under the panel: the floating face presses it. GPIO0 = BOOT strap: no capacitor here.')
b.part('SW301', 'Switch:SW_Push', 'Button_Switch_SMD:SW_SPST_SKQG_WithStem', 'SKQGADE010',
       {'1': 'PRESS_N', '2': 'GND'}, lcsc='C116647', mpn='SKQGADE010', mfr='ALPS', note='2.55 N, 0.25 mm travel')

b.at('interface', 'BODY TOUCH',
     'Four ESP32-S3 touch channels. 510R within 1 mm of the module (Espressif guide); 0.42 pF ESD at the '
     'electrode. LEFT/RIGHT: copper arcs at the rim; TOP/REAR: gold springs to printed or foil electrodes.')
for i, zone in enumerate(('LEFT', 'RIGHT', 'TOP', 'REAR')):
    b.R('R%d' % (307 + i), '510R', 'TOUCH_' + zone, 'TOUCH_%s_E' % zone, note='touch series resistor')
    b.part('D%d' % (301 + i), 'Power_Protection:TPD1E05U06DPY', 'Package_SON:Texas_DPY0002A_0.6x1mm_P0.65mm',
           'TPD1E05U06', {'1': 'TOUCH_%s_E' % zone, '2': 'GND'}, lcsc='C436349', mpn='TPD1E05U06DPYR', mfr='TI',
           note='0.42 pF ESD')
b.part('E301', 'MAO:Electrode', 'MAO:TouchArc_R26.4-28.6_50deg', 'TOUCH LEFT', {'1': 'TOUCH_LEFT_E'},
       mpn='PCB feature, no part', note='rim electrode, 9 o\'clock', **{'Exclude from BOM': 'yes'})
b.part('E302', 'MAO:Electrode', 'MAO:TouchArc_R26.4-28.6_50deg', 'TOUCH RIGHT', {'1': 'TOUCH_RIGHT_E'},
       mpn='PCB feature, no part', note='rim electrode, 3 o\'clock', **{'Exclude from BOM': 'yes'})
b.part('J302', 'MAO:Spring', SPRING_FP, 'TOUCH TOP', {'1': 'TOUCH_TOP_E'}, note='spring to the window-border electrode',
       **SPRING)
b.part('J303', 'MAO:Spring', SPRING_FP, 'TOUCH REAR', {'1': 'TOUCH_REAR_E'}, note='spring to the base electrode',
       **SPRING)

# --------------------------------------------------------------------------------------------
# SENSE
# --------------------------------------------------------------------------------------------
b.at('sense', 'IMU',
     'LSM6DSOX at 0x6A (SA0 low), I2C (CS high). SDx/SCx to VDDIO keeps the land compatible with the '
     'LSM6DS3TR-C fallback. INT1/INT2 push-pull, configured active-low for ext1 wake.')
b.part('U401', 'MAO:LSM6DSOX', 'Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y', 'LSM6DSOXTR',
       {'1': 'GND', '2': '+3V3', '3': '+3V3', '4': 'IMU_INT1', '5': '+3V3', '6': 'GND', '7': 'GND',
        '8': '+3V3', '9': 'IMU_INT2', '10': NC, '11': NC, '12': '+3V3', '13': 'I2C_SCL', '14': 'I2C_SDA'},
       lcsc='C481766', mpn='LSM6DSOXTR', mfr='ST', note='6-axis IMU')
b.C('C401', '100n', '+3V3', note='IMU VDD')
b.C('C402', '100n', '+3V3', note='IMU VDDIO')

b.at('sense', 'PROXIMITY',
     'VL53L4CD time-of-flight, 0-1.3 m, under the window border. XSHUT from the expander (off by default); '
     'GPIO1 open-drain with the 10k pull-up ST recommends. AVDD decoupled 4.7 uF + 100 nF per ST.')
b.part('U402', 'Sensor_Distance:VL53L0CXV0DH1', 'OptoDevice:ST_VL53L0X', 'VL53L4CDV0DH/1',
       {'1': '+3V3', '2': 'GND', '3': 'GND', '4': 'GND', '5': 'TOF_XSHUT', '6': 'GND', '7': 'TOF_INT_N',
        '8': NC, '9': 'I2C_SDA', '10': 'I2C_SCL', '11': '+3V3', '12': 'GND'},
       lcsc='C3178291', mpn='VL53L4CDV0DH/1', mfr='ST', note='ToF; VL53L0X-family land, pin-identical')
b.C('C403', '100n', '+3V3', note='ToF AVDD')
b.C('C404', '4.7u', '+3V3', pkg='0603', note='ToF VCSEL bulk')
b.R('R401', '10k', '+3V3', 'TOF_INT_N', note='GPIO1 pull-up (ST: 10k)')

b.at('sense', 'AMBIENT LIGHT', 'OPT3004 at 0x44, human-eye response, under the window border. INT joins SENSE_ALRT_N.')
b.part('U403', 'MAO:OPT3004', 'MAO:TI_DNP0006A_USON-6_2x2mm_P0.65mm_EP0.65x1.35mm', 'OPT3004DNPR',
       {'1': '+3V3', '2': 'GND', '3': 'GND', '4': 'I2C_SCL', '5': 'SENSE_ALRT_N', '6': 'I2C_SDA', '7': 'GND'},
       lcsc='C2655153', mpn='OPT3004DNPR', mfr='TI', note='ambient light, 0.01 lux resolution')
b.C('C405', '100n', '+3V3', note='ALS VDD')

b.at('sense', 'MICROPHONE',
     'SPH0641 bottom-port PDM mic on B.Cu, port through the board (0.5 mm hole) to the top. Powered from GPIO%d ' % G['MIC_PWR'] +
     'through 100R/1 uF only while listening: it draws 80 uA even with the clock stopped. SEL low.')
b.part('MK401', 'Sensor_Audio:SPH0641LU4H-1', 'Sensor_Audio:Knowles_LGA-5_3.5x2.65mm', 'SPH0641LU4H-1',
       {'1': 'MIC_DATA', '2': 'GND', '3': 'GND', '4': 'MIC_CLK', '5': 'MIC_VDD'},
       lcsc='C2879853', mpn='SPH0641LU4H-1', mfr='Knowles', note='PDM MEMS microphone, 64 dB SNR')
b.R('R402', '100R', 'MIC_PWR', 'MIC_VDD', note='supply filter')
b.C('C406', '1u', 'MIC_VDD', note='mic supply filter')
b.C('C407', '100n', 'MIC_VDD', note='mic HF')
b.R('R403', '100k', 'MIC_PWR', 'GND', note='mic off at reset')

# --------------------------------------------------------------------------------------------
# FEEDBACK
# --------------------------------------------------------------------------------------------
b.at('feedback', 'SPEAKER AMP',
     'MAX98357A I2S class-D from VSYS. SD_MODE from the expander through 2.2k (datasheet: series R when VDDIO can '
     'exceed VDD): low = 0.6 uA shutdown, 3.2 V = left channel. '
     'GAIN_SLOT open = 9 dB (datasheet state, deliberate). Speaker: Same Sky CMS-150803-088S-X8 (15 x 8 x 3 mm, '
     '8 ohm, 0.8 W) in the base at 9 o\'clock; its own spring contacts press on two pads on B.Cu (LS501).')
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
     'DRV2605L closed-loop LRA driver from VSYS, I2C 0x5A, EN from the expander (4 uA off). IN/TRIG low: '
     'playback by I2C. Pin 6 VDD/NC left open (SLOS854D: optional supply, tie to VDD or float). '
     'LRA LD0832AA (235 Hz, 1.8 Vrms) glued to the base, leads to J501.')
b.part('U502', 'Driver:DRV2605LDGS', 'Package_SO:MSOP-10_3x3mm_P0.5mm', 'DRV2605LDGSR',
       {'1': 'HAP_REG', '2': 'I2C_SCL', '3': 'I2C_SDA', '4': 'GND', '5': 'HAPTIC_EN', '6': NC,
        '7': 'LRA_P', '8': 'GND', '9': 'LRA_N', '10': 'VSYS'},
       lcsc='C527464', mpn='DRV2605LDGSR', mfr='TI', note='haptic driver, auto-resonance')
b.C('C504', '1u', 'VSYS', note='haptic VDD')
b.C('C505', '1u', 'HAP_REG', note='DRV2605L internal regulator (required)')
b.part('J501', 'Connector_Generic:Conn_01x02', 'MAO:WirePads_1x02_P2.5mm_1.0x1.8mm', 'LRA',
       {'1': 'LRA_P', '2': 'LRA_N'}, mpn='PCB feature: LD0832AA-0099F leads', note='LRA lead pads',
       **{'Exclude from BOM': 'yes'})

b.at('feedback', 'IR TRANSMIT',
     'Two side-emitting 940 nm LEDs at the back edge (B.Cu), each with its own 56R from VSYS (26-59 mA pulses at '
     '38 kHz, 33 %% duty: under the 65 mA rating at any VSYS up to 4.5 V and any Vf). AO3400A low side, gate '
     'pulled down and driven from GPIO%d, a pin without a reset pull-up: dark at reset (117).' % G['IR_TX'])
b.part('D501', 'Device:LED', 'MAO:Everlight_IR12-21C_RightAngle_3x1mm', 'IR12-21C', {'1': 'IR_LED_K', '2': 'IR_LED_A1'},
       lcsc='C53672', mpn='IR12-21C/TR8', mfr='Everlight', note='940 nm side-emitting IR LED')
b.part('D502', 'Device:LED', 'MAO:Everlight_IR12-21C_RightAngle_3x1mm', 'IR12-21C', {'1': 'IR_LED_K', '2': 'IR_LED_A2'},
       lcsc='C53672', mpn='IR12-21C/TR8', mfr='Everlight', note='940 nm side-emitting IR LED')
b.R('R501', '56R', 'VSYS', 'IR_LED_A1', pkg='0603', note='IR LED current')
b.R('R502', '56R', 'VSYS', 'IR_LED_A2', pkg='0603', note='IR LED current')
b.part('Q501', 'Transistor_FET:AO3400A', 'Package_TO_SOT_SMD:SOT-23', 'AO3400A',
       {'1': 'IR_TX_G', '2': 'GND', '3': 'IR_LED_K'}, lcsc='C20917', mpn='AO3400A', mfr='AOS', note='IR LED switch')
b.R('R503', '100R', 'IR_TX', 'IR_TX_G', note='gate resistor')
b.R('R504', '100k', 'IR_TX_G', 'GND', note='IR off at reset')
b.C('C506', '10u', 'VSYS', pkg='0603', note='IR pulse reservoir')

b.at('feedback', 'IR RECEIVE',
     'IRM-H638T (as LCDkit), top-view, looking up through the window border. Powered from expander P5 through '
     '100R/4.7 uF only when listening (0.4 mA). OUT pull-up goes to the switched supply, never to +3V3, '
     'so an unpowered receiver is never back-powered and GPIO%d reads a defined low while it is off.' % G['IR_RX'])
b.part('U503', 'Interface_Optical:IRM-H6xxT', 'OptoDevice:Everlight_IRM-H6xxT', 'IRM-H638T/TR2',
       {'1': 'GND', '2': 'GND', '3': 'IR_RX', '4': 'IR_RX_VCC'}, lcsc='C91447', mpn='IRM-H638T/TR2',
       mfr='Everlight', note='38 kHz IR receiver')
b.R('R505', '100R', 'IR_RX_PWR', 'IR_RX_VCC', note='receiver supply filter (datasheet application circuit)')
b.C('C507', '4.7u', 'IR_RX_VCC', pkg='0603', note='receiver supply filter')
b.R('R506', '10k', 'IR_RX_VCC', 'IR_RX', note='OUT pull-up to the switched supply (defined level, no back-powering)')


# --------------------------------------------------------------------------------------------
# A0 revision (2026-10-05 verification): parts added at the end of their sheets, so every earlier
# reference keeps its number (docs and bring-up procedures name them). Each joins its block.
# --------------------------------------------------------------------------------------------
b.at('power', 'DISPLAY RAIL')
b.C('C90', '1u', '+3V3', note='TPS22919 VIN, at pin 1 (datasheet CIN; ODD JOBS 14)')
b.at('power', '3V3 BUCK-BOOST')
b.C('C91', '100n', 'BB_EN', note='UVLO filter: 16 ms with the 159k divider source, rides through load steps')
b.at('power', 'CHARGER')
b.R('R90', '100k', 'CHG_CE_N', 'GND', note='/CE default low: charging on from reset and in deep sleep')
b.at('interface', 'BACKLIGHT')
b.part('U303', 'MAO:AW9364', 'Package_DFN_QFN:DFN-8-1EP_2x2mm_P0.5mm_EP0.6x1.2mm', 'AW9364DNR',
       {'3': 'VSYS', '2': 'LCD_BL_CTRL', '8': 'LCD_BL_K', '7': 'LCD_BL_K', '6': 'GND', '5': 'GND',
        '1': 'GND', '4': 'GND', '9': 'GND'}, lcsc='C401007', mpn='AW9364DNR', mfr='Awinic',
       note='backlight current sinks: LED1+LED2 = 40 mA max, LED3/LED4 to GND (datasheet: unused channel)')
b.C('C305', '1u', 'VSYS', note='AW9364 VIN (datasheet 1 uF), also the LED anode bypass at J301 pin 7')
b.at('feedback', 'SPEAKER AMP')
b.R('R507', '2.2k', 'AMP_SD_N', 'AMP_SD', note='SD_MODE series resistor (MAX98357A datasheet, VDDIO > VDD case)')

# Parts removed by the revision: their numbers stay retired so later references do not move.
RETIRED = {'Q301', 'R303', 'R304', 'R305', 'R107', 'C108'}


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
    for p in c.parts:
        prefix = ''.join(ch for ch in p.ref if ch.isalpha())
        if prefix in keep:
            continue
        k = (p.sheet, prefix)
        while True:
            counters[k] = counters.get(k, 0) + 1
            ref = '%s%d' % (prefix, base[p.sheet] + counters[k])
            if ref not in RETIRED:
                break
        final[p.ref] = ref
        p.ref = ref
    refs = [p.ref for p in c.parts]
    assert len(refs) == len(set(refs)), 'duplicate references after renumbering'

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
