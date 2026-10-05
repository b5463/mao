"""Sheet 03: CORE_3V3 (TPS62840) and the fuel gauge (MAX17048).

TPS62840DLC (SLVSEC6D):
  VSET 267 k 1 % (Table 1: 3.3 V, band 256.3-277.7 k); keep < 100 pF on VSET
  MODE -> GND (power-save), STOP -> GND (unused), EN -> VIN (always on; UVLO 1.8 V)
  L 2.2 uH DFE201612E-2R2M (Table 2), CIN 4.7 uF, COUT 10 uF (effective 3-40 uF total
  on the rail: the Gate C report adds up every capacitor on CORE_3V3 against it)
LINK_REG measures the regulator input (so: everything on CORE_3V3 plus the regulator).
MAX17048G+T10: VDD and CELL on VBAT, 0.1 uF; CTG, QSTRT to GND; ALRT not connected
(SOC is polled); I2C address 0x36.
"""
from lib import NCNET as NC

from lib import L, R, TP, decouple, link, sig
from nets import CORE_3V3, GND, I2C_SCL, I2C_SDA, SYS, VBAT, VIN_REG
from parts_power import MAX17048, TPS62840


def build():
    link(SYS, VIN_REG, 'LINK_REG', 'regulator + CORE_3V3 current (cut and fit an ammeter)')
    u = TPS62840()
    u['VIN'] += VIN_REG
    u['EN'] += VIN_REG
    u['GND'] += GND
    u['MODE'] += GND
    u['STOP'] += GND
    rset = R('267k', tol='1%', why='VSET: 3.3 V (Table 1), E96, <= 200 ppm/K')
    rset[1, 2] += u['VSET'], GND
    sw = sig('REG_SW', None)
    u['SW'] += sw
    ind = L('2.2uH', 'Inductor_SMD:L_Murata_DFE201610P', 'DFE201612E-2R2M=P2', 'Murata',
            '2.2 uH 20 %, 116 mOhm DCR, 2016 (Table 2); DFE201610P land = DFE201612E land (Murata)')
    ind[1, 2] += sw, CORE_3V3
    u['VOS'] += CORE_3V3
    decouple(VIN_REG, GND, '4.7uF', 10, why='CIN 4.7 uF (GRM155R61A475MEAAD)')
    decouple(CORE_3V3, GND, '10uF', 10, '0603', why='COUT 10 uF (Table 3); 0603 10 V for DC-bias margin')
    TP(VIN_REG, 'VIN_REG')
    TP(CORE_3V3, '3V3')
    TP(sw, 'REG_SW')

    g = MAX17048()
    g['VDD'] += VBAT
    g['CELL'] += VBAT
    g['GND'] += GND
    g['EP'] += GND
    g['CTG'] += GND
    g['QSTRT'] += GND
    g['ALRT'] += NC
    g['SCL'] += I2C_SCL
    g['SDA'] += I2C_SDA
    decouple(VBAT, GND, '0.1uF', 10, why='MAX17048 VDD bypass (p6)')
