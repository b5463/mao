> **Superseded 2026-10-06 (kept as history):** this document describes MAO_MAIN **A0**. The current board is MAO_MAIN **A1** (A0 moved to the M5 Gate C architecture): see [mao-a1-report.md](mao-a1-report.md).

# MAO_MAIN A0: ODD JOBS standard, rule by rule

## A1 owner decisions 2026-10-06: rules affected

The rows below are A0's. For A1 (`MAO_MAIN_A1.kicad_pcb`, checks on the final files in
[mao-a1-report.md](mao-a1-report.md)) the owner's decisions of 2026-10-06 change these:

| Rule | A1 |
|---|---|
| 75 Z-height zones | `mech_check.py` 0 findings; the three face-tripod switches SW301–SW303 (1.5 mm with stem) are the designed exceptions under the panel |
| 138 Button tolerance | The face rests on three SKQGAFE010 domes at 120° (bosses on the carrier, flexures only centre it); nothing slides |
| 172 Standard colour | **Purple** solder mask, white legend, ENIG (owner decision; the standard lists black as one option, "if visible and brand-relevant, standardize") |
| 173 Black mask inspection | Not applicable: purple is lighter than black, tracks and joints stay inspectable |
| 175 Hidden details | Eggs are kanji only, all hidden once assembled: 猫猫 by the face press with "boop", 銀 by the BAT LINK 0R, 薬 by the charger, 酒 under the cell, 毒見 under the speaker, plus "MADE FOR BAD IDEAS" under the panel. Brush kanji are filled silk polygons sized so a 0.16 mm opening loses < 2 % of their ink (JLC silk line 0.153 mm) |


Every rule of `hardware/mao/ODD-JOBS-STANDARD.txt` (V1.0, rules 1–200 plus the RF picture and the release gates),
checked against the final board `hardware/mao/MAO_MAIN_A0.kicad_pcb` on 2026-10-05. Numbers come from the board, the
checker outputs in `hardware/mao/outputs/` and the third independent ODD JOBS audit (`mao-a0-verification.md`,
Audits), with the fixes made after that audit measured again. They do not come from intent.

- **✓**: met.
- **✓\***: met, with a recorded reason or a named bring-up check.
- **◐**: partly met. The table says what is missing, why it is acceptable for the 5-unit EVT, and the A1 action.
- **N/A**: does not apply to this board (reason given).

Count: 141 ✓, 45 ✓\*, 6 ◐, 8 N/A. No rule fails. The one open user decision is the missing enclosure CAD (rules 1, 46,
194, 198, 200: ✓\* with the first print as the fit check).

## 1. Baseline and RF (1–9)

