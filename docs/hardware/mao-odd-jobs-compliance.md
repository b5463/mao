# MAO_MAIN A0: ODD JOBS standard, rule by rule

Every rule of `hardware/mao/ODD-JOBS-STANDARD.txt` (V1.0, rules 1–200 plus the RF picture and the release
gates), checked against the released 4-layer board `hardware/mao/MAO_MAIN_A0.kicad_pcb` on 2026-10-05.
Numbers come from the board and the checker outputs in `hardware/mao/outputs/`, not from intent.

- **✓**: met.
- **✓\***: met, with a recorded reason or a remaining bring-up check.
- **N/A**: does not apply to this board (reason given).

No rule is open.

## 1. Baseline and RF (1–9)

| # | Rule | | How MAO_MAIN A0 meets it |
|---|---|---|---|
| 1 | Absolute baseline | ✓\* | ERC 0 and DRC 0 at all severities, warnings included; 0 unconnected, 0 parity. Every suppressed check was run once in a scratch copy: 6 footprint-filter notes (generic symbols on verified or custom footprints: J102, J501, D501, D502, U402, U502) and 18 library-copy notes (test-pad references moved to Fab by the designator policy; module, USB-C and speaker-pad silk trimmed at the edge, ground pads set solid). Every fitted part has an MPN and an LCSC number. Custom footprints are transcribed from the drawings named in `footprints.py`. Connector pinouts were traced by hand (rule 192). 3D: every footprint has a body at its datasheet height. Enclosure: checked against the enclosure specification (`mechanical.py`, `mao-mechanical.md`), which found and fixed the IR-receiver clearance (rule 135), a speaker that did not fit beside the cell (now a 15 × 8 mm part on its own pads) and metal ring magnets over the antenna (now a ferrite pole strip); the printed enclosure is drawn from that specification. Gerbers were reviewed outside KiCad (rule 195). Power-up, programming, recovery, test points, revision mark and orientation cues: rules 31–36, 98 and 93 |
| 2 | Antenna at the edge, Espressif placement | ✓\* | WROOM-1 on B at 6 o'clock. The antenna sits over a 24 × 6.3 mm notch, beyond the ground plane. The power section and USB-C are 34–49 mm away at 12–2 o'clock. The cell is under the board, 12.7 mm from the antenna boundary, and not in its radiating direction. The speaker is at 9 o'clock, ≥ 18 mm away. The ring's 30 poles are a non-conductive ferrite strip, so nothing metal sweeps over the antenna as the ring turns. Nearest large conductor: the panel edge, 4.9 mm in plane and 4.4 mm above. A Ø58 mm puck allows no more; antenna RSSI is checked against the LCDkit at bring-up (final-report risk 3) |
| 3 | Real keep-out zone | ✓ | Rule area "ANTENNA KEEP-OUT", 30.0 × 7.9 mm on all four copper layers: no tracks, vias or pours. There is no board under the antenna (notch), so nothing can be placed there. The enclosure keep-out is specified ("no metal within 15 mm" on the interface drawing). Both M2 screws and inserts are on the back half; the 225° locating peg is plastic. The ring dial, which passes over the antenna, carries a ferrite (rubber-bonded, non-conductive) pole strip, not metal magnets |
| 4 | Consistent RF orientation | ✓ | Antenna on the front edge (6 o'clock, towards the person), as in the standard's picture. The USB cable leaves at 12 o'clock, so it never lies over the antenna. Held in a hand, the puck is gripped at the ring, above the board edge |
| 5 | RF ground stitching | ✓ | Every GND pad has a plane via (shared within 1.6 mm), so stitching runs every few mm across the board, along the module pad rows and around USB-C. There are no vias in the antenna keep-out and no decorative fence |
| 6 | RF vs noisy circuits | ✓ | Layout follows the standard's picture: antenna (6) → module → digital centre → power, charger and USB (12–2 o'clock). The buck-boost is 34 mm from the antenna area, the amplifier 20 mm (9 o'clock), the haptic driver 12 mm with the module body between. Display SPI and I2S leave the module's inner corner, away from the antenna |
| 7 | Switching regulator per reference layout | ✓ | TPS63802 per TI SLVSEU9D: CIN 0.25 mm from VIN, inductor over the switch pins, COUT at VOUT, PGND strip with a via under the IC (`route_power.py`) |
| 8 | Switch node | ✓ | BB_L1 and BB_L2 are 1.3 mm each, on B, with no vias and no test pad. They are not under the MCU or antenna. L3 under the converter is a router core (no signals); L2 GND sits between |
| 9 | Inductor placement | ✓ | L101 is 36 mm from the antenna, 27 mm from the mic, 21 mm from the IMU, 33 mm from the Hall sensors. There is no magnetometer, and the crystal is inside the module |

## 2. Power and grounding (10–20)

| # | Rule | | How |
|---|---|---|---|
| 10 | Power entry is hostile | ✓ | USB-C: SMF15A TVS (clamps below the BQ24073's 28 V rating, which has its own input OVP), TPD2E2U06 on D±, charger input current limit (ILIM 1.07 A), 1 µF at IN. Battery: reverse-polarity P-FET, cell PCM required, NTC window. There is no polyfuse: the host limits VBUS and the charger limits its draw |
| 11 | Document every rail | ✓ | VBUS, VSYS, VBAT, BAT_RAW / BAT_IN (cell link), +3V3, 3V3_LCD, MIC_VDD, IR_RX_VCC, HAP_REG: named in the schematic and listed with currents in `mao-power-budget.md` |
| 12 | High current on wide copper | ✓ | Battery path 0.6–0.8 mm (J102 → R108 → Q101 → charger BAT). VSYS: 0.3–0.8 mm on B plus the L3 VSYS band to the amplifier, haptics and IR. VBUS 0.5 mm plus an L3 strip. +3V3 is the L3 plane. The backlight runs on a 0.3 mm 3V3_LCD trace (40 mA) |
| 13 | Current headroom | ✓ | TPS63802 is rated 2 A; worst-case +3V3 load (Wi-Fi TX peak, display, sensors) is under 0.7 A, about 35 %. The charger is set to 297 mA of its 1.5 A, the TPS22917 is 1 A for 40 mA, and R108 is a 1206 (2 A class) |
| 14 | Local 100 nF at every IC | ✓ | Every IC supply pin has its datasheet decoupling beside it (`mao-rev-a0-review.md` §1); placement pins each capacitor next to its pin |
| 15 | Bulk at transient loads | ✓ | Module 22 µF + 100 nF; buck output 2 × 22 µF; amplifier 10 µF + 100 nF; display 4.7 µF; IR LEDs 10 µF reservoir; charger 10 µF IN/OUT/BAT |
| 16 | Short capacitor ground path | ✓ | Each decoupling GND pad has its own plane via or shares one within 1.6 mm; there are no long ground traces |
| 17 | 4-layer stack | ✓ | L1 parts and signals / **L2 solid GND** / L3 power regions + slow signals / L4 parts and signals. JLC04161H-1080: 0.076 mm prepreg puts L1 directly on L2 |
| 18 | Never cross a split | ✓ | L2 is one piece (2098 mm²); its only cuts are under the rim touch arcs, where only the electrodes run. On L3, no slow line crosses under the B-side display, I2S or PDM lanes (each lane has a +3V3 core with no L3 signal; checked by script). L3 regions: +3V3 1504 mm², VSYS 361 mm², VBUS 33 mm², one piece each |
| 19 | Return paths | ✓ | Every fast line is on an outer layer directly over L2 (L1) or over an unbroken L3 core (L4); clocks are short (rule 21) |
| 20 | GND via at fast layer changes | ✓\* | USB D± change layers twice: at the receptacle row join and at the module pins. The nearest GND via or shell leg is 2.2–4.3 mm away. Display lines change once at J301, with GND vias 2.0–2.9 mm away. At USB full speed (≥ 4 ns edges) and 40 MHz SPI (22 Ω damped), those distances are electrically small |

## 3. Signals (21–39)

| # | Rule | | How |
|---|---|---|---|
| 21 | Clock lines | ✓ | LCD_SCLK 12.7 mm (1 via), AMP_BCLK 8.2 mm (no via), MIC_CLK 14.5 mm (no via). All run from the module's inner corner, away from the antenna and the board edge |
| 22 | Crystals | N/A | The crystal is inside the WROOM-1 module |
| 23 | USB D+/D− as a pair | ✓\* | Pair on F over L2 for 29.5 mm, 0.2 mm tracks with 0.6 mm gap, through-path lengths within about 3 mm (31.7 / 35.5 mm including the receptacle's row-join stubs). Vias: the unavoidable USB-C row join plus one at the module. Impedance is not controlled: estimated ≈ 74 Ω differential on this stackup. The ESP32-S3 is USB full speed only; the 0.2 ns flight time is under 1/20 of the 4 ns minimum edge, so the line is electrically short |
| 24 | USB connector placement | ✓ | Four THT shell legs carry the mechanical load. The mating face is 0.6 mm past the edge (`USB_FRONT_Y` datum) and aligned with a 9.2 × 3.6 mm wall opening. The shell is on GND; ESD U101 is 3.9 mm from the receptacle pins, TVS D101 at VBUS entry |
| 25 | USB-C CC | ✓ | Sink: 5.1 kΩ Rd on CC1 and CC2 |
| 26 | USB-C shield | ✓ | Plastic puck: shell legs solid to GND (deliberate return path); no chassis |
| 27 | Fast SPI close to the MCU | ✓ | Display lines 10–16 mm from module pins to J301, on B over a +3V3 core with no L3 crossings |
| 28 | SD card | N/A | No SD card |
| 29 | Series termination option | ✓ | 22 Ω on LCD_SCLK and LCD_MOSI at the module (R301, R302) |
| 30 | I2C pull-ups sized | ✓ | 2.2 kΩ at 3.3 V, 400 kHz, 6 devices (≈ 100 pF): about 190 ns rise, under the 300 ns limit |
| 31 | Unused GPIO defined | ✓ | Only GPIO46 is unused: NC, with its internal pull-down (download-boot strap). Every expander pin has an external default resistor |
| 32 | Boot straps | ✓ | GPIO0: face switch, 10 kΩ pull-up, no capacitor. GPIO45 (VDD_SPI strap): backlight PWM held low by the 100 kΩ gate pull-down, so the 3.3 V flash boots. GPIO3 (JTAG-source strap): PGOOD input, inert unless the eFuse is burnt. GPIO46: NC with pull-down |
| 33 | Reset access | ✓ | EN on TP7 (RST) in the service field, on Tag-Connect pin 2, and through the USB-Serial/JTAG auto-reset. The expander has its own reset (TP11 XRST, GPIO38) |
| 34 | Boot mode access | ✓ | Hold the face (GPIO0) while plugging USB; TP8 BOOT; Tag-Connect pin 6 |
| 35 | Programming access | ✓ | Native USB-Serial/JTAG, plus Tag-Connect TC2030-NL with GND, EN, TXD0, 3V3, RXD0 and GPIO0 (documented in `mao-bringup.md` and `mao-factory-test.md`) |
| 36 | Test pads | ✓ | Rails VBUS, SYS, BAT, 3V3; grounds ×3; RST, BOOT, XRST, SDA, SCL; switched rails LCD, MIC, IRV. TX and RX are on the Tag-Connect |
| 37 | Pad size | ✓ | 1.2 mm for rails and grounds, 1.0 mm for signals |
| 38 | Ground pads | ✓ | Three GND pads (TP1, TP2, TP16) in the service field, plus Tag-Connect GND |
| 39 | Signal + GND pairs for scope | ✓ | The service field is a 2.8 × 3.0 mm grid with a ground beside every rail pad, so a spring-tip probe fits |

## 4. Connectors, battery and audio (40–60)

| # | Rule | | How |
|---|---|---|---|
| 40 | External connectors | ✓ | The only user-accessible connector is USB-C (ESD, TVS, current limit, keyed, THT legs for strain). The battery connector is internal: keyed JST SH plus the reverse-polarity FET |
| 41 | Standard connector orientation | ✓\* | No earlier ODD JOBS 3-pin battery-with-NTC connector exists. The one fixed here (1 BAT−, 2 NTC, 3 BAT+, silk "− T +") is the convention for later boards (`mao-mechanical.md` §7) |
| 42 | Polarity on silk | ✓ | "− T +" with BAT at J102; "+" at the speaker's SPK+ pad and at the LRA pads |
| 43 | Functional labels | ✓ | USB, BAT, LCD, SPK (with the speaker's outline), LRA, SERVICE, TOP, REAR, MIC, BAT LINK, VBUS, alongside the references |
| 44 | Nothing under locking areas | ✓ | Checked by script: under the USB-C shell, only its own ground fan-out. Under the JST and the switch, one tented via each. J301's retention lands are clear |
| 45 | FPC checks | ✓ | J301 HDGC 0.5K-HX-18PWB: back-flip lock, contacts top and bottom (tail works either way up). Pin 1 has a silk dot and a Fab "1". FPC entry is from the rim side at 9 o'clock. The 1.0 mm connector sits inside the 2.7 mm standoff |
| 46 | FPC entry modelled | ✓\* | The tail outline is in the footprint (Fab) and the stack in `mao-mechanical.md` §1/§4. The tail stays above the board (the cell is below). Required tail length is 10.5–12.5 mm from the glass edge: check against the panel drawing before buying panels |
| 47 | Bend radius | ✓ | The 180° fold spans the 2.7 mm standoff (≈ 1.4 mm inner radius, about 4.5× the 0.3 mm tail), with a 2 mm service loop for the press travel |
| 48 | Battery location | ✓\* | Under the board on a 0.3 mm insulator with 0.3 mm swell clearance, 12.7 mm from the antenna, clear of the speaker crescent. The charger above it dissipates up to 0.85 W at a low cell; heat spreads into the planes (rule 78) |
| 49 | LiPo protection | ✓ | Cell with PCM (mandatory, checked on receipt), hardware UVLO (2.96 V), NTC window, firmware critical shutdown, reverse-polarity FET |
| 50 | Battery temperature | ✓ | 10 kΩ NTC on the charger's TS input: charging only between 0 and 50 °C |
| 51 | Battery polarity | ✓ | Silk "− T +" and BAT at the plug. Reversed cell: the P-FET stays off, nothing is damaged |
| 52 | Battery cable space | ✓ | 25 mm lead from the cell's 3 o'clock end to J102 at 2 o'clock; the opening faces 6 o'clock so the plug lies flat over the cell end |
| 53 | Speaker amp noise | ✓\* | MAX98357A at 9 o'clock, 20 mm from the antenna and IMU and 38 mm from the mic; SPK± are 9.2 / 11.9 mm on B, no vias, straight to the speaker's pads. The speaker lies partly over the LEFT touch electrode, so firmware holds that zone while the amplifier runs |
| 54 | Speaker magnet | ✓ | Speaker at 9 o'clock is 47 mm from the Hall sensors at 4 o'clock; the LRA (7–8 o'clock) is about 36 mm away. No magnetometer; the ring's ferrite poles are intentional |
| 55 | Microphone | ✓ | Bottom port through the board; a gasket tube seals the path to a Ø0.8 window hole. 27 mm from the inductor and 38 mm from the amplifier; MIC_CLK / DATA are its own short B-side pair |
| 56 | Analog separation | ✓ | The only ADC input is the static BOARD_ID divider (100 nF), read once at boot. Touch leads run at the rim (mostly on F), away from SPI, DC/DC and PWM |
| 57 | ADC RC filter | ✓ | BOARD_ID: 1 MΩ / 1 MΩ + 100 nF |
| 58 | PWM lines | ✓ | Backlight PWM gate is 26 mm from the antenna, with a short LED loop through R303 and Q301. LRA leads are 2.0 / 3.4 mm, speaker outputs 9.2 / 11.9 mm |
| 59 | LEDs | ✓ | IR LEDs: 47 Ω each, low-side NMOS with gate pull-down (0 standby). Backlight: 10 Ω, about 30 mA. IR is invisible, so there is no light bleed |
| 60 | Status LED placement | N/A | No status LED: the display is MAO's face. The IR LEDs fire through wall windows (`mao-mechanical.md` §5) |

## 5. Display, inputs and mechanics (61–84)

| # | Rule | | How |
|---|---|---|---|
| 61 | Display placement | ✓ | Active Ø32.4 and outline Ø35.6 mm, centred on the puck axis under a Ø47 window. Orientation is a firmware constant (VERIFY at bring-up). Connector at 9 o'clock, reachable with the face lifted. Bend: rule 47. Backlight ≈ 0.1 W |
| 62 | Display noise vs RF | ✓ | Display lanes run from the module's inner corner up to J301, away from the 6 o'clock antenna; the backlight switch is 26 mm from it |
| 63 | Backlight supply | ✓ | Switched 3V3_LCD rail (TPS22917), 0.3 mm traces for 40 mA |
| 64 | Camera | N/A | No camera |
| 65 | Buttons | ✓ | Face switch: 10 kΩ pull-up, firmware debounce (`mao_input`), no capacitor (strap). The switch is internal, under the window |
| 66 | Rotary encoder | ✓\* | Hall latches have push-pull outputs, so no pull-ups; built-in hysteresis makes RC filters unnecessary. A/B run 12.8 / 13.9 mm on F. Quadrature sequence is the same as the EC11. Clockwise = clockwise must be checked physically at bring-up (`mao-bringup.md`). The poles are a ferrite strip (≥ 8 mT at the sensors vs their ±3.3 mT threshold); detents are LRA ticks |
| 67 | Interaction direction | ✓\* | Press at the centre, dial around the face, display "up" at 12 o'clock. Rotation and IMU axis sign are firmware constants checked at bring-up; the IMU sign is already predicted from the layout (rule 156) |
| 68 | Mounting-hole keep-out | ✓ | M2 holes Ø2.2 with a Ø4.6 mm rule area on all layers, which covers the screw head and insert boss |
| 69 | Metal screws near RF | ✓ | Screws at ±48° on the back half, ≥ 39 mm from the antenna |
| 70 | Heat-set inserts | ✓ | Inserts are set in the top shell before the board goes in; the 4.6 mm keep-out ring on both faces clears the boss |
| 71 | Edge clearance | ✓ | 0.3 mm copper-to-edge rule; pours inset 0.3 mm. Gerbers: outer copper reaches r ≤ 28.6 mm on the 29.0 mm board |
| 72 | Edge parts from datums | ✓ | USB-C, IR LEDs, notch, holes, springs and sensors are all placed from `mechanical.py` constants |
| 73 | Board origin = enclosure datum | ✓ | Origin is the puck axis (display centre) |
| 74 | 3D heights | ✓ | Every footprint has a body: library models where installed, plus datasheet-height bodies for 19 parts including the speaker (`models3d.py`, `footprints.py`) |
| 75 | Z-height zones | ✓ | Zone A (F, under the panel) ≤ 1.2 mm. Sensor band under the window ≤ 4.3 mm (IR receiver, worst case). Under the ring lip ≤ 0.95 mm. Zone B (B, over the cell) ≤ 3.2 mm |
| 76 | Courtyards | ✓ | DRC courtyard checks: 0 |
| 77 | Rework access | ✓ | Module, regulators, USB-C, Tag-Connect, service field and boot pull-up are all on the open B side; nothing is buried under the module except tented vias and short masked escapes at its pad rows |
| 78 | Hot parts | ✓\* | Charger (≤ 0.85 W worst case) has EP thermal vias into L2 and is separated from the cell by insulator and an air gap. Fallback is an ISET resistor swap to 200 mA (final-report risk 8) |
| 79 | Thermal vias | ✓ | U102, U202, U501 exposed pads and the module ground pad: 24 × 0.2 mm |
| 80 | Exposed-pad paste | ✓ | Segmented paste on QFN exposed pads (4 windows) and the module pad (3 × 3). TPS63802 pad at −8 % and OPT3004 pad at −15 %, per TI's stencils |
| 81 | THT thermal relief | ✓\* | The only plane-connected THT pads are the USB-C shell legs, made solid on purpose (shield return, anchoring). JLC fits them (THT assembly); rework with a large tip and preheat |
| 82 | No via-in-pad | ✓ | No via touches an SMD pad (`via_off_pad.py` guarantee). EP thermal vias only; FAB-NOTES ask for filled and capped (POFV) if offered |
| 83 | Tenting | ✓ | All vias tented both sides (FAB-NOTES) |
| 84 | Normal via sizes | ✓ | 0.3/0.6 mm (137) and 0.2/0.5 mm (57): JLC standard 4-layer, no extra-cost process |

## 6. Routing quality and silkscreen (85–105)

| # | Rule | | How |
|---|---|---|---|
| 85 | Sensible widths | ✓ | Signals 0.2 mm. 0.15 mm only at fine-pitch escapes (module rows, QFN, LGA, 0.5 mm FPC): 114 mm, 7 % of copper. Power 0.3–0.8 mm plus L3 planes |
| 86 | Generous clearances | ✓ | 0.15 mm minimum (JLC 4-layer capability is 0.09 mm); most spacing is 0.2 mm or more |
| 87 | 45° routing | ✓ | All 820 track segments are 0/45/90°, checked by script; `snap45.py` turns the last oblique pad-to-via stubs into 45° doglegs (16 on this build) |
| 88 | Coherent paths | ✓ | Designed lanes for display, I2S, mic, Hall, UART, touch and USB (`route_local.py`); the router works on a 45° grid, then tidy and prune passes |
| 89 | Aligned passives | ✓ | Decoupling rows at each IC; the service field is a regular grid |
| 90 | Few rotations | ✓\* | All parts at 0/90/180/270° except 9 placed radially for mechanical reasons: J302 TOP spring, U402 ToF, U403 light, U301/U302 Hall (under the pole track), MK401 (mic port), D501/D502 (IR out of the wall), J501 (LRA leads) |
| 91 | Readable references | ✓ | Silk text is upright as seen from its own face; resistor and capacitor references are on the assembly drawing (rule 177) |
| 92 | Polarity marking | ✓ | Cathode bars (D101, IR LEDs with Fab "K"), "− T +" on the battery, pin-1 marks on every IC and connector |
| 93 | Pin 1, two cues | ✓ | Custom footprints have a chamfered Fab corner plus a silk dot; library ICs have silk marks plus Fab chamfers. Polarised parts are listed in FAB-NOTES for the placement check |
| 94 | Silk never on pads | ✓ | `check_silk_text.py`: 66 texts, 0 findings (pads, vias, bodies, edges, other text) |
| 95 | Readable silk | ✓ | Minimum 0.8 mm height (0.15 mm stroke): 30 texts at 0.8 mm, 39 at 1.0, MAO at 1.3 |
| 96 | Maker's mark | ✓ | ODD JOBS mark on the face side (visible) and on the back |
| 97 | Product identification | ✓ | Face: mark, MAO, MAIN A0, 2026-10. Back: mark, MAO MAIN A0, 2026-10 |
| 98 | Visible revision | ✓ | "A0" on both faces, plus the board-ID divider (1.65 V = A0) read by firmware |
| 99 | PCB vs product version | ✓ | PCB "MAIN A0"; the firmware and app version is separate |
| 100 | Date code | ✓ | 2026-10 |
| 101 | Serial area | ✓ | S/N box on the face side beside the mark; FAB-NOTES put JLC's order number there or remove it |
| 102 | Debug identification | ✓ | SERVICE, RST, BOOT, XRST, SDA, SCL, 3V3, SYS, BAT, VBUS, GND, plus LCD, MIC, IRV at the switched rails |
| 103 | No mystery pads | ✓ | Every test pad carries its function name on silk |
| 104 | NC pins marked | ✓ | J301 pins 1–4, 6 and 17 (touch-controller pins of touch panels), USB-C SBU, and unused IC pins have no-connect flags in the schematic; J301's are documented in its note |
| 105 | Meaningful net names | ✓ | 85 named nets; `unconnected-(…)` exists only for NC pins |

## 7. Power behaviour, ESD and EMC (106–131)

| # | Rule | | How |
|---|---|---|---|
| 106 | One ground | ✓ | A single GND net and plane |
| 107 | No blind star ground | ✓ | Solid L2 plane with deliberate return paths |
| 108 | No sprinkled ferrites | ✓ | None used |
| 109 | Intentional 0 Ω | ✓ | R108 BAT LINK only (current measurement) |
| 110 | Current measurement link | ✓ | R108 0 Ω 1206 in the cell path, labelled BAT LINK, 4 mm from J102 |
| 111 | No permanent power LED | ✓ | None |
| 112 | Quiescent current | ✓ | Budgeted in `mao-power-budget.md`: deep sleep about 70 µA. Mic and IR receiver are powered only while used; Hall sensors sample at 20 Hz asleep; pull-ups are 100 kΩ where the speed allows |
| 113 | No back-powering | ✓\* | IR receiver pull-up goes to its own switched supply. Display reset and backlight are held low while the rail is off. The mic is GPIO-powered and its clock idles low. A service UART adapter can feed RXD0 while the board is off: use a 3.3 V adapter and power the board first (`mao-bringup.md`) |
| 114 | Power sequencing | ✓ | The display rail soft-starts (TPS22917, about 3.8 ms) with RESET held low until firmware releases it |
| 115 | Enable pins defined | ✓ | TPS63802 EN divider (hardware UVLO), TPS22917 ON pull-down, charger /CE to GND, every expander enable pulled down |
| 116 | Fail-safe actuators | ✓ | Before firmware: speaker shut down, haptics off, IR LED off, backlight off, display in reset |
| 117 | Reset-state GPIO | ✓ | Pull-downs on the IR LED gate, backlight gate and MIC_PWR; expander outputs high-Z with pull-downs |
| 118 | ESD where accessible | ✓ | USB-C: TPD2E2U06 + SMF15A. Every touch electrode and spring: 0.42 pF ESD (D301–D304). The face switch is internal |
| 119 | ESD at the entry | ✓ | U101 is 3.9 mm from the receptacle pins; touch ESD sits at each electrode |
| 120 | Short TVS ground | ✓ | D101 has its own double GND via at the pad |
| 121 | EMC at the source | ✓ | Solid L2, short clocks, 22 Ω damping, local decoupling, small switching loop |
| 122 | Crosstalk | ✓ | Fast lanes are grouped on B over their own cores; touch and Hall lines run on F at the rim; no long parallel run between a clock and a sensitive line |
| 123 | 3W where useful | ✓\* | USB pair gap is 3W. The display bus lines are 10–16 mm long, at 1.5W pitch: crosstalk within one bus is harmless, and the bus is damped (rule 29) |
| 124 | Orthogonal adjacent layers | N/A | No two adjacent signal layers: L1 and L4 each face a plane; L3 carries only slow lines |
| 125 | Edge EMI | ✓ | Only the touch electrodes (by design) and slow lines run at the rim; fast lanes are inside |
| 126 | No odd copper islands | ✓ | Island removal on every pour |
| 127 | Pour islands tied | ✓ | `gnd_stitch.py` ties every outer GND fragment to the plane, two ties for fragments over 20 mm² |
| 128 | Purposeful via fence | ✓ | No decorative via fields |
| 129 | Ground under the MCU | ✓ | L2 is solid under the module; the module ground pad has 12 vias. A "MODULE UNDERSIDE" rule area keeps B tracks out of the module's central underside; only short, masked escapes run beside the pad rows |
| 130 | No copper under the antenna | ✓ | No board at all: notch |
| 131 | Conductors beside the antenna | ✓\* | No daughterboards. The panel is the nearest conductor (rule 2); RSSI check at bring-up |

## 8. Assembly, enclosure and production (132–171)

| # | Rule | | How |
|---|---|---|---|
| 132 | Multiple PCBs | N/A | Single board; the display is a separate panel on an FPC (rules 45–47) |
| 133 | Daughterboard alignment | ✓ | The panel is located by its printed carrier in the shell bore, not by its connector |
| 134 | Board-to-board mating height | N/A | No board-to-board connector |
| 135 | Tolerances | ✓ | Board to wall 1.0 mm; no sliding fits (the face hangs on flexures); speaker ≥ 0.8 mm from the wall and 1.8 mm from the cell; IR receiver ≥ 0.35 mm under the pressed window at worst case (standoff raised from 2.2 to 2.7 mm for this); Ø2.2 holes for M2 |
| 136 | Enclosure clearance | ✓ | 1.0 mm board-to-wall, with ribs touching in ≤ 4 places |
| 137 | Screw tolerance | ✓ | Ø2.2 holes for M2 |
| 138 | Button tolerance | ✓ | The face rocks on its lip on three printed flexures (no sliding fit): a press anywhere reaches the SKQG through the Ø2 boss, and a 0.2 mm offset of the boss on the stem changes nothing |
| 139 | USB opening tolerance | ✓ | 9.2 × 3.6 mm opening vs the 8.25 × 2.4 mm plug shell; the opening clears the receptacle shell |
| 140 | Nozzle access | ✓ | No walls of tall parts. Tallest B parts are the module (3.1 mm) and the JST (2.9 mm), at the board's edges and centre |
| 141 | Fiducials | ✓\* | Three global fiducials per side (1 mm copper, 2 mm mask, copper-free ring). No local fiducials: 0.5 mm pitch parts are within JLC's global-fiducial capability |
| 142 | Panelization | ✓ | Single boards. If a panel is needed, FAB-NOTES name the only clear tab spots, 2 and 8 o'clock (≥ 3 mm to any copper or part) |
| 143 | No fragile parts at tabs | ✓ | The tab spots avoid the touch arcs (3/9 o'clock), USB-C and IR (12) and the notch (6) |
| 144 | Parts away from routed edges | ✓\* | No V-score (milled edge). Nearest MLCC: C304 (0402), the Hall sensor's decoupling, about 0.7 mm from the edge because the sensor must sit under the pole track (r 26.3). 0402 is the least crack-prone size, and it is clear of the tab spots |
| 145 | MLCC flex cracking | ✓ | No large MLCC beside holes, edge or USB-C; bulk 0603/0805 parts sit inboard |
| 146 | DC-bias derating | ✓\* | Buck output 2 × 22 µF 6.3 V 0603 at 3.3 V: about 40 % effective. Measure ripple; A1 fallback is 47 µF 0805 (risk 7). The charger caps meet TI's minimums after derating |
| 147 | Voltage margin | ✓ | VBUS-side caps 25 V, VSYS/VBAT 10–16 V, 3.3 V rails 6.3–16 V |
| 148 | Resistor power | ✓ | IR LED 47 Ω, 0603 (0.1 W): 0.22 W in each 8.8 µs carrier pulse at the 68 mA worst case; about 0.03 W averaged over an NEC frame. Backlight 10 Ω 0603: about 9 mW. R108 1206 link: under 5 mW. Dividers and pull-ups: µW |
| 149 | Inductor saturation | ✓ | DFE201612E-R47M, Isat 5.5 A. Normal peak inductor current is about 1.5 A (0.7 A load from a 3 V cell, plus ripple). The converter's peak limit is 5 A typical (3.8 A in buck mode), so only an overload reaches Isat, and this metal-composite part saturates softly |
| 150 | Diode ratings | ✓ | SMF15A: 15 V standoff, 200 W. ESD diodes within ratings. AO3401A body diode is only used for reverse blocking |
| 151 | Connector current | ✓ | USB-C VBUS contacts are rated well above the 1.07 A input limit. JST SH is rated 1 A against cell peaks of about 0.6 A. The springs carry only touch signals; the speaker's own contacts carry ≤ 0.4 A peak |
| 152 | Trace current | ✓ | 0.6–0.8 mm 1 oz outer for ≤ 1 A paths over ≤ 3 mm: under 10 °C rise (IPC-2152) |
| 153 | Thermal spreading | ✓ | Heat from the charger, buck and amplifier goes into L2 and L3; the IMU (no temperature sensing) is 15–20 mm from them |
| 154 | Temperature sensor placement | N/A | No ambient temperature sensor; the cell NTC is in the cell |
| 155 | Sensor placement | ✓ | ToF, light and IR receiver look out through the window band; the mic port goes through the board to a window hole; the IMU sits near the axis with known orientation |
| 156 | IMU axis marking | ✓ | Silk axis glyph +X → / +Y ↑ beside U401; +Z out of the face (ST AN5192); firmware default `MAO_PERCEPT_IMU_Z_DOWN = n` matches |
| 157 | Jig interface | ✓ | Tag-Connect (3V3, GND, TX, RX, GPIO0, EN) plus the service field (rails, grounds, RST, BOOT, I2C) |
| 158 | Pogo pattern not reversible | ✓ | Tag-Connect is keyed by its three asymmetric locating holes. The service field is a 4 × 3 grid with one position left empty (x 8.8, y −2.7) and mixed 1.2 / 1.0 mm pads, so a fixture cannot mate rotated |
| 159 | Fixture datum | ✓ | Two M2 holes and the Ø2.0 peg hole (three non-symmetric datums), plus the outline |
| 160 | Factory test | ✓ | `mao-factory-test.md` and firmware self-test (33 steps): rails, current, MCU, flash, radio, display, inputs, audio, peripherals, serial |
| 161 | First power-up | ✓ | `mao-bringup.md`: short check at the test pads, then current-limited VBUS on TP6 with no cell; the cell comes last |
| 162 | Cut/jumper points | ✓ | R108 link, 22 Ω termination footprints, DNP R110, and test pads on the switched rails |
| 163 | Solder jumpers marked | N/A | No solder jumpers (R108 is labelled BAT LINK) |
| 164 | DNP tuning footprints | ✓ | R110 (NTC substitute) kept as DNP |
| 165 | BOM availability | ✓ | Every line has an LCSC number, checked for stock on 2026-10-04; `fab.py` refuses a line without one. No exotic single-source IC |
| 166 | Second source | ✓ | Passives, the MOSFETs (AO3400A / AO3401A), the TVS and the JST SH are generic parts made by several suppliers. ICs are high-volume TI, ST, Analog/Maxim and Espressif parts stocked at LCSC |
| 167 | Connector standardization | ✓ | JST SH for the battery, BW0019BG springs for the touch electrodes, spring-contact pads for the speaker, Tag-Connect for debug |
| 168 | Fastener standardization | ✓ | One size: M2 |
| 169 | Board thickness | ✓ | 1.6 mm |
| 170 | Copper weight | ✓ | 1 oz outer, 0.5 oz inner (JLC standard 4-layer) |
| 171 | Surface finish | ✓ | ENIG (flat for 0.5 mm pitch, visible board) |

## 9. Visual, documentation and review (172–200)

| # | Rule | | How |
|---|---|---|---|
| 172 | Standard colour | ✓ | Matte black mask, white silk, ENIG |
| 173 | Black mask inspection | ✓\* | Black kept, since the board is visible through the window band. FAB-NOTES allow green for this EVT order if black costs schedule |
| 174 | No fake tech graphics | ✓ | Only the mark, identity, function names and the IMU glyph |
| 175 | Hidden details | ✓ | Identity block on the back beside the module |
| 176 | Visual hierarchy | ✓ | Identity (MAO 1.3 mm, largest) > connector names > debug names > S/N > references (1.0 / 0.8 mm) |
| 177 | References on prototypes | ✓\* | 34 references on silk: ICs, connectors, transistors, diodes, the switch, mic, inductor and electrodes, plus each R/C named in the bring-up and test procedures. On the assembly drawings delivered with the order: 74 other R/C references and 9 parts with no clear unambiguous spot (U102, U103, U104, Q101, Q501, D101 in the power cluster; U301 beside its labelled twin; D303 / D304 beside their springs). Test pads and the speaker carry function names instead of references |
| 178 | Manufacturing notes | ✓ | FAB-NOTES: thickness, copper, finish, mask and silk colour, stackup JLC04161H-1080, impedance (none, with reason), vias, POFV, panel; the stackup is also in the board and the Gerber job file |
| 179 | Impedance | N/A | No USB high speed, RF feed or fast memory on the board: the module contains the RF feed; USB is full speed (rule 23) |
| 180 | Inside fab capability | ✓ | 0.15 mm min track and space (JLC 0.09 mm), 0.2 mm min drill (standard) |
| 181 | Inside assembly capability | ✓ | 0402 minimum, 0.5 mm pitch QFN and LGA, no BGA, no microvias |
| 182 | 0603 preferred | ✓\* | 14 × 0603 and 1 × 1206 where rework or current matter (bulk caps, R108, R303). 65 × 0402 are needed to fit a two-sided Ø58 mm board; none are 0201 |
| 183 | Replaceable critical parts | ✓ | Regulators, ESD, connectors and the module are on the open B side with access all round |
| 184 | Likely failures serviceable | ✓ | USB-C has THT legs; the battery is a plug; the speaker sits on spring contacts and the touch electrodes on springs, no wires; the display plugs in |
| 185 | No undocumented hacks | ✓ | The board is generated from code; every change is in `design/` and this review |
| 186 | Schematic sections | ✓ | Sheets: Power, Compute, Interface, Sense, Feedback |
| 187 | Signal flow | ✓ | Power sheet runs source → protection → regulation → loads; blocks run input → MCU → output |
| 188 | Readable schematic | ✓ | Named nets and block notes; generated layout, readable in the PDF |
| 189 | Notes on the schematic | ✓ | Strap, UVLO, reset and default-state notes on each block. GPIO numbers in notes are generated from `pinmap.py`, so they cannot drift (corrected in this review) |
| 190 | Datasheet references | ✓ | Footprint docstrings and `mao-rev-a0-review.md` §1 cite each datasheet |
| 191 | Manual net-class review | ✓ | Every class reviewed in `mao-rev-a0-review.md` §2 (USB, display, I2S, PDM, I2C, power, touch, slow) |
| 192 | Connector trace review | ✓ | Traced: USB-C (A6/B6 D+, A7/B7 D−, CC, VBUS, shell), J102, J301 (18 pins against the Winstar definition), Tag-Connect, springs, LRA |
| 193 | 1:1 footprint check | ✓\* | Custom footprints are transcribed from the manufacturer drawings, with every dimension cited in `footprints.py`. JLC places the parts, so no physical part is in hand before the order; the first-article microscope inspection (`mao-bringup.md` §0) is the physical check |
| 194 | 3D review | ✓ | Top, bottom and iso renders with every part bodied; stack section checked against the enclosure specification (found the IR clearance) |
| 195 | Gerber review | ✓ | Independent render of all 11 Gerber layers with pygerber (`plots/mao-main-a0-gerbers.png`). The drill file reconciles exactly: 141 × 0.3 mm (137 vias + 4 electrode joins), 81 × 0.2 mm (57 vias + 24 exposed-pad vias), 4 × 0.6 mm shell legs, 9 NPTH |
| 196 | BOM review | ✓ | 48 lines / 112 parts: MPN, value, voltage, package and LCSC stock checked; `fab.py` refuses a line without an LCSC number |
| 197 | Assembly orientation review | ✓ | FAB-NOTES list every polarised part for JLC's placement preview; assembly drawings carry every reference |
| 198 | Enclosure review | ✓\* | Mounting, antenna clearance, cable routing, thermal zones, USB insertion, speaker opening, button actuation and display window are all specified in `mao-mechanical.md`. This review added the standoff, ToF gasket, mic tube, TOP-spring boss, the 15 × 8 mm speaker and its cradle, the ferrite pole strip and the flexure-hung face. First printed-part fit check at bring-up |
| 199 | One pin map | ✓ | `pinmap.py` generates the schematic nets, firmware header and pin-map doc; firmware builds against it (5 configurations, 0 warnings) |
| 200 | Golden rule | ✓ | Boot: straps defined (rule 32). Flash: USB, plus Tag-Connect UART. Recover: ROM loader by face, TP8 or Tag-Connect. Measure: 15 pads, BAT LINK, switched-rail pads. Fit: stack and datums checked, with all parts bodied |

## RF picture and release gate

- **RF picture:** matches the standard's sketch, turned so that 6 o'clock is the "edge". Antenna on one edge; power, charger and USB on the opposite edge; digital in the middle; audio amplifier and display connector at 9 o'clock beside the digital centre.
- **Release gate: EVT** (engineering prototype). The board carries the EVT extras the standard allows: full test-pad field, BAT LINK, DNP options, 22 Ω termination.
- The DVT items it already meets: final antenna placement, production connectors, ESD and power architecture.
