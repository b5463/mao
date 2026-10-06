> **Superseded 2026-10-06 (kept as history):** this document describes MAO_MAIN **A0**. The current board is MAO_MAIN **A1** (A0 moved to the M5 Gate C architecture): see [mao-a1-report.md](mao-a1-report.md).

# MAO_MAIN A0: design review (4-layer release)

Four passes over the released 4-layer design:
1. Schematic, against datasheets.
2. PCB, against the 4-layer mandate, the ODD JOBS standard and the JLC capability.
3. ODD JOBS review: product, visual character, serviceability. Every one of the standard's 200 rules is
   checked one by one in [`mao-odd-jobs-compliance.md`](mao-odd-jobs-compliance.md).
4. The final order-readiness audit.

Sources of truth:
- capture `hardware/mao/design/circuit.py`, which generates the schematic and the netlist;
- placement `placement.py` + `mechanical.py`;
- designed copper `route_power.py`, `route_local.py`, and L3 regions `power_regions.py`;
- pins `pinmap.py`.

The whole board is rebuilt by `design/pipeline.py`.

Generated evidence in `hardware/mao/outputs/`: `ERC.json`, `DRC.json`, `PLANES.json`, `SILK-TEXT.json`, the schematic PDF and `fab/`.
Copper plots of all four layers are in `plots/`; renders are in `renders/`.

| L1 F.Cu | L2 In1.Cu (GND) |
|---|---|
| ![L1](plots/mao-main-a0-L1-F.png) | ![L2](plots/mao-main-a0-L2-In1.png) |
| **L3 In2.Cu (power + slow)** | **L4 B.Cu (seen from the back)** |
| ![L3](plots/mao-main-a0-L3-In2.png) | ![L4](plots/mao-main-a0-L4-B.png) |

## 1. Schematic review

ERC: 0 violations (2026-10-05).

KiCad 10 leaves four checks off by default. Run once in a scratch copy with all of them on, they give only 6 footprint-filter notes, all deliberate: generic connector and LED symbols on custom footprints (J102, J501, D501, D502), the VL53L0X symbol on the pin-identical VL53L4CD package (U402), and an MSOP footprint for the DRV2605L DGS package (U502).

BOM: every fitted part has an LCSC number, checked against LCSC on 2026-10-04 and asserted by `fab.py`.

### 1.1 Power

