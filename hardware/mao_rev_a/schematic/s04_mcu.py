"""Sheet 04: ESP32-S3-MINI-1-N8, its supply, EN, straps, USB series parts, board ID, I2C pull-ups.

- 3V3: 22 uF + 0.1 uF at the module (MINI-1 v1.7 Fig 9-1); the regulator's 10 uF is the
  power-entry capacitor (HDG 2.1).
- EN: 10 k up + 1 uF (Fig 9-1, HDG 2.3). Fixture pad.
- Straps: IO0 10 k up + BOOT pad (fixture pulls low). IO3, IO45, IO46 unconnected: their
  internal defaults (floating / pull-down / pull-down) give SPI boot and VDD_SPI = 3.3 V.
  Nothing on the board may pull IO45 high (VDD_SPI would switch to 1.8 V).
- IO47/IO48 (I2C): VDD_SPI or VDD3P3_CPU domain by eFuse; both are 3.3 V on the N8
  (VDD_SPI fed from VDD3P3_RTC through RSPI; S3 datasheet v2.2 Table 2-1 note 4).
- USB: 22 R series at the module, 0402 caps to GND reserved (DNP) (HDG 2.13).
- BOARD_ID (IO15): tri-state strap, both resistors DNP = open = Rev A.
- I2C: 4.7 k pull-ups to CORE_3V3, the bus's only pull-ups.
"""
from lib import NCNET as NC

from lib import C, R, TP, decouple, pulldown, pullup
from nets import (AMP_SD, AUX_PWR_EN, BOARD_ID, BOOT, CHG_STAT1, CHG_STAT2, CORE_3V3, EN, ENC_A, ENC_B,
                  ENC_SW, GND, HAPTIC_EN, HAPTIC_TRIG, I2C_SCL, I2C_SDA, I2S_BCLK, I2S_DIN, I2S_WS,
                  IMU_INT1, IMU_INT2, IR_RX, IR_TX, LCD_BL, LCD_CS, LCD_DC, LCD_MOSI, LCD_PWR_EN, LCD_RST,
                  LCD_SCLK, LCD_TE, RGB_DATA, SPARE_IO26, SPARE_IO37, U0RXD, U0TXD, USB_DN, USB_DN_C, USB_DP,
                  USB_DP_C, VBUS_SENSE)
from parts_misc import ESP32S3

GPIO = {
    'IO0': BOOT, 'IO1': ENC_SW, 'IO2': ENC_A, 'IO4': IMU_INT1, 'IO5': IMU_INT2, 'IO6': ENC_B,
    'IO7': LCD_PWR_EN, 'IO8': LCD_BL, 'IO9': LCD_TE, 'IO10': LCD_CS, 'IO11': LCD_MOSI, 'IO12': LCD_SCLK,
    'IO13': LCD_DC, 'IO14': LCD_RST, 'IO15': BOARD_ID, 'IO16': CHG_STAT2, 'IO17': I2S_BCLK, 'IO18': I2S_WS,
    'IO19': USB_DN, 'IO20': USB_DP, 'IO21': I2S_DIN, 'IO26': SPARE_IO26, 'IO33': RGB_DATA, 'IO34': IR_RX,
    'IO35': VBUS_SENSE, 'IO36': CHG_STAT1, 'IO37': SPARE_IO37, 'IO38': AMP_SD, 'IO39': IR_TX,
    'IO40': AUX_PWR_EN, 'IO41': HAPTIC_EN, 'IO42': HAPTIC_TRIG, 'IO43': U0TXD, 'IO44': U0RXD,
    'IO47': I2C_SDA, 'IO48': I2C_SCL,
}
UNUSED = ('IO3', 'IO45', 'IO46')


def build():
    u = ESP32S3()
    for p in u.pins:
        if p.name.startswith('GND'):
            p += GND
    u['3V3'] += CORE_3V3
    u['EN'] += EN
    for pin, net in GPIO.items():
        u[pin] += net
    for pin in UNUSED:
        u[pin] += NC

    decouple(CORE_3V3, GND, '22uF', 10, '0805', why='module bulk (Fig 9-1)')
    decouple(CORE_3V3, GND, '0.1uF', 10, why='module HF (Fig 9-1)')
    pullup(EN, CORE_3V3, '10k', 'EN RC (Fig 9-1)')
    decouple(EN, GND, '1uF', 10, why='EN RC delay (Fig 9-1)')
    pullup(BOOT, CORE_3V3, '10k', 'IO0 strap = 1 (SPI boot)')

    for a, b in ((USB_DP_C, USB_DP), (USB_DN_C, USB_DN)):
        r = R('22', why='USB series, initial value (HDG 2.13)')
        r[1, 2] += a, b
        c = C('10pF', 10, diel='C0G', tol='5%', dnp=True, why='reserved (HDG 2.13)')
        c[1, 2] += b, GND

    pullup(BOARD_ID, CORE_3V3, '10k', 'BOARD_ID high option').dnp = True
    pulldown(BOARD_ID, GND, '10k', 'BOARD_ID low option').dnp = True

    pullup(I2C_SDA, CORE_3V3, '4.7k', 'I2C SDA, 400 kHz')
    pullup(I2C_SCL, CORE_3V3, '4.7k', 'I2C SCL, 400 kHz')
    for net, name in ((EN, 'EN'), (BOOT, 'BOOT'), (U0TXD, 'U0TXD'), (U0RXD, 'U0RXD'),
                      (SPARE_IO26, 'IO26'), (SPARE_IO37, 'IO37'), (BOARD_ID, 'BOARD_ID'),
                      (I2C_SDA, 'SDA'), (I2C_SCL, 'SCL')):
        TP(net, name)
