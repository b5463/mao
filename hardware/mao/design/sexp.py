"""Minimal KiCad S-expression reader/writer.

Lists are Python lists. Atoms are either `Q` (a quoted string in the file) or
plain `str` (an unquoted token: keywords, numbers, yes/no). Serialisation
follows KiCad's tab-indented layout closely enough for diffs to stay readable.
Works on the Python bundled with KiCad (3.9) and on newer Pythons.
"""


class Q(str):
    """A string that is written quoted."""


def parse(text):
    pos = 0
    n = len(text)
    stack = [[]]
    while pos < n:
        c = text[pos]
        if c in ' \t\r\n':
            pos += 1
        elif c == '(':
            stack.append([])
            pos += 1
        elif c == ')':
            done = stack.pop()
            stack[-1].append(done)
            pos += 1
        elif c == '"':
            pos += 1
            buf = []
            while True:
                c = text[pos]
                if c == '\\':
                    nxt = text[pos + 1]
                    buf.append({'n': '\n', 't': '\t', '\\': '\\', '"': '"'}.get(nxt, nxt))
                    pos += 2
                elif c == '"':
                    pos += 1
                    break
                else:
                    buf.append(c)
                    pos += 1
            stack[-1].append(Q(''.join(buf)))
        else:
            start = pos
            while pos < n and text[pos] not in ' \t\r\n()"':
                pos += 1
            stack[-1].append(text[start:pos])
    assert len(stack) == 1, "unbalanced parentheses"
    return stack[0][0] if len(stack[0]) == 1 else stack[0]


def _atom(a):
    if isinstance(a, Q):
        return '"' + a.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
    if isinstance(a, bool):
        return 'yes' if a else 'no'
    if isinstance(a, float):
        s = ('%.6f' % a).rstrip('0').rstrip('.')
        return '0' if s in ('-0', '') else s
    return str(a)


# Lists whose children all fit on one line are written inline.
_INLINE = {'at', 'xy', 'size', 'font', 'start', 'end', 'mid', 'center', 'width', 'type',
           'length', 'uuid', 'thickness', 'offset', 'layer', 'layers', 'justify', 'color',
           'fill', 'stroke', 'pts', 'hide', 'unit', 'in_bom', 'on_board', 'dnp', 'lib_id',
           'exclude_from_sim', 'shape', 'reference', 'page', 'number', 'name', 'pin_names',
           'pin_numbers', 'net', 'drill', 'roundrect_rratio', 'rect_delta', 'radius',
           'version', 'generator', 'generator_version', 'paper', 'tstamp', 'property_type',
           'bold', 'italic', 'scale', 'rotate', 'xyz', 'mirror', 'tags'}


def dumps(node, indent=0):
    if not isinstance(node, list):
        return _atom(node)
    if not node:
        return '()'
    head = node[0]
    simple = all(not isinstance(c, list) for c in node)
    if simple or (head in _INLINE and sum(isinstance(c, list) for c in node) <= 4
                  and len(str(node)) < 160):
        return '(' + ' '.join(dumps(c, 0) for c in node) + ')'
    pad = '\t' * (indent + 1)
    parts = ['(' + _atom(head)]
    # leading atoms stay on the head line
    i = 1
    while i < len(node) and not isinstance(node[i], list):
        parts[0] += ' ' + _atom(node[i])
        i += 1
    for c in node[i:]:
        parts.append(pad + dumps(c, indent + 1))
    return '\n'.join(parts) + '\n' + '\t' * indent + ')'


def find(node, key):
    """First child list whose head is key."""
    for c in node:
        if isinstance(c, list) and c and c[0] == key:
            return c
    return None


def findall(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]
