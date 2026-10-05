"""Soundtrack for the MAO showcase film (numpy/scipy synthesis, no samples): build/soundtrack.wav, 48 kHz stereo.

A quiet ambient bed in D major that follows the cut, plus sound effects on the picture's events (TIMELINE). Every
sound is synthesised here, so nothing has a licence attached.
"""
import math
import wave
from pathlib import Path

import numpy as np
from scipy.signal import butter, sosfilt, fftconvolve

HERE = Path(__file__).resolve().parent
SR = 48000
DUR = 57.0
N = int(SR * DUR)
rng = np.random.default_rng(7)

# events (seconds), matching compose.py
T = {'hero': 3.0, 'explode': 9.0, 'board': 15.0, 'flip': 21.0, 'macro': 27.0, 'dial': 42.0, 'end': 51.0}
TICKS = [T['dial'] + (f + 4) / 30 for f in (30, 62, 94)]
PRESS = T['dial'] + 141 / 30
BLINKS = [T['dial'] + (f + 5) / 30 for f in (196, 222)]

NOTE = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}


def hz(name):
    n, o = name[:-1], int(name[-1])
    return 440.0 * 2 ** ((NOTE[n] + 12 * (o + 1) - 69) / 12)


CHORDS = [  # (start, end, notes)
    (0.0, 9.0, ['D3', 'A3', 'C#4', 'E4', 'F#4']),
    (9.0, 15.0, ['B2', 'F#3', 'A3', 'D4', 'E4']),
    (15.0, 21.0, ['G2', 'D3', 'F#3', 'A3', 'C#4']),
    (21.0, 27.0, ['A2', 'E3', 'F#3', 'B3', 'D4']),
    (27.0, 32.0, ['D3', 'A3', 'C#4', 'E4']),
    (32.0, 37.0, ['B2', 'F#3', 'A3', 'D4']),
    (37.0, 42.0, ['G2', 'D3', 'F#3', 'B3']),
    (42.0, 47.0, ['E3', 'G3', 'B3', 'D4', 'F#4']),
    (47.0, 51.0, ['A2', 'E3', 'G3', 'B3', 'D4']),
    (51.0, 57.0, ['D3', 'A3', 'C#4', 'E4', 'F#4', 'A4']),
]


def env(n, a, r, sustain=1.0):
    e = np.ones(n) * sustain
    na, nr = int(a * SR), int(r * SR)
    e[:na] = np.linspace(0, sustain, na) ** 1.6
    if nr:
        e[-nr:] *= np.linspace(1, 0, nr) ** 1.6
    return e


def lowpass(x, f, order=2):
    return sosfilt(butter(order, f, 'low', fs=SR, output='sos'), x)


def bandpass(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], 'band', fs=SR, output='sos'), x)


L = np.zeros(N); R = np.zeros(N)


def add(sig, t0, gain=1.0, pan=0.0):
    i0 = int(t0 * SR)
    if i0 >= N: return
    sig = sig[:N - i0]
    l, r = math.cos((pan + 1) * math.pi / 4), math.sin((pan + 1) * math.pi / 4)
    L[i0:i0 + len(sig)] += sig * gain * l * 1.414
    R[i0:i0 + len(sig)] += sig * gain * r * 1.414


# ---- pad --------------------------------------------------------------------------------------------------------
for k, (t0, t1, notes) in enumerate(CHORDS):
    t0p = max(0.0, t0 - 0.6); n = int((t1 - t0p + 1.8) * SR)
    t = np.arange(n) / SR
    for j, nm in enumerate(notes):
        f = hz(nm)
        s = np.zeros(n)
        for det, ph in ((-0.0035, 0.0), (0.0, 1.3), (0.0042, 2.1)):
            ff = f * (1 + det)
            s += np.sin(2 * np.pi * ff * t + ph) + 0.18 * np.sin(4 * np.pi * ff * t + ph) + 0.07 * np.sin(6 * np.pi * ff * t)
        s *= (1 + 0.15 * np.sin(2 * np.pi * (0.11 + 0.03 * j) * t))          # slow shimmer
        s = lowpass(s, 1800) * env(n, 1.4 if k else 2.6, 1.8)
        add(s, t0p, gain=0.018, pan=(j / max(1, len(notes) - 1) - 0.5) * 0.7)
    # sub on the root
    f = hz(notes[0]) / 2
    s = np.sin(2 * np.pi * f * t) * env(n, 1.0, 1.6)
    add(s, t0p, gain=0.035)

# ---- plucked arpeggio (motion while the picture moves) -----------------------------------------------------------
beat = 60 / 96 / 2
for (t0, t1, notes) in CHORDS:
    if t0 < 9.0 or t0 >= 51.0: continue
    k = 0
    t = t0
    while t < t1 - 0.05:
        nm = notes[[0, 2, 1, 3, 2, 4 % len(notes), 1, 3][k % 8] % len(notes)]
        f = hz(nm) * 2
        n = int(0.9 * SR); tt = np.arange(n) / SR
        s = (np.sin(2 * np.pi * f * tt) + 0.3 * np.sin(4 * np.pi * f * tt)) * np.exp(-tt * 6.5)
        s = lowpass(s, 3200)
        add(s, t, gain=0.020 * (0.75 if k % 2 else 1.0), pan=0.35 * math.sin(k * 0.9))
        k += 1; t += beat

