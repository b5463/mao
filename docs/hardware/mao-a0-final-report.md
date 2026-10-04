# MAO A0 FINAL REPORT

MAO_MAIN A0, 4-layer release, 2026-10-05.

- Design: `hardware/mao/`, generated from `hardware/mao/design/` by `pipeline.py`.
- Reviews: [`mao-rev-a0-review.md`](mao-rev-a0-review.md) (schematic, PCB, ODD JOBS, order readiness) and [`mao-odd-jobs-compliance.md`](mao-odd-jobs-compliance.md) (all 200 rules).

| Face (display side) | Back (service side) |
|---|---|
| ![MAO_MAIN A0 face side](renders/mao-main-a0-top.jpg) | ![MAO_MAIN A0 back side](renders/mao-main-a0-bottom.jpg) |

![MAO_MAIN A0 iso](renders/mao-main-a0-iso.jpg)

| L1 F.Cu | L2 In1.Cu: solid GND | L3 In2.Cu: power + slow | L4 B.Cu |
|---|---|---|---|
| ![L1](plots/mao-main-a0-L1-F.png) | ![L2](plots/mao-main-a0-L2-In1.png) | ![L3](plots/mao-main-a0-L3-In2.png) | ![L4](plots/mao-main-a0-L4-B.png) |

### Hardware architecture

- **Form:** a Ø58 mm round board inside a ~Ø64 × 18.3 mm FDM puck.
  - The face is a plug-in 1.28" round GC9A01 panel behind a clear window.
  - A 30-pole magnet ring dial is read by two Hall latches.
  - Pressing the face is the button.
  - Four hidden touch zones.
- **Processor:** ESP32-S3 module, with an I2C expander for the slow control lines.
- **Power:** single-cell LiPo.
  - A linear power-path charger (runs while charging) feeds VSYS.
  - A buck-boost makes 3.3 V, with a hardware UVLO.
  - Fuel gauge.
  - Every peripheral rail and enable is off at reset.
- **Antenna:** at 6 o'clock, over a notch in the board edge. The power section and USB-C are opposite, at 12–2 o'clock.
- **Board:** 4 layers (JLC04161H-1080):
  - L1 parts and critical lines;
  - **L2 solid GND**;
  - L3 power regions and slow lines;
  - L4 parts and lines.
- **Finish:** black mask, ENIG.

Detail: `mao-custom-board-architecture.md`.

### Existing functionality preserved

- Display: same GC9A01 controller, resolution, colours and timing.
  - New: it plugs into J301, so a panel can be swapped without solder.
  - Added: a reset line, a TE (tearing-effect) line and a switched, soft-started rail.
- Dial: 30 detents per turn and the same quadrature sequence, so `mao_input`'s decoder is unchanged.
- Press:
  - the face switch on GPIO0;
  - holding it while plugging in USB still enters the ROM loader.
- Sound: the synthesised voice plays through an I2S amplifier with shutdown, replacing the always-on NS4150.
- Wireless: ESP-NOW / ODD BUS on the same channel and protocol as LAMP (C3).
- Character, app, persistent state (NVS `mao`), USB-Serial/JTAG console and flashing.
- One firmware tree builds both the ESP32-C3-LCDkit and the A0.
- IR: now real, with separate TX and RX. The LCDkit's IR was never configured.
- Nothing was dropped going from the 6-layer board to this 4-layer one.

### New perception hardware

| Sense | Part |
|---|---|
| Motion, orientation, tap, free-fall, pick-up | LSM6DSOX (wake-on-motion), on the face side with known axes |
| Approach, 0–1.3 m | VL53L4CD time-of-flight |
| Light / covered | OPT3004 |
| Hearing | SPH0641 PDM microphone |
| Touch: left, right, top, rear | ESP32-S3 native touch (rim arcs + spring electrodes) |
| Dial | 2 × DRV5012 + 30-pole ring |
| Battery state | MAX17048 fuel gauge, charger CHG/PGOOD |
| IR receive / send | IRM-H638T, 2 × IR12-21C |

Physical feedback: a DRV2605L haptic driver with an LRA.

### MCU choice

**ESP32-S3-WROOM-1-N8R2**: 8 MB flash, 2 MB quad PSRAM, 85 °C. It is paired with a **TCA6408A** 8-bit I2C expander.

- The C3 has 15 usable GPIOs, no touch and no PDM input; the design needs 42 signals.
- The S3 adds:
  - native touch with deep-sleep wake;
  - two I2S units, so the mic and speaker run at once;
  - PSRAM for animation and audio.
