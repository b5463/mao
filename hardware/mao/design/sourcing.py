"""JLCPCB/LCSC sourcing status of every MAO_MAIN A1 part (owner rule 2026-10-06: every part preferably from
JLCPCB/LCSC, the parts that are not on the board included).

Data from the JLCPCB parts search (assembly library: Basic / Preferred Extended / Extended, JLC assembly stock)
and the LCSC product pages on 2026-10-06. fab.py adds the type, the stock and the note to every line of the
JLC BOM and refuses a BOM line whose LCSC number is not listed here; it writes OFFBOARD as its own CSV
("Bought separately - LCSC"). A C-number that could not be confirmed on LCSC is written UNKNOWN, never guessed.
"""

CHECKED = '2026-10-06'

# LCSC -> (JLC library type, JLC assembly stock on 2026-10-06, note)
JLC = {
    'C165948': ('Extended', 420357, ''),                    # TYPE-C-31-M-12
    'C25905': ('Basic', 5900000, ''),                       # 5.1k
    'C123802': ('Extended', 61610, ''),                     # SMF15A
    'C1972959': ('Extended', 7569, ''),                     # TPD2E2U06DRLR
    'C19725033': ('Extended', 3623, ''),                    # BQ25185DLHR
    'C52923': ('Basic', 8300000, ''),                       # 1u 0402
    'C96446': ('Basic', 3300000, ''),                       # 10u 0603 25 V
    'C163483': ('Extended', 9576, 'no Basic 1.43k 0402'),
    'C25762': ('Preferred Extended', 468801, 'no Basic 18k 0402'),
    'C7430445': ('Extended', 90750, 'JST SH-compatible 3-pin (battery)'),
    'C17888': ('Basic', 2600000, ''),                       # 0R 1206
    'C15127': ('Basic', 814915, ''),                        # AO3401A
    'C25744': ('Basic', 21500000, ''),                      # 10k
    'C2682616': ('Extended', 25799, ''),                    # MAX17048G+T10
    'C1525': ('Basic', 22000000, 'LCSC shop stock 0 on 2026-10-06; JLC assembly stock fine'),
    'C2071859': ('Extended', 3882, ''),                     # TPS62840DLCR
    'C337893': ('Extended', 33137, ''),                     # DFE201612E-2R2M
    'C19666': ('Basic', 2500000, ''),                       # 4.7u 0603
    'C19702': ('Basic', 10300000, ''),                      # 10u 0603
    'C2933066': ('Extended', 177764, 'no Basic 102k 0402'),
    'C2680319': ('Extended', 77, 'LOW STOCK (2 per board, enough for the 5-unit build); 0.4 mm WCSP: order JLC Standard '
                 'PCBA. Fallback TPS22916BYFPR C2150095 (21,486): fast rise, check the 3V3_LCD / AUX_3V3 inrush'),
    'C25741': ('Basic', 8200000, ''),                       # 100k
    'C2913206': ('Extended', 6018, ''),                     # ESP32-S3-MINI-1-N8
    'C59461': ('Basic', 8000000, ''),                       # 22u 0603
    'C25900': ('Basic', 15400000, ''),                      # 4.7k
    'C2919497': ('Extended', 16270, 'listed as HDGC 0.5K-HX-18PWB (same part)'),
    'C25092': ('Basic', 4600000, ''),                       # 22R
    'C2655038': ('Extended', 5226, ''),                     # DRV5012AEDMRR
    'C116647': ('Extended', 66138, ''),                     # SKQGADE010
    'C3288646': ('Extended', 6692, ''),                     # ICM-42670-P
    'C15195': ('Basic', 3900000, ''),                       # 10n 0402 50 V X7R
    'C3178291': ('Extended', 11527, ''),                    # VL53L4CDV0DH/1
    'C910544': ('Extended', 25277, ''),                     # MAX98357AETE+T
    'C425927': ('Extended', 858, 'DRV2605LDGST: the DGSR part on a 250-piece reel (review 2026-10-06)'),
    'C53672': ('Extended', 12341, ''),                      # IR12-21C/TR8
    'C25196': ('Preferred Extended', 1070000, 'no Basic 56R 0603'),
    'C20917': ('Basic', 953963, ''),                        # AO3400A
    'C8545': ('Basic', 1576966, ''),                        # 2N7002 (USB presence inverter, review 2026-10-06)
    'C25076': ('Basic', 4000000, 'LCSC shop stock 0 on 2026-10-06; JLC assembly stock fine'),
    'C91447': ('Extended', 177048, ''),                     # IRM-H638T/TR2
    'C25755': ('Preferred Extended', 270669, 'no Basic 150k 0402'),
    'C21189': ('Basic', 23600000, ''),                      # 0R 0603
    'C26974': ('Extended', 37795, 'no Basic 32.4k 0402'),
    'C11702': ('Basic', 7000000, 'LCSC shop stock 0 on 2026-10-06; JLC assembly stock fine'),
    'C398358': ('Extended', 289451, ''),                    # TLV9061IDBVR
    'C5224573': ('Extended', 990, 'DMG2302UKQ-7: the automotive-qualified DMG2302UK-7 (C460977, 124 left), same '
                 'SOT-23 pinout'),
    'C22979': ('Extended', 280671, 'no Basic 3.3R 0603'),
    'C100082': ('Extended', 354360, 'no Basic 2.2u X7R 0603 (Basic C23630 is X5R)'),
    'C307418': ('Extended', 897637, 'no Basic/Preferred 2.2u 25 V 0402 in the JLC library (review 2026-10-06)'),
    # DNP lines (not in the JLC BOM; listed so a fitted variant has its status)
    'C318588': ('Extended', 14698, 'DNP'),                  # 10p C0G
    'C26409': ('Extended', 809562, 'DNP'),                  # 100p C0G
    'C14442': ('Extended', 1900000, 'DNP'),                 # 1n X7R
}

