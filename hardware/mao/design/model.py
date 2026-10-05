"""Circuit model shared by the schematic and PCB generators.

One Python description (circuit.py) is the single source of truth: the
schematic, the PCB netlist, the BOM and the CPL are all generated from it, so
they cannot drift apart. Every pin of every part must be connected to a net or
explicitly declared NC; anything else is a generator error.
"""
import uuid as _uuid

import kicadlib

NC = None   # explicit no-connect marker


def stable_uuid(*parts):
    """Deterministic UUIDs keep regenerated files diff-friendly."""
    return str(_uuid.uuid5(_uuid.NAMESPACE_URL, 'oddjobs/mao/' + '/'.join(str(p) for p in parts)))


class Part:
    def __init__(self, ref, symbol, footprint, value, conns, sheet, block,
                 lcsc='', mpn='', mfr='', dnp=False, note='', pcb=None, fields=None,
                 symbol_node=None):
        self.ref = ref
        self.symbol = symbol            # lib_id, e.g. "Device:R" or "MAO:BQ24074"
        self.footprint = footprint      # "Lib:Name"
        self.value = value
        self.conns = conns              # {pin number or pin name: net name or NC}
        self.sheet = sheet
        self.block = block
        self.lcsc = lcsc
        self.mpn = mpn
        self.mfr = mfr
        self.dnp = dnp
        self.note = note
        self.pcb = pcb or {}            # x, y, rot, side ('F'/'B')
        self.fields = fields or {}
        self.symbol_node = symbol_node  # for custom symbols
        self.pin_net = {}               # resolved: pin number -> net (None = NC)

    @property
    def uuid(self):
        return stable_uuid('sym', self.ref)


class Circuit:
    def __init__(self):
        self.parts = []
        self.custom_symbols = {}        # lib_id -> node
        self.net_classes = {}           # net -> class name
        self.sheets = []                # (file stem, title, description)
        self.blocks = {}                # (sheet, block) -> description text

    def add(self, part):
        assert part.ref not in {p.ref for p in self.parts}, 'duplicate ref ' + part.ref
        self.parts.append(part)
        return part

    def symbol_node(self, lib_id):
        if lib_id in self.custom_symbols:
            return self.custom_symbols[lib_id]
        return kicadlib.library_symbol(lib_id)

    def resolve(self):
        """Map every pin to a net; fail loudly on anything ambiguous."""
        errors = []
        for p in self.parts:
            pins = kicadlib.symbol_pins(self.symbol_node(p.symbol))
            by_num = {}
            for num, name, etype, *_ in pins:
                by_num.setdefault(num, (name, etype))
            used = set()
            for key, net in p.conns.items():
                key = str(key)
                if key in by_num:
                    nums = [key]
                else:
                    nums = sorted({num for num, name, *_ in pins if name == key})
                if not nums:
                    errors.append('%s: no pin %r' % (p.ref, key))
                    continue
                for n in nums:
                    if n in p.pin_net and p.pin_net[n] != net:
                        errors.append('%s pin %s connected twice (%s, %s)' % (p.ref, n, p.pin_net[n], net))
                    p.pin_net[n] = net
                    used.add(n)
            for num in by_num:
                if num not in used:
                    errors.append('%s: pin %s (%s) not connected and not declared NC'
                                  % (p.ref, num, by_num[num][0]))
        if errors:
            raise SystemExit('circuit errors:\n  ' + '\n  '.join(errors))

    def nets(self):
        out = {}
        for p in self.parts:
            for num, net in p.pin_net.items():
                if net is not None:
                    out.setdefault(net, []).append((p.ref, num))
        return out

    def check_nets(self, allow_single=()):
        """Nets with a single connection are almost always mistakes."""
        bad = [n for n, c in self.nets().items() if len(c) < 2 and n not in allow_single]
        if bad:
            raise SystemExit('single-pin nets: ' + ', '.join(sorted(bad)))
