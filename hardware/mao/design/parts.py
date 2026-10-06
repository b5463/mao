"""Part helpers and the verified part catalogue for MAO_MAIN A1.

Every LCSC number here was checked against JLCPCB/LCSC on 2026-10-03; 470k, 240k and 56R were added
on 2026-10-05, the A1 values on 2026-10-06 (JLCPCB parts search: part number, value, package and stock).
The JLC Basic/Extended status and stock of every line is in sourcing.py and docs/hardware/mao-a1-report.md.
fab.py refuses to write a BOM line without an LCSC number, so a value missing from this table stops the release.
"""
from model import Part

# (value, package) -> (LCSC, MPN, manufacturer)
PASSIVES = {
    ('10k', '0402'): ('C25744', '0402WGF1002TCE', 'UNI-ROYAL'),
    ('5.1k', '0402'): ('C25905', '0402WGF5101TCE', 'UNI-ROYAL'),
    ('4.7k', '0402'): ('C25900', '0402WGF4701TCE', 'UNI-ROYAL'),
    ('2.2k', '0402'): ('C25879', '0402WGF2201TCE', 'UNI-ROYAL'),
    ('100k', '0402'): ('C25741', '0402WGF1003TCE', 'UNI-ROYAL'),
    ('1k', '0402'): ('C11702', '0402WGF1001TCE', 'UNI-ROYAL'),
    ('0R', '0402'): ('C17168', '0402WGF0000TCE', 'UNI-ROYAL'),
    ('1.5k', '0402'): ('C25867', '0402WGF1501TCE', 'UNI-ROYAL'),
    ('1M', '0402'): ('C26083', '0402WGF1004TCE', 'UNI-ROYAL'),
    ('4.3k', '0402'): ('C25899', '0402WGF4301TCE', 'UNI-ROYAL'),
    ('536k', '0402'): ('C68457', '0402WGF5363TCE', 'UNI-ROYAL'),
    ('470k', '0402'): ('C25790', '0402WGF4703TCE', 'UNI-ROYAL'),
    ('240k', '0402'): ('C64043', '0402WGF2403TCE', 'UNI-ROYAL'),
    ('0R', '1206'): ('C17888', '1206W4F0000T5E', 'UNI-ROYAL'),
    ('22R', '0402'): ('C25092', '0402WGF220JTCE', 'UNI-ROYAL'),
    ('100R', '0402'): ('C25076', '0402WGF1000TCE', 'UNI-ROYAL'),
    ('510R', '0402'): ('C25123', '0402WGF5100TCE', 'UNI-ROYAL'),
    ('10R', '0603'): ('C22859', '0603WAF100JT5E', 'UNI-ROYAL'),
    ('56R', '0603'): ('C25196', '0603WAF560JT5E', 'UNI-ROYAL'),
    ('100n', '0402'): ('C1525', 'CL05B104KO5NNNC', 'Samsung'),
    ('1u', '0402'): ('C52923', 'CL05A105KA5NQNC', 'Samsung'),
    ('2.2u', '0402'): ('C12530', 'CL05A225MQ5NSNC', 'Samsung'),
    ('10u', '0603'): ('C19702', 'CL10A106KP8NNNC', 'Samsung'),
    ('22u', '0603'): ('C59461', 'CL10A226MQ8NRNC', 'Samsung'),
    ('4.7u', '0603'): ('C19666', 'CL10A475KO8NNNC', 'Samsung'),
    ('10u', '0805'): ('C15850', 'CL21A106KAYNNNE', 'Samsung'),
    # A1 (2026-10-06)
    ('18k', '0402'): ('C25762', '0402WGF1802TCE', 'UNI-ROYAL'),
    ('1.43k', '0402'): ('C163483', 'RC0402FR-071K43L', 'YAGEO'),        # UNI-ROYAL C11671: 176 in stock
    ('102k', '0402'): ('C2933066', 'FRC0402F1023TS', 'FOJAN'),          # 1 %, 100 ppm; UNI-ROYAL C26991 had 4
    ('32.4k', '0402'): ('C26974', '0402WGF3242TCE', 'UNI-ROYAL'),
    ('1.0k', '0402'): ('C11702', '0402WGF1001TCE', 'UNI-ROYAL'),
    ('150k', '0402'): ('C25755', '0402WGF1503TCE', 'UNI-ROYAL'),
    ('3.3R', '0603'): ('C22979', '0603WAF330KT5E', 'UNI-ROYAL'),         # 3.3 ohm 1 %
    ('0R', '0603'): ('C21189', '0603WAF0000T5E', 'UNI-ROYAL'),           # 1 A jumper
    ('10n', '0402'): ('C15195', 'CL05B103KB5NNNC', 'Samsung'),           # X7R 50 V, JLC Basic (C318577 16 V is Extended)
    ('100p', '0402'): ('C26409', 'CL05C101JB5NNNC', 'Samsung'),          # C0G 50 V
    ('10p', '0402'): ('C318588', 'CL05C100CB5NNNC', 'Samsung'),          # C0G 50 V
    ('1n', '0402'): ('C14442', 'CL05B102KB5NNNC', 'Samsung'),            # X7R 50 V
    ('2.2u', '0603', '10V'): ('C100082', 'CL10B225KP8NNNC', 'Samsung'),  # X7R 10 V
    ('10u', '0603', '25V'): ('C96446', 'CL10A106MA8NRNC', 'Samsung'),    # X5R 25 V (charger SYS, TI: 25 V)
    ('2.2u', '0402', '25V'): ('C307418', 'CL05A225KA5NUNC', 'Samsung'),  # X5R 25 V (charger IN/BAT, review 2026-10-06)
}