| Block | Check | Result |
|---|---|---|
| USB-C J101 | Rd 5.1 k on CC1 and CC2 (sink, default power). D+/D− both rows joined. VBUS pins both sides joined. Shell to GND | ✓ |
| VBUS ESD | SMF15A: 15 V standoff, clamps below the BQ24073's 28 V absolute maximum. TPD2E2U06 on D+/D− (1.5 pF) | ✓ |
| BQ24073 (U102, on F under the panel) | /CE from expander P6 with a 100 k pull-down R117 (charging on by default; firmware pauses at ≥ 43 °C board temperature, resumes ≤ 40 °C: the cell allows 0–45 °C, the chip's TS window is 0–50 °C). EN2 = GND, EN1 = +3V3 → USB500 once the rail is up (USB100 before). ISET 4.3 k → 207 mA (185–227 mA; the cell's 0.5C is 250 mA). ILIM 1.5 k: its 1.07 A limit applies only in the resistor-set ILIM mode (EN2 high), which this board does not use; in USB500 the input limit is 450–500 mA. ILIM must still not be open. TD = 0 Ω to GND → termination on. TMR open → default safety timers. TS ← cell NTC 10 k. OUT regulated to 4.4 V on USB. /CHG unused | ✓ datasheet SLUS810 |
| Caps | IN 1 µF 25 V; OUT 10 µF; BAT 10 µF (≥ 4.7 µF each required) | ✓ |
| Reverse polarity | AO3401A P-FET: drain to the cell, source to VBAT, gate 10 k to GND. Reversed cell: the body diode blocks and Vgs = 0, so the FET stays off. Correct cell: the FET is enhanced and conducts both ways, so charging works | ✓; Vgs max ±12 V ≥ 4.2 V |
| BAT link | R108 0 Ω 1206 (2 A class) in the cell path: lift it to measure battery current (ODD JOBS 110) | ✓ |
| Gauge MAX17048 | VDD/CELL on VBAT; CTG, QSTRT, GND tied. ALRT is open-drain, wired-OR with the light sensor's INT to expander P7 through a 100 k pull-up, so a latched alert costs 33 µA, not 330 | ✓ |
| Buck-boost TPS63802 | EN divider 470 k / 240 k: on at 1.1 V × 2.958 = 3.25 V, off at 2.96 V. This is the hardware UVLO, independent of firmware. Worst case over 1 % resistors, the EN thresholds and the 0.2 µA EN leakage: off 2.75–3.16 V, on 3.05–3.46 V (simulation S1; the 1 M / 510 k draft reached 2.65 V) FB 536 k / 100 k: 0.5 V × 6.36 = 3.18 V (3.10–3.27 V over the FB and resistor tolerances; the 100 nA FB bias current × 536 k adds up to 54 mV, so 3.32 V at the very worst corner, 20 mV over the Winstar VCI 3.3 V operating maximum and far below the GC9A01's 4.6 V absolute maximum; A1 option: 523 k). EN filter 100 nF (C111). MODE = GND → power save (11 µA Iq). PG unused. L 0.47 µH DFE201612E (Isat 5.5 A). CIN 10 µF at VIN. COUT 2 × 22 µF (TI typical application) | ✓ SLVSEU9D. No 100 nF at VIN: TI's reference layout has none, and the 10 µF is 0.97 mm from the pin (C102 10 µF on the same net 1.5 mm away adds to it) |
| Display rail TPS22919 (U105, SC70-6) | ON pulled down by R116 (off until firmware; the part's smart pull-down too). Fixed slew: turn-on 1.7 ms, 10–90 % rise 1.0 ms at 3.18 V, 26 mA inrush (S10). QOD to VOUT through 100 Ω (R115): controlled discharge. VIN cap C110 1 µF at pin 1, VOUT cap C109 1 µF. SC70-6 is 1.1 mm: it sits under the panel (zone A ≤ 1.2 mm) | ✓ SLVSEN5B |
| Battery protection | Cell PCM (mandatory, checked on receipt), hardware UVLO, NTC window, firmware critical-battery shutdown, reverse-polarity FET | ✓ (no second protector IC; see the architecture doc) |

### 1.2 Compute

| Block | Check | Result |
|---|---|---|
| Module | ESP32-S3-WROOM-1-N8R2 (quad PSRAM keeps GPIO35–37 free; 85 °C). 22 µF + 100 nF at 3V3. EN 10 k / 1 µF (Espressif HDG RC) | ✓ |
| Straps | GPIO0: face switch to GND, 10 k pull-up, **no capacitor** (BOOT). GPIO3: charger PGOOD input with 100 k pull-up (JTAG-source strap, inert unless its eFuse is burnt). GPIO45 (VDD_SPI strap; on this module the flash voltage is fixed by eFuse): AW9364 EN, held low at reset by the driver's 150 k EN pull-down and GPIO45's internal pull-down, so the backlight stays dark. GPIO46 (must read 0 for download boot): NC with its internal pull-down | ✓ |
| Board ID | 1 M / 1 M + 100 nF on GPIO8 (ADC1_CH7) → 1.59 V on the 3.18 V rail (A0); 1.6 µA. The divider sits beside the pin: 4.1 mm of track, one via | ✓ |
| Expander TCA6408A 0x20 | Every output has a pull-down default (rails off, panel in reset, amp shut down, charging on). P6 = charger /CE (output). Input: SENSE_ALRT (P7, 100 k). RESET has a 10 k pull-up **and GPIO39** (recovery without a power cycle). INT to GPIO21 (deep-sleep wake) | ✓ |
| I2C | One bus, 2.2 k pull-ups, 400 kHz, 6 devices. Addresses 0x20, 0x29, 0x36, 0x44, 0x5A, 0x6A: unique | ✓ |
| Service | Tag-Connect TC2030-NL (GND, EN, TXD0, 3V3, RXD0, GPIO0) + 15 probe pads | ✓ |

### 1.3 Interface and sense

| Block | Check | Result |
|---|---|---|
| Display | **J301: HDGC 0.5K-HX-18PWB**, 18-pin 0.5 mm back-flip FPC connector with contacts top and bottom (LCSC C2919497). The panel plugs in and can be swapped without solder. Pinout follows the 18-pin round-panel standard (Winstar WF0128BTYAA4DNN0): 7 VLED+, 8 VLED−, 9 GND, 10 CS, 11 SCL, 12 SDA, 13 RS, 14 TE, 15 RESET, 16 VCI, 18 GND. Pins 1–6 are the touch controller of touch variants: TP_GND to GND, the rest NC. TE goes to GPIO9 for tear-free frames. 22 Ω series on SCLK/MOSI at the module. 100 nF + 4.7 µF at the connector. J301 sits on B: the stock panel's 70 mm tail passes through a routed slot at 9 o'clock (panel pin k on pad 19 − k). Backlight: VLED+ on VSYS, VLED− into an AW9364 (U303) LED1 + LED2: 40 mA nominal (33–47 mA), 16 steps by EN pulses on GPIO45, 150 k EN pull-down (dark from reset) | ✓ |
| Ring dial | 2 × DRV5012, 6° apart, under 30 poles (12° per pole): 90° electrical, 2 transitions per detent, 30 detents. HALL_FAST (GPIO2) pulled down → 20 Hz sampling asleep (1.6 µA). The poles are a flexible ferrite strip in the ring (no metal sweeps over the antenna); DRV5012 operates at ≤ ±3.3 mT, the strip is specified for ≥ 8 mT at the 1.5 mm gap. Detents are haptic ticks from the LRA | ✓ |
| Face press | SKQG on GPIO0. The face rocks on its lip on three printed flexures (mechanical §4): centre ≈ 2.6 N / 0.25 mm, rim ≈ 1.3 N / 0.5 mm; an LRA click on every press | ✓ |
| Touch | 510 Ω series within ≈ 1 mm of the module pins (on F, under the pins), 0.42 pF ESD at each electrode, no plane under the rim arcs | ✓ |
| IMU LSM6DSOX | SA0 = GND → 0x6A, CS = VDDIO → I2C. INT1 → GPIO14 (deep-sleep wake), INT2 → GPIO47. On F at 0°: +X = board +x, +Y = towards 12 o'clock, +Z out of the face (ST AN5192 fig. 1); axis glyph on the silkscreen | ✓ DS12814 |
| ToF VL53L4CD | XSHUT from the expander (off by default); GPIO1 has a 10 k pull-up (ST) and goes to GPIO48; 100 nF + 4.7 µF | ✓ |
| Light OPT3004 | ADDR = GND → 0x44; INT on the wired-OR alert | ✓ |
| Mic SPH0641 | Supply from GPIO35 through 100 Ω / 1 µF (it draws 80 µA even with the clock stopped). SEL low → left. PDM clock GPIO37, data GPIO36. Bottom port through the board | ✓ |

### 1.4 Feedback

| Block | Check | Result |
|---|---|---|
| Amp MAX98357A | VDD = VSYS (≤ 4.4 V < 5.5 V). SD_MODE from the expander: low → shutdown, 0.6 µA; 3.3 V (> 1.4 V) → left channel. GAIN_SLOT open → 9 dB. 10 µF + 100 nF at VDD, the datasheet's decoupling. EP to GND with thermal vias. I2S on GPIO16/17/18. Speaker LS501: Same Sky CMS-150803-088S-X8 (8 Ω, 0.8 W nominal, 1.2 W max), its spring contacts on two pads; full scale is 2.1 dBV + 9 dB = 3.59 Vrms; the A0 audio gain (0.58 at 100 % volume) gives 2.08 Vrms: 0.54 W into 8 Ω, 0.68 W at the 6.4 Ω impedance minimum, under the 0.8 W rating (simulation S12) | ✓ |
| Haptic DRV2605L | VDD (pin 10) = VSYS (2–5.2 V). Pin 6 VDD/NC left open: the datasheet allows tying it to VDD or leaving it floating. REG 1 µF (required). IN/TRIG = GND. EN from the expander (4 µA off) | ✓ SLOS854D |
| IR TX | 56 Ω 0603 per LED from VSYS: 26–59 mA peaks at 33 % duty over VSYS 3.0–4.5 V and the Vf spread, under the 65 mA rating at every point (simulation S4; the 47 Ω draft reached 70 mA on USB). AO3400A low side on GPIO38 (no reset pull-up on that pin), gate 100 k pull-down: dark at reset | ✓ |
| IR RX IRM-H638T | Supply from expander P5 through 100 Ω / 4.7 µF. R506 10 k pull-up to the switched supply: a defined level, and an unpowered receiver is never back-powered. Output on GPIO40 | ✓ |

### 1.5 Changes made in the reviews

Rows 1–14 come from the first (6-layer) review and still hold. Rows 15–29 are the 4-layer re-design and this release review.

| # | Change | Why |
|---|---|---|
| 1 | Expander RESET on a GPIO (now GPIO39); TP11 named XRST | recover a wedged I2C expander without a power cycle (firmware `exp_recover()`) |
| 2 | TP13–TP15 on 3V3_LCD, MIC_VDD, IR_RX_VCC | the fixture proves the switched rails really switch; firmware only sees the enables |
| 3 | R506 IR receiver pull-up to its own switched supply | removes reliance on an undocumented internal pull-up |
| 4 | C106 (100 nF at the TPS63802 VIN) removed | TI's reference layout; the 10 µF CIN is 0.97 mm from the pin, with C102 on the same net 1.5 mm away |
| 5 | U202 and U501 footprints with exposed-pad thermal vias | heat (amp) and a solid ground under the expander |
| 6 | Pin-map notes (TOF_INT 10 k, SENSE_ALRT 100 k) and the ring-dial block note corrected | documentation matched the circuit |
| 7 | All passives given verified LCSC numbers | BOM completeness |
| 8 | Left column: I2C on GPIO7/15; I2S DIN/BCLK/LRCLK on GPIO16/17/18; IMU_INT2 GPIO47, TOF_INT_N GPIO48, EXP_INT_N GPIO21 (an RTC pad, so deep-sleep wake is kept) | the I2S group runs as three nested lanes to the amplifier without crossing |
| 9 | DRV2605L pin 6 (VDD/NC) left open | the second supply pin sat between the LRA outputs; the datasheet allows it to float |
| 10 | Amplifier and haptic driver rotated 180° | outputs face their contacts: no crossing, short power loops |
| 11 | Tag-Connect pinout 1 GND, 2 EN, 3 TXD0, 4 3V3, 5 RXD0, 6 GPIO0 | TXD0 and RXD0 run straight from module pins into the top row |
| 12 | Probe pads grouped: rails (SYS, BAT, 3V3) with grounds in one service field; VBUS at the TVS; LCD, MIC and IRV at their sources | each rail probed where it is made, with a ground beside it (ODD JOBS 39) |
| 13 | Second 10 µF at the amplifier removed; capacitors renumbered | the MAX98357A datasheet asks for 10 µF + 0.1 µF |
| 14 | Silkscreen designator policy (§3); assembly drawings in the release | rule 177 serviceability without generic reference text everywhere |
| 15 | **6 → 4 layers** (mandate): JLC04161H-1080, L1 parts + critical lines / **L2 solid GND** / L3 power regions + slow lines / L4 parts + lines | cost and the ODD JOBS 4-layer preference (rule 17); the board was re-placed from scratch, not de-layered |
| 16 | L3 split into regions (`power_regions.py`): +3V3 plane over most of the board, a VSYS band (with a notch for the haptic lines), a VBUS strip under the receptacle. The router may not put L3 signals inside "cores" under the buck output, module supply, display lanes, I2S lanes and PDM pair | L4's fast lanes keep an unbroken reference; the planes stay whole |
| 17 | Placement re-done for 4 layers: IMU U401 on F at (3.2, 7.0); touch series resistors on F under the module pins; expander U202 on B at (−9.0, −13.1); J201 turned 90° (now at (15.2, 16.35)); service field on a grid (now 2.8 mm, row 49) | each fan-out gets its own escape on the layer it needs; no route wraps around a part |
| 18 | **Plug-in display**: the Limito 12-pin solder land became J301, the HDGC 18-pin back-flip connector, for the Winstar WF0128BTYAA4DNN0 panel standard. The TE line was added (GPIO9) | the panel can be swapped without solder; tear-free frames |
| 19 | 4-layer pin map: display group GPIO9–13 in the connector's own pin order; IMU_INT1 GPIO14; BOARD_ID GPIO8 (ADC1); USB_PRESENT_N GPIO3; HALL_FAST GPIO2; mic group GPIO35–37 (power, data, clock); EXP_RST_N GPIO39 and IR_TX GPIO38 (swapped in the A0 revision: MTCK's reset pull-up); backlight EN GPIO45 (strap held low by the AW9364's EN pull-down); GPIO46 NC; spare TP12 (IO35) removed | each group leaves the module towards its part without crossing another |
| 20 | Via policy: plane vias shared within 1.6 mm, pour classes for small pads, twins merged, designed links. 194 vias at the time; **205** after the audit-2 and audit-3 changes (row 45; 6-layer board: 230) | fewer drills; every pad still has a short ground |
| 21 | `via_off_pad.py`: no via may touch an SMD pad, guaranteed after routing | ODD JOBS 82 |
| 22 | `plane_check.py` in the pipeline: every In1/In2 zone must fill as one piece | a split plane fails the build, not the review |
| 23 | `snap45.py`: the last 18 oblique pad-to-via stubs became 45° doglegs; every segment is now 0/45/90° | ODD JOBS 87/88 |
| 24 | Datasheet-height 3D bodies for the 18 parts without an installed model (`models3d.py`, `footprints.py`) | ODD JOBS 74/194: the 3D and enclosure review needs every part |
| 25 | That 3D check found the IR receiver (4.0 ± 0.3 mm) under the window border. Display standoff 2.2 → 2.7 mm (window 4.9 mm above F.Cu), ToF light-blocking gasket (ST AN5231), mic gasket tube and TOP-spring boss specified (`mao-mechanical.md`) | ODD JOBS 135/198: worst-case 0.35 mm clearance with the face pressed |
| 26 | GPIO numbers in schematic notes generated from `pinmap.py` (6 stale notes fixed: TE, backlight, strap, mic supply, IR output, expander reset) | rule 189: notes that cannot drift |
| 27 | ~~FAB-NOTES panel tabs moved to 2 and 8 o'clock (were 3 and 9, on the touch electrodes)~~ | superseded by row 47 (130° and 320°, copper pulled back on every layer) |
| 28 | Pin-map I2C table names the OPT3004 (was OPT3001) | documentation matched the part |
| 29 | Layer plots at 1644 px, independent Gerber sheet (`plots/mao-main-a0-gerbers.png`) | ODD JOBS 195; the mandate's per-layer plots |
| 30 | **Speaker:** the specified Ø15 mm part did not fit (the crescent beside the cell is 12.2 mm wide). Now a Same Sky CMS-150803-088S-X8, 15 × 8 × 3 mm, under the board at 9 o'clock; springs J501/J502 replaced by its contact pads LS501, whose courtyard keeps B parts out from under it. LRA pads renamed J501 (capture order) | found by the assembled 3D model; ODD JOBS 46, 52, 198 |
| 31 | Amplifier supply capacitors C501/C502 moved to F above the 0.9 mm gap beside the VDD pins, fed by one 0.2/0.5 via into the L3 VSYS band; speaker lines on B as two nested L's (9.2 / 11.9 mm, no vias); REAR touch lead dropped below the speaker's contact; D303 fixed at its place | room for the speaker; decoupling stays within 2 mm of the pins |
| 32 | Touch-arc stitching holes moved to the arc ends (2 per arc, was 3) | none under the speaker; the arc is still one electrode on F and B |
| 33 | Ring poles: flexible ferrite multipole strip instead of 30 NdFeB discs; detents are LRA ticks in firmware | sintered magnets are metal and would sweep over the antenna (ODD JOBS 2, 3) |
| 34 | Face on three printed flexures, rocking on its lip, instead of a 0.1 mm sliding fit; LRA click on every press | FDM cannot hold the sliding fit; one switch works for presses anywhere |
| 35 | Firmware: LEFT touch zone holds its reading while the amplifier runs (+150 ms) | the speaker lies partly over that electrode and the class-D outputs switch at ~330 kHz |
| 36 | Pipeline: footprint-geometry cache rebuilt in the `place` step; placement imported lazily by `build_pcb.py` | a new footprint can no longer break placement |
| 37 | UVLO divider R111 / R112: 470 k / 240 k instead of 1 M / 510 k | simulation S1: the 0.2 µA EN leakage × 1 MΩ put the worst-case cut-off at 2.65 V; now 2.75–3.16 V (`mao-a0-verification.md`) |
| 38 | IR LED resistors R501 / R502: 56 Ω instead of 47 Ω | S4: 66–70 mA on USB, over the IR12-21C 65 mA rating; now ≤ 59 mA |
| 39 | ~~Firmware: 100 % brightness = 80 % backlight PWM~~ | superseded by the A0 revision: the AW9364 sets the current |
| 40 | A0 revision (2026-10-05): stock Winstar panel with its 70 mm tail through a slot to J301 on B; AW9364 backlight from VSYS; TPS22919 rail switch with its VIN cap; +3V3 3.18 V; charger ISET 4.3 k, /CE on expander P6, charger on F; SD_MODE 2.2 k; GPIO38/39 swap; U503/ToF inside the ring lip; boss / head keep-outs; new silk | the verification audits (`mao-a0-verification.md`, Revision) |
| 41 | Module moved 1.25 mm inward: antenna end y 27.45, its antenna-end corners at r 28.89 inside the board circle; notch (inner edge y 21.45) and keep-out follow; everything attached to the module moved with it (`MODULE_SHIFT`) | audit 2 X1: the corners reached r 30.08, into the wall |
| 42 | `mech_check.py` wall test: every body inside r 29.0 except the parts that sit in wall openings (USB-C, IR LEDs); speaker ≥ 0.8 mm from the wall (it has 1.1) | X1: the wall was never checked |
| 43 | Display-tail slack as one long loop turning in a 2.4 mm well of the carrier (≥ 1 mm radius); F.Cu under the well part-free (`TAIL_WELL`, checked) | X2: an S-fold in a 0.4 mm pocket creases |
| 44 | Router penalty for L3 under / B over another net's copper; HAPTIC_EN, EXP_INT_N and PRESS_N re-routed | X4: L3/L4 broadside 53.7 → 17.4 mm; no L3 track under a display, I2S or PDM lane |
| 45 | GND fence and coverage vias (`gnd_fence.py`, plane-neck check), decoupling-cap vias (`gnd_pad_vias.py --decap`), module GND pin 40 now 1.46 mm and pin 1 2.75 mm from a via (were 4.2 / 4.1) | X6: 61 → 74 GND vias, 1 → 12 at the rim; the remaining rim gaps are the touch arcs and the antenna |
| 46 | BOARD_ID divider moved beside GPIO8 | X5 / rule 56: 89.8 mm with 5 vias, under the USB-C shell and along the speaker → 4.1 mm, 1 via |
| 47 | Panel tabs at 130° and 328° (320° until audit 3), with all-layer keep-outs; `panel_tabs.py` checks real copper shapes | W-e: copper ≥ 1.55 mm from the edge on every layer, parts ≥ 1.35 mm |
| 48 | Silk: references with leaders where no adjacent spot is unambiguous (leader ≥ 0.8 mm); 0.15 mm minimum outline width; B identity enlarged; eggs keep out of reference room | X7, W-x, W-aa, W-ab |
| 49 | Service field re-laid: 2.8 mm grid, three rows, each name upright beside its pad on a via-free spot of its own (SCL on its left: the I2C pull-ups take the grid spot on its right) | W-v: labels nearer a neighbour than their pad |
| 50 | Assembly drawing rebuilt (A3, 4.5:1, every reference in or beside its part, no pad numbers) | X7: kicad-cli's sheets printed references over pad numbers |
| 51 | Schematic regenerated with wires, one frame per block, no duplicate notes | X8 |
| 52 | Bring-up: panel orientation pre-check before the first plug-in, first power with the backlight off, 80 MHz colour test with a 40 MHz fallback (`CONFIG_MAO_A0_LCD_PCLK_MHZ`), USB cold start without a cell, charger thermal check | electrical audit 2: I-1, N-2, N-9, N-10 |
| 53 | Panel tab 320° → 328°; via-free corner where the VSYS branch turns into the backlight bar; `plane_check.py` fails on any GND or rail neck under 0.5 mm | electrical audit 3, I-1: the VSYS feed necked to 0.27 / 0.26 mm |
| 54 | VSYS branch extended under both speaker lines; LCD_RST_N off L3 | ODD JOBS audit 3: SPK_N 4.47 mm over a zone gap and over an L3 line; broadside 13.8 mm now |
| 55 | Leaders end 0.3 mm off their own part and nearer it than any other; no silk on touch-electrode copper (REAR, MIC moved); the back identity on a reserved via-free spot | ODD JOBS audit 3: leaders ending between parts, labels on electrodes |
| 56 | Schematic notes on a 2.54 mm line pitch; tail note updated; D101 as a unidirectional TVS | ODD JOBS audit 3 / electrical audit 2 D10 |
| 57 | Self-test board_id 1480–1720 mV; comments for the 3.10–3.32 V rail, AW9364 dropout, TPS22919 slew, panel clock | electrical audit 3 N-5, N-7, audit-2 N-1/N-3/N-5 |
| 58 | L101's 3D body scaled to 1.2 mm (`models3d.py` had scaled a copy); EXP_RST_N's leg to TP11 designed; module +3V3 via on its branch axis | ODD JOBS audit 3 rules 74, 88; KiCad's off-centre check |

## 2. PCB review

Board: `hardware/mao/MAO_MAIN_A0.kicad_pcb`, generated by `design/pipeline.py`:
capture → place → designed power and fan-out copper → plane vias → grid router → prune → stitch (pour ties, GND fence, decoupling vias) → starved-thermal fix → tidy → via-off-pad → 45° snap → plane check → silk → labels → silk check → enclosure check → panel tabs → fab → assembly drawing.

### 2.1 The 4-layer mandate

| Gate | Result |
|---|---|
| DRC / ERC / parity, nothing unconnected | **DRC 0 violations at all severities, 0 unconnected, 0 parity; ERC 0.** Checks KiCad leaves off were run once in a scratch copy: only deliberate footprint-filter and library-copy notes (§1, compliance rule 1) |
| L2 continuous | In1 GND is **one piece, 2002 mm²** (73 vias), with no tracks on L2. Its only shaping is the antenna keep-out, the cuts under the two rim touch arcs and the tail slot |
| Power robust | L3: +3V3 one piece 1348 mm² (27 vias), VSYS one piece 332 mm² (9 vias, no neck under 0.5 mm), VBUS one piece 33 mm² (2 vias). Battery path 0.6–0.8 mm; all of VSYS leaves the charger through two 0.6/0.3 mm vias; VSYS branches 0.3–0.8 mm on B plus the L3 band; TPS63802 loop per TI's layout |
| USB reference uninterrupted | D+/D− run 28.2 mm each on F directly over L2 (0.076 mm prepreg), plus 2.2 / 6.1 mm on B at the ends (30.4 / 34.3 mm in all), 0.2 mm tracks with 0.6 mm gap. Vias only at the receptacle row join and the module pins (3 / 2). Nearest ground transition 2.2–4.3 mm. Full speed only, so impedance is not controlled (≈ 74 Ω differential estimated; 0.2 ns flight time vs ≥ 4 ns edges) |
| SPI reference uninterrupted | LCD_SCLK / MOSI / DC / CS / TE: 8.6–15.6 mm on B over the L3 "display lanes" +3V3 core, which no L3 signal crosses. No vias: the module and J301 are both on B |
| I2S / PDM clean | AMP_BCLK 20.6 / DIN 23.3 / LRCLK 19.6 mm and MIC_CLK 15.2 / DATA 12.1 mm: on B only, **no vias**, each over its own L3 core; no L3 track under any of them (`stackup_check.py`) |
| L3 only slow signals | 227 mm of L3 track over 13 nets: MCU_EN, I2C_SCL, TOF_INT_N, IR_TX, HAPTIC_EN, EXP_INT_N, USB_PRESENT_N, I2C_SDA, IR_RX_PWR, EXP_RST_N, PRESS_N, TOF_XSHUT, LCD_BL_CTRL. All static or slow (IR_TX is a 38 kHz carrier). L3/L4 broadside 13.8 mm in all, longest 3.0 mm, slow pairs only; the speaker outputs lie over the VSYS band (SPK_N 0.21 mm, SPK_P 0.84 mm without L3 copper, where they cross its edge) |
| Touch keep-outs | Rim arcs E301/E302 with L2 and L3 cut beneath (rule areas "TOUCH PLANE CUT"); 510 Ω at the module, 0.42 pF ESD at each electrode |
| Antenna keep-out | Module antenna over a 24.0 mm notch (inner edge y 21.45); rule area 30.0 mm wide from 0.6 mm inside the antenna boundary to the edge, on all four layers (no track, via or pour); module corners inside the board circle; no metal fastener within 15 mm |
| Fewer vias than 6 layers | **205** (146 × 0.6/0.3, 59 × 0.5/0.2) vs 230; plus 24 × 0.2 mm thermal vias inside exposed pads. No via touches an SMD pad |
| No functionality removed | All functions of the 6-layer A0 are present, plus the display TE line and a plug-in display (§4) |
| Service access | Service field on B (rails + 3 grounds, RST, BOOT, XRST, SDA, SCL), VBUS/LCD/MIC/IRV pads at their sources, Tag-Connect, BAT LINK |
| Visual standard | §3, the renders, and [`mao-odd-jobs-compliance.md`](mao-odd-jobs-compliance.md) |

### 2.2 Board metrics

| Check | Result |
|---|---|
| Stackup | 4 layers, 1.6 mm, JLC04161H-1080 written into the board: F / 1080 prepreg 0.0764 / In1 GND / core 1.265 / In2 power / 1080 prepreg 0.0764 / B. 1 oz outer, 0.5 oz inner, ENIG, black mask, white legend |
| Copper | F 675 mm, B 774 mm, In2 227 mm, In1 none. 921 segments, all at 0/45/90° (none off by more than 0.05°) |
| Widths | 0.2 mm signals. 0.15 mm on the display lanes and at fine-pitch escapes (module rows, QFN, LGA, 0.5 mm FPC): 108.5 mm. Power 0.3–0.8 mm plus L3 regions |
| Outer GND pours | Every fragment tied to the plane; fragments over 20 mm² have two ties (`gnd_stitch.py`). Island removal on, no floating copper |
| Switching loop | TPS63802 per TI's layout: CIN at VIN, inductor over the switch pins, COUT at VOUT, PGND strip with a via under the IC; L3 beneath is a router core |
| RF | Nearest metal: screws on the back half (≥ 39 mm); the LRA's steel can about 7.4 mm from the antenna's corner (the only free spot in the base, RSSI check). Cell edge 12.05 mm from the antenna boundary; speaker in the left crescent (≥ 18 mm); panel edge 3.65 mm in plane (the round format's limit, see risks) |
| Fiducials | 3 per side: 1 mm copper in a 2 mm mask opening, with a copper-free keep-out |
| Mounting | 2 × M2 screw holes Ø2.2 at ±48° on r 25 mm with a 4.6 mm copper keep-out on all layers, Ø6.5 part-free for the boss on F and Ø5.5 for the head on B; Ø2.0 NPTH peg at 225° |
| Silkscreen | `check_silk_text.py`: 83 texts, **0 findings** (no text on pads, vias, bodies, edges, touch-electrode copper or other text; no ambiguous label; 0.8 mm minimum, 0.15 mm stroke). Footprint outlines ≥ 0.15 mm |
| Gerbers | Reviewed outside KiCad with pygerber: all 11 layers rendered from the final files (`plots/mao-main-a0-gerbers.png`), extents checked (outer copper r ≤ 28.6 mm). The drill file reconciles exactly with the board: 150 × 0.3 (146 vias + 4 electrode joins), 83 × 0.2 (59 vias + 24 exposed-pad vias), 4 × 0.6 PTH slots and 9 NPTH |
| 3D | Every footprint has a body; top, bottom, two isos and the four edges rendered from the final board and reviewed (`renders/`) |

## 3. ODD JOBS review

Run against `hardware/mao/ODD-JOBS-STANDARD.txt`, ignoring ERC/DRC. Rule-by-rule results, with the justification for every ✓\*, are in [`mao-odd-jobs-compliance.md`](mao-odd-jobs-compliance.md): 200 rules, none open.

| Question | Answer |
|---|---|
| Does anything look generic? | No. Matte black mask, white legend, ENIG. The ODD JOBS mark with MAO / MAIN A0 / 2026-10 and the S/N box on the face side; mark and identity on the back. Labels name what MAO has (BAT LINK, USB, LCD, TAG, MIC, IRV, LCDV, TOP, REAR, LRA), not GPIO numbers |
| Does anything look accidental? | No. All tracks are at 45° multiples; buses run as designed lanes; the designator rule is applied everywhere |
| Anything present without a reason? | No. Every pad and part traces to a function in `circuit.py`; test pads trace to the factory test; fiducials serve two-sided assembly |
| Anything important missing? | No: rails, reset, boot, I2C, UART (Tag-Connect), USB, battery-current link, switched-rail pads, board ID, expander reset line, display TE |
| Unnecessary visual noise? | No: 84 silk texts in total. 63 resistor/capacitor references that no procedure names, and 9 parts in tight spots, are on the assembly drawing; test pads and the speaker carry function names instead of references |
| Components scattered? | No. Zones: power and charging (B, 12–2 o'clock); compute (B, 6 o'clock); audio and haptics (B, 8–9 o'clock); service field (B, between charger and module); sensing at the window band (F, 11–3 o'clock); user I/O at the centre (F: face switch, IMU) |
| Could placement be cleaner? | Only the power cluster is dense, which follows TI's reference layouts and is as tight as its loops should be |
| Could routing be simpler? | Paths are short and 45°. Long nets are inherent: I2C reaches 6 devices on both faces, MCU_EN runs from the module to TP7 and the Tag-Connect, and VSYS feeds the amplifier, haptics, IR and backlight. L3 carries 231 mm of slow lines without splitting a plane |
| Is silkscreen inconsistent? | No. Designator rule: a reference is on silk wherever a technician must find the part on the bare board: ICs, connectors, transistors, diodes, the switch, mic, inductor and electrodes, plus each R/C named in the bring-up or factory test. 45 references are on silk, 8 of them with a leader that ends 0.3 mm off its own part (C406, D101, D502, Q101, Q501, R505, U102, U301). Ten find no clear spot even with a leader (C111, C407, D304, J303, R108, R205, R213, R214, U104, U303; J303 and R108 carry their names REAR and BAT LINK); the assembly drawing identifies them. Type scale: identity 1.5 mm (MAO), connector names 1.2 mm, references and test-pad names 0.8 mm (0.15 mm stroke), each upright as seen from its own face |
| Are labels misaligned? | No: each label sits beside its part, nearer to it than to any other (checker), or points at it with a leader. The service-field names all stand upright beside their pads on one side (SCL on the other, where the I2C pull-ups take its grid spot), each on a via-free spot reserved for it |
| Are connectors awkward? | No. USB-C at 12 o'clock flush with the wall; battery JST SH opening towards the cell; springs for the touch electrodes and the speaker's own contacts on pads (no wires); Tag-Connect instead of a header; display plugs in |
| Are test points intentional? | Yes: one service field of 11 pads on a 2.8 mm grid, a ground beside every rail, plus 4 pads at their sources; all named by function |
| Does the board look like a dev board? | No (`renders/`) |
| Any obvious engineering shortcut? | None left. Recorded trade-offs: no second battery-protector IC (cell PCM + hardware UVLO); USB-C THT legs; uncontrolled impedance for full-speed USB |
| Does anything make the enclosure harder? | No. Every datum comes from `mechanical.py`. The reviews found and specified the standoff, gaskets, spring boss, tail well and wall clearance the enclosure needs. The enclosure itself is not modelled yet (final report, risk list) |
| Would another ODD JOBS designer understand the logic? | Yes: zones, function names, the assembly drawing, the architecture doc and a rebuildable pipeline |
| Comfortable showing high-resolution photos? | Yes. The face shows the mark, identity, display connector, face switch and sensors; the back shows the service field and identity block beside the module |

## 4. Order-readiness audit

Reviewed as if looking for reasons not to order.

| Question | Answer |
|---|---|
| Every existing MAO function still works? | Display (same GC9A01 controller, now a plug-in panel), dial (same 30-detent quadrature from Hall sensors), press (GPIO0), speaker (I2S amp, same vocabulary), ESP-NOW / ODD BUS, character and app. Firmware builds for both boards from one tree: 5 configurations, 0 warnings; host tests 240 checks, 0 failures |
| Anything lost going from 6 to 4 layers? | No. Every functional net of the 6-layer A0 is present (GPIOs re-assigned, §1.5 row 19). Only the spare IO35 test pad is gone (GPIO35 now supplies the mic); the display TE line was added |
| Prototype assumptions retained? | No: LCD reset line added, amp has shutdown, IR TX/RX separate, no WS2812, battery power states, chip check per board |
| GPIO conflicts? | None: `pinmap.py` is the single source; the header, pin-map doc and schematic notes are generated from it |
| Boot-strap conflicts? | None: GPIO0 face switch with pull-up and no capacitor; GPIO3 input (JTAG strap inert); GPIO45 held low by the AW9364's EN pull-down and its own internal pull-down; GPIO46 NC with pull-down |
| USB-C correct? | Sink, 5.1 k Rd on CC1/CC2, both D± rows joined, TVS + ESD at the connector |
| Charging safe? | BQ24073 at 207 mA (0.4 C), NTC window 0–50 °C plus the firmware pause at 43 °C board temperature (cell 0–45 °C), timers on, cell PCM required, reverse-polarity FET |
| Operates while charging? | Yes (DPPM: VSYS from USB, the cell takes the rest) |
| Battery polarity unambiguous? | JST SH keyed; silk "− T +" at the plug; reverse-polarity FET as backstop; pinout check on receipt (`mao-mechanical.md` §7) |
| Fuel gauge correct? | MAX17048 on VBAT, ALRT wired-OR to the expander |
| All IC power pins decoupled? | Yes (§1, every block) |
| I2C addresses compatible? | 0x20, 0x29, 0x36, 0x44, 0x5A, 0x6A: unique |
| Interrupt lines valid / wake? | IMU INT1 (GPIO14), expander INT (GPIO21), face (GPIO0), touch, USB plug (GPIO3): deep-sleep wake. ToF via light-sleep GPIO wake |
| Display interface correct? | J301 pinout traced pin by pin against the Winstar 18-pin definition (panel pin k on pad 19 − k through the tail's tangential folds). 22 Ω on SCLK/MOSI; reset and TE lines; switched rail with soft start. Bring-up checks the tail's orientation before the first plug-in and the 80 MHz clock through the 70 mm tail (40 MHz fallback in Kconfig) |
| Speaker, haptic, IR drive? | MAX98357A from VSYS, shut down by default; DRV2605L closed-loop LRA; IR 26–59 mA pulses, low-side NMOS |
| Microphone, IMU placement sane? | Mic bottom port at 3–4 o'clock with a gasket tube to the window. IMU near the puck axis on F, beside the face switch, with known axes |
| Touch electrodes viable? | Rim arcs with planes cut, series R and ESD; thresholds are a VERIFY AT BRING-UP item |
| ToF optical access? | Ø3 clear aperture at 11 o'clock; light-blocking gasket from sensor to window (AN5231); factory crosstalk calibration |
| Antenna keep-out / blocked? | §2.1: notch, rule area, no metal fastener within 15 mm |
| Ground plane continuous? | Yes: L2 one piece, checked in the build |
| Switching loops compact? | Yes (§2.2) |
| Connectors oriented, pin 1 correct? | USB-C fixed by the outline; JST SH opening to the cell; springs symmetric; J301 on B, pad 1 towards 6 o'clock, FPC entry facing the tail slot (silk dot + Fab "1"); pin-1 marks on every IC |
| Silkscreen aligned? | Yes (§3, checker 0 findings) |
| Test points accessible? | All on B, unobstructed by parts; the cell sits under the board with an insulator, so the fixture probes before the cell goes in |
| Dead board recoverable? | ROM loader: hold the face (or TP8 BOOT) during a reset: TP7 RST, Tag-Connect EN, or plugging USB into a board with no cell. Tag-Connect UART with EN/GPIO0 |
| All parts orderable? | Every BOM line carries an LCSC number checked on 2026-10-04 (asserted by `fab.py`). The panel, cell, LRA, speaker and ferrite pole strip are bought separately (`mao-bringup.md`) |
| Assembly packages correct? | Footprints from KiCad's libraries, or drawn from datasheets in `lib/MAO.pretty`, matched to the LCSC part's package |
| Rotation / origin sane? | Gerbers and CPL share KiCad's absolute origin; KiCad rotations. JLC's placement preview must be checked at order time (FAB-NOTES lists the polarised parts) |
| BOM ↔ schematic ↔ PCB? | DRC parity 0. BOM and CPL are written from the board's footprints, whose fields come from the schematic (`sync_fields.py`) |
| CPL ↔ PCB? | 112 rows = 112 assembled parts (DNP R110, test pads, fiducials, Tag-Connect, electrodes, speaker pads and LRA pads excluded; J301 fitted) |
| Fab files from the latest design? | Yes: regenerated from the final board after the last change; Gerbers re-reviewed |
| Does it physically fit? | Against the enclosure specification, yes: every part bodied and checked for height, keep-outs, the wall (r ≤ 29.0) and the tail path. There is no enclosure CAD yet, so the first print is the real fit check (final report, risks) |
| Anything to hesitate over? | Only the items in the final report's risk list, none of which is a design defect |

Status: all gates pass. Three rounds of independent audits ran (`mao-a0-verification.md`); the third found no
electrical blocker and no failed ODD JOBS rule, and its recommended fixes are in (rows 53–58). See the final
report (`mao-a0-final-report.md`).
