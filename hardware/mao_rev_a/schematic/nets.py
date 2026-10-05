"""Every net that crosses a sheet boundary, with its rail range or logic domain.

Rail ranges are the operating envelope the checks use:
  VBUS      USB input (4.4-5.5 V; the charger's IN is 25 V tolerant)
  VBAT      the cell after its protection board, 3.0-4.2 V (BQ25185 BATFET off below 3.0 V)
  SYS       BQ25185 SYS: 4.5 V regulated on USB, else VBAT minus the BATFET drop
  VIN_REG   SYS after the LINK_REG measurement link (TPS62840 VIN)
  CORE_3V3  TPS62840 output; follows VIN in 100 % mode below ~3.5 V (see the audit)
  LCD_3V3   CORE_3V3 through the display load switch
  AUX_SYS   SYS through the aux switch (RGB)
  AUX_3V3   CORE_3V3 through the aux switch (IR receiver)
"""
from lib import rail, sig

GND = rail('GND', 0, 0)
VBUS = rail('VBUS', 4.4, 5.5, 'USB-C VBUS after the input TVS')
BATT_P = rail('BATT_P', 3.0, 4.2, 'battery connector +, before LINK_BAT')
VBAT = rail('VBAT', 3.0, 4.2, 'cell + after LINK_BAT')
SYS = rail('SYS', 2.9, 4.6, 'BQ25185 SYS')
VIN_REG = rail('VIN_REG', 2.9, 4.6, 'TPS62840 VIN after LINK_REG')
CORE_3V3 = rail('CORE_3V3', 3.0, 3.4, 'TPS62840 3.3 V (3.0 V is the firmware floor, see audit)')
LCD_3V3 = rail('LCD_3V3', 0, 3.4, 'panel VCI, gated (0 V when off)')
AUX_SYS = rail('AUX_SYS', 0, 4.6, 'RGB LED supply, gated (0 V when off)')
AUX_3V3 = rail('AUX_3V3', 0, 3.4, 'IR receiver supply, gated (0 V when off)')

GATED = ('LCD_3V3', 'AUX_SYS', 'AUX_3V3', 'IR_VS')

# never_above pairs for checks.check_domains: (low, high) by topology
NEVER_ABOVE = {('CORE_3V3', 'SYS'), ('CORE_3V3', 'VIN_REG'), ('LCD_3V3', 'SYS'), ('AUX_3V3', 'SYS'),
               ('LCD_3V3', 'CORE_3V3'), ('AUX_3V3', 'CORE_3V3'), ('IR_VS', 'AUX_3V3')}

# USB (connector side _C, MCU side after the series resistors)
USB_DP_C = sig('USB_DP_C', 'CORE_3V3', 'connector D+')
USB_DN_C = sig('USB_DN_C', 'CORE_3V3', 'connector D-')
USB_DP = sig('USB_DP', 'CORE_3V3', 'IO20, 90 ohm diff')
USB_DN = sig('USB_DN', 'CORE_3V3', 'IO19, 90 ohm diff')
VBUS_SENSE = sig('VBUS_SENSE', 'CORE_3V3', 'IO35, VBUS divider')

# charger
CHG_STAT1 = sig('CHG_STAT1', 'CORE_3V3', 'IO36')
CHG_STAT2 = sig('CHG_STAT2', 'CORE_3V3', 'IO16')

# I2C (CORE_3V3 pull-ups): MAX17048 0x36, DRV2605L 0x5A, ICM-42670-P 0x68
I2C_SDA = sig('I2C_SDA', 'CORE_3V3', 'IO47')
I2C_SCL = sig('I2C_SCL', 'CORE_3V3', 'IO48')

# MCU service
EN = sig('EN', 'CORE_3V3', 'module EN / reset')
BOOT = sig('BOOT', 'CORE_3V3', 'IO0 strap')
BOARD_ID = sig('BOARD_ID', 'CORE_3V3', 'IO15 tri-state strap')
U0TXD = sig('U0TXD', 'CORE_3V3', 'IO43 spare / factory log')
U0RXD = sig('U0RXD', 'CORE_3V3', 'IO44 spare')
SPARE_IO26 = sig('SPARE_IO26', 'CORE_3V3', 'IO26 spare pad')
SPARE_IO37 = sig('SPARE_IO37', 'CORE_3V3', 'IO37 spare pad')

# encoder and IMU
ENC_SW = sig('ENC_SW', 'CORE_3V3', 'IO1 ext1')
ENC_A = sig('ENC_A', 'CORE_3V3', 'IO2 ext0')
ENC_B = sig('ENC_B', 'CORE_3V3', 'IO6')
IMU_INT1 = sig('IMU_INT1', 'CORE_3V3', 'IO4 ext1')
IMU_INT2 = sig('IMU_INT2', 'CORE_3V3', 'IO5')

# display
LCD_PWR_EN = sig('LCD_PWR_EN', 'CORE_3V3', 'IO7')
LCD_BL = sig('LCD_BL', 'CORE_3V3', 'IO8 LEDC PWM')
LCD_TE = sig('LCD_TE', 'LCD_3V3', 'IO9 from the panel')
LCD_CS = sig('LCD_CS', 'CORE_3V3', 'IO10')
LCD_MOSI = sig('LCD_MOSI', 'CORE_3V3', 'IO11')
LCD_SCLK = sig('LCD_SCLK', 'CORE_3V3', 'IO12')
LCD_DC = sig('LCD_DC', 'CORE_3V3', 'IO13')
LCD_RST = sig('LCD_RST', 'CORE_3V3', 'IO14')

# audio
I2S_BCLK = sig('I2S_BCLK', 'CORE_3V3', 'IO17')
I2S_WS = sig('I2S_WS', 'CORE_3V3', 'IO18')
I2S_DIN = sig('I2S_DIN', 'CORE_3V3', 'IO21')
AMP_SD = sig('AMP_SD', 'CORE_3V3', 'IO38')

# haptics
HAPTIC_EN = sig('HAPTIC_EN', 'CORE_3V3', 'IO41')
HAPTIC_TRIG = sig('HAPTIC_TRIG', 'CORE_3V3', 'IO42')

# IR / RGB
RGB_DATA = sig('RGB_DATA', 'CORE_3V3', 'IO33')
IR_RX = sig('IR_RX', 'IR_VS', 'IO34, receiver output (pulled up inside to its VS)')
IR_TX = sig('IR_TX', 'CORE_3V3', 'IO39')
AUX_PWR_EN = sig('AUX_PWR_EN', 'CORE_3V3', 'IO40')

# hardware default-off enables (checks.check_default_off)
ENABLES = ['LCD_PWR_EN', 'LCD_BL', 'AMP_SD', 'HAPTIC_EN', 'IR_TX', 'AUX_PWR_EN', 'HAPTIC_TRIG']
I2C_ADDR = {'MAX17048': 0x36, 'DRV2605L': 0x5A, 'ICM-42670-P': 0x68}