| # | Rule | | How MAO_MAIN A0 meets it |
|---|---|---|---|
| 1 | Absolute baseline | ✓\* | ERC 0 and DRC 0 at all severities, 0 unconnected, 0 parity. KiCad's default-off checks were run once on a copy (third audits). They give 6 footprint-filter notes (generic symbols on verified footprints: J102, J501, D501, D502, U402, U502), 12 single-sheet global labels, 1 four-way junction (FB node), and 18 library-copy notes. The library-copy notes are the parts whose silk is trimmed at the rim (U201, J101, LS501) and the 15 test pads whose silk ring moved to Fab; their pads are identical to the library (`project.py`). Every fitted part has an MPN and an LCSC number. Enclosure: every part is checked against `mechanical.py` for heights, keep-outs, the wall and the tail path (`mech_check.py`, 0 findings). There is no enclosure CAD yet (user decision), so the first print is the fit check |
| 2 | Antenna at the edge, Espressif placement | ✓\* | WROOM-1 on B at 6 o'clock, antenna over a 24.0 mm notch (inner edge y 21.45, 7.55 mm deep on the axis). The module's antenna-end corners are at r 28.89, inside the board circle. Distances from the antenna: L101 35.0 mm, USB-C 47 mm, cell 12.05 mm, speaker 17.1 mm. The panel edge is 3.65 mm in plane. Antenna RSSI is checked against the LCDkit at bring-up |
| 3 | Real keep-out zone | ✓\* | Rule area "ANTENNA KEEP-OUT" on all four layers, from 0.6 mm inside the antenna boundary outwards and 3 mm beyond the notch's sides: no track, via or pour. The screws are on the back half and the locating peg is plastic. The ring's poles are a ferrite strip. One metal part is closer than the 15 mm guideline: the LRA's steel can, 7.38 mm from the antenna's corner, in the only free spot in the base. Bring-up runs the RSSI test with and without the LRA (more than 3 dB lost: move it in A1). The interface drawing states this |
| 4 | Consistent RF orientation | ✓ | Antenna on the front edge (6 o'clock, towards the person); the USB cable leaves at 12 o'clock; the hand grips the ring |
| 5 | RF ground stitching | ✓\* | 73 GND vias in all. A ring at r 27.6 puts 10 vias near the rim, plus the 2 USB shell legs; its only gaps over 40° are the two touch arcs and the antenna notch, where vias are banned by design. Module GND pin 40 is 1.46 mm from a via. Pin 1 is 2.75 mm: it is the module's own ground, tied inside the module to its 12-via ground pad, and the 1.2 mm notch-edge rule leaves no via site there |
| 6 | RF vs noisy circuits | ✓ | From the antenna boundary: U104 32.5 mm, L101 35.0, U501 30.6, U303 25.1, U502 12.2 (with the module body between). Display, I2S and PDM leave the module's inner corner |
| 7 | Switching regulator per reference layout | ✓\* | TPS63802 per TI SLVSEU9D: inductor over the switch pins, COUT at VOUT, PGND strip with a via under the IC. CIN C105 is 0.97 mm from VIN, with C102 on the same net 1.5 mm away. C105's ground returns to PGND over a 2 mm, 0.3 mm track and has no via of its own (no site within 1.6 mm). A1: turn C105 so its ground faces PGND |
| 8 | Switch node | ✓\* | BB_L1 and BB_L2 are 1.3 mm each on B: no vias, no test pad, nowhere near the MCU or antenna. One L3 slow line, TOF_INT_N (open drain, 10 k pull-up), passes 0.55 mm under BB_L1 and 0.89 mm under the FB node: about 0.1 pF, tens of mV against its 0.8 V input low (electrical audit 3). A1: route it outside the regulator |
| 9 | Inductor placement | ✓ | L101 to the antenna 35.0 mm, mic 26.8, IMU 20.6, Hall sensors 33.3 mm. No magnetometer; the crystal is inside the module |

## 2. Power and grounding (10–20)

| # | Rule | | How |
|---|---|---|---|
| 10 | Power entry is hostile | ✓ | USB-C: SMF15A TVS (clamps below the BQ24073's 28 V absolute maximum), TPD2E2U06 on D±, the charger's input OVP and USB500 limit (450–500 mA; USB100 until +3V3 is up), 1 µF at IN. Battery: reverse-polarity P-FET, cell PCM, NTC window |
| 11 | Document every rail | ✓ | VBUS, VSYS, VBAT, BAT_RAW/BAT_IN, +3V3, 3V3_LCD, MIC_VDD, IR_RX_VCC, HAP_REG: named in the schematic, with currents in `mao-power-budget.md` |
| 12 | High current on wide copper | ✓ | Battery path 0.6–0.8 mm. All of VSYS leaves the charger through two 0.6/0.3 mm vias into a 332 mm² L3 band that feeds the amplifier, haptics, IR and backlight. `plane_check.py` refills the rail regions at a 0.5 mm minimum width: no neck. +3V3 is the L3 plane (1348 mm², 27 vias); the module's feed is a 0.5 mm track with two vias |
| 13 | Current headroom | ✓ | TPS63802 rated 2 A against a worst-case +3V3 under 0.7 A. Charger 207 mA of its 1.5 A. TPS22919 1.5 A for ~20 mA; AW9364 40 mA by design; R108 1206 (2 A class) |
| 14 | Local 100 nF at every IC | ✓\* | Every IC supply pin has its capacitor on its net within 1.9 mm (pad edge to pad edge; electrical audit 3, §5). The two Hall latches are the exception at 2.96 / 2.86 mm: their caps moved inward for the 1.5 mm MLCC-to-edge rule, and the latches draw µA pulses |
| 15 | Bulk at transient loads | ✓ | Module 22 µF + 100 nF; buck output 2 × 22 µF; amplifier 10 µF + 100 nF; display 4.7 µF; IR LEDs 4.7 µF reservoir; charger 10 µF IN/OUT/BAT |
| 16 | Short capacitor ground path | ✓\* | Plane vias are shared within 1.6 mm and a decoupling pass adds a via within 1.2 mm where one fits. Five caps have none within 1.6 mm, each for a stated reason. C202 (4.29 mm) and C205 (2.44 mm) close their loop on their IC's own ground pin: module pin 1 and expander pin 16. C205 sits over the 2 mm VSYS bar, so a via beside it would neck that rail. C105 (2.36) is covered in rule 7. C302 (2.29) and C502 (2.10) return through the outer pour, which is stitched to L2 |
| 17 | 4-layer stack | ✓ | L1 parts and signals / **L2 solid GND** / L3 power regions + slow signals / L4 parts and signals. JLC04161H-1080 (0.076 mm prepreg) |
| 18 | Never cross a split | ✓ | L2 is one piece (2002 mm²); its only cuts are the touch arcs, the antenna keep-out and the tail slot. On L3, every display, I2S and PDM line has +3V3 copper under 100 % of its length. The speaker lines lie over the VSYS band: SPK_N has 0.21 mm and SPK_P 0.84 mm without L3 copper, where each crosses the band's edge |
| 19 | Return paths | ✓ | Fast lines lie on an outer layer directly over L2 (L1) or over unbroken L3 copper (L4); clocks are short (rule 21) |
| 20 | GND via at fast layer changes | ✓\* | USB D± change layers at the receptacle row join and at the module, with ground 2.6–4.2 mm away; at full speed (≥ 4 ns edges) that is electrically short. Display, I2S and PDM lines have no vias |

## 3. Signals (21–39)

| # | Rule | | How |
|---|---|---|---|
| 21 | Clock lines | ✓ | LCD_SCLK_P 11.8 mm, MIC_CLK 15.2 mm, AMP_BCLK 20.6 mm: no vias, all from the module's inner corner |
| 22 | Crystals | N/A | The crystal is inside the WROOM-1 module |
| 23 | USB D+/D− as a pair | ✓\* | On F over L2, 28.2 mm each plus 2.2 / 6.1 mm on B at the ends; 0.2 mm tracks, 0.30–0.71 mm gap; 3 / 2 vias. Impedance is not controlled (≈ 74 Ω differential estimated): the ESP32-S3 is USB full speed, and 0.2 ns of flight time is under 1/20 of its edge |
| 24 | USB connector placement | ✓ | Four THT shell legs; mating face 0.6 mm past the edge (`USB_FRONT_Y`), in a 9.2 × 3.6 mm wall opening; ESD U101 2.1 mm from the data pads, TVS D101 at the VBUS entry |
| 25 | USB-C CC | ✓ | Sink: 5.1 kΩ Rd on CC1 and CC2 |
| 26 | USB-C shield | ✓ | Plastic puck: shell legs solid to GND |
| 27 | Fast SPI close to the MCU | ✓ | Display lines 8.6–15.6 mm on B from the module pins to J301, no vias, over +3V3 that no L3 line crosses |
| 28 | SD card | N/A | No SD card |
| 29 | Series termination option | ✓ | 22 Ω on LCD_SCLK and LCD_MOSI at the module (R301, R302) |
| 30 | I2C pull-ups sized | ✓ | 2.2 kΩ, 400 kHz, 6 devices: 116 / 186 ns rise against 300 ns (simulation S7) |
| 31 | Unused GPIO defined | ✓ | Only GPIO46 is unused: NC with its internal pull-down. Every expander output has an external default resistor |
| 32 | Boot straps | ✓ | GPIO0: face switch, 10 kΩ pull-up, no capacitor. GPIO45: AW9364 EN, held low by its 150 kΩ pull-down and the pin's own (the module's flash voltage is fixed by eFuse). GPIO3: PGOOD input, inert unless its eFuse is burnt. GPIO46 NC. All 36 module pins match the pin map |
| 33 | Reset access | ✓ | EN on TP7 RST, Tag-Connect pin 2 and the USB-Serial/JTAG auto-reset; the expander's reset on TP11 XRST (GPIO39) |
| 34 | Boot mode access | ✓ | Hold the face (GPIO0) or TP8 BOOT during a reset: TP7, Tag-Connect EN, or plugging USB into a board with no cell (with a cell, plugging USB does not reset the chip; bring-up and the mechanical doc say so) |
| 35 | Programming access | ✓ | Native USB-Serial/JTAG, plus Tag-Connect TC2030-NL (GND, EN, TXD0, 3V3, RXD0, GPIO0) |
| 36 | Test pads | ✓ | VBUS, SYS, BAT, 3V3, three GND, RST, BOOT, XRST, SDA, SCL; switched rails LCDV, MIC, IRV; TX/RX on the Tag-Connect |
| 37 | Pad size | ✓ | 1.2 mm for rails and grounds, 1.0 mm for signals |
| 38 | Ground pads | ✓\* | Three GND pads (TP1, TP2, TP16) in the service field and Tag-Connect GND. The switched-rail pads at their sources (VBUS, LCDV, MIC, IRV) are 9–17 mm from a ground pad: the fixture measures them with its DMM against TP1/TP2 (`mao-factory-test.md`) |
| 39 | Signal + GND pairs for scope | ✓\* | In the service field every rail pad has a ground pad 2.8 mm away (spring-tip probe). The pads at their sources use the field's grounds (rule 38) |

## 4. Connectors, battery and audio (40–60)

| # | Rule | | How |
|---|---|---|---|
| 40 | External connectors | ✓ | USB-C only (ESD, TVS, current limit, THT legs). The battery connector is internal: keyed JST SH plus the reverse-polarity FET |
| 41 | Standard connector orientation | ✓ | J102: 1 GND (BAT−), 2 NTC, 3 BAT+, silk "− T +", the same order as KINO's J1100; fixed in `mao-mechanical.md` §7 for later boards |
| 42 | Polarity on silk | ✓ | "− T + / BAT" at J102; "+" at the speaker's SPK+ pad and the LRA; diode bars |
| 43 | Functional labels | ✓\* | USB, BAT LINK, "− T + BAT", LCD, LCDV, TAG, TOP, REAR, MIC, IRV, LRA, VBUS and the field names. No label sits on electrode copper. LCD sits by J301's open end, 0.81 mm from the module's pads and 1.65 mm from J301's: it names the panel's connector, the only one there |
| 44 | Nothing under locking areas | ✓\* | Under J101's shell: IR_RX_PWR (a static enable) for 9.0 mm and one tented GND via, all masked. Under J301's plastic body: the three I2S lines and LCD_RST_N, masked. No track crosses a latch or retention land |
| 45 | FPC checks | ✓\* | J301 HDGC 0.5K-HX-18PWB: back-flip lock, contacts top and bottom (either way up), entry facing the tail slot, pin 1 marked on silk and Fab. Pad map panel pin k → pad 19 − k is verified on the board (both audits). The panel's own pin 1 is checked against the Winstar specification before the first plug-in (bring-up §3) |
| 46 | FPC entry modelled | ✓\* | The tail path is dimensioned in `mechanical.py` and the interface drawing: slot, corridor, J301 entry, carrier pocket and well. It is not in CAD (no enclosure CAD yet) |
| 47 | Bend radius | ✓\* | The slack is one loop turning in a 2.4 mm well: R = (2.4 − 2 × 0.11) / 2 = 1.09 mm, ≥ 1 mm. No part under the well (`mech_check.py`). The 90° drop into the slot and the bend into J301's entry (4.5 mm of room) are checked on the first print |
| 48 | Battery location | ✓ | Under the board on a 0.3 mm insulator with 0.3 mm swell clearance, 12.05 mm from the antenna boundary, clear of the speaker. B parts ≤ 3.2 mm |
| 49 | LiPo protection | ✓ | Cell PCM (mandatory, checked on receipt), hardware UVLO (2.96 V), NTC window, firmware cut-off, reverse-polarity FET |
| 50 | Battery temperature | ✓ | 10 kΩ NTC on TS (0–50 °C), plus a firmware pause at ≥ 43 °C board temperature for the cell's 45 °C limit |
| 51 | Battery polarity | ✓ | Silk at the plug; a reversed cell keeps the P-FET off (simulation S2) |
| 52 | Battery cable space | ✓\* | 25 mm lead from the cell's end to J102, the opening towards the cell. Lead, LRA and speaker cradle are specified in `mao-mechanical.md`, not modelled: first print |
| 53 | Speaker amp noise | ✓\* | MAX98357A at 9 o'clock, 30.6 mm from the antenna. Its outputs run on B over the VSYS band, its own supply. The speaker lies under the LEFT touch arc (user decision); firmware holds LEFT while the amplifier runs |
| 54 | Speaker magnet | ✓ | Speaker 47 mm from the Hall sensors; no magnetometer |
| 55 | Microphone | ✓ | Bottom port Ø0.5 through the board, gasket tube to the window, 26.8 mm from L101; MIC_CLK/DATA fully referenced, no vias |
| 56 | Analog separation | ✓ | The only ADC input, BOARD_ID, is 4.1 mm long with one via, its divider beside GPIO8; no other track within 0.6 mm |
| 57 | ADC RC filter | ✓ | BOARD_ID: 1 MΩ / 1 MΩ + 100 nF (C204) |
| 58 | PWM lines | ✓\* | SPK_P 27.8 mm and SPK_N 17.3 mm on B, over the VSYS band except at its edge (rule 18); no L3 signal under them. SPK_P runs 11.7 mm in the tail corridor under the FPC, which carries the panel's slow-edged SPI (22 Ω damped). LRA leads 2.0 / 3.4 mm. The backlight has no PWM: the AW9364 is a constant-current sink |
| 59 | LEDs | ✓ | IR: 56 Ω each, 26–59 mA peaks against the 65 mA rating (S4); low-side NMOS with gate pull-down. Backlight: AW9364 at 40 mA (33–47 mA), set by the part, never by firmware |
| 60 | Status LED placement | N/A | No status LED: the display is MAO's face |

## 5. Display, inputs and mechanics (61–84)

| # | Rule | | How |
|---|---|---|---|
| 61 | Display placement | ✓\* | Active Ø32.4 mm centred on the axis under a Ø47 window, image up at 12 o'clock (rotation a Kconfig default, checked at bring-up). J301 on B, plug-in. Bends: rule 47. The charger under the panel: rule 78 |
| 62 | Display noise vs RF | ✓ | Display lanes run from the module's inner corner to J301, 21.6 mm from the antenna boundary |
| 63 | Backlight supply | ✓ | VLED+ on VSYS (L3 band, one via) into the AW9364's constant-current sinks: no PWM, no ripple into the LED |
| 64 | Camera | N/A | No camera |
| 65 | Buttons | ✓ | Face switch: 10 kΩ pull-up, firmware debounce, no capacitor (strap), internal |
| 66 | Rotary encoder | ✓\* | Hall latches with push-pull outputs, 6° apart under the 30-pole strip (≥ 8 mT against their 3.3 mT threshold); same quadrature as the EC11. Clockwise is checked physically at bring-up |
| 67 | Interaction direction | ✓\* | Press at the centre, dial round the face, image up at 12; rotation and IMU signs checked at bring-up (the IMU sign is predicted from the layout, rule 156) |
| 68 | Mounting-hole keep-out | ✓ | Ø4.6 mm copper-free on all layers; Ø6.5 mm part-free on F for the insert boss, Ø5.5 mm on B for the screw head |
| 69 | Metal screws near RF | ✓ | Screws at ±48° on the back half, ≥ 39 mm from the antenna |
| 70 | Heat-set inserts | ✓ | M2 × 3 inserts (OD 3.2) set in a Ø6.0 printed boss before assembly; no heat reaches the board |
| 71 | Edge clearance | ✓ | GND pours stop 0.5 mm from the rim and 0.3 mm from the slot; the L3 VSYS band keeps the board's 0.3 mm copper-to-edge rule; tracks ≥ 0.75 mm. Outer copper r ≤ 28.6 mm in the Gerbers |
| 72 | Edge parts from datums | ✓ | USB-C, IR LEDs, notch, holes, slot, springs and sensors from `mechanical.py` |
| 73 | Board origin = enclosure datum | ✓ | The puck axis |
| 74 | 3D heights | ✓ | Every footprint has a body at its datasheet height: library models, datasheet boxes for 19 parts, and L101's 1.0 mm library model scaled to the DFE201612E's 1.2 mm (`models3d.py`) |
| 75 | Z-height zones | ✓ | `mech_check.py`, 0 findings: under the panel ≤ 1.2 mm (SW301 by design), under the pressed window ≤ 4.35 mm (U503 4.0), under the ring lip ≤ 0.95, B ≤ 3.2 (J101 3.2, U201 3.1) |
| 76 | Courtyards | ✓ | DRC courtyard checks: 0 |
| 77 | Rework access | ✓ | Module, regulators, USB-C, Tag-Connect and service field on the open B side; the charger on F with the face off |
| 78 | Hot parts | ✓\* | The charger (0.72 W worst case, TJ 88 °C in S13) sits on F under the panel, off the cell's top. Its exposed-pad vias still reach the B pour over the cell. Bring-up §4 looks at the panel with a thermal camera while charging a 3.0 V cell; the fallback is an ISET swap to 200 mA |
| 79 | Thermal vias | ✓ | U102, U202, U501 exposed pads and the module ground pad: 24 × 0.2 mm |
| 80 | Exposed-pad paste | ✓ | Segmented paste on the QFN pads and the module pad (library ThermalVias footprints) |
| 81 | THT thermal relief | ✓\* | Only the USB-C shell legs are plane-connected THT, solid on purpose (return path, anchoring) |
| 82 | No via-in-pad | ✓\* | No via touches an SMD pad (`via_off_pad.py`). The 24 exposed-pad thermal vias are open unless POFV is ordered (FAB-NOTES ask for it if offered) |
| 83 | Tenting | ✓ | No via flash in either mask Gerber |
| 84 | Normal via sizes | ✓ | 146 × 0.6/0.3 mm and 59 × 0.5/0.2 mm: JLC standard 4-layer |

## 6. Routing quality and silkscreen (85–105)

| # | Rule | | How |
|---|---|---|---|
| 85 | Sensible widths | ✓ | 0.2 mm signals; 0.15 mm (108.5 mm, 6 %) on the display bus and fine-pitch escapes; power 0.3–0.8 mm plus the L3 regions |
| 86 | Generous clearances | ✓ | 0.15 mm minimum against JLC's 0.09 mm |
| 87 | 45° routing | ✓ | 921 segments, 0 arcs: every segment within 0.05° of 0/45/90° (`snap45.py`, checked by script) |
| 88 | Coherent paths | ◐ | Paths are short and on 45°, and the buses run as designed lanes. The router still leaves a few duplicate overlapping GND segments, a handful of 45° hairpins on slow nets (EXP_INT_N, HAPTIC_EN, PRESS_N, VBAT), and touch leads that overlap their electrode by only 0.04–0.05 mm. None is an electrical fault. A1: a smoothing pass for hairpins and duplicates |
| 89 | Aligned passives | ✓ | Decoupling rows; the R111–R114 row; the service field on a 2.8 mm grid |
| 90 | Few rotations | ✓\* | Off-axis parts follow the geometry: D501/D502 (±159°) fire out of the wall, U402 (28°) looks out of the window, and U301/U302 (−120°/−126°) sit square to the ring's radius under the pole strip |
| 91 | Readable references | ✓ | Every text reads upright from its own face (B mirrored; verticals bottom to top) |
| 92 | Polarity marking | ✓ | Diode bars, pin-1 marks, battery silk |
| 93 | Pin 1, two cues | ✓ | Fab chamfer plus silk marker on every IC |
| 94 | Silk never on pads | ◐ | No silk text on a pad, via, hole, the slot or electrode copper (`check_silk_text.py`: 83 texts, 0 findings), and no silk on exposed copper in the Gerbers. Two cosmetic overlaps remain. Some library footprint outlines cross tented vias (27 places, e.g. pin-1 marks). The S/N box encloses 10 tented module thermal-via holes. A1: trim library outlines round vias; move the S/N field |
| 95 | Readable silk | ✓ | All texts ≥ 0.8 mm with ≥ 0.15 mm stroke; every outline ≥ 0.15 mm; egg art ≥ 0.16 mm |
| 96 | Maker's mark | ✓\* | 4.6 × 3.75 mm on both faces, finest strokes ≥ 0.15 mm except 0.24 % of tips. On F it sits under the panel and on B over the cell, on a reserved via-free spot. Both are seen on the bare board and at service, not in the closed puck, which shows the face |
| 97 | Product identification | ✓ | F: mark, MAO 1.5 mm, MAIN A0, 2026-10. B: mark, MAO A0 1.3 mm, 2026-10 |
| 98 | Visible revision | ✓ | A0 on both faces, plus the board-ID divider |
| 99 | PCB vs product version | ✓ | "MAIN A0" names the board on F |
| 100 | Date code | ✓ | 2026-10 on both faces |
| 101 | Serial area | ◐ | A 6 × 6 mm S/N field with JLC's order-number placeholder on F. It sits under the panel and over 10 tented module thermal-via holes, so a sticker is better than a laser mark there. A1: a field outside the panel on B |
| 102 | Debug identification | ✓ | RST, BOOT, XRST, SDA, SCL, 3V3, SYS, BAT, VBUS, GND, LCDV, MIC, IRV, TAG, BAT LINK |
| 103 | No mystery pads | ✓\* | Every pad named. The 11 field names follow one convention, each upright on the same side of its pad. SCL is the one exception, where the I2C pull-ups take its grid spot. Read by that convention, SYS (between TP4 and TP5) and GND (beside J102) are unambiguous |
| 104 | NC pins marked | ✓ | 24 `unconnected-(…)` nets with schematic NC flags |
| 105 | Meaningful net names | ✓ | 108 nets, no Net-(…) |

## 7. Power behaviour, ESD and EMC (106–131)

| # | Rule | | How |
|---|---|---|---|
| 106 | One ground | ✓ | A single GND |
| 107 | No blind star ground | ✓ | Solid L2 |
| 108 | No sprinkled ferrites | ✓ | None |
| 109 | Intentional 0 Ω | ✓ | R108 BAT LINK (current measurement); R105 (charger TD, BQ24074 option) |
| 110 | Current measurement link | ✓ | R108 0 Ω 1206 in the cell path, labelled BAT LINK, 0.85 mm from J102 |
| 111 | No permanent power LED | ✓ | None |
| 112 | Quiescent current | ✓ | `mao-power-budget.md`: deep sleep ~85 µA incl. the charger's EN1 pull-down; TPS22919 2 nA off, AW9364 0.1 µA |
| 113 | No back-powering | ✓\* | IR receiver pull-up on its switched supply; display bus parked before its rail drops. The Tag-Connect adapter is plugged only after the board is powered (bring-up §2) |
| 114 | Power sequencing | ✓ | TPS22919 fixed slew (t_ON 1.7 ms, rise 1.0 ms); panel reset held low until the rail is up |
| 115 | Enable pins defined | ✓ | BB_EN divider + C111; R116 on the rail switch; R117 on the charger's /CE; the AW9364's 150 k; R208 at the amplifier's SD |
| 116 | Fail-safe actuators | ✓ | Amplifier shut down, IR gate pulled down, backlight EN low, haptic EN pulled down |
| 117 | Reset-state GPIO | ✓ | IR_TX on GPIO38 (no reset pull) with a 100 k pull-down; GPIO39's reset pull-up drives the expander reset, which wants one |
| 118 | ESD where accessible | ✓ | USB: TPD2E2U06 + SMF15A. D301–D304 at the four touch electrodes and springs |
| 119 | ESD at the entry | ✓ | U101 2.1 mm from the USB data pads; each D30x beside its electrode |
| 120 | Short TVS ground | ✓ | D101's ground pad 0.20 mm from a via; U101 0.10 mm |
| 121 | EMC at the source | ✓ | Solid L2; short clocks; fast lanes 100 % referenced; class-D outputs over their own supply plane; no switch node near a connector |
| 122 | Crosstalk | ✓\* | L3/L4 broadside 13.8 mm, all slow lines (rule 124). I2S and SPK_P run under the FPC in the tail corridor, where the tail carries the panel's 22 Ω-damped SPI. TOUCH_REAR_E runs on F 0.30 mm from the slot; touch is sampled, filtered and calibrated at bring-up |
| 123 | 3W where useful | ✓\* | The USB pair keeps its own gap at full speed; the display bus is damped; elsewhere there is no fast parallel run |
| 124 | Orthogonal adjacent layers | ✓\* | L3 and L4 overlap broadside for 13.8 mm in all (53.7 mm in audit 2). Every pair is slow and static: USB_PRESENT_N/VSYS 3.0, MCU_EN/MIC_VDD 1.9, USB_PRESENT_N/BB_EN 1.7 mm and smaller. No L3 track lies under a display, I2S, PDM or speaker line |
| 125 | Edge EMI | ✓ | Fast lanes inside r 17 mm; the rim carries the touch arcs (by design) and slow lines |
| 126 | No odd copper islands | ✓ | Every pour fragment is tied to L2 (`gnd_stitch.py`); island removal on |
| 127 | Pour islands tied | ✓ | 0 floating fragments in the refilled board |
| 128 | Purposeful via fence | ✓\* | A ring at r 27.6 mm (`gnd_fence.py`) wherever the rim is not a touch arc, a tab spot or the antenna; coverage vias where a pour point is far from the plane |
| 129 | Ground under the MCU | ✓ | L2 solid under the module; 12 vias in its ground pad |
| 130 | No copper under the antenna | ✓ | Notch, plus the all-layer keep-out |
| 131 | Conductors beside the antenna | ✓\* | No daughterboard; the panel above it and the LRA (rule 3) are covered by the RSSI checks |

## 8. Assembly, enclosure and production (132–171)

| # | Rule | | How |
|---|---|---|---|
| 132 | Multiple PCBs | N/A | One board |
| 133 | Daughterboard alignment | ✓ | The panel is located by its printed carrier, not by its connector |
| 134 | Board-to-board mating height | N/A | None |
| 135 | Tolerances | ✓ | Module corners at r 28.89 (Fab outline 28.94): 1.06–1.11 mm to the wall. U503 ≥ 0.35 mm under the pressed window at worst tolerance; the speaker 1.1 mm from the wall |
| 136 | Enclosure clearance | ✓ | Board r 29.0 in a wall at r 30.0. `mech_check.py` keeps every body inside r 29.0 except the parts that sit in wall openings by design (USB-C, IR LEDs); largest r 28.96 (LS501's contact pads) |
| 137 | Screw tolerance | ✓ | Ø2.2 mm for M2 |
| 138 | Button tolerance | ✓ | The flexure-hung face presses a Ø2 boss on the SKQG stem; nothing slides |
| 139 | USB opening tolerance | ✓\* | 9.2 × 3.6 mm opening with a 0.5 mm chamfer; the receptacle face 0.6 mm past the board edge. Full plug mating is checked on the first print |
| 140 | Nozzle access | ✓ | No walls of tall parts |
| 141 | Fiducials | ✓ | Three per side, 1.0 mm copper in a 2.0 mm mask opening, copper-free to 1.1 mm |
| 142 | Panelization | ✓ | Tabs only at 130° and 328° (`mechanical.TAB_ANGLES`): copper on every layer ≥ 1.55 mm from the edge over 4 mm of rim (keep-outs on all layers, checked on the real fills). 328° clears the VSYS feed beside H1, which 320° necked (electrical audit 3) |
| 143 | No fragile parts at tabs | ✓ | Nearest part bodies 1.35 mm (130°) and 1.45 mm (328°); no MLCC within 4 mm |
| 144 | Parts away from routed edges | ✓ | MLCC pads ≥ 1.5 mm from the rim and the notch (nearest C202, 1.54 mm); vias ≥ 1.0 mm |
| 145 | MLCC flex cracking | ✓ | Bulk 0603s inboard; none at the holes or USB-C |
| 146 | DC-bias derating | ✓\* | 2 × 22 µF 6.3 V at 3.18 V keep enough effective capacitance; C105 10 µF at 4.4 V (~5 µF) is backed by C102 1.5 mm away (A1: 0805) |
| 147 | Voltage margin | ✓ | 25 V parts on VBUS, 10 V on VSYS, 6.3 V on 3.18 V |
| 148 | Resistor power | ✓ | IR 56 Ω 0603 at 0.03 W average; R115 QOD transient; dividers µW |
| 149 | Inductor saturation | ✓ | DFE201612E Isat 5.5 A against the 1.27 A peak (S14) |
| 150 | Diode ratings | ✓ | SMF15A 15 V / 200 W; ESD within ratings |
| 151 | Connector current | ✓ | VBUS ≤ 0.5 A (USB500); JST SH 1 A against the 0.9 A system peak |
| 152 | Trace current | ✓ | Battery path 0.6–0.8 mm; VSYS through two 0.6/0.3 mm vias and an L3 band with no neck under 0.5 mm; ≤ 12 mV drop to any VSYS load (electrical audit 3) |
| 153 | Thermal spreading | ✓\* | Charger EP vias into L2 and both pours (rule 78) |
| 154 | Temperature sensor placement | ✓\* | The charge pause reads the IMU's die temperature 19.8 mm from the charger, a proxy for neither the cell nor the charger; its offset is characterised at bring-up and the chip's own 0–50 °C NTC window stays in force |
| 155 | Sensor placement | ✓ | ToF, light and IR receiver in the window band; mic port with gasket; IMU orientation known |
| 156 | IMU axis marking | ✓ | +X / +Y glyph beside U401 |
| 157 | Jig interface | ✓ | Tag-Connect + service field |
| 158 | Pogo pattern not reversible | ✓ | One grid spot holds the I2C pull-ups instead of a pad; pads outside the field; three datum holes |
| 159 | Fixture datum | ✓ | 2 × M2 holes + Ø2.0 peg hole + outline |
| 160 | Factory test | ✓ | `mao-factory-test.md` and the firmware self-test (33 steps) |
| 161 | First power-up | ✓ | `mao-bringup.md`: staged, current-limited, orientation check before the first panel plug-in |
| 162 | Cut/jumper points | ✓ | R108 BAT LINK, R105, DNP R110, 22 Ω terminations |
| 163 | Solder jumpers marked | N/A | None |
| 164 | DNP tuning footprints | ✓ | R110 (NTC substitute) |
| 165 | BOM availability | ✓ | 48 lines, every one with an LCSC number (`fab.py` refuses a line without) |
| 166 | Second source | ✓ | Generic passives, FETs and TVS; the AW9364 is single-maker but widely stocked |
| 167 | Connector standardization | ✓ | JST SH, BW0019BG springs, Tag-Connect |
| 168 | Fastener standardization | ✓ | M2 only |
| 169 | Board thickness | ✓ | 1.6 mm |
| 170 | Copper weight | ✓ | 1 oz outer, 0.5 oz inner |
| 171 | Surface finish | ✓ | ENIG |

## 9. Visual, documentation and review (172–200)

| # | Rule | | How |
|---|---|---|---|
| 172 | Standard colour | ✓ | Matte black mask, white legend, ENIG |
| 173 | Black mask inspection | ✓\* | FAB-NOTES accept green for the EVT if black costs schedule |
| 174 | No fake tech graphics | ✓ | Maker's mark, identity, function names, IMU axes, small hidden eggs |
| 175 | Hidden details | ✓ | Five small eggs, all hidden once assembled and clear of every reference spot: a sleeping cat with paw prints and "boop" at the face switch and "MADE FOR BAD IDEAS" under the panel, "9 lives" under the cell, "meow" under the speaker |
| 176 | Visual hierarchy | ✓ | F: MAO 1.5 > TOP 1.2 > MAIN A0 1.0 > references 0.8 mm. B: MAO A0 1.3 > connector names 1.2 > references and names 0.8 mm |
| 177 | References on prototypes | ✓\* | The user's policy, applied by `mao_labels.py`. Silk carries the references of ICs, connectors, transistors, diodes, the switch, mic, inductor and electrodes, plus every R/C a procedure names: 45 on silk. 8 of them have a leader that ends 0.3 mm off its own part and nearer it than any other. 10 find no clear spot (C111, C407, D304, J303, R108, R205, R213, R214, U104, U303; J303 and R108 carry REAR and BAT LINK). They are on the assembly drawing (A3, 4.5:1, every reference legible) and listed in FAB-NOTES |
| 178 | Manufacturing notes | ✓ | FAB-NOTES: stackup, thickness, copper, finish, mask/silk, impedance (none, with reason), via counts, POFV, notch, slot, tabs, order-number spot, polarised parts |
| 179 | Impedance | N/A | No high-speed USB, RF feed or fast memory on the board |
| 180 | Inside fab capability | ✓ | 0.15/0.15 mm, 0.2 mm drill |
| 181 | Inside assembly capability | ✓ | 0402, 0.5 mm QFN/LGA, no BGA, no microvias |
| 182 | 0603 preferred | ✓\* | 13 × 0603, 1 × 1206; 66 × 0402, which a two-sided Ø58 mm board needs |
| 183 | Replaceable critical parts | ✓ | Regulators, ESD and connectors reachable |
| 184 | Likely failures serviceable | ✓ | THT USB legs, plug-in display and battery, spring contacts |
| 185 | No undocumented hacks | ✓ | The board is generated from `design/`; every change is in its scripts and these docs |
| 186 | Schematic sections | ✓ | Power, Compute, Interface, Sense, Feedback |
| 187 | Signal flow | ◐ | Blocks follow source → protection → regulation → loads, one frame each, but most local nets are joined by labels rather than drawn wires (22 wires, 192 global labels), so the flow is read, not seen. A1: draw the local passives' wires in `gen_sch.py` |
| 188 | Readable schematic | ◐ | One frame per block, notes inside their frames on a clean 2.54 mm line pitch, no overprinted text; label-connected as in rule 187 |
| 189 | Notes on the schematic | ✓ | Strap, UVLO, reset, default-state and tail notes on each block; GPIO numbers generated from `pinmap.py` |
| 190 | Datasheet references | ✓ | `footprints.py` docstrings and the review's §1 cite each datasheet |
| 191 | Manual net-class review | ✓ | `mao-rev-a0-review.md` §2 (USB, display, I2S, PDM, I2C, power, touch, slow), against this board |
| 192 | Connector trace review | ✓ | J301 (18 pads against the Winstar definition, pin k → pad 19 − k), USB-C, J102/Q101 and Tag-Connect traced in the third electrical audit |
| 193 | 1:1 footprint check | ◐ | The custom footprints (J301, BW0019BG, LS501, IR12-21C) are transcribed from the makers' drawings with dimensions cited in `footprints.py`, but no 1:1 print against a real part was made: JLC places the parts, so none is in hand. First-article microscope check (bring-up §0); A1: 1:1 print with the parts |
| 194 | 3D review | ✓\* | Top, bottom, two isos and four edges rendered from the final board; every part bodied. No exploded or transparent enclosure view (no enclosure CAD yet) |
| 195 | Gerber review | ✓ | All final Gerber layers rendered outside KiCad (`plots/mao-main-a0-gerbers.png`). The drill file reconciles with the board: 150 × 0.3 mm (146 vias + 4 electrode joins), 83 × 0.2 mm (59 vias + 24 exposed-pad vias), 4 slots, 9 NPTH |
| 196 | BOM review | ✓ | 48 lines / 112 parts: MPN, value, rating, package, LCSC |
| 197 | Assembly orientation review | ✓ | FAB-NOTES list every polarised part for JLC's placement preview |
| 198 | Enclosure review | ✓\* | Heights, keep-outs, wall, tail path, speaker and tabs checked by script against `mechanical.py`; the interface drawing is generated from it. No enclosure CAD: the first print is the fit check (final report, risks) |
| 199 | One pin map | ✓ | `pinmap.py` generates the schematic nets, the firmware header and the pin-map doc; all 36 module pins agree with the board |
| 200 | Golden rule | ✓\* | Boot: straps defined (rule 32). Flash: USB, plus the Tag-Connect UART. Recover: ROM loader by face or TP8 during a reset. Measure: 15 named pads and BAT LINK. Fit: on paper, every part bodied and checked; the first print confirms it |

## RF picture and release gate

- **RF picture:** matches the standard's sketch, turned so that 6 o'clock is the "edge". Antenna on one edge; power,
  charger and USB on the opposite edge; digital in the middle; audio amplifier and display connector at 9 o'clock
  beside the digital centre.
- **Release gate: EVT** (engineering prototype). The board carries the EVT extras the standard allows: full test-pad
  field, BAT LINK, DNP options, 22 Ω termination.
- Already at DVT level: final antenna placement, production connectors, ESD and power architecture.
