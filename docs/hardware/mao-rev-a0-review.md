# MAO_MAIN A0: design review

Four passes over the design:
1. Schematic, against datasheets.
2. PCB, against the ODD JOBS standard and the JLC capability.
3. A dedicated ODD JOBS review: product, visual character, serviceability.
4. The final order-readiness audit.

Sources of truth:
- capture `hardware/mao/design/circuit.py`, which generates the schematic and the netlist;
- placement `placement.py` + `mechanical.py`;
- designed copper `route_power.py` / `route_local.py`;
- pins `pinmap.py`.

Generated evidence in `hardware/mao/outputs/`: `ERC.json`, `DRC.json`, schematic PDF and SVG, `fab/`.

## 1. Schematic review

ERC: 0 violations. BOM: every fitted part has an LCSC number (checked against LCSC on 2026-10-04 for the
passives added last; asserted again by `fab.py`).

### 1.1 Power

| Block | Check | Result |
|---|---|---|
| USB-C J101 | Rd 5.1 k on CC1 and CC2 (sink, default power); D+/D− both rows joined; VBUS pins both sides joined; shell to GND | ✓ |
| VBUS ESD | SMF15A: 15 V standoff, clamps below the BQ24073 28 V absolute maximum; TPD2E2U06 on D+/D− (1.5 pF) | ✓ |
| BQ24073 | /CE = GND (charge on); EN2 = GND, EN1 = VSYS → USB500. ISET 3.0 k → 297 mA (0.6 C). ILIM 1.5 k → 1.07 A (must not be open). TD = 0 Ω to GND → termination on. TMR open → default safety timers. TS ← cell NTC 10 k (0–50 °C). OUT regulated 4.4 V on USB | ✓ datasheet SLUS810 |
| Caps | IN 1 µF 25 V; OUT 10 µF; BAT 10 µF (≥ 4.7 µF each required) | ✓ |
| Reverse polarity | AO3401A P-FET: drain to the cell, source to VBAT, gate 10 k to GND. Reversed cell: body diode blocks and Vgs = 0, so the FET stays off. Correct cell: the FET is enhanced and conducts in both directions, so charging works | ✓; Vgs max ±12 V ≥ 4.2 V |
| BAT link | R108 0 Ω 1206 (2 A class) in the cell path: lift it to measure battery current (ODD JOBS 110) | ✓ |
| Gauge MAX17048 | VDD/CELL on VBAT; CTG, QSTRT, GND tied; ALRT open-drain wired-OR with the light sensor's INT to expander P7 with a 100 k pull-up (a latched alert costs 33 µA, not 330) | ✓ |
| Buck-boost TPS63802 | EN divider 1 M / 510 k: on at 1.1 V × 2.96 = 3.26 V, off at 2.96 V. This is the hardware UVLO, independent of firmware. FB 560 k / 100 k: 0.5 V × 6.6 = 3.30 V. MODE = GND → power save (11 µA Iq). PG unused. L 0.47 µH DFE201612E (Isat 5.5 A). CIN 10 µF at VIN. COUT 2 × 22 µF (TI typical application) | ✓ SLVSEU9D. **Removed the 100 nF VIN cap**: TI's reference layout has none and the 10 µF sits 0.25 mm from the pin |
| Display rail TPS22917 | ON pulled down (off until firmware). CT 1 nF to **VIN**, as the datasheet specifies for this part (≈ 3.8 ms ramp, CT rated 7 V). QOD to VOUT through 100 Ω: controlled discharge | ✓ SLVSDN2 |
| Battery protection | Cell PCM (mandatory, checked on receipt) + hardware UVLO + NTC window + firmware critical-battery shutdown + reverse-polarity FET | ✓ (no second protector IC: see the architecture doc) |

### 1.2 Compute

