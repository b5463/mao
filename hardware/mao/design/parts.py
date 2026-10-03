"""Part helpers and the verified part catalogue for MAO_MAIN A0.

Every LCSC number here was checked against JLCPCB/LCSC on 2026-10-03 (research notes in
docs/hardware/mao-bom-notes.md). Values without a verified number carry lcsc='' and are filled
by bom_verify.py, which refuses to release a BOM with an empty or unverified C-number.
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
    ('3.0k', '0402'): ('C25784', '0402WGF3001TCE', 'UNI-ROYAL'),
    ('560k', '0402'): ('C132339', '0402WGF5603TCE', 'UNI-ROYAL'),
    ('510k', '0402'): ('C11616', '0402WGF5103TCE', 'UNI-ROYAL'),
    ('0R', '1206'): ('', 'RC1206JR-070RL', 'YAGEO'),
    ('100n', '0402'): ('C1525', 'CL05B104KO5NNNC', 'Samsung'),
    ('1u', '0402'): ('C52923', 'CL05A105KA5NQNC', 'Samsung'),
    ('2.2u', '0402'): ('C12530', 'CL05A225MQ5NSNC', 'Samsung'),
    ('10u', '0603'): ('C19702', 'CL10A106KP8NNNC', 'Samsung'),
    ('22u', '0603'): ('C59461', 'CL10A226MQ8NRNC', 'Samsung'),
    ('4.7u', '0603'): ('C19666', 'CL10A475KO8NNNC', 'Samsung'),
    ('10u', '0805'): ('C15850', 'CL21A106KAYNNNE', 'Samsung'),
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
        lcsc, mpn, mfr = PASSIVES.get((value, pkg), ('', '', ''))
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
            voltage = {'0402': '16V' if value == '100n' else '25V' if value == '1u' else '6.3V',
                       '0603': '10V' if value == '10u' else '6.3V' if value == '22u' else '16V',
                       '0805': '25V'}[pkg]
        return self._passive('C', ref, value, a, b, pkg, voltage=voltage, note=note, dnp=dnp)

    def TP(self, ref, net, label, size='1.5'):
        """Test pad (no fitted part). label is the silkscreen name (ODD JOBS 103)."""
        return self.part(ref, 'Connector:TestPoint', 'TestPoint:TestPoint_Pad_D%smm' % size, label,
                         {'1': net}, mpn='PCB feature, no part', note='test pad ' + label,
                         **{'Exclude from BOM': 'yes'})
