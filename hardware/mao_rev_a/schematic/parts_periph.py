"""IC templates: IMU, haptic driver, amplifier (pinouts from the vendor datasheets cited)."""
from lib import IN, IO, NC, OC, OUT, PAS, PWR, ic

ICM42670P = ic('ICM-42670-P', 'U', [
    (1, 'AP_AD0', IN, {'lim': 'VDDIO'}), (2, 'RESV2', PAS), (3, 'RESV3', PAS),
    (4, 'INT1', OC, {'lim': 'VDDIO'}), (5, 'VDDIO', PWR), (6, 'GND', PWR), (7, 'FSYNC', IN),
    (8, 'VDD', PWR), (9, 'INT2', OC, {'lim': 'VDDIO'}), (10, 'RESV10', PAS), (11, 'RESV11', PAS),
    (12, 'AP_CS', IN), (13, 'AP_SCL', IN, {'lim': 'VDDIO'}), (14, 'AP_SDA', IO, {'lim': 'VDDIO'})],
    'Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y',
    'ICM-42670-P', 'TDK InvenSense', 'DS-000451 r1.0 Table 9 p18; INT open-drain via INT_CONFIG')

DRV2605L = ic('DRV2605L', 'U', [
    (1, 'REG', PAS), (2, 'SCL', IN, {'lim': 'VDD'}), (3, 'SDA', IO, {'lim': 'VDD'}),
    (4, 'IN/TRIG', IN, {'lim': 'VDD'}), (5, 'EN', IN, {'lim': 'VDD', 'pd': '2M'}),
    (6, 'VDD/NC', PWR), (7, 'OUT+', OUT), (8, 'GND', PWR), (9, 'OUT-', OUT), (10, 'VDD', PWR)],
    'Package_SO:MSOP-10_3x3mm_P0.5mm',
    'DRV2605LDGSR', 'Texas Instruments', 'SLOS854D p5 (DGS)')

MAX98357A = ic('MAX98357A', 'U', [
    (1, 'DIN', IN, {'lim': 'VDD'}), (2, 'GAIN_SLOT', PAS), (3, 'GND', PWR), (4, 'SD_MODE', IN, {'pd': '100k', 'lim': 'VDD'}),
    (5, 'NC5', NC), (6, 'NC6', NC), (7, 'VDD', PWR), (8, 'VDD2', PWR), (9, 'OUTP', OUT),
    (10, 'OUTN', OUT), (11, 'GND2', PWR), (12, 'NC12', NC), (13, 'NC13', NC), (14, 'LRCLK', IN, {'lim': 'VDD'}),
    (15, 'GND3', PWR), (16, 'BCLK', IN, {'lim': 'VDD'}), (17, 'EP', PAS)],
    'Package_DFN_QFN:TQFN-16-1EP_3x3mm_P0.5mm_EP1.23x1.23mm',
    'MAX98357AETE+T', 'Analog Devices (Maxim)', 'MAX98357A/B datasheet p17 (TQFN), Table 8 p30')
