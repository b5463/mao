"""Templates: MCU module, connectors, protection, discretes, opto (sources cited per part).

Footprints were audited against the vendor drawings at Gate C (docs/hardware/m5_0_gate_c.md).
"""
from lib import IN, IO, NC, OC, OUT, PAS, POUT, PWR, ic

# ESP32-S3-MINI-1 v1.7 Table 3-1: 65 pads. The footprint is KiCad's ESP32-S2-MINI-1: its pads
# match MINI-1 Fig 11-1 (60 x 0.4x0.8 at 0.85, spans 11.9 / 14, 3x3 x 1.2 EPAD 7.7 from the
# bottom edge, 4 corner 0.8x0.8); KiCad has no S3-MINI-1 footprint.
_S3 = [(1, 'GND1', PWR), (2, 'GND2', PWR), (3, '3V3', PWR), (4, 'IO0', IO)]
_S3 += [(n + 4, f'IO{n}', IO) for n in range(1, 19)]           # pads 5-22 = IO1-IO18
_S3 += [(23, 'IO19', IO), (24, 'IO20', IO), (25, 'IO21', IO), (26, 'IO26', IO), (27, 'IO47', IO),
        (28, 'IO33', IO), (29, 'IO34', IO), (30, 'IO48', IO)]
_S3 += [(n - 4, f'IO{n}', IO) for n in range(35, 43)]           # pads 31-38 = IO35-IO42
_S3 += [(39, 'IO43', IO), (40, 'IO44', IO), (41, 'IO45', IO), (42, 'GND42', PWR), (43, 'GND43', PWR),
        (44, 'IO46', IO), (45, 'EN', IN)]
_S3 += [(n, f'GND{n}', PWR) for n in range(46, 66)]
ESP32S3 = ic('ESP32-S3-MINI-1', 'U', _S3, 'RF_Module:ESP32-S2-MINI-1', 'ESP32-S3-MINI-1-N8', 'Espressif',
             'ESP32-S3-MINI-1/1U datasheet v1.7 Table 3-1, Fig 11-1')

USBC = ic('USB_C', 'J', [
    ('A1', 'GND_A1', PAS), ('A12', 'GND_A12', PAS), ('B1', 'GND_B1', PAS), ('B12', 'GND_B12', PAS),
    ('A4', 'VBUS_A4', PAS), ('A9', 'VBUS_A9', PAS), ('B4', 'VBUS_B4', PAS), ('B9', 'VBUS_B9', PAS),
    ('A5', 'CC1', PAS), ('B5', 'CC2', PAS), ('A6', 'DP1', PAS), ('B6', 'DP2', PAS),
    ('A7', 'DN1', PAS), ('B7', 'DN2', PAS), ('A8', 'SBU1', PAS), ('B8', 'SBU2', PAS), ('SH', 'SHIELD', PAS)],
    'Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal', 'USB4105-GF-A', 'GCT',
    'GCT USB4105 drawing; USB 2.0, 16 pos, top mount (audited)')

TPD2E2U06 = ic('TPD2E2U06', 'D', [(1, 'NC1', NC), (2, 'NC2', NC), (3, 'IO1', PAS), (4, 'GND', PAS),
                                  (5, 'IO2', PAS)],
               'Package_TO_SOT_SMD:SOT-553', 'TPD2E2U06DRLR', 'Texas Instruments',
               'SLLSEG9C DRL pinout: 3 IO1, 5 IO2, 4 GND; 1.5 pF, VRWM 5.5 V (audited)')

TPD1E10B06 = ic('TPD1E10B06', 'D', [(1, '1', PAS), (2, '2', PAS)],
                'Package_SON:Texas_DPY0002A_0.6x1mm_P0.65mm', 'TPD1E10B06DPYR', 'Texas Instruments',
                'SLLSEB1G: bidirectional, VRWM 5.5 V, <= 100 nA at 5 V, 12 pF (audited)')

