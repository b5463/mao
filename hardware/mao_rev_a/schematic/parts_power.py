"""IC templates: charger, regulator, load switch, fuel gauge (pinouts from the vendor datasheets cited)."""
from lib import IN, IO, NC, OC, OUT, PAS, POUT, PWR, ic

BQ25185 = ic('BQ25185', 'U', [
    (1, 'SYS', POUT), (2, 'BAT', PWR), (3, 'STAT2', OC), (4, 'CE', IN), (5, 'GND', PWR),
    (6, 'TS/MR', PAS), (7, 'ILIM/VSET', PAS), (8, 'ISET', PAS), (9, 'STAT1', OC), (10, 'IN', PWR),
    (11, 'EP', PAS)],
    'Package_DFN_QFN:Texas_DLH0010A_WSON-10-1EP_2.2x2mm_P0.4mm_EP0.9x1.5mm',
    'BQ25185DLHR', 'Texas Instruments', 'SLUSF65B Table 4-1 p4')

TPS62840 = ic('TPS62840', 'U', [
    (1, 'GND', PWR), (2, 'VIN', PWR), (3, 'MODE', IN), (4, 'EN', IN, {'pd': '450k'}),
    (5, 'VSET', PAS), (6, 'STOP', IN, {'pd': 'internal'}), (7, 'SW', OUT), (8, 'VOS', PAS)],
    'MAO_RevA:TI_DLC0008B_VSON-HR-8_1.5x2mm_P0.5mm',
    'TPS62840DLCR', 'Texas Instruments', 'SLVSEC6D p5 (DLC)')

# YFP WCSP-4: A1 VOUT, A2 VIN, B1 GND, B2 ON.  Pads = YFP0004 land example (4x D0.23 NSMD, 0.4 pitch).
TPS22916 = ic('TPS22916C', 'U', [
    ('A1', 'VOUT', POUT), ('A2', 'VIN', PWR), ('B1', 'GND', PWR), ('B2', 'ON', IN, {'pd': '750k smart'})],
    'Package_BGA:Texas_PicoStar_BGA-4_0.758x0.758mm_Layout2x2_P0.4mm',
    'TPS22916CYFPR', 'Texas Instruments', 'SLVSDO5F p3; C = slow rise, QOD, active high')

MAX17048 = ic('MAX17048', 'U', [
    (1, 'CTG', PAS), (2, 'CELL', PWR), (3, 'VDD', PWR), (4, 'GND', PWR), (5, 'ALRT', OC),
    (6, 'QSTRT', IN), (7, 'SCL', IN), (8, 'SDA', IO), (9, 'EP', PAS)],
    'Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm',
    'MAX17048G+T10', 'Analog Devices (Maxim)', 'MAX17048/9 datasheet p6, address 0x36 p16')
