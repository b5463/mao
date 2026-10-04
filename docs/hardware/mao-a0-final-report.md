# MAO A0 FINAL REPORT

MAO_MAIN A0, 2026-10. Design: `hardware/mao/` (generated from `hardware/mao/design/`). Reviews:
`mao-rev-a0-review.md`. Renders: `renders/`.

| Face (display side) | Back (service side) |
|---|---|
| ![MAO_MAIN A0 face side](renders/mao-main-a0-top.jpg) | ![MAO_MAIN A0 back side](renders/mao-main-a0-bottom.jpg) |

### Hardware architecture

- **Form:** a Ø58 mm round board inside a ~Ø64 × 18 mm FDM puck.
  - The face is the LCDkit's 1.28" GC9A01 panel behind a clear window.
  - A 30-pole magnet ring dial is read by two Hall latches.
  - Pressing the face is the button.
  - Four hidden touch zones.
- **Processor:** ESP32-S3 module, with an I2C expander for slow control lines.
- **Power:** single-cell LiPo.
  - A linear power-path charger (runs while charging) feeds VSYS.
  - A buck-boost makes 3.3 V, with a hardware UVLO.
  - Fuel gauge.
  - Every peripheral rail or enable is off at reset.
- **Antenna:** at 6 o'clock over a notch in the board edge; the power section is opposite, at 1–2 o'clock.
- **Board:** 6 layers, both outer layers on solid ground, black mask, ENIG.

Detail: `mao-custom-board-architecture.md`.

### Existing functionality preserved

- Display: same panel, colours, orientation and timing, plus a reset line and a switched, soft-started rail.
- Dial: 30 detents per turn and the same quadrature sequence, so `mao_input`'s decoder is unchanged.
- Press:
  - the face switch on GPIO0;
  - holding it while plugging in USB still enters the ROM loader.
- Sound: the synthesised voice now plays through an I2S amplifier with shutdown, replacing the always-on NS4150.
- Wireless: ESP-NOW / ODD BUS on the same channel and protocol as LAMP (C3).
- Character, app, persistent state (NVS `mao`), USB-Serial/JTAG console and flashing.
- One firmware tree builds both the ESP32-C3-LCDkit and the A0.
- IR: now real, with separate TX and RX. The LCDkit's IR was never configured.

### New perception hardware

| Sense | Part |
|---|---|
| Motion, orientation, tap, free-fall, pick-up | LSM6DSOX (wake-on-motion) |
| Approach, 0–1.3 m | VL53L4CD time-of-flight |
| Light / covered | OPT3004 |
| Hearing | SPH0641 PDM microphone |
| Touch: left, right, top, rear | ESP32-S3 native touch (rim arcs + spring electrodes) |
| Dial | 2 × DRV5012 + 30-pole ring |
| Battery state | MAX17048 fuel gauge, charger CHG/PGOOD |
| IR receive / send | IRM-H638T, 2 × IR12-21C |

Physical feedback adds a DRV2605L haptic driver with an LRA.

### MCU choice

**ESP32-S3-WROOM-1-N8R2**: 8 MB flash, 2 MB quad PSRAM, 85 °C rating. It is paired with a **TCA6408A** 8-bit I2C expander.

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
- Limito LH128R-IG01 panel;
- LP503035 cell with PCM and 10 k NTC;
- LD0832AA LRA;
- speaker;
- Ø3 × 2 N52 magnets × 30;
- Tag-Connect TC2030-NL cable.

### Power system

**Path:** USB-C sink (5.1 k Rd) → SMF15A TVS → BQ24073.
- DPPM, USB500.
- 297 mA charge.
- NTC 0–50 °C, timers on.

**VSYS** (4.4 V on USB, the cell voltage on battery) feeds:
- the TPS63802, which makes +3V3 (2 A); its EN divider is a hardware UVLO (on 3.26 V, off 2.96 V);
- the amplifier;
- the haptic driver;
- the IR LEDs.

**Cell side:**
- The cell needs its own PCM.
- R108 is a 0 Ω link for current measurement.
- AO3401A provides reverse-polarity protection.
- MAX17048 gauges the cell.

**Switched rails, all off at reset:**
- display (load switch, soft start, discharge);
- microphone (GPIO-powered through RC);
- IR receiver (expander-powered through RC).

**Enables, all off at reset:** amplifier SD, haptic EN, ToF XSHUT, Hall sampling.

**Budget (`mao-power-budget.md`, 500 mAh):**
- active ~137 mA, ~3.6 h;
- drowsy ~1.6 mA, ~13 days;
- deep sleep ~70 µA, ~10 months.

### GPIO

34 of the module's 36 GPIOs are used natively, including USB and UART0 (`mao-pin-map.md`, generated from `pinmap.py`). The two left:

- **GPIO35**: on test pad TP12 (IO35), unused by the firmware, reserved for fixture handshakes.
- **GPIO45**: not connected, deliberately. It is the VDD_SPI strap and must read 0 at boot.
- **Expander:** all 8 bits are used.

### Firmware changes

Committed as `daf0407`; pins updated with this board.

- **Board profiles:** `boards/lcdkit` and `boards/main_a0`, chosen by target. The pin header is generated from `pinmap.py`.
- **A0 HAL:**
  - shared I2C bus;
  - TCA6408A rails/status, with GPIO37 reset recovery;
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
- **Power policy:** ACTIVE / IDLE / DROWSY / deep sleep.
- **Boot and test:**
  - boot experience;
  - factory self-test (33 steps on A0) with `tools/factory_test.py`.