# Hirose FH12-18S-0.5SH(55): bottom contact, flip-lock, 0.3 mm FPC. Panel contacts are on its viewing
# face; with the tail folded 180 deg behind the panel the contacts face the PCB -> bottom contact.
FPC18 = ic('FPC_18', 'J', [(n, f'P{n}', PWR if n == 16 else PAS) for n in range(1, 19)] + [('MP', 'MP', PAS)],
           'Connector_FFC-FPC:Hirose_FH12-18S-0.5SH_1x18-1MP_P0.50mm_Horizontal', 'FH12-18S-0.5SH(55)',
           'Hirose', 'Hirose FH12 catalog (audited, EDC3-150229-11); pinout = Winstar WF0128BTYAA4DNN0 spec p4')

ENCODER = ic('EC11E', 'SW', [('A', 'A', PAS), ('B', 'B', PAS), ('C', 'C', PAS), ('S1', 'S1', PAS),
                             ('S2', 'S2', PAS), ('MP', 'MP', PAS)],
             'Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm', 'EC11E1534408', 'Alps Alpine',
             'EC11E 15 pulses / 30 detents with push switch; shaft and height fixed at Gate D')

# SOT-23: 1 G, 2 S, 3 D (TO-236 numbering; DS38439 draws letters only)
NMOS = ic('DMG2302UK', 'Q', [(1, 'G', IN), (2, 'S', PAS), (3, 'D', PAS)], 'Package_TO_SOT_SMD:SOT-23',
          'DMG2302UK-7', 'Diodes Inc.', 'DS38439 Rev 2-2: Vgs(th) 0.3-1.0 V, 120 mOhm max at 2.5 V, Ciss 130 pF')

OPAMP = ic('TLV9061', 'U', [(1, 'OUT', OUT), (2, 'V-', PWR), (3, 'IN+', IN, {'lim': 'V+'}),
                            (4, 'IN-', IN, {'lim': 'V+'}), (5, 'V+', PWR)],
           'Package_TO_SOT_SMD:SOT-23-5', 'TLV9061IDBVR', 'Texas Instruments',
           'TLV9061 datasheet DBV pinout (KiCad TLV9061xDBV); 10 MHz RRIO 1.8-5.5 V')

# SK6805-EC20 (OPSCO SK6805-EC20-001 Rev A1 p4): 1 GND, 2 DIN, 3 VDD, 4 DOUT. Not the WS2812B-2020
# order, and KiCad's WS2812B-2020 land doesn't fit the EC20 terminals: custom footprint (p5 pads).
RGB = ic('SK6805-EC20', 'LED', [(1, 'GND', PWR), (2, 'DIN', IN, {'lim': 'VDD'}), (3, 'VDD', PWR), (4, 'DOUT', OUT)],
         'MAO_RevA:LED_SK6805-EC20_2.0x2.0mm', 'SK6805-EC20', 'OPSCO',
         'SK6805-EC20-001 Rev A1: VDD 3.5-5.5 V, VIH 0.6 VDD')

IR_LED = ic('VSMB2943', 'D', [(1, 'K', PAS), (2, 'A', PAS)],
            'MAO_RevA:Vishay_VSMB2943_GullWing', 'VSMB2943GX01', 'Vishay',
            'Vishay 83486 Rev 1.7 p5 pad proposal; no pin numbers in the doc: 1 = K (pin-ID side), 2 = A')

IR_RX = ic('TSOP75438', 'U', [(1, 'GND1', PWR), (2, 'VS', PWR), (3, 'OUT', OUT, {'lim': 'VS'}),
                              (4, 'GND4', PWR)],
           'MAO_RevA:Vishay_TSOP75xxx_Heimdall_SMD', 'TSOP75438TR', 'Vishay',
           'Vishay 82494 Rev 2.4: 38 kHz AGC4, VS 2.0-5.5 V, 0.35 mA at 3.3 V')

# LRA and speaker leads: JST SH 2-pin (1 A/contact); the actuator and speaker are chosen at Gate D
PADS2 = ic('WIRE_2', 'J', [(1, '+', PAS), (2, '-', PAS), ('MP', 'MP', PAS)],
           'Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal', 'SM02B-SRSS-TB(LF)(SN)', 'JST',
           'JST SH 1.0 mm, 2 pos, SMD right angle')
