"""MAO_MAIN A0 simulation suite: ngspice (KiCad's libngspice) for the analog blocks, Python for the
statistics, the dial decoder and the datasheet arithmetic. Writes plots + results.json to OUT.

usage: python3 run_sims.py [OUT]     (default ../../../docs/hardware/sim; needs matplotlib and KiCad 10)
Results and the datasheet values behind each model: docs/hardware/mao-a0-verification.md."""
import json
import math
import os
import random
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(__file__))
import ngs  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../docs/hardware/sim')
os.makedirs(OUT, exist_ok=True)
R = {}
random.seed(20261005)

plt.rcParams.update({'font.size': 9, 'axes.grid': True, 'grid.alpha': 0.3, 'figure.dpi': 130,
                     'axes.spines.top': False, 'axes.spines.right': False})


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, name))
    plt.close(fig)


def verdict(ok):
    return 'PASS' if ok else 'FAIL'


def tol(v, pct):
    return v * (1 + random.uniform(-pct, pct) / 100)


# Device models (datasheet-fitted, level-1 MOSFETs are adequate for DC/switch use)
PMOS_AO3401A = '.model AO3401A PMOS(LEVEL=1 VTO=-0.9 KP=6 RD=0.01 RS=0.01 IS=1e-12 N=1.1)'
NMOS_AO3400A = '.model AO3400A NMOS(LEVEL=1 VTO=1.05 KP=15 RD=0.005 RS=0.005)'
NMOS_OD = '.model NOD NMOS(LEVEL=1 VTO=0.8 KP=0.05)'   # I2C open-drain driver, ~20 ohm at 3.3 V


def white_led(name, vf20):
    """White LED: Vf = vf20 at 20 mA, ~0.35 V lower at 1 mA (N 3.3, RS 5 ohm)."""
    n, rs, vt = 3.3, 5.0, 0.02585
    i_s = 0.02 / math.exp((vf20 - 0.02 * rs) / (n * vt))
    return '.model %s D(IS=%.4g N=%.3f RS=%.2f)' % (name, i_s, n, rs)


def ir_led(name, vf20):
    """940 nm GaAs IR LED: Vf = vf20 at 20 mA (N 1.8, RS 2.5 ohm)."""
    n, rs, vt = 1.8, 2.5, 0.02585
    i_s = 0.02 / math.exp((vf20 - 0.02 * rs) / (n * vt))
    return '.model %s D(IS=%.4g N=%.3f RS=%.2f)' % (name, i_s, n, rs)


# ------------------------------------------------------------------------------------------------
# S1  Hardware UVLO (TPS63802 EN divider R13 1M / R14 510k, both 1 %) - Monte Carlo
# ------------------------------------------------------------------------------------------------
def uvlo_mc(rtop, rbot, n=20000):
    on, off = [], []
    for _ in range(n):
        r13, r14 = tol(rtop, 1), tol(rbot, 1)
        vr, vf = random.uniform(1.07, 1.13), random.uniform(0.97, 1.03)
        ilk = random.uniform(-0.2e-6, 0.2e-6)          # EN input leakage, datasheet max 0.2 uA (-40..125 C)
        # VSYS at which EN reaches the threshold: (VSYS - Ven)/R13 = Ven/R14 + ilk
        on.append(vr * (1 + r13 / r14) + ilk * r13)
        off.append(vf * (1 + r13 / r14) + ilk * r13)
    return on, off


