"""Minimal ctypes driver for the ngspice shared library that ships with KiCad 10."""
import ctypes as C
import os

LIB = os.path.expanduser('~/Applications/KiCad/KiCad.app/Contents/Frameworks/libngspice.0.dylib')
_ng = C.CDLL(LIB)

SendChar = C.CFUNCTYPE(C.c_int, C.c_char_p, C.c_int, C.c_void_p)
SendStat = C.CFUNCTYPE(C.c_int, C.c_char_p, C.c_int, C.c_void_p)
ControlledExit = C.CFUNCTYPE(C.c_int, C.c_int, C.c_bool, C.c_bool, C.c_int, C.c_void_p)
SendData = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_void_p)
SendInitData = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_void_p)
BGThreadRunning = C.CFUNCTYPE(C.c_int, C.c_bool, C.c_int, C.c_void_p)


class VectorInfo(C.Structure):
    _fields_ = [('v_name', C.c_char_p), ('v_type', C.c_int), ('v_flags', C.c_short),
                ('v_realdata', C.POINTER(C.c_double)), ('v_compdata', C.c_void_p), ('v_length', C.c_int)]


_ng.ngGet_Vec_Info.restype = C.POINTER(VectorInfo)
_ng.ngGet_Vec_Info.argtypes = [C.c_char_p]
_ng.ngSpice_Command.argtypes = [C.c_char_p]
_ng.ngSpice_Circ.argtypes = [C.POINTER(C.c_char_p)]

LOG = []


@SendChar
def _out(s, ident, user):
    LOG.append(s.decode(errors='replace'))
    return 0


@SendStat
def _stat(s, ident, user):
    return 0


@ControlledExit
def _exit(status, unload, quit_, ident, user):
    return 0


_ng.ngSpice_Init(_out, _stat, _exit, None, None, None, None)


def run(netlist, analysis, vectors):
    """netlist: list of lines (title first, no .end); analysis: e.g. '.tran 1u 10m' or '.dc V1 0 5 0.01'.
    Returns {vector: list of floats}."""
    LOG.clear()
    lines = list(netlist) + [analysis, '.end']
    arr = (C.c_char_p * (len(lines) + 1))(*[l.encode() for l in lines], None)
    _ng.ngSpice_Command(b'remcirc')
    _ng.ngSpice_Circ(arr)
    _ng.ngSpice_Command(b'run')
    out = {}
    for v in vectors:
        p = _ng.ngGet_Vec_Info(v.encode())
        if not p:
            raise RuntimeError('no vector %s; log:\n%s' % (v, ''.join(LOG[-30:])))
        vi = p.contents
        out[v] = [vi.v_realdata[i] for i in range(vi.v_length)]
    return out


if __name__ == '__main__':
    r = run(['* rc test', 'V1 in 0 PULSE(0 1 0 1n 1n 1 2)', 'R1 in out 1k', 'C1 out 0 1u'],
            '.tran 10u 5m', ['time', 'v(out)'])
    t, v = r['time'], r['v(out)']
    i = min(range(len(t)), key=lambda k: abs(t[k] - 1e-3))
    print('RC 1k/1u at t=1 ms: %.4f V (expect 0.632)' % v[i])
