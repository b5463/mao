"""Sheets 06-09: encoder, IMU, haptics, audio.

06 Encoder (EC11E, 15 pulses / 30 detents, push): A -> ENC_A (IO2, ext0), B -> ENC_B (IO6),
   switch -> ENC_SW (IO1, ext1). External pulls on A and SW only, 470 k PROVISIONAL (the
   100 k / 220 k / 470 k / 1 M rig test chooses); B has none (internal pull while awake,
   isolated in deep sleep). 1 nF RC footprints DNP. TPD1E10B06 ESD on each line.
07 IMU ICM-42670-P: VDD 0.1 + 2.2 uF, VDDIO 10 nF (Table 10); AP_AD0 = 0 -> 0x68; AP_CS to
   VDDIO (I2C); FSYNC to GND; RESV pins unconnected; INT1/INT2 open-drain, 100 k pull-ups.
08 Haptics DRV2605L on CORE_3V3 (its I2C/EN/TRIG pins are rated VDD + 0.3 V, so it shares the
   bus rail and is never supply-gated). REG 1 uF, VDD 1 uF (Table 32). EN and TRIG 100 k down.
09 Audio MAX98357A on SYS (2.5-5.5 V). SD_MODE driven directly from AMP_SD = left channel,
   low = shutdown (internal 100 k down). GAIN_SLOT open = 9 dB; the DNP 0 R to GND gives 12 dB.
   VDD 0.1 uF + 10 uF. Outputs through 0 R / DNP 1 nF EMI footprints (ferrite option).
"""
from lib import NCNET as NC

from lib import C, R, TP, decouple, pulldown, pullup, sig
from nets import (AMP_SD, CORE_3V3, ENC_A, ENC_B, ENC_SW, GND, HAPTIC_EN, HAPTIC_TRIG, I2C_SCL, I2C_SDA,
                  I2S_BCLK, I2S_DIN, I2S_WS, IMU_INT1, IMU_INT2, SYS)
from parts_misc import ENCODER, PADS2, TPD1E10B06
from parts_periph import DRV2605L, ICM42670P, MAX98357A


def encoder():
    e = ENCODER()
    e['A'] += ENC_A
    e['B'] += ENC_B
    e['C'] += GND
    e['S1'] += ENC_SW
    e['S2'] += GND
    e['MP'] += GND
    pullup(ENC_A, CORE_3V3, '470k', 'ext0 wake line pull, PROVISIONAL (rig test)')
    pullup(ENC_SW, CORE_3V3, '470k', 'ext1 wake line pull, PROVISIONAL (rig test)')
    for net in (ENC_A, ENC_B, ENC_SW):
        c = C('1nF', 10, diel='X7R', dnp=True, why='RC filter option, only if bounce needs it')
        c[1, 2] += net, GND
        d = TPD1E10B06()
        d[1, 2] += net, GND
        TP(net, net.name)


def imu():
    u = ICM42670P()
    u['VDD'] += CORE_3V3
    u['VDDIO'] += CORE_3V3
    u['GND'] += GND
    u['AP_AD0'] += GND
    u['AP_CS'] += CORE_3V3
    u['FSYNC'] += GND
    for p in ('RESV2', 'RESV3', 'RESV10', 'RESV11'):
        u[p] += NC
    u['INT1'] += IMU_INT1
    u['INT2'] += IMU_INT2
    u['AP_SCL'] += I2C_SCL
    u['AP_SDA'] += I2C_SDA
    decouple(CORE_3V3, GND, '0.1uF', 10, diel='X7R', why='IMU VDD (Table 10)')
    decouple(CORE_3V3, GND, '2.2uF', 10, '0603', diel='X7R', why='IMU VDD (Table 10)')
    decouple(CORE_3V3, GND, '10nF', 10, diel='X7R', why='IMU VDDIO (Table 10)')
    pullup(IMU_INT1, CORE_3V3, '100k', 'open-drain INT1, wake')
    pullup(IMU_INT2, CORE_3V3, '100k', 'open-drain INT2')
    TP(IMU_INT1, 'IMU_INT1')


def haptics():
    u = DRV2605L()
    u['VDD'] += CORE_3V3
    u['VDD/NC'] += CORE_3V3
    u['GND'] += GND
    u['SCL'] += I2C_SCL
    u['SDA'] += I2C_SDA
    u['EN'] += HAPTIC_EN
    u['IN/TRIG'] += HAPTIC_TRIG
    reg = sig('HAP_REG', None)
    u['REG'] += reg
    decouple(reg, GND, '1uF', 10, why='DRV2605L REG (Table 32)')
    decouple(CORE_3V3, GND, '1uF', 10, why='DRV2605L VDD (Table 32)')
    pulldown(HAPTIC_EN, GND, '100k', 'default off (plus internal 2 M)')
    pulldown(HAPTIC_TRIG, GND, '100k', 'default low')
    j = PADS2()
    op, on = sig('LRA_P', None), sig('LRA_N', None)
    u['OUT+'] += op
    u['OUT-'] += on
    j['+'] += op
    j['-'] += on
    j['MP'] += GND
    TP(HAPTIC_EN, 'HAPTIC_EN')


def audio():
    u = MAX98357A()
    u['VDD'] += SYS
    u['VDD2'] += SYS
    for p in ('GND', 'GND2', 'GND3', 'EP'):
        u[p] += GND
    for p in ('NC5', 'NC6', 'NC12', 'NC13'):
        u[p] += NC
    u['DIN'] += I2S_DIN
    u['BCLK'] += I2S_BCLK
    u['LRCLK'] += I2S_WS
    u['SD_MODE'] += AMP_SD
    gain = sig('AMP_GAIN', None)
    u['GAIN_SLOT'] += gain
    R('0', dnp=True, tol='jumper', why='GAIN_SLOT to GND = 12 dB; open = 9 dB')[1, 2] += gain, GND
    decouple(SYS, GND, '0.1uF', 10, why='MAX98357A VDD')
    decouple(SYS, GND, '10uF', 10, '0603', why='MAX98357A VDD bulk')
    j = PADS2()
    j['MP'] += GND
    for pin, pad, name in (('OUTP', '+', 'SPK_P'), ('OUTN', '-', 'SPK_N')):
        a, b = sig(name + '_A', None), sig(name, None)
        u[pin] += a
        R('0', tol='jumper', why='ferrite-bead option (EMI near the antenna)')[1, 2] += a, b
        C('1nF', 10, diel='X7R', dnp=True, why='EMI cap option')[1, 2] += b, GND
        j[pad] += b
    TP(AMP_SD, 'AMP_SD')