| Block | Check | Result |
|---|---|---|
| Module | ESP32-S3-WROOM-1-N8R2 (quad PSRAM: GPIO35–37 free, 85 °C). 22 µF + 100 nF at 3V3. EN 10 k / 1 µF (Espressif HDG RC) | ✓ |
| Straps | GPIO0: face switch to GND, 10 k pull-up, **no capacitor** (BOOT). GPIO3 board ID (JTAG source strap, inert unless the eFuse is burnt). GPIO45 NC (VDD_SPI strap must read 0; internal pull-down). GPIO46 backlight PWM with 100 k pull-down: must read 0 for download boot, and it doubles as "backlight off" | ✓ |
| Board ID | 1 M / 1 M + 100 nF on GPIO3 → 1.65 V (A0); 1.65 µA | ✓ |
| Expander TCA6408A 0x20 | Every output has a pull-down default (rails off, panel in reset, amp shut down). Inputs CHG (100 k pull-up) and SENSE_ALRT (100 k). RESET 10 k pull-up **and GPIO37** (recovery without a power cycle; added in this review). INT to GPIO17 (deep-sleep wake) | ✓ |
| I2C | One bus, 2.2 k pull-ups, 400 kHz, 6 devices. Addresses 0x20, 0x29, 0x36, 0x44, 0x5A, 0x6A: unique | ✓ |
| Service | Tag-Connect TC2030-NL (3V3, EN, GPIO0, TXD0, GND, RXD0) + 15 probe pads | ✓ |

### 1.3 Interface and sense

| Block | Check | Result |
|---|---|---|
| Display | LH128R FPC pin order per the Limito drawing (1 GND … 12 GND). 22 Ω series on SCLK/MOSI at the module. 100 nF + 4.7 µF at the land. Backlight: 2 parallel LEDs, 10 Ω from 3V3_LCD → ≈ 30 mA, AO3400A low side, gate 100 k pull-down | ✓ |
| Ring dial | 2 × DRV5012 6° apart under 30 poles (12° per pole): 90° electrical, 2 transitions per detent, 30 detents. HALL_FAST pulled down → 20 Hz sampling asleep (1.6 µA). *Fixed in this review: the block note said 30°; the placement was already 6°* | ✓ |
| Face press | SKQG on GPIO0 | ✓ |
| Touch | 510 Ω series within ≈ 1 mm of the module pins, 0.42 pF ESD at each electrode, no plane under the rim arcs | ✓ |
| IMU LSM6DSOX | SA0 = GND → 0x6A, CS = VDDIO → I2C, SDx/SCx to VDDIO, INT1/INT2 to GPIO2/GPIO47 | ✓ DS12814 |
| ToF VL53L4CD | XSHUT from the expander (off by default), GPIO1 10 k pull-up (ST), 100 nF + 4.7 µF | ✓ |
| Light OPT3004 | ADDR = GND → 0x44, INT on the wired-OR alert | ✓ |
| Mic SPH0641 | Supply from GPIO13 through 100 Ω/1 µF (80 µA even with the clock stopped). SEL low → left. Bottom port through the board | ✓ |

### 1.4 Feedback

| Block | Check | Result |
|---|---|---|
| Amp MAX98357A | VDD = VSYS (≤ 4.4 V < 5.5 V). SD_MODE from the expander: low → shutdown 0.6 µA, 3.3 V (> 1.4 V) → left channel. GAIN_SLOT open → 9 dB. 10 µF (C501) + 100 nF (C503) at VDD, the datasheet's decoupling. EP to GND with thermal vias | ✓ |
| Haptic DRV2605L | VDD (pin 10) = VSYS (2–5.2 V). Pin 6 VDD/NC left open: the datasheet allows tying it to VDD or leaving it floating. REG 1 µF (required). IN/TRIG = GND, EN from the expander (4 µA off) | ✓ SLOS854D |
| IR TX | 47 Ω per LED from VSYS: 36–68 mA peaks at 33 % duty (avg ≤ 23 mA ≪ 65 mA). AO3400A low side, gate 100 k pull-down | ✓ |
| IR RX IRM-H638T | Supply from expander P5 through 100 Ω / 4.7 µF. **Added R506 10 k pull-up to the switched supply**: a defined level and no back-powering of an unpowered receiver | ✓ |

