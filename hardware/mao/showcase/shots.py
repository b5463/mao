"""Shot list for the MAO showcase film and stills (plain Python; KiCad renders through render.py).

    python3 shots.py film [workers]     -> build/frames/<segment>/NNNN.png  (1920 x 1080, transparent)
    python3 shots.py stills [workers]   -> build/stills/*.png               (3840 x 2160, transparent)

Every 3D frame is KiCad's raytracer on the final board plus the concept enclosure (enclosure.py). Rendering is
resumable: frames already on disk are skipped. Cameras: kicad-cli --rotate 'X,Y,Z' (Z turns the board in its plane,
X tilts it away, Y turns it over), --pivot in cm as (x, -y, z) of board mm / 10.
"""
import math
import sys
from pathlib import Path

import enclosure as enc
import render

HERE = Path(__file__).resolve().parent
FR = HERE / 'build' / 'frames'
ST = HERE / 'build' / 'stills'
FPS = 30
PUCK = ['base', 'lra', 'cell', 'display', 'window', 'bezel', 'ring']   # the speaker is the board's own B.Cu model
ASSEMBLED = {k: (0, 0, 0) for k in PUCK}
EXPLODED = {'base': (0, 0, -24), 'lra': (0, 0, -19), 'cell': (0, 0, -13),
            'display': (0, 0, 9), 'ring': (0, 0, 16), 'window': (0, 0, 22), 'bezel': (0, 0, 26)}
LIGHT = {'light_top': '0.85', 'light_camera': '0.25', 'light_side': '0.40', 'light_bottom': '0.12'}
BOARD_LIGHT = {'light_top': '0.80', 'light_camera': '0.30', 'light_side': '0.45', 'light_bottom': '0.30'}
MACRO_LIGHT = dict(BOARD_LIGHT, light_camera='0.15')      # close up, the camera light glares on the mask
ORTHO_LIGHT = {'light_top': '0.35', 'light_camera': '0.0', 'light_side': '0.35', 'light_bottom': '0.0'}  # flat-on: no hot spot


def ease(t):                                   # smootherstep
    t = min(1.0, max(0.0, t))
    return t * t * t * (t * (6 * t - 15) + 10)


def lerp(a, b, t):
    return a + (b - a) * t


def cam(rx, ry, rz, zoom, pivot=(0, 0, 0), **extra):
    c = {'rotate': '%.3f,%.3f,%.3f' % (rx, ry, rz), 'zoom': zoom, 'pivot': '%.4f,%.4f,%.4f' % pivot}
    c.update(extra)
    return c


def face(blink=1.0, look=(0, 0), squint=0.0):
    """Swap entry for a face variant; writes its model on first use."""
    name = enc.display_name(blink, look, squint)
    if not (enc.OUT / (name + '.wrl')).exists():
        enc.display(blink, look, squint)
    return {} if name == 'display' else {'display': name}


# ---- the film's 3D segments ---------------------------------------------------------------------------------------
def seg_hero():                                 # 6 s: the assembled puck turns into view
    out = []
    n = 6 * FPS
    for i in range(n):
        t = ease(i / (n - 1))
        out.append({'parts': ASSEMBLED, 'cam': cam(-62, 0, lerp(-70, -5, t), lerp(0.74, 0.82, t), (0, 0, -0.25), **LIGHT)})
    return out


def seg_explode():                              # 3 s apart, 3 s hold with a slow drift
    out = []
    n = 3 * FPS
    for i in range(n):
        t = ease(i / (n - 1))
        parts = {k: (0, 0, ASSEMBLED[k][2] + (EXPLODED[k][2]) * t) for k in PUCK}
        out.append({'parts': parts, 'cam': cam(lerp(-62, -76, t), 0, lerp(-5, 12, t), lerp(0.82, 0.50, t),
                                                (0, 0, lerp(-0.25, 0.20, t)), **LIGHT)})
    for i in range(n):
        t = i / (n - 1)
        out.append({'parts': EXPLODED, 'cam': cam(-76, 0, lerp(12, 22, t), lerp(0.50, 0.53, t), (0, 0, 0.20), **LIGHT)})
    return out


def seg_board():                                # 6 s top turntable, 6 s turning over to the back
    out = []
    n = 6 * FPS
    for i in range(n):
        t = ease(i / (n - 1))
        out.append({'parts': {}, 'cam': cam(lerp(-50, -58, t), 0, lerp(-45, 35, t), lerp(0.86, 0.95, t), **BOARD_LIGHT)})
    for i in range(n):
        t = ease(i / (n - 1))
        out.append({'parts': {}, 'cam': cam(-58, lerp(0, 180, t), lerp(35, 30, t), 0.95, (0, 0, lerp(0, -0.08, t)), **BOARD_LIGHT)})
    return out


def dial_angle(i):
    """Three detent ticks of 12 degrees (30 per turn), each a quick eased snap then a rest."""
    a = 0.0
    for start in (30, 62, 94):
        a += 12.0 * ease((i - start) / 9.0)
    return -a