# ---- effects ----------------------------------------------------------------------------------------------------
def swell(t0, dur, lo=200, hi=2500, gain=0.08):
    n = int(dur * SR)
    s = bandpass(rng.standard_normal(n), lo, hi) * env(n, dur * 0.8, dur * 0.2)
    add(s, t0, gain)


def whoosh(t0, dur, gain=0.07, pan=0.0):
    n = int(dur * SR); tt = np.linspace(0, 1, n)
    noise = rng.standard_normal(n)
    out = np.zeros(n)
    for k in range(0, n, 2048):                               # moving band
        c = 300 + 3000 * math.sin(math.pi * tt[k]) ** 2
        seg = noise[max(0, k - 512):k + 2048 + 512]
        y = bandpass(seg, c * 0.7, min(c * 1.4, 20000))
        out[k:k + 2048] = y[(k - max(0, k - 512)):(k - max(0, k - 512)) + len(out[k:k + 2048])]
    out *= np.sin(np.pi * tt) ** 2
    add(out, t0, gain, pan)


def tick(t0, gain=0.22):
    n = int(0.06 * SR); tt = np.arange(n) / SR
    s = bandpass(rng.standard_normal(n), 1500, 7000) * np.exp(-tt * 180) + 0.6 * np.sin(2 * np.pi * 2600 * tt) * np.exp(-tt * 90)
    add(s, t0, gain, 0.15)


def thunk(t0, gain=0.35):
    n = int(0.25 * SR); tt = np.arange(n) / SR
    s = np.sin(2 * np.pi * (140 - 60 * tt / 0.25) * tt) * np.exp(-tt * 22) + 0.3 * bandpass(rng.standard_normal(n), 800, 4000) * np.exp(-tt * 120)
    add(s, t0, gain)


def blip(t0, f=1900, gain=0.06):
    n = int(0.07 * SR); tt = np.arange(n) / SR
    add(np.sin(2 * np.pi * f * tt) * np.sin(np.pi * tt / 0.07) ** 2, t0, gain, 0.1)


def boop(t0, gain=0.10):
    n = int(0.16 * SR); tt = np.arange(n) / SR
    f = 620 + 380 * (tt / 0.16) ** 0.7
    ph = 2 * np.pi * np.cumsum(f) / SR
    add(np.sin(ph) * np.sin(np.pi * tt / 0.16) ** 1.5, t0, gain)


def chime(t0, f0=hz('A5'), gain=0.05):
    n = int(2.5 * SR); tt = np.arange(n) / SR
    s = sum(a * np.sin(2 * np.pi * f0 * m * tt) * np.exp(-tt * d) for m, a, d in ((1, 1, 1.4), (2.76, 0.4, 2.5), (5.4, 0.2, 4)))
    add(s, t0, gain, -0.2)


swell(T['hero'] - 1.4, 1.6, gain=0.06)
whoosh(T['explode'] + 0.2, 2.6, 0.06)
whoosh(T['flip'] + 0.6, 2.8, 0.05, pan=-0.2)
chime(T['macro'] + 0.2)
boop(T['macro'] + 0.9)
for t in TICKS: tick(t)
thunk(PRESS)
for t in BLINKS: blip(t)
swell(T['end'] - 1.2, 1.4, lo=120, hi=1500, gain=0.05)
n = int(3.5 * SR); tt = np.arange(n) / SR
add(np.sin(2 * np.pi * hz('D2') * tt) * np.exp(-tt * 1.2), T['end'], 0.18)

# ---- space and master ---------------------------------------------------------------------------------------------
ir_n = int(2.4 * SR); ti = np.arange(ir_n) / SR
irL = rng.standard_normal(ir_n) * np.exp(-ti * 2.9); irR = rng.standard_normal(ir_n) * np.exp(-ti * 2.9)
irL = lowpass(irL, 6000); irR = lowpass(irR, 6000)
irL /= np.sqrt(np.sum(irL ** 2)); irR /= np.sqrt(np.sum(irR ** 2))
wetL = fftconvolve(L, irL)[:N]; wetR = fftconvolve(R, irR)[:N]
outL = L * 0.8 + wetL * 0.35; outR = R * 0.8 + wetR * 0.35
fade = np.ones(N)
fade[:int(0.4 * SR)] = np.linspace(0, 1, int(0.4 * SR))
fade[-int(2.5 * SR):] = np.linspace(1, 0, int(2.5 * SR)) ** 1.5
outL *= fade; outR *= fade
peak = max(np.max(np.abs(outL)), np.max(np.abs(outR)))
g = 10 ** (-1.5 / 20) / peak
pcm = (np.stack([outL, outR], 1) * g * 32767).astype(np.int16)
out = HERE / 'build' / 'soundtrack.wav'
with wave.open(str(out), 'wb') as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
rms = np.sqrt(np.mean((pcm.astype(float) / 32767) ** 2))
print('soundtrack: %s, %.1f s, peak -1.5 dBFS, rms %.1f dBFS' % (out, DUR, 20 * math.log10(rms)))
