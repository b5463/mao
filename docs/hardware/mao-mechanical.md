> **Superseded 2026-10-06 (kept as history):** this document describes MAO_MAIN **A0**. The current board is MAO_MAIN **A1** (A0 moved to the M5 Gate C architecture): see [mao-a1-report.md](mao-a1-report.md).

# MAO_MAIN A0: mechanical integration

Everything here comes from `hardware/mao/design/mechanical.py` (datums) and `placement.py`
(parts). The drawing below is generated from the same numbers by `design/mech_drawing.py`, so it
cannot drift from the board.

![Enclosure interface, top view](mao-mechanical-interface.png)

Coordinates: board millimetres, origin on the puck axis (display centre), +x to the right and +y towards
6 o'clock, viewed from the face. 12 o'clock is MAO's back (USB-C, IR); 6 o'clock faces the person.

## 1. Stack (section through the axis)

| Layer (top → bottom) | Thickness | Notes |
|---|---:|---|
| Clear window | 1.0 | **Not FDM**: FDM is never optically clear. Use laser-cut 1 mm PMMA or PC (Ø47), bonded to the display carrier so window, panel and carrier float together in the top shell's bore (§4). Back-printed or masked black except over the face and the three sensor apertures. Its underside is 4.9 mm above F.Cu, fixed: the press moves the board with the face (§4) |
| Air / foam | 0.2 | no pressure on the panel glass |
| Display, 1.28" round GC9A01 (Winstar WF0128BTYAA4DNN0, 35.6 × 37.74 × 1.56) | ~2.0 | on a printed carrier that stands on the board on three feet; window, panel, carrier and board press together (§4) |
| Panel standoff | 2.7 | carrier + 0.5 mm foam; the slack of the panel's 70 mm FPC tail lies as one long loop in a 0.4 mm pocket of the carrier, turning in a 2.4 mm well, and the tail drops through the board slot at 9 o'clock to J301 on B.Cu (§4a). Set by the tallest part under the window border, the IR receiver (4.0 ± 0.3 mm with its dome): 0.6 mm clearance at worst case, which a press no longer closes (ODD JOBS 135) |
| PCB F.Cu side (Zone A) | (≤ 1.2) | inside the standoff, under the panel; tallest parts are the display rail switch U105 (SC70-6, 1.1) and the charger U102 (1.0); the carrier's three feet stand on bare mask. Nothing under the carrier's tail well (x 6 … 12, y ±5.75) or beside the slot. Under the window border (sensor band) parts may reach 4.3 mm; under the ring's lower lip (r > 24 mm, lip 1.9 mm above F.Cu) ≤ 0.95 mm (0603). `design/mech_check.py` checks every part's 3D-model height against these limits: 0 findings |
| PCB | 1.6 | 4 layers (JLC04161H-1080), A1: purple mask, white legend, ENIG (owner decision 2026-10-06; A0: black) |
| PCB B.Cu side (Zone B) | ≤ 3.2 | USB-C 3.2 mm, module 3.1 mm, JST SH 2.96 mm, the press switch SW301 1.5 mm with its stem (on the base's steel finger, §4), everything else ≤ 1.2; nothing in the tail corridor between the slot and J301, nothing under the speaker but its contact pads |
| Insulator | 0.3 | Kapton or fish paper on the cell's top face |
| Cell LP503035 | 5.1 | 35.5 × 30 mm, centred at (0, −5.6), long axis along x: 12.05 mm from the antenna boundary |
| Base floor | 1.2 | speaker sits in the left crescent beside the cell |

Total ≈ 17.3 mm. With 0.6 mm top lip and 0.4 mm foot it comes to an 18.3 mm puck.

## 2. Board and fixing

- Ø58.0 mm disc. Antenna notch at 6 o'clock: 24.0 mm wide, inner edge at y 21.45 (7.55 mm deep on the axis), 1 mm
  milled corner radii.
- **Module position:** U201 on B.Cu, antenna end at y 27.45. Its position is set by its antenna-end corners (x ±9),
  not its axis: they sit at r 28.89, inside the board circle, so the module keeps the board's 1 mm to the wall. The
  notch and the antenna keep-out follow the module (`mechanical.MODULE_OUTER_R`).
- Display-tail slot at 9 o'clock: routed, non-plated, 1.0 × 11.5 mm with round ends, x −19.3 … −18.3, y −5.75 … +5.75
  (0.5 mm outside the panel outline, 0.35 mm clear of the speaker's SPK− pad).
- **Fixing:** two M2 screws at ±48° on r 25 mm (holes Ø2.2), from the base through the board into
  M2 heat-set inserts in the top shell.
  - Copper-free 4.6 mm rings on every layer; part-free Ø6.5 mm on F.Cu for the printed boss with the brass insert and Ø5.5 mm on B.Cu for the screw head (ISO 7045 M2, head Ø4.0).
  - Screws sit on the back half only: no metal within 15 mm of the antenna (ODD JOBS 69).
- **Locating peg:** one plastic peg at 225° (NPTH Ø2.0, printed peg Ø1.9) on the antenna half.
- **Board to wall:** 1.0 mm. Rib the wall so the board edge touches it in at most 4 places, so the touch arcs keep a constant gap.
  `mech_check.py` tests it for every part: each body stays inside the board circle (r ≤ 29.0) except the parts that
  sit in a wall opening by design (USB-C, the two IR LEDs). Largest: the speaker contact pads LS501 at r 28.96; the
  speaker itself keeps 1.1 mm from the wall.
- **Panel tabs** (for a panelised order): at 130° and 328° clockwise from the USB-C (about 4 and 11 o'clock). Over 4 mm
  of rim at each, no copper on any layer within 1.55 mm of the edge and no part within 1.35 / 1.45 mm, so a mouse
  bite cuts laminate only (`panel_tabs.py`, PANEL-TABS.json). 328°, not 320°: there the tab's keep-out would
  neck the L3 VSYS band where it rounds H1's screw ring.

## 3. Ring dial (contactless)

| Item | Value |
|---|---|
| Ring | ID 48 / OD 64 mm, printed, rides on the top shell's track. Grease (PTFE) or a 0.5 mm PTFE washer under it |
| Pole strip | **Flexible ferrite multipole strip**, about 1.5 mm thick and 3 mm wide, 165 mm long, magnetised with 30 alternating poles across its face (pitch 12°, 5.5 mm), glued into a groove in the ring's lower lip at r 26.3 mm. Ferrite in rubber does not conduct, so nothing metal sweeps over the antenna at 6 o'clock as the ring turns (sintered NdFeB magnets would, about 4–5 mm above it; ODD JOBS 2, 3) |
| Sensors | U301/U302 DRV5012 on F.Cu at r 26.3 mm, 120° and 126° (6° = a quarter of one N/S pair = quadrature) |
| Gap | strip face to sensor top 1.5 mm through the ring lip. DRV5012 switches at ±3.3 mT at most (SLIS158); order the strip to give ≥ 8 mT peak at 1.5 mm (2.4× margin) and confirm with the dial test at bring-up |
| Detent | **haptic**: the ring turns smoothly on its PTFE washer, and every one of the 30 steps per turn is a short tick from the LRA (DRV2605L "sharp tick") plus a soft sound tick, both from firmware (`dial_tick`), thinned when the ring is spun fast. It feels like the Surface Dial: a crisp tick in the fingers, nothing that wears, nothing to tune in the print |
| Firmware | 30 detents / 15 quadrature cycles per turn, rest state 00/11 (identical to the EC11, `mao_input` unchanged) |

Alternative if a mechanical click is wanted: a printed leaf spring with a 0.6 mm bump riding 30 notches on
the ring's inner wall (audible, some wear). The earlier steel-pin magnetic detent needs strong sintered
magnets, which must not pass over the antenna.

## 4. Press (A1: P3 whole-top press, owner decision 2026-10-07)

The whole top presses: wheel, bezel, window, panel, carrier, frame and the board slide down on the base as one
piece, and **one tact switch on the board's back** meets a steel finger fixed to the base. This replaces A1's face
tripod of 2026-10-06 (three SKQGAFE010 on F.Cu under the display carrier) and A0's single centre switch, under which
the face rocked about the lip. The owner chose it for manufacture: one switch instead of three, nothing in the face
stack moves against the board, and the click is the same wherever the stone is pressed. The enclosure side is in
[mao-a1-enclosure-wheelstone.md](mao-a1-enclosure-wheelstone.md) (Press).

- **Switch:** ALPS **SKQGADE010** (LCSC C116647; 2.55 N, 0.25 mm travel, 1.5 mm with stem), SW301 on **B.Cu** at
  (8.0, 3.8), `mechanical.PRESS_SWITCH`. It is on PRESS_N (GPIO14, the 100 k pull-up R318), so there is no firmware or
  pin change.
- **Why there:** on B the cell fills the board's centre, so the finger has to reach over the cell to the switch. (8.0, 3.8),
  8.9 mm off the axis, is the nearest B spot to the centre that clears every courtyard and the service field's names.
  To get it, the LCD_TE debug pad TP19 moved 0.45 mm (it has no fixture position).
- **Finger:** 1.5 mm stainless (301), laser cut. Its root is held by two M2 screws on base posts at 12 o'clock, past
  the cell. It reaches 24.7 mm over the cell to a 0.3 mm dimple under the stem, and gives 0.08 mm at the click
  (about 30 N/mm). `mechanical.FINGER`; the outline is `wheelstone.FINGER_POLY`.
- **Guide and stop:** three M2 screws through Ø3 brass spacers sliding in base columns at r 32.4 (0°, 150°, 300°)
  into the frame's inserts. The columns' tops stop the top after `mechanical.TOP_TRAVEL` = **0.45 mm**.
- **Force and travel:** the dome carries the top at rest. It clicks after 0.33 mm (0.25 mm switch travel plus the
  finger's give) and stops at 0.45 mm, with about 6 N on the switch. Wherever the stone is pressed, the spacers keep
  the top level, so it clicks the same.
- **Clearances:** the finger is 0.35 mm over the cell. Its nearest B parts (U104, D101) clear it by 0.80 mm at rest
  and 0.35 mm at the stop. The wheel-stone clash check is 0 at rest and pressed.
- **Feel:** one short click from anywhere on the stone, plus one LRA click on every press (firmware).
- A1 has no strap on the press (GPIO14): holding the stone down during a reset does not enter the ROM bootloader; use
  the BOOT pad (TP8) or the Tag-Connect.

## 4a. Display tail (stock Winstar WF0128BTYAA4DNN0, 70.1 mm)

The panel is the stock part with its full 70.1 ± 0.5 mm tail (spec §8); nothing is cut or re-terminated.

| Step | Path | Geometry |
|---|---|---|
| 1 | leaves the panel at 9 o'clock (image up = 12 o'clock: the GC9A01 turns only in 90° steps, so the exit is cardinal) | 13.05 mm wide near the glass, 9.50 ± 0.1 mm at the contacts |
| 2 | folds back under the panel at its bending area | fold radius ≥ 1 mm |
| 3 | one long loop under the panel: two flat layers (2 × 0.11 mm) in a 0.4 mm deep pocket of the face carrier, inward to about x +8, where the loop turns in a 2.4 mm deep well of the carrier, and back out | takes up the slack (about 45 mm). The turn has room for a ≥ 1 mm radius, and the ±0.5 mm tail tolerance only moves it within the well (x 6 … 12, y ±5.75; no parts under it on F.Cu, `mech_check.py`). If the first print shows the loop longer than the well, the drop to J301 takes up to 2 mm more in its bend (4.5 mm of room). The press moves panel and board together, so the tail does not flex on a press |
| 4 | leaves the face at r ≈ 19 and drops through the board slot | slot x −19.3 … −18.3, y ±5.75; F.Cu part-free x −19.4 … −17.7, y ±6.4 |
| 5 | bends inward under the board into J301 on B.Cu | J301 (HDGC 0.5K-HX-18PWB, back-flip, 1.0 mm high) at (−11.15, 0), entry facing the slot, 4.5 mm for the bend; B.Cu corridor x −18.3 … −14.4, y ±5.9: tracks only, no parts |

All fold axes are tangential, so the tail keeps its lateral order: panel pin 1 arrives at the 12 o'clock end of
J301, which is pad 18 on the flipped connector (panel pin k = pad 19 − k; `circuit.py` carries the mapping). The
contacts are on both faces of J301, so the tail can enter either way up.

For the enclosure: model the carrier pocket (0.4 mm deep, 14 mm wide, from the rim channel to x +6), the well
(2.4 mm deep, x 6 … 12, y ±5.75, its far wall rounded to ≥ 1.2 mm) and a 1.5 mm wide channel in the carrier's rim at
9 o'clock for the tail to pass from the pocket down to the slot. Check the first print by pressing the face at the
rim above the tail a few hundred times: the panel must stay lit and the colour test must stay clean. Check the
Winstar bending rule (spec §8) against the turn before the first print.

## 5. Sensors that look out

| Sensor | Position | Window treatment |
|---|---|---|
| ToF VL53L4CD (U402) | 11 o'clock, r 21.2 | Clear aperture Ø3, no paint. The sensor top is 3.9 mm below the window, so the air gap is bridged by a **black light-blocking gasket**: closed-cell foam (PORON or EPDM) 4.2 mm free, about 3.9 mm fitted, with separate Ø1.2 openings over the emitter and the receiver, bonded to the window underside and resting on the sensor cap. This is ST AN5231's configuration for sub-1 m ranging (gasket required, window ≤ 1.5 mm, no open air gap). The press moves the sensor and the window together, so it does not load the foam. Run crosstalk calibration at the factory station |
| Light OPT3004 (U403) | 1 o'clock, r 21.2 | Clear aperture Ø2; or a 50 % ink dot so it reads "dim" like an eye. The aperture, 4.25 mm above the die, limits the view to about ±17°: enough for room level and "covered"; calibrate the lux scale at bring-up |
| IR receiver IRM-H638T (U503) | 3 o'clock, r 21.4 | IR-transparent (visible-black) ink acceptable. The dome top (4.0 ± 0.3 mm) sits 0.6–0.9 mm under the window |
| Microphone SPH0641 (MK401) | 3–4 o'clock, B.Cu, bottom port through the board | Ø0.8 hole in the window border above it, with a mesh. A foam gasket tube (Ø4 / Ø1.2, 5.2 mm free, about 4.9 mm fitted) seals the path from the copper-free ring round the port hole on F.Cu to the window hole, so the mic hears the room, not the cavity |
| IR LEDs (D501/D502) | back edge, 11 and 1 o'clock, side-emitting outwards | Ø3 IR-transparent windows in the wall (or the wall itself in IR-clear PETG) |

## 6. Touch electrodes

| Zone | Implementation |
|---|---|
| LEFT / RIGHT | Copper arcs at the board rim (E301/E302, 50° each, F+B). They sense through the 2 mm wall and the ring; nothing to build. No ground plane under them |
| TOP | Spring J302 (BW0019BG, free 3.8 mm, working 3.0 mm, limit 2.5 mm) presses on a boss that hangs from the carrier under the window border with its face 3.1 mm above F.Cu (1.8 mm below the window). The electrode is copper tape on the window's underside wrapped down over the boss, or the boss printed in conductive filament if material B is conductive. The hardest case, a rim press right beside it, takes the spring to about 2.6 mm, inside its 2.5 mm limit |
| REAR | Spring J303 presses on an electrode on the base's inner surface: copper tape or conductive filament |

## 7. Battery

- Cell under the board on the 0.3 mm insulator, held in a printed pocket in the base. It must not be the clamping element: leave 0.3 mm clearance above it for swelling.
- **Lead:** from the cell's 3 o'clock end, about 25 mm, to J102 (JST SH 3-pin) at 2 o'clock on B.Cu.
  - The opening faces 6 o'clock, so the plug lies flat over the cell end.
  - Pinout 1 BAT−, 2 NTC, 3 BAT+.
  - Order the cell with that pinout or re-terminate it, and check polarity on receipt.
- The R108 0 Ω link ("BAT LINK") sits beside J102 (0.85 mm pad to pad): lift it to measure battery current.

## 8. Speaker and haptics

- **Speaker:** Same Sky CMS-150803-088S-X8, 15 × 8 × 3 mm, 8 Ω, 0.8 W, 93 dB, with its own spring contacts (a round Ø15 part does not fit: the crescent beside the 35.5 mm cell is 12.2 mm wide). It lies under the board at 9 o'clock, long side along the rim, centred at (−23.74, 0.6): 2.0 mm from the cell and 1.1 mm from the wall at its corners (`mech_check.py`).
  - Its spring contacts press up on the two pads of LS501 on B.Cu: SPK+ at (−20.65, 7.55), SPK− at (−20.65, −6.35). Its back sits about 0.25 mm under the board (the footprint's courtyard keeps every other B part out from under it); its membrane faces down.
  - A printed cradle in the base holds it and compresses its contacts to about 0.2 mm. A foam ring round its front frame seals a small printed front chamber that ducts to the slot grille in the wall at 9 o'clock; the rest of the puck's interior is its back volume.
  - Its outer edge lies over part of the LEFT touch arc; firmware holds the LEFT zone while the amplifier runs (its outputs switch at ~330 kHz).
- **LRA:** LD0832AA (Ø8 coin), glued with 3M VHB to the base under 7–8 o'clock, centred on r 21 at 232° (−16.5, 12.9), leads soldered to J501 at (−20.8, 11.1). Its steel can is about 7.4 mm from the antenna's corner, inside the 15 mm metal guideline: it is the only free spot in the base, so the bring-up RSSI check covers it. Firmly coupling it to the shell makes the haptics felt in the hand.

## 9. USB-C

- The receptacle's mating face is 0.6 mm past the board edge at y −29.6.
- **Wall opening:** 9.2 × 3.6 mm, outer chamfer 0.5 mm. The wall at 12 o'clock is 2 mm, so a standard plug overmold (≤ 6.5 mm thick) seats fully.
- The 4 THT shell legs take the insertion force. The opening must not touch the shell.

## 10. Two-material print plan (Bambu X2D + 2 × AMS HT)

| Option | Material A | Material B | Use |
|---|---|---|---|
| **Recommended** | Matte PETG (or PLA-matte), body, base, ring | TPU 95A | B for the ring's grip band and four feet. Touch electrodes from copper tape |
| Electrodes printed | Matte PETG | Conductive PLA | B as the TOP and REAR electrodes, printed in the shell and contacted by the springs. Expect 1–3 kΩ/cm: fine for capacitive sensing |

- The window is a separate clear part in both options (§1).
- Print the ring flat, lip down, with the groove for the pole strip in the lip.
- Print the top shell face-down so the window bore is accurate.

## 11. Assembly order

1. Glue the pole strip into the ring's groove (check the 30 poles with magnetic viewing film or the dial test).
2. Bond the window to the display carrier, fit the display onto the carrier with its tail laid as one loop in the carrier pocket (turning in the well) and out through the rim channel at 9 o'clock (§4a), fit the ToF and mic gaskets under the window border. The carrier stands on the board on its three feet and moves with it (§4; the wheel stone's own order is in mao-a1-enclosure-wheelstone.md).
3. Thread the tail down through the board's slot as the board goes into the top shell on the peg and inserts, fold it inward under the board, open J301's back-flip actuator (bottom side), insert the tail (contacts either way up) and close the actuator. The display can be lifted out and replaced the same way.
4. Insulator, cell (plug J102), speaker in its cradle (contacts up, under LS501), LRA; the press finger screwed to its two posts in the base (§4). Close the base.