- Quad PSRAM (not octal) keeps GPIO35–37 usable and keeps the 85 °C rating.
- Wake and fast lines stay native; the expander carries only rail enables and slow status.

### Major components

| Function | Part | LCSC |
|---|---|---|
| Module | ESP32-S3-WROOM-1-N8R2 | C2913204 |
| I/O expander | TCA6408ARGTR | C181499 |
| Charger | BQ24073RGTR | C15220 |
| 3.3 V buck-boost | TPS63802DLAR + DFE201612E-R47M | C2845237, C668312 |
| Fuel gauge | MAX17048G+T10 | C2682616 |
| Display rail switch | TPS22917DBVR | C2681320 |
| Display connector | HDGC 0.5K-HX-18PWB, 18-pin 0.5 mm back-flip FPC, top + bottom contacts | C2919497 |
| Reverse-polarity FET | AO3401A | C15127 |
| USB-C / ESD / TVS | TYPE-C-31-M-12, TPD2E2U06, SMF15A | C165948, C1972959, C123802 |
| IMU | LSM6DSOXTR | C481766 |
| ToF | VL53L4CDV0DH/1 | C3178291 |
| Ambient light | OPT3004DNPR | C2655153 |
| Microphone | SPH0641LU4H-1 | C2879853 |
| Hall latches | DRV5012AEDMRR × 2 | C2655038 |
| Speaker amp | MAX98357AETE+T | C910544 |
| Haptic driver | DRV2605LDGSR | C527464 |
| IR receiver / LEDs | IRM-H638T, IR12-21C × 2 | C91447, C53672 |
| Face switch | SKQGADE010 | C116647 |
| Battery connector | JST SH 3-pin (SM03B-SRSS-TB compatible) | C7430445 |
| Spring contacts (touch top/rear, speaker) | BW0019BG × 4 | C2826516 |

Bought separately (not on the BOM):
- Winstar WF0128BTYAA4DNN0 panel, or any 1.28" round GC9A01 panel with the 18-pin 0.5 mm tail;
- LP503035 cell with PCM and 10 k NTC;
- LD0832AA LRA;
- Ø15 mm speaker;
- Ø3 × 2 N52 magnets × 30;
- Tag-Connect TC2030-NL cable.

### Power system

**Path:** USB-C sink (5.1 k Rd) → SMF15A TVS → BQ24073.
- DPPM, USB500.
- 297 mA charge.
- NTC 0–50 °C, timers on.

**VSYS** (4.4 V on USB, the cell voltage on battery) feeds:
- the TPS63802, which makes +3V3 (2 A) and whose EN divider is a hardware UVLO (on 3.26 V, off 2.96 V);
- the amplifier;
- the haptic driver;
- the IR LEDs.

On the board, VSYS is an L3 band plus wide B tracks, and +3V3 is the L3 plane.

**Cell side:**
- The cell needs its own PCM.
- R108 is a 0 Ω link for current measurement (BAT LINK).
- AO3401A provides reverse-polarity protection.
- MAX17048 gauges the cell.

**Switched rails, all off at reset:**
- display (load switch, soft start, discharge);
- microphone (GPIO-powered through RC);
- IR receiver (expander-powered through RC).

**Enables, all off at reset:** amplifier SD, haptic EN, ToF XSHUT, Hall sampling.

**Budget (`mao-power-budget.md`, 500 mAh):**

| State | Battery current | Runtime |
|---|---:|---:|
| Active | ~137 mA | ~3.6 h |
| Drowsy | ~1.6 mA | ~13 days |
| Deep sleep | ~70 µA | ~10 months |

### GPIO

Every module GPIO is used natively except **GPIO46**, the download-boot strap, which is NC with its internal pull-down. That count includes USB, UART0 and the display TE line.

- **GPIO45**, the VDD_SPI strap, drives the backlight gate; the gate's 100 k pull-down holds it at 0 during reset.
- **Expander:** all 8 bits are used.
- **Source:** `mao-pin-map.md`, generated from `pinmap.py`, which also generates the firmware header and the GPIO numbers in the schematic notes.

### Firmware

Committed as `daf0407`; pins updated with this board.

- **Board profiles:** `boards/lcdkit` and `boards/main_a0`, chosen by target. The pin header is generated from `pinmap.py`.
- **A0 HAL:**
  - shared I2C bus;
  - TCA6408A rails and status, with GPIO38 reset recovery;
  - display with runtime rotation and reset;
  - I2S amplifier;
  - deep-sleep wake sources.
