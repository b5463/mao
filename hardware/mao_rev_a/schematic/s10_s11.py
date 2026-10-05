"""Sheets 10-11: IR / RGB with the aux gates; service and test.

10 AUX_PWR_EN (IO40) drives two TPS22916C (each held off by its own 750 k smart pull-down):
   - AUX_SYS from SYS for the RGB LED (SK6805-EC20 needs VDD >= 3.5 V: nothing regulated
     reaches that, so it runs from SYS and firmware inhibits it below SYS 3.6 V);
   - AUX_3V3 from CORE_3V3 for the IR receiver, so its output never exceeds the S3's IO
     rating (on a SYS-fed gate the receiver's pulled-up output would reach 4.5 V).
   RGB data through 330 R (limits the back-power current if RGB_DATA were ever left high
   with the rail off; the rule is still: low while AUX is off). DOUT unconnected.
   TSOP75438: VS through 100 R / 0.1 uF (Vishay's supply filter).
   IR TX: DMG2302UK low-side from IR_TX (IO39), gate 100 k to GND; VSMB2943 from SYS through
   33 R (0805): 98 mA at SYS 4.6 V, ~65 mA at 3.5 V; within IF 100 mA continuous.
11 Service: GND pads, fixture pads (sheet 04 carries EN/BOOT/UART/spares).
"""
from lib import NCNET as NC

from lib import R, TP, decouple, pulldown, rail, sig
from nets import AUX_3V3, AUX_PWR_EN, AUX_SYS, CORE_3V3, GND, IR_RX, IR_TX, RGB_DATA, SYS
from parts_misc import IR_LED, IR_RX as TSOP, NMOS, RGB
from parts_power import TPS22916


def ir_rgb():
    for vin, vout in ((SYS, AUX_SYS), (CORE_3V3, AUX_3V3)):
        s = TPS22916()
        s['VIN'] += vin
        s['VOUT'] += vout
        s['GND'] += GND
        s['ON'] += AUX_PWR_EN
        decouple(vin, GND, '1uF', 10, why='TPS22916 CIN (10)')

    led = RGB()
    led['VDD'] += AUX_SYS
    led['GND'] += GND
    led['DOUT'] += NC
    din = sig('RGB_DIN', 'CORE_3V3')
    R('330', why='data series; limits back-power current')[1, 2] += RGB_DATA, din
    led['DIN'] += din
    decouple(AUX_SYS, GND, '0.1uF', 10, why='RGB VDD')

    rx = TSOP()
    vs = rail('IR_VS', 0, 3.4, 'AUX_3V3 through the 100 R / 0.1 uF filter')
    R('100', why='Vishay supply filter')[1, 2] += AUX_3V3, vs
    decouple(vs, GND, '0.1uF', 10, why='Vishay supply filter')
    rx['VS'] += vs
    rx['GND1'] += GND
    rx['GND4'] += GND
    rx['OUT'] += IR_RX

    q = NMOS()
    g, k, a = sig('IR_GATE', None), sig('IR_K', None), sig('IR_A', None)
    R('100', why='gate series')[1, 2] += IR_TX, g
    q['G'] += g
    pulldown(IR_TX, GND, '100k', 'default off through reset')
    pulldown(g, GND, '100k', 'gate held off')
    q['S'] += GND
    q['D'] += k
    d = IR_LED()
    d['K'] += k
    d['A'] += a
    R('33', '0805', '1%', power='1/8 W', why='IR LED current from SYS')[1, 2] += SYS, a
    decouple(SYS, GND, '10uF', 10, '0603', why='IR pulse reservoir')
    TP(IR_TX, 'IR_TX')
    TP(IR_RX, 'IR_RX')
    TP(AUX_PWR_EN, 'AUX_EN')


def service():
    TP(GND, 'GND')
    TP(GND, 'GND')
    TP(GND, 'GND')
