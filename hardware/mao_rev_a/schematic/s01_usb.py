"""Sheet 01: USB-C receptacle, CC, ESD, VBUS sense.

- CC1/CC2: 5.1 k Rd each (UFP sink). The source then advertises Default USB power,
  500 mA for USB 2.0, which is what the BQ25185 input limit (500 mA) draws at most.
- D+/D-: TPD2E2U06 (1.5 pF, VRWM 5.5 V) at the connector; route 90 ohm differential
  to the module; series resistors sit at the module (sheet 04, Espressif HDG 2.13).
- VBUS: TPD1E10B06 (clamps 10 V at 1 A, under the charger's 25 V IN abs max).
- VBUS_SENSE: 100 k / 150 k, 5.5 V -> 3.30 V at IO35; draws 22 uA only with USB present.
- SBU pins unused.
"""
from lib import NCNET as NC

from lib import R, TP, pulldown, sig
from nets import GND, USB_DN_C, USB_DP_C, VBUS, VBUS_SENSE
from parts_misc import TPD1E10B06, TPD2E2U06, USBC


def build():
    j = USBC()
    for p in ('GND_A1', 'GND_A12', 'GND_B1', 'GND_B12', 'SHIELD'):
        j[p] += GND
    for p in ('VBUS_A4', 'VBUS_A9', 'VBUS_B4', 'VBUS_B9'):
        j[p] += VBUS
    j['DP1'] += USB_DP_C
    j['DP2'] += USB_DP_C
    j['DN1'] += USB_DN_C
    j['DN2'] += USB_DN_C
    j['SBU1'] += NC
    j['SBU2'] += NC
    cc1 = sig('CC1', None)
    cc2 = sig('CC2', None)
    j['CC1'] += cc1
    j['CC2'] += cc2
    pulldown(cc1, GND, '5.1k', 'Rd, UFP (USB Type-C)')
    pulldown(cc2, GND, '5.1k', 'Rd, UFP (USB Type-C)')

    esd = TPD2E2U06()
    esd['IO1'] += USB_DP_C
    esd['IO2'] += USB_DN_C
    esd['GND'] += GND
    esd['NC1'] += NC
    esd['NC2'] += NC
    tvs = TPD1E10B06()
    tvs[1] += VBUS
    tvs[2] += GND

    r1 = R('100k', why='VBUS sense top')
    r1[1, 2] += VBUS, VBUS_SENSE
    pulldown(VBUS_SENSE, GND, '150k', 'VBUS sense bottom: 5.5 V -> 3.30 V')
    TP(USB_DP_C, 'USB_DP')
    TP(USB_DN_C, 'USB_DN')