- **Builds:**
  - all five configurations build with 0 warnings: c3-dev, c3-release, s3-dev, s3-release, s3-factory;
  - host unit tests: 160 checks, 0 failures.
- **Hardware assumptions:** every value that only real hardware can confirm is a named constant marked VERIFY AT BRING-UP (`docs/firmware/mao-a0-firmware.md` §13).

### Manufacturing

**Board:**
- JLCPCB 6-layer JLC06161H-3313, 1.6 mm, FR-4 TG155.
- Stack: F / In1 GND / In2 signals / In3 +3V3 / In4 GND / B.
- 1 oz outer, 0.5 oz inner copper.
- ENIG, matte black mask, white legend.
- Through vias only: 0.3/0.6 and 0.2/0.5 mm, tented.
- No impedance control (USB full speed only).

**Assembly:**
- JLC PCBA, both sides; 113 parts, 47 BOM lines, every line with an LCSC number.
- USB-C shell legs: THT assembly or hand-solder.
- Hand-fitted after assembly: the display FPC (solder land) and the LRA.
- Fitted by JLC: the springs.

**Files** (`hardware/mao/outputs/fab/`, regenerated by `design/pipeline.py drc fab`):
- `MAO_MAIN_A0-gerbers.zip`: 6 copper layers, mask, paste, silk, outline, Gerber job, Excellon PTH/NPTH, drill maps;
- `BOM-MAO_MAIN_A0-JLC.csv`;
- `CPL-MAO_MAIN_A0-JLC.csv` (+ KiCad CPL);
- `ASSEMBLY-MAO_MAIN_A0-top.pdf` / `-bottom.pdf` (every reference);
- `FAB-NOTES.txt` (order options, assembly notes, polarised parts to check).

**Schematic** (`hardware/mao/outputs/`): PDF and SVG, netlist, ERC/DRC reports.

**Checks:**
- ERC 0;
- DRC 0 violations, 0 unconnected, 0 parity;
- silkscreen checker 0 findings.

### Known risks

1. **JLC placement preview.** Package rotation conventions differ between libraries. Check every polarised part and pin 1 in JLC's viewer at order time; FAB-NOTES lists them.
2. **Display FPC is a solder land.** The 0.7 mm pitch panel tail must be hand-soldered or hot-bar bonded after assembly.
3. **Antenna detuning** by the cell and enclosure. The module has no matching network; measure RSSI against the LCDkit.
4. **Touch sensitivity** through the 2 mm wall and the ring. Thresholds are tuned at bring-up; the fallback is conductive filament or copper tape.
5. **Ring dial** magnet-gap tolerance in FDM.
6. **ToF window crosstalk.** It depends on the window and needs factory calibration.
7. **Buck-boost ripple** with 0603 output capacitors at 3.3 V bias. Measure it; A1 fallback is 47 µF 0805.
8. **Charger temperature** at a low cell (0.85 W worst case). The fallback is a resistor swap to 200 mA.
9. **First use of four footprints:** JST SH clone, DFE201612E, the FPC land and the springs. Inspect the first boards under a microscope.
10. **Exposed-pad thermal vias** are unplugged unless JLC's via-in-pad fill is chosen. Watch for voids under U102, U202 and U501 (X-ray if offered).

### DNP / optional components

- **R110** (DNP): battery NTC substitute. Fit it only for a 2-wire cell without a thermistor.
- **Footprints that are not parts:**
  - test pads TP1–TP16;
  - Tag-Connect J201;
  - touch electrodes E301/E302 (copper);
  - display FPC land J301;
  - LRA pads J503;
  - fiducials FID1–FID6;
  - mounting holes H1–H3.

### Bring-up sequence

Full procedure: `mao-bringup.md`.

1. Inspect the board. Check R108 is fitted and that no rail is shorted to GND at the test pads.
2. Power VBUS only from a current-limited supply on TP6 (100 mA, no cell, no panel). Check:
   - TP4 SYS ≈ 4.4 V;
   - TP3 3V3 = 3.3 V;
   - TP13–TP15 < 0.3 V.
3. Connect USB. It enumerates as USB-Serial/JTAG. Flash `s3-dev` (hold the face, or TP8 BOOT, to force the ROM loader; Tag-Connect UART as fallback).
4. Run `mao selftest auto nopads`; the automatic steps must pass. Then check that TP13 switches when the display rail is enabled.
5. Fit the display and check the boot experience.
6. Connect the cell, after checking its pinout on receipt. Then check:
   - charging current through R108's pads;
   - UVLO thresholds;
   - running from the cell.
7. Bring up each subsystem in order: speaker, haptics, IMU, ToF, light, mic, touch, dial, IR, radio.
8. Measure the power states and fill in `mao-power-budget.md`.
9. Run the full factory self-test with the fixture (`mao-factory-test.md`).

### Final status

**READY FOR 5-UNIT A0 PROTOTYPE ORDER**

Both gates pass:
- the electrical / manufacturing / order-readiness audit (`mao-rev-a0-review.md` §1, §2, §4);
- the ODD JOBS Product Standard review (§3).

At order time, check JLC's placement preview (risk 1) and choose black mask and ENIG as in FAB-NOTES.