- **Drivers:**
  - `mao_power` (MAX17048, charger, power policy);
  - `mao_haptics` (DRV2605L);
  - `mao_sense` (LSM6DSOX, VL53L4CD, OPT3004, touch, PDM mic);
  - IR NEC.
- **Perception:** `mao_perception`, a platform-independent percept engine:
  - filters, hysteresis, fusion windows and confidence;
  - feeds `mao_events`;
  - the app reacts by context, never sensor-to-animation.
  - The IMU axis default matches the layout: +Z out of the face, per ST AN5192.
- **Power policy:** ACTIVE / IDLE / DROWSY / deep sleep.
- **Boot and test:**
  - boot experience;
  - factory self-test (33 steps on A0) with `tools/factory_test.py`.
- **Builds (2026-10-05, against this pin map):**
  - all five configurations build with 0 warnings: s3-dev, s3-factory, s3-release, c3-dev, c3-release;
  - host unit tests: 160 checks, 0 failures.
- **Hardware assumptions:** every value only real hardware can confirm is a named constant marked VERIFY AT BRING-UP (`docs/firmware/mao-a0-firmware.md` §13).

### Board

| Item | Value |
|---|---|
| Stackup | JLC04161H-1080, 1.6 mm. F / 0.0764 prepreg / In1 GND / 1.265 core / In2 power / 0.0764 prepreg / B. 1 oz outer, 0.5 oz inner |
| Planes | L2 GND one piece, 2099 mm², no tracks. L3: +3V3 1504 mm², VSYS 361 mm², VBUS 33 mm², each one piece |
| Vias | **191** (135 × 0.3/0.6, 56 × 0.2/0.5), vs 230 on the 6-layer board. 24 × 0.2 mm thermal vias in exposed pads. No via touches an SMD pad |
| Copper | F 714 mm, B 692 mm, L3 135 mm (slow lines only). All 827 segments at 0/45/90° |
| USB | D± on F over L2, 29.5 mm, through-paths within about 3 mm; full speed |
| Fast lanes | Display SPI, I2S and PDM on B over their own uncrossed L3 cores; I2S and PDM have no vias |
| Silkscreen | 71 texts, checker 0 findings. Mark, MAO / MAIN A0 / 2026-10 and an S/N box on the face; identity on the back; every test pad named |

### Manufacturing

**Board:**
- JLCPCB 4-layer **JLC04161H-1080**, 1.6 mm, FR-4 TG155.
- 1 oz outer, 0.5 oz inner copper.
- ENIG, matte black mask, white legend.
- Through vias only: 0.3/0.6 and 0.2/0.5 mm, tented.
- Epoxy-filled and capped (POFV) for the exposed-pad vias if offered.
- No impedance control (USB full speed only).
- Order single boards. Panel tabs, if needed, only at 2 and 8 o'clock (FAB-NOTES).

**Assembly:**
- JLC PCBA, both sides: **114 parts, 48 BOM lines**, every line with an LCSC number.
- J301, the display connector, is fitted by JLC; the panel plugs in after assembly.
- USB-C shell legs: THT assembly or hand-solder.
- Fitted by JLC: the springs.
- Hand-fitted after assembly: the LRA leads.

**Files** (`hardware/mao/outputs/fab/`, regenerated from the final board):
- `MAO_MAIN_A0-gerbers.zip`: 4 copper layers, mask, paste, silk, outline, Gerber job, Excellon PTH/NPTH, drill maps;
- `BOM-MAO_MAIN_A0-JLC.csv`;
- `CPL-MAO_MAIN_A0-JLC.csv` (+ KiCad CPL);
- `ASSEMBLY-MAO_MAIN_A0-top.pdf` / `-bottom.pdf` (every reference);
- `FAB-NOTES.txt` (order options, assembly notes, polarised parts to check, panel tabs).

**Schematic** (`hardware/mao/outputs/`): PDF and SVG, netlist, ERC/DRC reports, plane report.

**Checks:**
- ERC 0, including KiCad's default-off checks run once: only deliberate footprint-filter notes.
- DRC 0 at all severities, 0 unconnected, 0 parity.
- Every L2/L3 plane in one piece.
- Silkscreen checker: 0 findings.
- 45°: every segment.
- Gerbers reviewed outside KiCad (`plots/mao-main-a0-gerbers.png`); the drill file reconciles with the board.

### Known risks