### 1.5 Changes made in this review

| # | Change | Why |
|---|---|---|
| 1 | GPIO37 → expander RESET; TP11 renamed XRST | recover a wedged I2C expander without a power cycle (firmware `exp_recover()`) |
| 2 | TP13–TP15 on 3V3_LCD, MIC_VDD, IR_RX_VCC | the fixture proves the switched rails really switch; firmware sees only the enables |
| 3 | R506 IR receiver pull-up to its own switched supply | removes the reliance on an undocumented internal pull-up |
| 4 | C106 (100 nF at the TPS63802 VIN) removed | TI's reference layout; the 10 µF CIN is 0.25 mm from the pin |
| 5 | U202 and U501 footprints with exposed-pad thermal vias | heat (amp) and a solid ground under the expander |
| 6 | Pin-map notes (TOF_INT 10 k, SENSE_ALRT 100 k) and the ring-dial block note corrected | documentation matched the circuit |
| 7 | All passives given verified LCSC numbers (C17888, C25092, C25076, C25123, C22859, C23182, C1523) | BOM completeness |
| 8 | Left column (pins 7-11): I2C SDA/SCL on GPIO7/GPIO15, I2S DIN/BCLK/LRCLK on GPIO16/17/18. IMU_INT2 → GPIO47, TOF_INT_N → GPIO48, EXP_INT_N → GPIO21 (still an RTC pad: deep-sleep wake kept). The ToF light-sleep wake moved to the GPIO wake source | the I2S group runs as three nested lanes to the amplifier with no crossing, and the I2C pair passes under it straight into the haptic driver |
| 9 | DRV2605L pin 6 (VDD/NC) left open | the second supply pin sat between the LRA outputs; the datasheet allows it to float |
| 10 | Amplifier and haptic driver rotated 180°; speaker springs swapped (SPK+ lower) | outputs face their contacts: no crossing, short power loops |
| 11 | Right column (pins 28-35): spare GPIO35 (TP12), MIC CLK/DATA GPIO36/38, EXP_RST_N stays GPIO37, IR TX/RX GPIO39/40, Hall A/B GPIO41/42 | each group leaves the module towards its part without crossing another: mic pair straight to MK401, IR up to its parts, Hall pair to the ring sensors, UART straight into the Tag-Connect |
| 12 | Tag-Connect pinout 1 GND, 2 EN, 3 TXD0, 4 3V3, 5 RXD0, 6 GPIO0 (was 3V3/EN/GPIO0/TXD0/GND/RXD0) | TXD0 and RXD0 run straight from module pins 37/36 into its top row; EN, 3V3 and GPIO0 drop through vias below it |
| 13 | Probe pads regrouped: rails (SYS, BAT, 3V3) with grounds in a service field straight below the charger; VBUS on the VBUS bus at the TVS; LCD, MIC and IRV at their sources; RST beside the EN via; added TP16 GND | each rail is probed where it is made, with a ground pad next to it for a spring-tip probe (ODD JOBS 39), and no long probe branches cross the board |
| 14 | Second 10 µF at the amplifier (old C502) removed; later capacitors renumbered (C503-C507 → C502-C506) | the MAX98357A datasheet asks for 10 µF + 0.1 µF; the extra bulk sat in the I2S/FPC escape |
| 15 | 4 → 6 layers (JLC06161H-3313): In1 GND / In2 signals / In3 +3V3 / In4 GND | 4 layers left ~40 of 110 connections unroutable in the centre without detours (architecture §6) |
| 16 | IMU moved to (0, 8.6) beside the face switch; expander U202 to (−5.5, −12.8) on B; pull resistors pinned beside their pins | each fan-out gets its own escape; no route wraps around a part |
| 17 | Supply and ground probe pads 1.2 mm (was 1.5) on a 3.0 × 3.2 mm grid; TP16 GND added; J201 to (13.5, 18.9) | the field fits between the charger and the module with each name beside its pad |
| 18 | Silkscreen designator policy (section 3); assembly drawings added to the release | rule 177 serviceability without generic reference text everywhere |
| 19 | Board stackup written into the board (JLC06161H-3313, black mask, white legend, ENIG) | the Gerber job file and the renders state the board as ordered (rule 178) |