# Bought separately - LCSC (not assembled on the board): (item, LCSC, MPN, maker, qty, status)
OFFBOARD = [
    ('Display 1.28in round GC9A01 IPS, 18-pin FPC (J301)', 'UNKNOWN', 'WF0128BTYAA4DNN0 (Gate C)', 'Winstar', 1,
     'not on LCSC. Closest LCSC listing C17215183 PG1301HA-ZA0 (Pacific Goal, GC9A01): stock 0, pinout/TE/'
     'backlight/FPC UNKNOWN - owner decision'),
    ('LRA 8 mm coin, wire leads (J501)', 'C2682305', 'LD0832AA-0099F', 'LEADER', 1,
     'JLC stock 1,093; 1.8 V, 80 mA; resonant frequency UNKNOWN (DRV2605L auto-resonance tracks it); '
     'alternatives C2942349 LD0832AA-0126F (107), C5632413 LD0825BC-0168F (41)'),
    ('Speaker 15 x 8 mm, 8 ohm, spring contacts (LS501)', 'C20181964', 'CMS-150803-088S-X8', 'CUI', 1,
     'JLC stock 14 (very low); no alternative found on LCSC'),
    ('LiPo cell LP503035 class, 500 mAh, with PCM and 10k NTC', 'UNKNOWN', '', '', 1,
     'not found on LCSC - owner decision (buy outside LCSC)'),
    ('Battery housing, JST SH 1.0 mm 3-pin', 'C268100', 'SHR-03V-S-B', 'JST', 1,
     'stock 5,786; mates the SH-compatible header C7430445 on the board (fit of genuine JST to the clone: check)'),
    ('Battery crimp contacts, SH', 'C189897', 'SSHL-002T-P0.2', 'JST', 3, 'stock 841,495; or pre-crimped cable '
     'C54529088 SH1.0-3P-1-100(3)-28A (HanElectricity, 865; wiring UNKNOWN)'),
    ('LRA / speaker leads', 'none needed', '', '', 0, 'the LRA wires solder to the J501 pads, the speaker springs press on the LS501 pads'),
]