def s1_uvlo():
    res = {}
    fig, axs = plt.subplots(1, 2, figsize=(7.4, 3.2), sharey=True)
    for ax, (rt, rb, tag) in zip(axs, ((1e6, 510e3, 'A0 draft: 1 M / 510 k'), (470e3, 240e3, 'release: 470 k / 240 k'))):
        on, off = uvlo_mc(rt, rb)
        on.sort(); off.sort()
        r = {'on_min': round(on[0], 3), 'on_typ': round(on[len(on) // 2], 3), 'on_max': round(on[-1], 3),
             'off_min': round(off[0], 3), 'off_typ': round(off[len(off) // 2], 3), 'off_max': round(off[-1], 3),
             'hyst_min': round(min(a - b for a, b in zip(on, off)), 3),
             'divider_uA_at_4V2': round(4.2 / (rt + rb) * 1e6, 1)}
        res[tag] = r
        ax.hist(off, 80, color='#c0392b', alpha=.75, label='turn-off')
        ax.hist(on, 80, color='#2e86c1', alpha=.75, label='turn-on')
        ax.axvspan(2.4, 2.75, color='k', alpha=.08, label='PCM / cell cut-off band')
        ax.set_xlim(2.4, 3.65); ax.set_title(tag, fontsize=9); ax.set_xlabel('VSYS (V)')
    axs[0].set_ylabel('boards (of 20 000)')
    fig.suptitle('S1 · hardware UVLO worst case: 1 % resistors, EN thresholds, EN leakage', fontsize=9)
    fig.tight_layout(rect=(0, 0.08, 1, 1))
    h, l = axs[1].get_legend_handles_labels()
    fig.legend(h, l, fontsize=7, ncol=3, loc='lower center', frameon=False)
    fig.savefig(os.path.join(OUT, 's1_uvlo.png'))
    plt.close(fig)
    rel = res['release: 470 k / 240 k']
    # off stays above the cell's 2.75 V discharge cut-off; on stays below a 3.5 V (~10 % SoC) resting cell;
    # hysteresis > 0.2 V so the cell's recovery after cut-off cannot restart the rail
    res['verdict'] = verdict(rel['off_min'] > 2.75 and rel['on_max'] < 3.5 and rel['hyst_min'] > 0.2)
    R['S1_uvlo'] = res


# ------------------------------------------------------------------------------------------------
# S2  Reverse-polarity P-FET Q1 (AO3401A, gate to GND through R12)
# ------------------------------------------------------------------------------------------------
def s2_rpp():
    base = ['* Q1 RPP', PMOS_AO3401A,
            'M1 cell_d g vbat vbat AO3401A',                # D S order: M d g s b
            'R12 g 0 10k', 'C3 vbat 0 10u']
    res = {}
    # (a) discharge: cell 3.0..4.2 V, load 0.6 A peak on VBAT
    drops = []
    for vc in (3.0, 3.7, 4.2):
        r = ngs.run(base + ['Vcell cell 0 %g' % vc, 'Rint cell cell_d 0.15', 'Iload vbat 0 0.6'],
                    '.op', ['v(vbat)', 'v(cell_d)'])
        drops.append((vc, r['v(cell_d)'][0] - r['v(vbat)'][0]))
    res['discharge_drop_mV_at_0.6A'] = {('%.1fV' % a): round(b * 1e3, 1) for a, b in drops}
    # (b) charge: 297 mA from VBAT into the cell through the channel (reverse conduction)
    r = ngs.run(base + ['Vcell cell 0 3.0', 'Rint cell cell_d 0.15', 'Ichg 0 vbat 0.297'],
                '.op', ['v(vbat)', 'v(cell_d)'])
    res['charge_drop_mV_at_297mA_VBAT3.0'] = round((r['v(vbat)'][0] - r['v(cell_d)'][0]) * 1e3, 1)
    # (c) reversed cell, battery only: leakage into the system
    r = ngs.run(base + ['Vcell cell 0 -4.2', 'Rint cell cell_d 0.15', 'Rsys vbat 0 1k'],
                '.op', ['v(vbat)', 'i(vcell)'])
    res['reversed_battery_only_uA'] = round(abs(r['i(vcell)'][0]) * 1e6, 3)
    res['reversed_battery_only_vbat'] = round(r['v(vbat)'][0], 4)
    # (d) reversed cell with USB in: charger is in BAT short-circuit mode (VBAT < VBAT(SC) 1.6 V min),
    #     sourcing IBAT(SC) 4..11 mA; Q1 settles at its threshold and that current flows into the cell.
    rows = []
    for isc in (4e-3, 7.5e-3, 11e-3):
        r = ngs.run(base + ['Vcell cell 0 -4.2', 'Rint cell cell_d 0.15', 'Isc 0 vbat %g' % isc,
                            'Rgauge vbat 0 200k'], '.op', ['v(vbat)', 'i(vcell)'])
        rows.append((isc, r['v(vbat)'][0], abs(r['i(vcell)'][0])))
    res['reversed_with_usb'] = [{'Isc_mA': a * 1e3, 'VBAT_V': round(b, 3), 'I_cell_mA': round(c * 1e3, 2)}
                                for a, b, c in rows]
    worst_drop = max(d for _, d in drops)
    ok = (worst_drop < 0.1 and res['reversed_battery_only_uA'] < 1 and
          all(v < 1.6 for _, v, _ in rows) and all(c < 0.012 for _, _, c in rows))
    res['verdict'] = verdict(ok)
    res['note'] = ('VBAT stays below the charger VBAT(SC) 1.6 V min, so the BQ24073 never leaves its '
                   '4-11 mA short-circuit probe: a reversed pack sees at most 11 mA, no fast charge')
    # plot: Q1 drop vs load current at 3.0/3.7/4.2 V
    fig, ax = plt.subplots(figsize=(6.2, 2.8))
    for vc, col in ((3.0, '#c0392b'), (3.7, '#2e86c1'), (4.2, '#27ae60')):
        ii = [x / 20 for x in range(0, 21)]
        dv = []
        for i in ii:
            r = ngs.run(base + ['Vcell cell 0 %g' % vc, 'Rint cell cell_d 0.15', 'Iload vbat 0 %g' % i],
                        '.op', ['v(vbat)', 'v(cell_d)'])
            dv.append((r['v(cell_d)'][0] - r['v(vbat)'][0]) * 1e3)
        ax.plot(ii, dv, color=col, label='cell %.1f V' % vc)
    ax.set_xlabel('battery current (A)'); ax.set_ylabel('Q1 drop (mV)')
    ax.set_title('S2 · reverse-polarity FET loss (AO3401A, Vgs = -VBAT)')
    ax.legend(fontsize=8)
    save(fig, 's2_rpp.png')
    R['S2_reverse_polarity'] = res


# ------------------------------------------------------------------------------------------------
# S3  Backlight current by panel Vf bin and 3V3 tolerance (two LEDs in parallel, R303, Q301)
# ------------------------------------------------------------------------------------------------
def s3_backlight(r303=10.0, tag=''):
    rows = []
    for vf in (2.8, 3.0, 3.2):
        for v33 in (3.25, 3.30, 3.35):
            net = ['* BL', white_led('WL', vf), NMOS_AO3400A,
                   'V33 v33 0 %g' % v33, 'Rsw v33 vled 0.08',            # TPS22917 80 mohm
                   'D1 vled k WL', 'D2 vled k WL', 'R303 k d %g' % r303,
                   'M1 d g 0 0 AO3400A', 'Vg g 0 3.3']
            r = ngs.run(net, '.op', ['i(v33)'])
            rows.append({'vf_bin': vf, 'v33': v33, 'I_mA': round(-r['i(v33)'][0] * 1e3, 1)})
    return rows


def s3():
    res = {'R303_10R': s3_backlight(10.0), 'R303_6.8R': s3_backlight(6.8), 'R303_4.7R': s3_backlight(4.7)}
    res['firmware_ceiling_pct'] = 80
    res['worst_average_mA'] = round(max(r['I_mA'] for r in res['R303_10R']) * 0.8, 1)
    res['verdict'] = verdict(res['worst_average_mA'] <= 40.0)
    for k, rows in [(k, v) for k, v in res.items() if k.startswith('R303')]:
        res[k + '_summary'] = {('Vf%.1f' % vf): [r['I_mA'] for r in rows if r['vf_bin'] == vf]
                               for vf in (2.8, 3.0, 3.2)}
    fig, ax = plt.subplots(figsize=(6.6, 3.4))
    xs = [3.25, 3.30, 3.35]
    for vf, col in ((2.8, '#27ae60'), (3.0, '#2e86c1'), (3.2, '#c0392b')):
        for key, ls in (('R303_10R', '-'), ('R303_6.8R', '--')):
            ys = [r['I_mA'] for r in res[key] if r['vf_bin'] == vf]
            ax.plot(xs, ys, ls, color=col, marker='o', ms=3,
                    label='Vf %.1f V bin, %s' % (vf, key.split('_')[1]))
    ax.axhline(40, color='k', lw=.8, ls=':'); ax.text(3.251, 41, 'panel spec 40 mA', fontsize=7)
    ax.set_xlabel('3V3 rail (V)'); ax.set_ylabel('backlight current at 100 % PWM (mA)')
    ax.set_title('S3 · backlight at 100 % PWM by panel LED bin (10 Ω fitted; 6.8 Ω dashed)')
    ax.legend(fontsize=7, ncol=3, loc='upper center', bbox_to_anchor=(0.5, -0.22), frameon=False)
    save(fig, 's3_backlight.png')
    R['S3_backlight'] = res


# ------------------------------------------------------------------------------------------------
# S4  IR LED peak current over VSYS (each LED has its own resistor; Q501 shared)
# ------------------------------------------------------------------------------------------------
def s4_ir(rled):
    out = {}
    for name, vf20, rtol in (('min', 1.05, -1), ('typ', 1.20, 0), ('max', 1.50, 1)):
        vs, ii = [], []
        for k in range(0, 16):
            vsys = 3.0 + k * 0.1
            net = ['* IR', ir_led('IRL', vf20), NMOS_AO3400A, 'Vs vs 0 %g' % vsys,
                   'R1 vs a1 %g' % (rled * (1 + rtol / 100)), 'R2 vs a2 %g' % (rled * (1 + rtol / 100)),
                   'D1 a1 k IRL', 'D2 a2 k IRL', 'M1 k g 0 0 AO3400A', 'Vg g 0 3.3']
            r = ngs.run(net, '.op', ['i(vs)'])
            vs.append(vsys); ii.append(-r['i(vs)'][0] / 2 * 1e3)
        out[name] = (vs, ii)
    return out


def s4():
    res = {}
    fig, ax = plt.subplots(figsize=(6.6, 3.4))
    for rled, ls in ((47.0, '--'), (56.0, '-')):
        o = s4_ir(rled)
        res['R_%dR' % rled] = {k: {'I_at_3.0V': round(v[1][0], 1), 'I_at_3.7V': round(v[1][7], 1),
                                   'I_at_4.5V': round(v[1][-1], 1)} for k, v in o.items()}
        for k, col in (('min', '#c0392b'), ('typ', '#2e86c1'), ('max', '#27ae60')):
            ax.plot(o[k][0], o[k][1], ls, color=col, label='%d Ω, Vf %s' % (rled, k))
    ax.axhline(65, color='k', lw=.8, ls=':'); ax.text(3.0, 66, 'IR12-21C IF rating 65 mA', fontsize=7)
    ax.axvspan(4.3, 4.5, color='k', alpha=.06); ax.text(4.31, 28, 'USB in\n(VO(REG))', fontsize=7)
    ax.set_xlabel('VSYS (V)'); ax.set_ylabel('peak current per LED (mA)')
    ax.set_title('S4 · IR LED pulse current (56 Ω fitted, 47 Ω draft dashed)')
    ax.legend(fontsize=7, ncol=3, loc='upper center', bbox_to_anchor=(0.5, -0.22), frameon=False)
    save(fig, 's4_ir.png')
    res['verdict'] = verdict(res['R_56R']['min']['I_at_4.5V'] < 65)
    R['S4_ir_led'] = res


# ------------------------------------------------------------------------------------------------
# S5/S6  Switched sensor supplies: mic (GPIO -> 100R -> 1u+100n) and IR receiver (expander -> 100R -> 4.7u)
# ------------------------------------------------------------------------------------------------
def s5_s6():
    res = {}
    # GPIO / expander output modelled as 3.3 V behind 30 ohm (ESP32-S3 drive 3 / TCA6408A VOH spec)
    mic = ['* mic', 'Vg g 0 PULSE(0 3.3 100u 1u 1u 1 2)', 'Rout g p 30', 'R402 p v 100',
           'C406 v 0 1u', 'C407 v 0 100n', 'R403 p 0 100k', 'Imic v 0 0.62m']
    r = ngs.run(mic, '.tran 1u 2m', ['time', 'v(v)', 'i(vg)'])
    t, v, i = r['time'], r['v(v)'], r['i(vg)']
    v_end = v[-1]
    t99 = next(tt for tt, vv in zip(t, v) if vv > 0.99 * v_end) - 100e-6
    res['mic'] = {'V_steady': round(v_end, 3), 'settle_99pct_us': round(t99 * 1e6), 'peak_mA': round(max(-x for x in i) * 1e3, 1),
                  'spec': 'SPH0641 VDD 1.62-3.6 V; ESP32-S3 GPIO 40 mA abs max'}
    res['mic']['verdict'] = verdict(1.62 <= v_end <= 3.6 and res['mic']['peak_mA'] < 40)
    tm, vm = t, v
    irx = ['* irrx', 'Vg g 0 PULSE(0 3.3 100u 1u 1u 1 2)', 'Rout g p 30', 'R505 p v 100',
           'C507 v 0 4.7u', 'Iirm v 0 0.7m', 'R506 v o 10k', 'Vo o 0 0']   # worst: output low (pull-up loads)
    r = ngs.run(irx, '.tran 2u 6m', ['time', 'v(v)', 'i(vg)'])
    t, v, i = r['time'], r['v(v)'], r['i(vg)']
    v_end = v[-1]
    t99 = next(tt for tt, vv in zip(t, v) if vv > 0.99 * v_end) - 100e-6
    t27 = next(tt for tt, vv in zip(t, v) if vv > 2.7) - 100e-6
    res['ir_rx'] = {'V_steady': round(v_end, 3), 'to_2V7_us': round(t27 * 1e6), 'settle_99pct_us': round(t99 * 1e6),
                    'peak_mA': round(max(-x for x in i) * 1e3, 1),
                    'spec': 'IRM-H638T Vs 2.7-5.5 V; TCA6408A 50 mA per pin abs max'}
    res['ir_rx']['verdict'] = verdict(v_end >= 2.7 and res['ir_rx']['peak_mA'] < 50)
    fig, ax = plt.subplots(figsize=(6.6, 3.0))
    ax.plot([x * 1e3 for x in tm], vm, color='#2e86c1', label='MIC_VDD (0.62 mA load)')
    ax.plot([x * 1e3 for x in t], v, color='#c0392b', label='IR_RX_VCC (0.7 mA + pull-up)')
    ax.axhline(2.7, color='#c0392b', lw=.7, ls=':'); ax.axhline(1.62, color='#2e86c1', lw=.7, ls=':')
    ax.text(5.9, 2.5, 'IRM-H638T min 2.7 V', fontsize=7, ha='right', color='#c0392b')
    ax.text(5.9, 1.42, 'SPH0641 min 1.62 V', fontsize=7, ha='right', color='#2e86c1')
    ax.set_xlabel('time after enable (ms)'); ax.set_ylabel('V')
    ax.set_title('S5/S6 · switched sensor supplies (RC filters)')
    ax.legend(fontsize=7.5, ncol=2, loc='upper center', bbox_to_anchor=(0.5, -0.28), frameon=False)
    save(fig, 's5_s6_switched_supplies.png')
    R['S5_S6_switched_supplies'] = res


# ------------------------------------------------------------------------------------------------
# S7  I2C rise time (2.2k pull-ups, 7 devices + MCU + tracks)
# ------------------------------------------------------------------------------------------------
def s7_i2c(c_bus_pf):
    net = ['* i2c', NMOS_OD, 'Vdd vdd 0 3.3', 'Rp vdd sda 2.2k', 'Cb sda 0 %gp' % c_bus_pf,
           'Vdrv g 0 PULSE(3.3 0 0.5u 5n 5n 1.25u 2.5u)', 'M1 sda g 0 0 NOD']
    r = ngs.run(net, '.tran 2n 5u', ['time', 'v(sda)'])
    t, v = r['time'], r['v(sda)']
    # rising edge after the driver releases at 0.5 us + 1.25 us
    seg = [(tt, vv) for tt, vv in zip(t, v) if 0.505e-6 <= tt <= 1.75e-6]
    t30 = next(tt for tt, vv in seg if vv >= 0.3 * 3.3)
    t70 = next(tt for tt, vv in seg if vv >= 0.7 * 3.3)
    vol = min(v)
    return (t70 - t30) * 1e9, vol, t, v


def s7():
    # bus C: 7 device pins + MCU-side TP (6 pF typ, 10 pF max each) + 101 mm of track and 6 vias (measured, ~17 pF)
    c_typ, c_max = 8 * 6 + 14, 8 * 10 + 20
    tr_typ, vol, t1, v1 = s7_i2c(c_typ)
    tr_max, _, t2, v2 = s7_i2c(c_max)
    res = {'C_bus_typ_pF': c_typ, 'C_bus_max_pF': c_max, 'tr_typ_ns': round(tr_typ), 'tr_max_ns': round(tr_max),
           'VOL_V': round(vol, 3), 'I_sink_mA': round(3.3 / 2.2, 2),
           'spec': 'Fast-mode tr <= 300 ns (30-70 %), VOL <= 0.4 V at 3 mA'}
    res['verdict'] = verdict(tr_max < 300 and vol < 0.4)
    fig, ax = plt.subplots(figsize=(6.6, 3.0))
    ax.plot([x * 1e6 for x in t1], v1, color='#2e86c1', label='%d pF (typ)' % c_typ)
    ax.plot([x * 1e6 for x in t2], v2, color='#c0392b', label='%d pF (all pins at max)' % c_max)
    ax.axhline(0.3 * 3.3, color='k', lw=.6, ls=':'); ax.axhline(0.7 * 3.3, color='k', lw=.6, ls=':')
    ax.set_xlabel('µs'); ax.set_ylabel('SDA (V)')
    ax.set_title('S7 · I2C at 400 kHz, 2.2 kΩ pull-ups (30 % / 70 % dotted)')
    ax.legend(fontsize=7.5, ncol=2, loc='upper center', bbox_to_anchor=(0.5, -0.28), frameon=False)
    save(fig, 's7_i2c.png')
    R['S7_i2c'] = res


# ------------------------------------------------------------------------------------------------
# S8/S9  Board-ID divider settling and ESP32-S3 EN (CHIP_PU) RC against the 3V3 soft start
# ------------------------------------------------------------------------------------------------
def s8_s9():
    net = ['* id+en', 'V33 v33 0 PWL(0 0 1m 3.3 300m 3.3)',          # TPS63802 soft start ~1 ms
           'Rid1 v33 id 1Meg', 'Rid2 id 0 1Meg', 'Cid id 0 100n',
           'Ren v33 en 10k', 'Cen en 0 1u']
    r = ngs.run(net, '.tran 20u 300m', ['time', 'v(id)', 'v(en)', 'v(v33)'])
    t, vid, ven, v33 = r['time'], r['v(id)'], r['v(en)'], r['v(v33)']
    t_id = next(tt for tt, vv in zip(t, vid) if vv >= 1.4)
    t_en = next(tt for tt, vv in zip(t, ven) if vv >= 0.75 * 3.3)
    t_rail = next(tt for tt, vv in zip(t, v33) if vv >= 3.0)
    res = {'board_id_reaches_1V4_ms': round(t_id * 1e3, 1), 'board_id_final_V': round(vid[-1], 3),
           'board_id_window_mV': [1400, 1900], 'en_VIH_ms': round(t_en * 1e3, 2),
           'rail_3V0_ms': round(t_rail * 1e3, 2), 'en_after_rail_ms': round((t_en - t_rail) * 1e3, 2),
           'spec': 'ESP32-S3 tSTBL >= 0.05 ms after VDD stable; firmware samples board ID after 150 ms'}
    res['verdict'] = verdict(t_en - t_rail > 0.05e-3 and t_id < 0.15 and 1.4 <= vid[-1] <= 1.9)
    fig, ax = plt.subplots(figsize=(6.6, 3.0))
    ax.plot([x * 1e3 for x in t], v33, color='k', lw=.8, label='+3V3')
    ax.plot([x * 1e3 for x in t], ven, color='#27ae60', label='MCU_EN (10 k / 1 µF)')
    ax.plot([x * 1e3 for x in t], vid, color='#8e44ad', label='BOARD_ID (1 M / 1 M / 100 nF)')
    ax.axhspan(1.4, 1.9, color='#8e44ad', alpha=.08)
    ax.text(0.3, 1.55, 'firmware board-ID window', fontsize=7, color='#8e44ad')
    ax.set_xscale('symlog', linthresh=1); ax.set_xlabel('ms (log after 1 ms)'); ax.set_ylabel('V')
    ax.set_title('S8/S9 · reset and board-ID timing at power-up')
    ax.legend(fontsize=7.5, ncol=3, loc='upper center', bbox_to_anchor=(0.5, -0.28), frameon=False)
    save(fig, 's8_s9_reset_id.png')
    R['S8_S9_reset_board_id'] = res


# ------------------------------------------------------------------------------------------------
# S10  TPS22917 display-rail soft start (CT 1 nF) - datasheet slew, ngspice for the 3V3 droop
# ------------------------------------------------------------------------------------------------
def s10():
    sr = 1900 / 1000 * 1e3           # V/s: SRON 1900 (mV/us)*pF / 1000 pF at VIN 3.6 V
    tr = 1.6e-6 * 1000               # tR 1.6 us/pF
    c_load = 1e-6 + 0.1e-6 + 4.7e-6 + 4.7e-6    # C10 + C301 + C302 + panel internal (assumed 4.7 uF)
    i_in = c_load * sr
    # ngspice: TPS63802 modelled as 3.3 V behind 30 mohm + 44 uF; rail ramps at SR into the caps + 9 mA panel
    net = ['* lcd', 'V0 src 0 3.3', 'Rout src v33 0.03', 'Cout v33 0 44u',
           'B1 v33 lcd I=(v(v33)-v(lcd) > 0 ? min(%g*%g, (v(v33)-v(lcd))/0.08) : 0) * (time > 1e-4 ? 1 : 0)'
           % (c_load, sr),
           'Cl lcd 0 %g' % c_load, 'Rpanel lcd 0 390']
    r = ngs.run(net, '.tran 5u 4m', ['time', 'v(lcd)', 'v(v33)'])
    t, vl, v33 = r['time'], r['v(lcd)'], r['v(v33)']
    res = {'slew_V_per_ms': round(sr / 1e3, 2), 'rise_time_ms': round(tr * 1e3, 2), 'C_load_uF': round(c_load * 1e6, 1),
           'inrush_mA': round(i_in * 1e3, 1), 'droop_3V3_mV': round((3.3 - min(v33)) * 1e3, 2)}
    res['verdict'] = verdict(i_in < 0.1 and 3.3 - min(v33) < 0.033)
    fig, ax = plt.subplots(figsize=(6.2, 2.4))
    ax.plot([x * 1e3 for x in t], vl, color='#2e86c1', label='3V3_LCD')
    ax.plot([x * 1e3 for x in t], v33, color='k', lw=.8, label='+3V3')
    ax.set_xlabel('ms'); ax.set_ylabel('V'); ax.set_title('S10 · display rail soft start (CT 1 nF)')
    ax.legend(fontsize=8)
    save(fig, 's10_lcd_rail.png')
    R['S10_display_rail'] = res


# ------------------------------------------------------------------------------------------------
# S11  Ring dial: 30-pole strip, two DRV5012 latches 90 deg electrical apart, sampled at 2.5 kHz,
#      firmware decoder (mao_input.c) ported line for line
# ------------------------------------------------------------------------------------------------
QUAD = [0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0]


class Decoder:
    def __init__(self, ab, rest_mask=0b1001, full=2):
        self.prev, self.rest, self.full = ab, rest_mask, full
        self.synced = bool(rest_mask & (1 << ab))
        self.accum = 0; self.count = 0; self.invalid = 0

    def isr(self, ab):
        step = QUAD[(self.prev << 2) | ab]
        if step == 0 and ab != self.prev:
            self.invalid += 1
        self.accum += step
        self.prev = ab
        if self.rest & (1 << ab):
            half = self.full // 2
            if not self.synced:
                self.synced = True
            elif self.accum >= half:
                self.count += 1
            elif self.accum <= -half:
                self.count -= 1
            self.accum = 0


class Latch:
    """DRV5012: samples the field at its own free-running rate (2.5 kHz typ, +/-30 %)."""
    def __init__(self, phase):
        self.bop = random.uniform(0.6, 3.3) * 1e-3
        self.brp = -random.uniform(0.6, 3.3) * 1e-3
        self.phase, self.out = phase, None
        self.period = 400e-6 * random.uniform(0.7, 1.3)
        self.t0 = random.uniform(0, self.period)

    def sample(self, b):
        if self.out is None:
            self.out = 0 if b > 0 else 1
        elif b >= self.bop:
            self.out = 0          # south pole (B > BOP) drives OUT low
        elif b <= self.brp:
            self.out = 1
        return self.out


def run_dial(bpk, segments, misalign_deg=0.0, trace_until=None):
    """segments: list of (duration_s, delta_theta_deg). Event-driven: each latch samples on its own clock,
    the ISR runs on every output change and reads both lines, as the firmware does."""
    lat = [Latch(0.0), Latch(90.0 + misalign_deg)]
    pole_k = [random.uniform(0.85, 1.15) for _ in range(30)]       # strip magnetisation spread per pole

    def field(theta, k):
        el = theta * 15 - lat[k].phase
        pole = int(math.floor(el / 180.0)) % 30
        return bpk * pole_k[pole] * math.sin(math.radians(el))

    # piecewise-linear motion
    times, thetas = [0.0], [random.uniform(0, 360)]
    for dur, dth in segments:
        times.append(times[-1] + dur); thetas.append(thetas[-1] + dth)

    def theta_at(t):
        import bisect
        i = min(bisect.bisect_right(times, t), len(times) - 1)
        if i == 0:
            return thetas[0]
        t0, t1 = times[i - 1], times[i]
        return thetas[i - 1] + (thetas[i] - thetas[i - 1]) * (t - t0) / (t1 - t0) if t1 > t0 else thetas[i]

    for k in range(2):
        lat[k].sample(field(thetas[0], k))
    ab = lambda: (lat[0].out << 1) | lat[1].out
    dec = Decoder(ab())
    events = []
    for k in range(2):
        n = 0
        while True:
            t = lat[k].t0 + n * lat[k].period
            if t > times[-1] + 0.01:
                break
            events.append((t, k)); n += 1
    events.sort()
    trace = []
    for t, k in events:
        th = theta_at(t)
        old = ab()
        lat[k].sample(field(th, k))
        if ab() != old:
            dec.isr(ab())
        if trace_until is not None and th - thetas[0] < trace_until:
            trace.append((th - thetas[0], lat[0].out, lat[1].out, dec.count))
    return dec, thetas[-1] - thetas[0], trace


def dial_trial(bpk, n_moves=15):
    segs = []
    for _ in range(n_moves):
        span = random.uniform(-400, 400)
        rps = random.choice([0.1, 0.3, 1.0, 2.0, 4.0])
        segs.append((abs(span) / (rps * 360), span))
        segs.append((random.uniform(0.005, 0.2), 0.0))       # stop anywhere: the ring has no detent
    net = sum(s[1] for s in segs)
    turns = random.choice([-2, -1, 1, 2, 3])
    back = 360 * turns - net                                  # return to the start angle + N whole turns
    segs.append((abs(back) / 360.0, back))
    dec, travel, _ = run_dial(bpk, segs)
    return dec.count, 30 * turns, dec.invalid


def dial_spin(bpk, rev=3, rps=1.0):
    dec, travel, tr = run_dial(bpk, [(0.05, 0.0), (rev / rps, 360.0 * rev), (0.05, 0.0)], trace_until=48)
    return dec.count, tr


def s11():
    res = {}
    for bpk in (8e-3, 5e-3, 4e-3):
        counts = []
        bad = 0
        invalid = 0
        for _ in range(60):
            got, want, inv = dial_trial(bpk)
            invalid += inv
            if abs(got - want) > 1:
                bad += 1
            counts.append(got - want)
        spins = [dial_spin(bpk, rev=3, rps=r)[0] for r in (0.2, 1.0, 4.0, 10.0)]
        res['Bpk_%gmT' % (bpk * 1e3)] = {'random_walk_trials': 60, 'trials_off_by_more_than_1': bad,
                                         'error_histogram': {str(e): counts.count(e) for e in sorted(set(counts))},
                                         'invalid_transitions': invalid,
                                         'spin_3_turns_at_0.2_1_4_10_rps': spins}
    # failure boundary: peak field below BOP max -> latches may never switch
    res['field_floor_mT'] = 3.3
    ok = all(v['trials_off_by_more_than_1'] == 0 and all(abs(s) in (89, 90) for s in v['spin_3_turns_at_0.2_1_4_10_rps'])
             for k, v in res.items() if k.startswith('Bpk'))
    res['verdict'] = verdict(ok)
    _, tr = dial_spin(8e-3, rev=1, rps=0.5)
    fig, ax = plt.subplots(3, 1, figsize=(6.2, 3.4), sharex=True)
    th = [x[0] for x in tr]
    ax[0].plot(th, [x[1] for x in tr], color='#2e86c1', drawstyle='steps-post'); ax[0].set_ylabel('HALL_A')
    ax[1].plot(th, [x[2] for x in tr], color='#c0392b', drawstyle='steps-post'); ax[1].set_ylabel('HALL_B')
    ax[2].plot(th, [x[3] for x in tr], color='k', drawstyle='steps-post'); ax[2].set_ylabel('detents')
    ax[2].set_xlabel('ring angle (deg)')
    ax[0].set_title('S11 · ring dial: latches + firmware decoder, 8 mT strip, random thresholds')
    save(fig, 's11_dial.png')
    R['S11_dial'] = res


# ------------------------------------------------------------------------------------------------
# S12-S15  Datasheet arithmetic: speaker power, charger heat, buck-boost ripple, runtime
# ------------------------------------------------------------------------------------------------
def s12_s15():
    # S12 speaker: MAX98357A FS = 2.1 dBV + GAIN_SLOT open 9 dB; firmware digital gain 0.58
    fs_vrms = 10 ** ((2.1 + 9) / 20)
    v_out = fs_vrms * 0.58
    p8 = v_out ** 2 / 8
    p8_min_imp = v_out ** 2 / 6.4
    clip = {}
    for vdd in (3.0, 3.7, 4.4):
        vmax = vdd * 0.95 / math.sqrt(2)              # BTL sine just below clipping (~0.3 ohm Rds total)
        clip['%.1fV' % vdd] = round(vmax, 2)
    R['S12_speaker'] = {'full_scale_Vrms': round(fs_vrms, 2), 'firmware_gain': 0.58, 'out_Vrms': round(v_out, 2),
                        'P_8ohm_W': round(p8, 2), 'P_6.4ohm_W': round(p8_min_imp, 2),
                        'speaker_rated_W': 0.8, 'speaker_max_W': 1.2,
                        'unclipped_sine_Vrms_by_VSYS': clip,
                        'verdict': verdict(p8_min_imp <= 0.8)}
    # S13 charger: USB500 (Iin 475 mA max), 297 mA charge (KISET max 975/3.0k = 325 mA), VO(REG) 4.4 V
    ich_max = 975 / 3000
    rows = []
    for vbus in (4.75, 5.0, 5.25):
        for vbat in (3.0, 3.7, 4.1):
            iin = 0.475
            p = (vbus - 4.4) * iin + (4.4 - vbat) * min(ich_max, iin)
            rows.append({'VBUS': vbus, 'VBAT': vbat, 'P_W': round(p, 2)})
    pmax = max(r['P_W'] for r in rows)
    theta_ja = 44.5 * 1.35        # JEDEC 44.5 C/W; small 58 mm board inside a closed puck: +35 %
    tj = 45 + pmax * theta_ja      # 45 C inside the enclosure while charging
    R['S13_charger_thermal'] = {'worst_P_W': pmax, 'theta_JA_used': round(theta_ja, 1), 'TJ_worst_C': round(tj),
                                'TJ_REG_C': 125, 'rows': rows,
                                'verdict': verdict(tj < 125),
                                'note': 'above 125 C the BQ24073 folds back charge current; it never trips'}
    # S14 buck-boost inductor (0.47 uH, Isat 5.5 A) and switch current
    L, isat = 0.47e-6, 5.5
    cases = []
    for vin, f, mode in ((4.4, 1.6e6, 'buck'), (3.3, 1.4e6, 'buck-boost'), (2.96, 2.1e6, 'boost')):
        iout = 0.6
        if mode == 'buck':
            d = 3.3 / vin; dI = 3.3 * (1 - d) / (L * f); il = iout
        elif mode == 'boost':
            d = 1 - vin * 0.92 / 3.3; dI = vin * d / (L * f); il = iout * 3.3 / (vin * 0.9)
        else:
            dI = 3.3 * 0.5 / (L * f) * 0.5; il = iout * 3.3 / (vin * 0.9)
        cases.append({'mode': mode, 'VIN': vin, 'ripple_App': round(dI, 2), 'I_L_avg_A': round(il, 2),
                      'I_peak_A': round(il + dI / 2, 2)})
    R['S14_buck_boost'] = {'Iout_A': 0.6, 'cases': cases, 'Isat_A': isat, 'IPK_limit_min_A': 3.8,
                           'verdict': verdict(all(c['I_peak_A'] < 0.5 * min(isat, 3.8) for c in cases))}
    # S15 runtime from the power budget (500 mAh, 85 % usable above the 2.96 V UVLO)
    cap = 500 * 0.85
    states = {'active': 133, 'active_radio_ps': 75, 'idle': 116, 'drowsy': 1.6, 'deep_sleep': 0.073}
    R['S15_runtime'] = {k: ('%.1f h' % (cap / v) if cap / v < 48 else '%.0f days' % (cap / v / 24))
                        for k, v in states.items()}
    R['S15_runtime']['note'] = '500 mAh x 85 % usable above the UVLO; budget rows from mao-power-budget.md'


if __name__ == '__main__':
    for f in (s1_uvlo, s2_rpp, s3, s4, s5_s6, s7, s8_s9, s10, s11, s12_s15):
        f()
        print('done', f.__name__, flush=True)
    with open(os.path.join(OUT, 'results.json'), 'w') as fh:
        json.dump(R, fh, indent=1)
    print(json.dumps({k: v.get('verdict') for k, v in R.items() if isinstance(v, dict)}, indent=1), flush=True)
    os._exit(0)
