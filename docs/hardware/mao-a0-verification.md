# MAO_MAIN A0 verification

Pre-order verification of the 4-layer MAO_MAIN A0, 2026-10-05, in two rounds:

1. **First round** on the board as released: release checks, circuit simulation of every analog block and of the
   ring dial with its firmware decoder, a visual review of both faces, and two independent audits (the ODD JOBS
   standard rule by rule, and an electrical review). The audits found blockers, and the board was withdrawn.
2. **Revision** (A0 rev, same date): the blockers fixed in the circuit, the placement, the copper, the silk and
   the firmware; then every check run again, plus a second pair of audits.

Every finding was verified before it was acted on. The revision's changes are listed in
[Revision](#revision-what-changed-and-why).

## Summary (revised board)

| Check | Result |
|---|---|
| ERC (all checks KiCad runs by default) | 0 violations |
| DRC, all severities, warnings included | 0 violations, 0 unconnected, 0 schematic parity |
| Plane continuity (L2 GND, L3 +3V3 / VSYS / VBUS) | every plane one piece (`outputs/PLANES.json`) |
| Silk text check (`check_silk_text.py`: overlaps, pads, vias, holes, crowding, size, direction, ambiguity) | 77 texts, 0 findings |
| Enclosure check (`mech_check.py`: every part's 3D-model height against the stack, keep-outs) | 142 parts, 0 findings |
| Fab package | 12 Gerbers, 4 drill files, BOM 48 lines / 112 fitted parts, CPL 112 rows; 1 DNP (R110) |
| Vias | 192 through vias (126 × 0.3/0.6 mm, 66 × 0.2/0.5 mm), 24 thermal vias in exposed pads; the 6-layer first pass had 230 |
| Firmware | 5 configurations (S3 dev / factory / release, C3 dev / release): 0 errors, 0 warnings |
| Host tests | 240 checks, 0 failures |
| Simulation (S1–S15) | all pass |
| Visual review | both faces rendered (`hardware/mao/outputs/review/`), see [Visual review](#visual-review) |
| Second audits | see [Audits](#audits) |

## Simulation

ngspice is KiCad 10's own `libngspice`, driven from Python (`hardware/mao/sim/`). Device models are fitted to
the datasheet points named in each row. Statistics, the dial decoder and the datasheet arithmetic run in
Python. The +3V3 rail is 3.18 V since the revision. Reproduce with:

```
python3 hardware/mao/sim/run_sims.py
```

Plots and `results.json` are written to `docs/hardware/sim/`.

| # | Block | Model and corners | Result | Verdict |
|---|---|---|---|---|
| S1 | Hardware UVLO (TPS63802 EN divider) | 20 000 boards: 1 % resistors, EN 1.07–1.13 V rising / 0.97–1.03 V falling, EN leakage ±0.2 µA | 470 k / 240 k: off 2.75–3.16 V, on 3.05–3.46 V, hysteresis ≥ 0.29 V. The 1 M / 510 k draft reached 2.65 V off and 3.57 V on: the leakage × 1 MΩ alone was ±0.2 V | pass |
| S2 | Reverse-polarity FET Q101 (AO3401A, gate to GND) | level-1 PMOS fitted to RDS(on) at VGS = −2.5 / −4.5 V; cell 3.0–4.2 V, 0.15 Ω | 43–63 mV at 0.6 A, 22 mV while charging at 227 mA. Reversed cell, battery only: 0 µA. Reversed cell with USB: VBAT settles at 0.94–0.96 V, below the charger's 1.6 V VBAT(SC), so the BQ24073 stays in its 4–11 mA probe | pass |
| S3 | Backlight: panel LEDs (two in parallel) from VSYS into the AW9364 sinks LED1 + LED2 | white-LED model per Winstar bin, Vf 3.0 / 3.2 / 3.4 V at 20 mA per LED; sinks regulated at 40 mA, 2.5 Ω per sink below the 50 mV dropout; VSYS 3.0–4.4 V | 40 mA at step 1 on every bin above VSYS 3.2 V (3.0 V bin), 3.4 V (3.2 V bin), 3.6 V (3.4 V bin); dimmer below. Never above 40 mA nominal; the part's ±17.5 % makes it 33–47 mA. Driver dissipation ≤ 56 mW. Steps (17 − n)/16 × 40 mA | pass |
| S4 | IR LEDs (IR12-21C, own resistor each) | IR-LED model, Vf 1.05 / 1.20 / 1.50 V at 20 mA; resistor ±1 %; VSYS 3.0–4.5 V | 56 Ω: 26–59 mA peak, under the 65 mA rating everywhere | pass |
| S5 | Mic supply (GPIO → 100 Ω → 1 µF + 100 nF) | GPIO as 3.18 V behind 30 Ω, SPH0641 at 0.62 mA | 3.10 V steady, settled in 0.66 ms, 25 mA inrush peak (40 mA GPIO limit) | pass |
| S6 | IR receiver supply (expander → 100 Ω → 4.7 µF) | IRM-H638T 0.7 mA max + 10 k pull-up loaded | 3.05 V steady (≥ 2.7 V), above 2.7 V after 1.3 ms, 25 mA peak (50 mA pin limit) | pass |
| S7 | I2C, 2.2 k pull-ups, 400 kHz | measured bus: 7 devices, 62 pF typical, 100 pF with every pin at its 10 pF maximum | rise (30–70 %) 116 / 186 ns against the Fast-mode 300 ns; VOL 12 mV, 1.45 mA sink | pass |
| S8 | Board-ID divider (1 M / 1 M / 100 nF) | 3V3 soft start 1 ms | 1.59 V final (window 1.40–1.90 V; self-test 1.48–1.70 V), above 1.40 V at 107 ms; firmware samples at 150 ms | pass |
| S9 | ESP32-S3 EN (10 k / 1 µF) | same ramp | EN reaches VIH 13.4 ms after +3V3 is up (datasheet: ≥ 0.05 ms) | pass |
| S10 | Display rail (TPS22919, fixed slew) | datasheet SRON 1.8 / 2.7 mV/µs at 1.8 / 3.6 V, interpolated to 3.18 V; 10.5 µF load (incl. 4.7 µF panel estimate) | 1.69 ms rise, 26 mA inrush, 0.9 mV dip on +3V3; firmware waits 10 ms | pass |
| S11 | Ring dial: 30-pole strip, 2 × DRV5012, firmware decoder | the `mao_input.c` decoder ported line for line; random latch thresholds and sampling clocks; peak field 8 / 5 / 4 mT; random moves, stops, reversals | 180 random-walk trials: never off by more than the ±1 start/stop partial detent, 0 invalid transitions. Spins of 3 turns: 89–90 detents. Needs a peak field above 3.3 mT; the strip is specified ≥ 8 mT | pass |
| S12 | Speaker (MAX98357A, GAIN_SLOT open) | datasheet full scale 2.1 dBV + 9 dB = 3.59 Vrms; firmware gain 0.58 | 2.08 Vrms: 0.54 W into 8 Ω, 0.68 W at the 6.4 Ω minimum, under the 0.8 W rating | pass |
| S13 | Charger heat (BQ24073, USB500, ISET 4.3 k) | VBUS 4.75–5.25 V, VBAT 3.0–4.1 V, KISET max (227 mA), θJA 60 °C/W (JEDEC 44.5 + 35 % for a closed puck), 45 °C inside | 207 mA typical (185–227 mA, the cell's 0.5C is 250 mA); worst 0.72 W → TJ 88 °C, under the 125 °C fold-back | pass |
| S14 | Buck-boost inductor (0.47 µH, Isat 5.5 A) | 0.6 A load at 3.18 V; buck / buck-boost / boost | peak 0.93–1.27 A, under half of the 3.8 A minimum switch limit and of Isat | pass |
| S15 | Runtime | power-budget rows × 85 % usable capacity above the UVLO | active 3.0 h, active with radio power save 5.0 h, idle 3.6 h, drowsy 11 days, deep sleep 8 months | (informative) |

Plots: [UVLO](sim/s1_uvlo.png) · [Q101](sim/s2_rpp.png) · [backlight](sim/s3_backlight.png) ·
[IR](sim/s4_ir.png) · [switched supplies](sim/s5_s6_switched_supplies.png) · [I2C](sim/s7_i2c.png) ·
[reset and board ID](sim/s8_s9_reset_id.png) · [display rail](sim/s10_lcd_rail.png) · [dial](sim/s11_dial.png)

![Backlight](sim/s3_backlight.png)
![UVLO](sim/s1_uvlo.png)
![Dial decoder](sim/s11_dial.png)

### What simulation cannot prove

These are measured at bring-up (`mao-bringup.md`):

- **Backlight current.** On the cell, battery current at 100 % minus at 0 %: 33–47 mA (the AW9364 is a linear
  sink, so the difference is the LED current). The datasheet dropout reaches 170 mV at worst, so a high-Vf panel
  dims about 0.1 V of VSYS earlier than S3 shows.
- **Strip field.** Measure the strip with a gaussmeter at the Hall height (≥ 8 mT), then spin the ring three
  turns: the dial counter must read 90 ± 1.
- **RF.** Antenna match and range with the enclosure closed. No simulation here covers the antenna.
- **Thermal.** Charger case temperature after 30 min of charging from 3.0 V inside the closed puck
  (< 85 °C on the package); the board-temperature charge pause at 43 °C (`mao power charge off` checks the /CE
  path by hand first).
- **Display tail.** The 70 mm tail's S-fold in the carrier pocket and its passage through the slot, with the face
  pressed at the rim above it.

## Revision: what changed and why

| Area | Before | After | Why (audit finding) |
|---|---|---|---|
| Panel connection | J301 on F, needing a 10.5–12.5 mm tail | stock Winstar WF0128BTYAA4DNN0 with its 70.1 mm tail: S-fold in the face carrier, through a routed 1.0 × 11.5 mm slot at 9 o'clock, into J301 on B (panel pin k on pad 19 − k) | the stock tail is 70.1 mm (Winstar spec §8); the user chose to keep the stock panel |
| Backlight | 3V3_LCD → 10 Ω → LEDs → AO3400A (Q301, R303–R305), PWM capped at 80 % | VLED+ on VSYS, VLED− into an AW9364 (U303) LED1 + LED2: 40 mA nominal, 16 steps on GPIO45 | the Winstar LEDs need VLED+ 3.0–3.4 V at 40 mA (spec §4.2): from 3V3 through 10 Ω they got 6–20 mA |
| Display rail | TPS22917 (SOT-23-6, 1.45 mm) with CT 1 nF, no input cap | TPS22919 (SC70-6, 1.1 mm), fixed 1.7 ms slew, C110 1 µF on VIN | U105 had no CIN; the SOT-23-6 was over the 1.2 mm limit under the panel (found by `mech_check.py`) |
| +3V3 | 3.30 V (FB 560 k) | 3.18 V (FB 536 k), 3.10–3.28 V worst case | the Winstar VCI maximum is 3.3 V |
| UVLO | divider only | + C111 100 nF on BB_EN | load steps could trip the EN comparator |
| Charger | ISET 3.0 k (297 mA), /CE to GND, on B under the cell | ISET 4.3 k (207 mA), /CE from expander P6 with R117 100 k pull-down, on F under the panel | the LP503035 allows 250 mA (0.5C) and 0–45 °C (firmware pauses at 43 °C board temperature); its heat stays off the cell |
| Amplifier SD_MODE | expander pin straight to SD_MODE | 2.2 k series (R507) and the 100 k pull-down at the amplifier (R208) | MAX98357A datasheet: series R when VDDIO can exceed VDD |
| GPIO | IR_TX on GPIO39, EXP_RST_N on GPIO38 | IR_TX on GPIO38, EXP_RST_N on GPIO39 | GPIO39 (MTCK) has a weak pull-up at reset that lit the IR LEDs at every boot |
| Window parts | U503 reaching r 24.5 | U503 turned (4.0 mm side radial), centre r 21.0; ToF r 20.9 | the ring lip starts at r 24.0 |
| Fixing | 4.6 mm rings | + Ø6.5 mm part-free boss on F, Ø5.5 mm on B | M2 screw from B into a heat-set insert boss on F |
| L3 power | VSYS block east of the buck-boost | gone (UVLO divider and TP4 fed by B tracks); 10 o'clock branch to the amplifier and a 2 mm bar to the backlight driver; a chamfer where the top band meets the rim | the block boxed the +3V3 fill into a dead end, so slow lines stranded its vias |
| Router | — | `grid_router.py` checks plane continuity after every route and undoes any L3 route or via that splits a plane; real outlines for arc and rotated pads | the first revised route split L3 into six pieces |
| Silk | 2.0 mm back mark; S/N 3.4 × 1.75 mm; mixed sizes; labels over vias | type scale 1.5 / 1.2 / 0.85 / 0.8 mm; 4.6 mm back mark (finest stroke ≥ 0.15 mm); 6 × 6 mm S/N field with the JLC order-number placeholder; no silk on vias, holes or the slot; easter eggs (rule 175) | ODD JOBS F14–F18 |
| Firmware | LEDC backlight, CHG line | AW9364 1-wire driver; /CE thermal pause; `mao power charge off|on`; self-test limits for the 3.18 V rail (`docs/firmware/mao-a0-rev-merge.md`) | follows the hardware |

The values changed in the first round stay: UVLO divider 470 k / 240 k (S1), IR LED resistors 56 Ω (S4).

## Visual review

Both faces were rendered from the revised board (`hardware/mao/outputs/review/render-top.png`, `render-bottom.png`,
`render-iso.png`) and looked at as a product photograph:

- Face side: the maker's mark over MAO / MAIN A0 / 2026-10, the 6 × 6 mm S/N field and its JLC placeholder, the
  antenna note centred over the notch, function labels, the IMU axes; under the panel a cat asleep beside the face
  switch ("boop"), and ODD JOBS' "MADE FOR BAD IDEAS".
- Back: the 4.6 mm maker's mark with MAO A0 / 2026-10 outside the cell, the service field with its names (BAT, SYS,
  3V3, GND, SCL, SDA, XRST, BOOT, RST), connector names (USB, LCD, TAG, REAR, LRA, BAT with its pin cues), "9 lives"
  at the reverse-polarity FET and "meow" under the speaker.
- Black mask with ENIG, white silk.

`check_silk_text.py` measures what a review by eye misses: no text over a pad, via, hole or the slot, no crowding,
no ambiguous reference. 14 references found no clear silk spot (C407, D101, J303, Q101, Q501, R106, R108, R213,
R214, R505, U102, U103, U104, U301); they are on the assembly drawing with every other reference, and FAB-NOTES
lists them.

## Audits

**First round: not ready to order.** Two independent audits (electrical; ODD JOBS rule by rule) found 3
electrical errors and 18 ODD JOBS failures. The confirmed blockers, each fixed by the revision above:

| Blocker | Evidence | Fix |
|---|---|---|
| GPIO39 (IR_TX) is MTCK, with a weak pull-up at reset: the IR LEDs lit at every boot | ESP32-S3 datasheet v2.2, Table 2-1 note 7 | IR_TX on GPIO38 |
| The Winstar backlight needs VLED+ 3.0 / 3.2 / 3.4 V at 40 mA; from 3V3 through 10 Ω it got about 20 mA typical | Winstar spec §4.2 | AW9364 sinks from VSYS |
| The FPC tail is 70.10 ± 0.5 mm; J301's position needed 10.5–12.5 mm | Winstar spec §8 | slot + J301 on B |
| U503 reached r 24.5 mm; the ring ID is r 24.0 | board | U503 turned, r 21.0 |
| The charger, buck-boost, gauge and RPP FET sat inside the cell outline on B | board | the charger (the heat source, up to 0.72 W) moved to F under the panel; the buck-boost (≈ 60 mW loss at 0.6 A), gauge and FET stay on B under the 0.3 mm insulator, all ≤ 1.2 mm tall |
| U105 (TPS22917) had no input capacitor | board | C110 at pin 1 |
| Silk labels ambiguous or over vias | board | silk rebuilt; checker extended |

**Second round:** see the end of this document (filled in from the second audits).