def seg_dial():                                 # 9 s close on the face: turn, press, look back
    out = []
    n = 9 * FPS
    for i in range(n):
        t = i / (n - 1)
        look = (0, 0); blink = 1.0; squint = 0.0
        if 26 <= i < 128:                        # eyes follow the ring
            look = (int(round(8 * ease((i - 26) / 10.0) - 8 * ease((i - 118) / 10.0))), 0)
        if 140 <= i < 176:                       # pressed: pleased squint
            squint = ease((i - 140) / 6.0) - ease((i - 168) / 6.0)
        for b0 in (196, 222):                    # two blinks
            if b0 <= i < b0 + 10:
                blink = 1.0 - math.sin(math.pi * (i - b0) / 10.0) * 0.95
        blink = round(blink, 2); squint = round(squint, 2)
        dz = -0.45 * (ease((i - 140) / 4.0) - ease((i - 172) / 4.0))     # the face dips on the press
        parts = dict(ASSEMBLED)
        for k in ('display', 'window', 'bezel'):
            parts[k] = (0, 0, dz)
        out.append({'parts': parts, 'swap': face(blink, look, squint), 'rot': {'ring': dial_angle(i)},
                    'cam': cam(-40, 0, lerp(-10, 6, ease(t)), lerp(1.30, 1.38, t), (0, -0.1, 0.30), **LIGHT)})
    return out


SEGMENTS = {'hero': seg_hero, 'explode': seg_explode, 'board': seg_board, 'dial': seg_dial}


# ---- stills -------------------------------------------------------------------------------------------------------
def ortho(side, zoom=0.92, light=ORTHO_LIGHT):
    return {'side': side, 'zoom': zoom, 'perspective': False, **light}


STILLS = {
    'board-top': ({'parts': {}, 'cam': ortho('top')}),
    'board-bottom': ({'parts': {}, 'cam': ortho('bottom')}),
    'board-front': ({'parts': {}, 'cam': ortho('front', 1.6, BOARD_LIGHT)}),
    'board-back': ({'parts': {}, 'cam': ortho('back', 1.6, BOARD_LIGHT)}),
    'board-left': ({'parts': {}, 'cam': ortho('left', 1.6, BOARD_LIGHT)}),
    'board-right': ({'parts': {}, 'cam': ortho('right', 1.6, BOARD_LIGHT)}),
    'board-iso-face': ({'parts': {}, 'cam': cam(-55, 0, -30, 0.90, **BOARD_LIGHT)}),
    'board-iso-back': ({'parts': {}, 'cam': cam(-55, 180, 30, 0.90, **BOARD_LIGHT)}),
    'board-low-face': ({'parts': {}, 'cam': cam(-74, 0, 18, 1.05, **BOARD_LIGHT)}),
    'board-low-back': ({'parts': {}, 'cam': cam(-74, 180, -18, 1.05, **BOARD_LIGHT)}),
    'macro-cat': ({'parts': {}, 'cam': cam(-42, 0, -12, 4.2, (-0.40, -0.12, 0), **MACRO_LIGHT)}),
    'macro-mark': ({'parts': {}, 'cam': cam(-42, 0, 10, 3.4, (-0.05, -1.25, 0), **MACRO_LIGHT)}),
    'macro-field': ({'parts': {}, 'cam': cam(-45, 180, 0, 3.6, (1.05, 0.30, -0.16), **MACRO_LIGHT)}),
    'macro-lives': ({'parts': {}, 'cam': cam(-45, 180, -20, 4.6, (1.40, -0.45, -0.16), **MACRO_LIGHT)}),
    'macro-antenna': ({'parts': {}, 'cam': cam(-38, 0, 0, 3.2, (0, -2.15, 0), **MACRO_LIGHT)}),
    'macro-module': ({'parts': {}, 'cam': cam(-50, 180, 15, 2.4, (0, -1.3, -0.16), **MACRO_LIGHT)}),
    'puck-hero': ({'parts': ASSEMBLED, 'cam': cam(-62, 0, -25, 0.80, (0, 0, -0.25), **LIGHT)}),
    'puck-front': ({'parts': ASSEMBLED, 'cam': cam(-78, 0, 0, 0.85, (0, 0, -0.25), **LIGHT)}),
    'puck-top': ({'parts': ASSEMBLED, 'cam': cam(0, 0, 0, 0.78, (0, 0, -0.25), **LIGHT)}),
    'puck-face': ({'parts': ASSEMBLED, 'cam': cam(-40, 0, -4, 1.32, (0, -0.1, 0.30), **LIGHT)}),
    'puck-under': ({'parts': ASSEMBLED, 'cam': cam(-130, 0, 20, 0.80, (0, 0, -0.35), light_top='0.15', light_camera='0.1',
                                                  light_side='0.22', light_bottom='0.15')}),
    'puck-bottom': ({'parts': ASSEMBLED, 'cam': ortho('bottom', 0.80, {'light_top': '0.0', 'light_camera': '0.05', 'light_side': '0.2',
                                                                       'light_bottom': '0.28'})}),
    'puck-exploded': ({'parts': EXPLODED, 'cam': cam(-76, 0, 18, 0.52, (0, 0, 0.20), **LIGHT)}),
}


def film(workers):
    jobs = []
    for name, fn in SEGMENTS.items():
        for i, fr in enumerate(fn()):
            jobs.append((fr, FR / name / ('%04d.png' % i)))
    print('film: %d frames, %d to render' % (len(jobs), sum(1 for _, o in jobs if not o.exists())), flush=True)
    done = 0
    for k in range(0, len(jobs), 30):                    # progress per 30 frames
        done += render.batch(jobs[k:k + 30], workers=workers)
        print('film: %d / %d' % (min(k + 30, len(jobs)), len(jobs)), flush=True)


def stills(workers):
    jobs = [(fr, ST / (name + '.png')) for name, fr in STILLS.items()]
    render.batch(jobs, workers=workers, w=3840, h=2160)
    print('stills: %d' % len(jobs), flush=True)


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'film'
    workers = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    {'film': film, 'stills': stills}[what](workers)