1. **JLC placement preview.** Package rotation conventions differ between libraries. Check every polarised part and pin 1 in JLC's viewer at order time; FAB-NOTES lists them.
2. **Panel tail.** J301 takes any 18-pin 0.5 mm round-panel tail, either way up. Check that the bought panel follows the Winstar pin definition and that its tail is 10.5–12.5 mm from the glass edge to the contacts' end.
3. **Antenna detuning** by the cell, the panel (its edge is 4.9 mm from the antenna boundary in plane) and the enclosure. The module has no matching network; measure RSSI against the LCDkit.
4. **Touch sensitivity** through the 2 mm wall and the ring. Thresholds are tuned at bring-up; the fallback is conductive filament or copper tape.
5. **Ring dial** magnet-gap tolerance in FDM.
6. **ToF window crosstalk.** It needs the light-blocking gasket specified in `mao-mechanical.md` §5 and factory calibration.
7. **Buck-boost ripple** with 0603 output capacitors at 3.3 V bias. Measure it; the A1 fallback is 47 µF 0805.
8. **Charger temperature** at a low cell (0.85 W worst case). The fallback is a resistor swap to 200 mA.
9. **First use of four footprints:** JST SH clone, DFE201612E, the HDGC J301 and the springs. Inspect the first boards under a microscope.
10. **Exposed-pad thermal vias** are unplugged unless POFV is chosen. Watch for voids under U102, U202 and U501 (X-ray if offered).
11. **Enclosure first fit.** The puck is printed from the specification. Check on the first print:
    - the IR receiver's 0.35 mm worst-case clearance under the pressed window;
    - the TOP spring boss;
    - the ToF and mic gaskets.

### DNP / optional components

- **R110** (DNP): battery NTC substitute. Fit it only for a 2-wire cell without a thermistor.
- **Footprints that are not parts:**
  - test pads TP1–TP11, TP13–TP16;
  - Tag-Connect J201;
  - touch electrodes E301/E302 (copper);
  - LRA pads J503;
  - fiducials FID1–FID6;
  - mounting holes H1–H3.
- **J301 is fitted:** it is a real connector now, not a land.

### Bring-up sequence

Full procedure: `mao-bringup.md`.

1. Inspect the board. Check R108 is fitted and that no rail is shorted to GND at the test pads.
2. Power VBUS only from a current-limited supply on TP6 (100 mA, no cell, no panel). Check:
   - TP4 SYS ≈ 4.4 V;
   - TP3 3V3 = 3.3 V;
   - TP13–TP15 < 0.3 V.
3. Connect USB. It enumerates as USB-Serial/JTAG. Flash `s3-dev` (hold the face, or TP8 BOOT, to force the ROM loader; the Tag-Connect UART is the fallback).
4. Run `mao selftest auto nopads`; the automatic steps must pass. Then check that TP13 switches when the display rail is enabled.
5. Plug the panel into J301 and check the boot experience and orientation.
6. Connect the cell, after checking its pinout on receipt. Then check:
   - charging current through R108's pads;
   - UVLO thresholds;
   - running from the cell.
7. Bring up each subsystem in order: speaker, haptics, IMU, ToF, light, mic, touch, dial, IR, radio. Then do the enclosure first fit.
8. Measure the power states and fill in `mao-power-budget.md`.
9. Run the full factory self-test with the fixture (`mao-factory-test.md`).

### ODD JOBS standard

All 200 rules were checked one by one in [`mao-odd-jobs-compliance.md`](mao-odd-jobs-compliance.md); none is open. Rules marked ✓\* carry their reason or a named bring-up check. The release gate is **EVT** (engineering prototype), and the DVT items for antenna, connectors, ESD and power architecture are already met.

The golden rule:

| Question | Answer |
|---|---|
| Can it boot? | Yes |
| Can it be flashed? | Yes: USB, plus the Tag-Connect UART |
| Can it be recovered? | Yes: ROM loader by face, TP8 or Tag-Connect |
| Can it be measured? | Yes: 15 named pads and BAT LINK |
| Can it physically fit? | Yes: every part bodied and checked against the stack |

### Final status

**READY FOR 5-UNIT A0 PROTOTYPE ORDER — 4 LAYER**

Every gate of the 4-layer review passes (`mao-rev-a0-review.md` §2.1):
- DRC/ERC/parity 0 and nothing unconnected;
- L2 continuous;
- power robust;
- USB and SPI references uninterrupted;
- I2S/PDM clean;
- touch and antenna keep-outs correct;
- no functionality removed;
- service access;
- visual standard.

The ODD JOBS review (§3 and the compliance document) also passes.

At order time:
- choose JLC04161H-1080, black mask and ENIG, as in FAB-NOTES;
- check JLC's placement preview (risk 1).