## 2. PCB review

Board: `hardware/mao/MAO_MAIN_A0.kicad_pcb`, generated by `design/pipeline.py` (placement → designed power
and fan-out copper → plane vias → grid router → prune → stitch → tidy → silk). Checked on the final file:

| Check | Result |
|---|---|
| DRC (KiCad 10, project rules, zones refilled) | **0 violations, 0 unconnected, 0 schematic-parity issues** (`outputs/DRC.json`) |
| Stackup | 6 layers, 1.6 mm, JLC06161H-3313 written into the board: F / In1 GND (0.099 mm below F) / In2 signals / In3 +3V3 / In4 GND (0.099 mm above B) / B. Both outer layers sit directly on a solid ground |
| Planes | In1 and In4 GND: one continuous fill each (2086 mm²), cut only under the two rim touch arcs (by design). In3 +3V3: one piece (1995 mm²); the three slow signals that hop on In3 (CHG_N 22 mm, AMP_SD_N 9 mm, EXP_INT_N 6 mm) do not split it. In2 GND fill: stitched |
| Outer GND pours | every fragment tied to the planes; fragments over 20 mm² have two ties (`gnd_stitch.py`); KiCad island removal on, no floating copper |
| Copper | F 706 mm, B 674 mm, In2 181 mm, In3 38 mm. In2 carries only slow nets: I2C (400 kHz), enables, interrupts, 3V3_LCD |
| Vias | 230 through vias: 180 × 0.3/0.6 (planes, power), 50 × 0.2/0.5 (designed fan-out); 24 × 0.2 mm thermal vias in exposed pads. No via in an SMD pad except the exposed-pad thermal vias; no blind/buried |
| Widths | 0.2 mm signals (0.15 mm only inside fine-pitch escapes, 31 segments), 0.3–0.8 mm power (VSYS, VBAT, VBUS, +3V3 branches) |
| USB | D+/D− 40.8 / 40.1 mm, mostly on B over In4 GND, 2–3 vias each; USB 2.0 full speed (12 Mbit/s) needs no impedance control (rule 179 applies to HS) |
| RF | module antenna over the 24 × 6.3 mm notch; rule area (no track, via or pour, all layers) 3 mm round the notch plus the module's own keep-out; nearest metal: screws on the back half, cell edge 12.7 mm from the antenna, speaker in the left crescent |
| Switching loop | TPS63802 per TI's layout: CIN at VIN, inductor over the switch pins, COUT at VOUT, PGND strip with a via under the IC |
| Touch | electrode arcs at the rim with every inner plane cut beneath; 510 Ω series at the module; 0.42 pF ESD at each electrode |
| Fiducials | 3 per side, 1 mm copper in a 2 mm mask opening, copper-free keep-out round each |
| Mounting | 2 × M2 screw keep-outs (4.6 mm ring, both faces) on the back half, Ø2.0 NPTH peg at 225° |
| Silkscreen | `check_silk_text.py`: 73 texts, **0 findings** (no text on pads, vias, bodies, edges or other text; minimum 0.8 mm, 0.15 mm stroke) |
| Visual review | 3D renders (`docs/hardware/renders/`), layer plots and the assembly drawings were looked at after every pass; findings fixed (sections 1.5 and 3) |

## 3. ODD JOBS review

Run against the gate in the brief and `hardware/mao/ODD-JOBS-STANDARD.txt`, ignoring ERC/DRC.