FP = {
    'R0402': 'Resistor_SMD:R_0402_1005Metric', 'C0402': 'Capacitor_SMD:C_0402_1005Metric',
    'R0603': 'Resistor_SMD:R_0603_1608Metric', 'C0603': 'Capacitor_SMD:C_0603_1608Metric',
    'C0805': 'Capacitor_SMD:C_0805_2012Metric', 'R1206': 'Resistor_SMD:R_1206_3216Metric',
}


class Builder:
    """Small DSL: b.R('R1', '10k', 'A', 'B') adds a resistor to the current sheet/block."""

    def __init__(self, circuit):
        self.c = circuit
        self.sheet = None
        self.block = None

    def at(self, sheet, block, desc=''):
        self.sheet, self.block = sheet, block
        if desc:
            self.c.blocks[(sheet, block)] = desc
        return self

    def part(self, ref, symbol, footprint, value, conns, lcsc='', mpn='', mfr='', dnp=False, note='',
             datasheet='', symbol_node=None, **fields):
        f = dict(fields)
        if datasheet:
            f['Datasheet'] = datasheet
        if note:
            f['Description'] = note
        return self.c.add(Part(ref, symbol, footprint, value, conns, self.sheet, self.block,
                               lcsc=lcsc, mpn=mpn, mfr=mfr, dnp=dnp, note=note, fields=f,
                               symbol_node=symbol_node))

    def _passive(self, kind, ref, value, a, b, pkg, voltage='', note='', dnp=False):
        lcsc, mpn, mfr = PASSIVES.get((value, pkg, voltage), PASSIVES.get((value, pkg), ('', '', '')))
        symbol = 'Device:R' if kind == 'R' else 'Device:C'
        disp = value + ('' if kind == 'R' else 'F') if value != '0R' else '0R'
        if voltage:
            disp += ' ' + voltage
        return self.part(ref, symbol, FP[kind + pkg], disp, {'1': a, '2': b}, lcsc=lcsc, mpn=mpn,
                         mfr=mfr, note=note, dnp=dnp)

    def R(self, ref, value, a, b, pkg='0402', note='', dnp=False):
        return self._passive('R', ref, value, a, b, pkg, note=note, dnp=dnp)

    def C(self, ref, value, a, b='GND', pkg='0402', voltage='', note='', dnp=False):
        if not voltage:
            voltage = {'0402': '16V' if value == '100n' else '25V' if value == '1u' else
                               '50V' if value in ('100p', '10p', '1n', '10n') else '6.3V',
                       '0603': '10V' if value == '10u' else '6.3V' if value == '22u' else '16V',
                       '0805': '25V'}[pkg]
        return self._passive('C', ref, value, a, b, pkg, voltage=voltage, note=note, dnp=dnp)

    def TP(self, ref, net, label, size='1.5'):
        """Test pad (no fitted part). label is the silkscreen name (ODD JOBS 103)."""
        lib = 'MAO' if size == '1.2' else 'TestPoint'          # no 1.2 mm round pad in the stock library
        return self.part(ref, 'Connector:TestPoint', '%s:TestPoint_Pad_D%smm' % (lib, size), label,
                         {'1': net}, mpn='PCB feature, no part', note='test pad ' + label,
                         **{'Exclude from BOM': 'yes'})
