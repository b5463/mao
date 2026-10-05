"""Sheet 05: Winstar WF0128BTYAA4DNN0 (GC9A01) on FH12-18S, panel load switch, backlight sink.

FPC (Winstar spec p4): 1-6 NC, 7 VLED+, 8 VLED-, 9 GND, 10 CS, 11 SCL, 12 SDA, 13 RS(DC),
14 TE, 15 RESET, 16 VCI, 17 NC, 18 GND. IOVCC is not on the FPC (tied to VCI on the panel).
Panel power: TPS22916C (slow rise, 150 ohm quick discharge) from CORE_3V3; ON = LCD_PWR_EN,
off through reset by its 750 k smart pull-down.
Backlight: constant-current low-side sink from SYS.
  LCD_BL (IO8, LEDC ~30 kHz) -> 32.4 k / 1.0 k -> BL_REF (0 / 98.8 mV)
  TLV9061 (on LCD_3V3, so 0 uA in deep sleep) drives DMG2302UK; Rs 3.3 ohm -> 30 mA full.
  30 mA is 75 % of the sibling panel's 40 mA typical: the LED count, Vf and max current of
  this panel are UNKNOWN until Winstar's full spec arrives; Rs sets it.
  Regulates for SYS >= Vf(3.4 V max) + 0.1 V + FET drop, i.e. SYS >= ~3.5 V.
  Off through reset: LCD_BL 100 k to GND, BL_REF 1.0 k to GND, gate 100 k to GND, op-amp
  unpowered while the panel switch is off.
SPI and control lines: 22 ohm series damping on SCLK and MOSI (tune at bring-up); RESET 100 k
to GND so the panel is held in reset while its rail rises. Back-powering rule: every line to
the panel is low or isolated whenever LCD_3V3 is off (firmware, Gate C sequence).
"""
from lib import NCNET as NC

from lib import C, R, TP, decouple, pulldown, sig
from nets import (CORE_3V3, GND, LCD_3V3, LCD_BL, LCD_CS, LCD_DC, LCD_MOSI, LCD_PWR_EN, LCD_RST, LCD_SCLK,
                  LCD_TE, SYS)
from parts_misc import FPC18, NMOS, OPAMP
from parts_power import TPS22916


def build():
    sw = TPS22916()
    sw['VIN'] += CORE_3V3
    sw['VOUT'] += LCD_3V3
    sw['GND'] += GND
    sw['ON'] += LCD_PWR_EN
    decouple(CORE_3V3, GND, '1uF', 10, why='TPS22916 CIN 1 uF (10)')
    decouple(LCD_3V3, GND, '1uF', 10, why='panel VCI bulk')
    decouple(LCD_3V3, GND, '0.1uF', 10, why='panel VCI HF, at the connector')

    sclk_p = sig('LCD_SCLK_P', 'CORE_3V3', 'panel side of the damping resistor')
    mosi_p = sig('LCD_MOSI_P', 'CORE_3V3', 'panel side of the damping resistor')
    for a, b in ((LCD_SCLK, sclk_p), (LCD_MOSI, mosi_p)):
        r = R('22', why='SPI damping, tune at bring-up')
        r[1, 2] += a, b
    pulldown(LCD_RST, GND, '100k', 'panel held in reset while its rail rises')

    vled_k = sig('VLED_K', None, 'backlight cathode, sink drain')
    j = FPC18()
    for n in (1, 2, 3, 4, 5, 6, 17):
        j[f'P{n}'] += NC
    j['P7'] += SYS
    j['P8'] += vled_k
    j['P9'] += GND
    j['P10'] += LCD_CS
    j['P11'] += sclk_p
    j['P12'] += mosi_p
    j['P13'] += LCD_DC
    j['P14'] += LCD_TE
    j['P15'] += LCD_RST
    j['P16'] += LCD_3V3
    j['P18'] += GND
    j['MP'] += GND

    # backlight constant-current sink
    ref = sig('BL_REF', None, '0 / 98.8 mV reference')
    sense = sig('BL_SENSE', None, 'sense resistor node')
    gate = sig('BL_GATE', None)
    drive = sig('BL_DRIVE', None)
    pulldown(LCD_BL, GND, '100k', 'default off through reset')
    rt = R('32.4k', why='reference divider top: 3.3 V -> 98.8 mV')
    rt[1, 2] += LCD_BL, ref
    pulldown(ref, GND, '1.0k', 'reference divider bottom')
    decouple(LCD_3V3, GND, '0.1uF', 10, why='TLV9061 supply')
    a = OPAMP()
    a['V+'] += LCD_3V3
    a['V-'] += GND
    a['IN+'] += ref
    fb = sig('BL_FB', None)
    a['IN-'] += fb
    rf = R('1.0k', why='feedback isolation')
    rf[1, 2] += sense, fb
    cc = C('100pF', 10, diel='C0G', tol='5%', dnp=True, why='loop compensation, fit if the sink rings')
    cc[1, 2] += drive, fb
    a['OUT'] += drive
    rg = R('100', why='gate isolation from the op-amp output')
    rg[1, 2] += drive, gate
    q = NMOS()
    q['G'] += gate
    q['D'] += vled_k
    q['S'] += sense
    pulldown(gate, GND, '100k', 'gate held off with the op-amp unpowered')
    rs = R('3.3', '0603', '1%', power='1/10 W', why='LED current: 98.8 mV / 3.3 ohm = 30 mA')
    rs[1, 2] += sense, GND
    TP(LCD_BL, 'LCD_BL')
    TP(LCD_TE, 'LCD_TE')
    TP(LCD_3V3, 'LCD_3V3')
    TP(sense, 'BL_SENSE')