| Question | Answer |
|---|---|
| Does anything look generic? | No. Matte black mask, white legend, ENIG; the ODD JOBS mark with MAO / MAIN A0 / 2026-10 on the face side, mark + product line + S/N box on the back. No GPIO numbers, no "POWER" or "DEBUG": labels name what MAO has (BAT LINK, SERVICE, LCD, MIC, TOP, REAR, SPK±, LRA). One exception kept on purpose: IO35, the single spare GPIO, named by its pin because it has no function yet |
| Does anything look accidental? | The first silk pass did: a 1 mm reference wherever one fitted, so some passives had a number and their neighbours did not. **Fixed**: one rule (below) |
| Anything present without a reason? | No. Every pad and part traces to a function in `circuit.py` notes; test pads to the factory test (§4 of `mao-factory-test.md`); 3 fiducials per side for two-sided assembly; the LED and spare footprints were removed in capture |
| Anything important missing? | No: rails, reset, boot, I2C, UART (Tag-Connect), USB, battery-current link, switched-rail pads, board ID, expander reset line |
| Unnecessary visual noise? | Reduced: 69 resistor/capacitor numbers moved to the assembly drawing; tracks follow 45° routing without decorative runs |
| Components scattered? | No. Zones: power and charging (B, 1–2 o'clock), compute (B, centre-bottom), audio and haptics (B, 4–5 o'clock), service field (B, between charger and module), sensing at the window (F, 11–1 o'clock), user I/O at the centre (F: face switch, IMU) |
| Could placement be cleaner? | Only in the power cluster, which follows TI's reference layouts and is as tight as its loops should be |
| Could routing be simpler? | The router's paths are short and 45°; the long nets are inherent (I2C reaches 6 devices on both faces; VSYS feeds amp, haptics and IR). In3 carries 38 mm of slow signals where In2 was full: acceptable, the plane stays whole |
| Is silkscreen inconsistent? | No. Designator rule: a reference is on silk wherever a technician must find the part on the bare board: every IC, connector, transistor, diode, the switch, mic, inductor and electrodes, and each resistor/capacitor named in the bring-up or factory-test procedure (read from those docs by `mao_labels.py`). Ten parts in the dense power cluster and the Hall pair have no clear unambiguous spot and are identified on the assembly drawing (U102, U103, U104, Q101, R103–R105, R108 (marked BAT LINK), C406, U301). Text: identity 0.8–1.3 mm (MAO largest), references 1.0 mm (4 at the 0.8 mm minimum), function and test names 0.8–1.0 mm, each upright as seen from its own face |
| Are labels misaligned? | No: each sits beside its own part, centred where clear, nearer its part than any other (checker) |
| Are connectors awkward? | No. USB-C at 12 o'clock flush with the edge; battery JST SH opening towards the cell; springs for touch electrodes and speaker (no wires); Tag-Connect footprint instead of a header |
| Are test points intentional? | Yes: one service field of 11 pads on a 3.0 × 3.2 mm grid, a ground beside every rail, plus 5 pads at their sources; names by function |
| Does the board look like a dev board? | No (`docs/hardware/renders/`) |
| Any obvious engineering shortcut? | None left. Recorded trade-offs: no second battery-protector IC (cell PCM + hardware UVLO), USB-C THT legs, display FPC as a solder land |
| Does anything make the enclosure harder? | No. All datums come from `mechanical.py`; `mao-mechanical.md` and the interface drawing give every window, spring and fastener |
| Would another ODD JOBS designer understand the logic? | Yes: zones, function names, the assembly drawing and the architecture doc |
| Comfortable showing high-resolution photos? | Yes. The face side shows the mark, identity, the display land, face switch and sensors; the back shows the service field and identity block beside the module |

Rule 173 (black mask makes inspection harder) is accepted for A0: the board is the visible product board; FAB-NOTES allow green
if black costs schedule.

## 4. Order-readiness audit

Reviewed as if looking for reasons not to order.

| Question | Answer |
|---|---|
| Every existing MAO function still works? | Display (same panel), dial (same 30-detent quadrature from Hall sensors), press (GPIO0), speaker (I2S amp, same vocabulary), ESP-NOW / ODD BUS, character and app: all carried; firmware builds for both boards from one tree |
| Prototype assumptions retained? | No: LCD reset line added, amp has shutdown, IR TX/RX separate, no WS2812, battery power states, chip check per board (audit §9) |
| GPIO conflicts? | None: `pinmap.py` is the single source; the header and pin-map doc are generated from it |
| Boot-strap conflicts? | None: GPIO0 face switch with pull-up and no capacitor; GPIO3 board ID (inert); GPIO45 NC; GPIO46 backlight with a pull-down (must read 0) |
| USB-C correct? | Sink, 5.1 k Rd on CC1/CC2, both D± rows joined, TVS + ESD at the connector |
| Charging safe? | BQ24073 297 mA (0.6 C), NTC window 0–50 °C, timers on, cell PCM required, reverse-polarity FET |
| Operates while charging? | Yes (DPPM: VSYS from USB, the cell takes the rest) |
| Battery polarity unambiguous? | JST SH keyed; silk "−  T  +" at the plug; reverse-polarity FET as backstop; pinout check on receipt (`mao-mechanical.md` §7) |
| Fuel gauge correct? | MAX17048 on VBAT, ALRT wired-OR to the expander |
| All IC power pins decoupled? | Yes (section 1, every block) |
| I2C addresses compatible? | 0x20, 0x29, 0x36, 0x44, 0x5A, 0x6A: unique |
| Interrupt lines valid / wake? | IMU INT1 (GPIO2, RTC), expander INT (GPIO21, RTC), face (GPIO0), touch TOP, USB plug: deep-sleep wake; ToF via light-sleep GPIO wake |
| Display interface correct? | Same GC9A01 panel and timing as the LCDkit; 22 Ω series on SCLK/MOSI; reset line; switched rail with soft start |
| Speaker, haptic, IR drive? | MAX98357A from VSYS, shutdown by default; DRV2605L closed-loop LRA; IR 36–68 mA pulses, low-side NMOS |
| Microphone, IMU placement sane? | Mic bottom port at 3 o'clock with its own gasket path; IMU near the puck axis beside the face switch (press and motion share the centre) |
| Touch electrodes viable? | Rim arcs with planes cut, series R and ESD; thresholds are a VERIFY AT BRING-UP item |
| ToF optical access? | Ø3 clear aperture at 11 o'clock, cover gap < 0.5 mm, factory crosstalk calibration |
| Antenna keep-out / blocked? | Section 2 RF row: notch, rule area, no metal within 15 mm |
| Ground plane continuous? | Yes (section 2 Planes row) |
| Switching loops compact? | Yes (section 2) |
| Connectors oriented, pin 1 correct? | USB-C (fixed by the outline), JST SH opening to the cell, springs symmetric, FPC land per the Limito drawing (pin 1 marked); pin-1 marks on every IC footprint |
| Silkscreen aligned? | Yes (section 3, checker 0 findings) |
| Test points accessible? | All on B, unobstructed by parts; the cell sits under the board with an insulator, so the fixture probes before the cell goes in |
| Dead board recoverable? | USB ROM loader (hold the face while plugging in, or TP8 BOOT); Tag-Connect UART with EN/GPIO0; RST pad |
| All parts orderable? | Every BOM line carries an LCSC number checked on 2026-10-04 (asserted by `fab.py`); the panel, cell, LRA, speaker and magnets are bought separately (`mao-bringup.md`) |
| Assembly packages correct? | Footprints from KiCad's libraries or drawn from datasheets in `lib/MAO.pretty`, matched to the LCSC part's package |
| Rotation / origin sane? | Gerbers and CPL share KiCad's absolute origin; KiCad rotations; JLC's placement preview must be checked at order time (FAB-NOTES lists the polarised parts) |
| BOM ↔ schematic ↔ PCB? | DRC parity 0; BOM and CPL are written from the board's footprints, whose fields come from the schematic (`sync_fields.py`) |
| CPL ↔ PCB? | 113 rows = 113 assembled parts (DNP R110, test pads, fiducials, Tag-Connect, electrodes and the FPC land excluded) |
| Fab files from the latest design? | Yes: `pipeline.py … drc fab` regenerates them from the final board in one run |
| Anything to hesitate over? | Only the items in the final report's risk list, none of which is a design defect |

Status: both gates pass. See the final report (`mao-a0-final-report.md`).
