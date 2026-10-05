"""Sheet 02: battery connector, measurement link, BQ25185 linear charger / power path.

BQ25185 (SLUSF65B):
  ILIM/VSET 18 k  -> 4.2 V regulation, 500 mA input limit (Table 6-1, 7.2.2.1)
  ISET 1.0 k      -> 300 mA charge (KISET 285-315 AOhm), PROVISIONAL until the cell is chosen
  TS/MR           -> the cell's 10 k NTC (beta 3435); 10 k to GND if the cell has none (DNP)
  /CE             -> GND, charging always enabled
  STAT1/STAT2     -> 10 k pull-ups to CORE_3V3 (open-drain, 0 uA on battery: both high-Z)
Factory (ship) mode: with USB present hold TS/MR low 10 s (TP_TSMR to GND at the
fixture), then remove USB. Only a valid VIN brings it out (6.3.8). No user button.
"""
from lib import PAS, R, TP, decouple, ic, link, pullup, sig
from nets import (BATT_P, CHG_STAT1, CHG_STAT2, CORE_3V3, GND, SYS, VBAT, VBUS)
from parts_power import BQ25185


def build():
    # battery: JST PH 3-pin (BAT+, NTC, GND); the cell carries its own protection board
    j = ic('BATT', 'J', [(1, 'BAT+', PAS), (2, 'NTC', PAS), (3, 'GND', PAS), ('MP', 'MP', PAS)],
           'Connector_JST:JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal',
           'S3B-PH-SM4-TB(LF)(SN)', 'JST', 'JST PH 2.0 mm, 3 pos, SMD right angle, 2 A/contact (SH/GH are 1 A: below the 1.2 A worst case)')()
    ntc = sig('BAT_NTC', None, 'cell NTC to TS/MR')
    j['BAT+'] += BATT_P
    j['NTC'] += ntc
    j['GND'] += GND
    j['MP'] += GND
    link(BATT_P, VBAT, 'LINK_BAT', 'whole-board battery current (cut and fit an ammeter)')
    decouple(VBAT, GND, '1uF', 10, why='BQ25185 CBAT >= 1 uF (7.2.2.3)')

    u = BQ25185()
    u['IN'] += VBUS
    u['SYS'] += SYS
    u['BAT'] += VBAT
    u['GND'] += GND
    u['EP'] += GND
    u['CE'] += GND
    u['TS/MR'] += ntc
    R('10k', tol='1%', dnp=True, why='fit only if the chosen cell has no NTC (6.3.9)')[1, 2] += ntc, GND
    riset = R('1.0k', tol='1%', why='ICHG = 300 AOhm / 1.0 k = 300 mA (PROVISIONAL)')
    riset[1, 2] += u['ISET'], GND
    rilim = R('18k', tol='1%', why='4.2 V / 500 mA input limit (Table 6-1)')
    rilim[1, 2] += u['ILIM/VSET'], GND
    u['STAT1'] += CHG_STAT1
    u['STAT2'] += CHG_STAT2
    pullup(CHG_STAT1, CORE_3V3, '10k', 'STAT1 open-drain, 1-20 k')
    pullup(CHG_STAT2, CORE_3V3, '10k', 'STAT2 open-drain, 1-20 k')
    decouple(VBUS, GND, '1uF', 25, '0603', why='CIN >= 1 uF, 25 V rated (7.2.2.3)')
    decouple(SYS, GND, '10uF', 25, '0805', why='CSYS 10 uF nom, 25 V rated (7.2.2.3)')
    TP(VBUS, 'VBUS')
    TP(VBAT, 'VBAT')
    TP(SYS, 'SYS')
    TP(ntc, 'TS_MR')
    TP(CHG_STAT1, 'STAT1')
    TP(CHG_STAT2, 'STAT2')
