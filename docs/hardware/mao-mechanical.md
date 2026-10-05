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
| Clear window | 1.0 | **Not FDM**: FDM is never optically clear. Use laser-cut 1 mm PMMA or PC (Ø47), bonded to the display carrier so window, panel and carrier float together in the top shell's bore (§4). Back-printed or masked black except over the face and the three sensor apertures. Its underside is 4.9 mm above F.Cu (4.65 mm with the face pressed) |
| Air / foam | 0.2 | no pressure on the panel glass |
| Display, 1.28" round GC9A01 (Winstar WF0128BTYAA4DNN0, 35.6 × 37.74 × 1.56) | ~2.0 | on a printed carrier that floats with the window (press travel 0.25 mm) |
| Panel standoff | 2.7 | carrier + 0.5 mm foam; the slack of the panel's 70 mm FPC tail lies as one long loop in a 0.4 mm pocket of the carrier, turning in a 2.4 mm well, and the tail drops through the board slot at 9 o'clock to J301 on B.Cu (§4a). Set by the tallest part under the window border, the IR receiver (4.0 ± 0.3 mm with its dome): 0.35 mm clearance at worst case with the face pressed (ODD JOBS 135) |
| PCB F.Cu side (Zone A) | (≤ 1.2) | inside the standoff, under the panel; tallest parts are the SKQG switch with stem (1.5, pressed by the carrier boss by design), the display rail switch U105 (SC70-6, 1.1) and the charger U102 (1.0). Nothing under the carrier's tail well (x 6 … 12, y ±5.75) or beside the slot. Under the window border (sensor band) parts may reach 4.3 mm; under the ring's lower lip (r > 24 mm, lip 1.9 mm above F.Cu) ≤ 0.95 mm (0603). `design/mech_check.py` checks every part's 3D-model height against these limits: 0 findings |
| PCB | 1.6 | 4 layers (JLC04161H-1080), black mask, ENIG |
| PCB B.Cu side (Zone B) | ≤ 3.2 | USB-C 3.2 mm, module 3.1 mm, JST SH 2.96 mm, everything else ≤ 1.2; nothing in the tail corridor between the slot and J301, nothing under the speaker but its contact pads |
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
- **Panel tabs** (for a panelised order): at 130° and 320° clockwise from the USB-C (about 4 and 11 o'clock). Over 4 mm
  of rim at each, no copper on any layer within 1.55 mm of the edge and no part within 1.35 / 2.13 mm, so a mouse
  bite cuts laminate only (`panel_tabs.py`, PANEL-TABS.json).

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

## 4. Face press

The face (window, display and printed carrier) is one rocking plate on flexures. FDM cannot hold the 0.1 mm
sliding fit a guided plunger would need, so nothing slides:

- **Suspension:** three S-shaped flexure arms printed in one piece with the carrier (PETG, 0.8 mm thick, 1.2 mm
  wide, about 18 mm long along the sensor band) at 60°, 180° and 300°, clear of the sensor apertures and the TOP
  spring. Their outer ends are pinned to bosses in the top shell. They centre the face, stop it turning with the
  ring, and preload it up against the shell's 0.6 mm lip by about 0.5 mm of deflection (≈ 0.3 N).
- **Hinge:** the lip all round is the fulcrum. A press anywhere tips the face about the lip opposite the finger
  and drives the Ø2 boss under the carrier (at (0, −2.5), reaching down through the 2.7 mm standoff) onto the
  SKQG stem. At the centre that is about 2.6 N and 0.25 mm; at the rim about 1.3 N and 0.5 mm.
- **Stop:** the switch bottoming out is the hard stop; at full travel nothing under the panel comes closer than
  0.5 mm (U105 and U102, 1.1 / 1.0 mm, the tallest parts there besides the switch). Flexure stress at full travel is about 11 MPa, a fifth of PETG's yield.
- **Feel:** a short, firm, quiet tick from the switch's dome as the face dips a fraction of a millimetre, and one
  firm LRA click on every press (firmware) so centre and rim presses feel alike.
- **Look:** a flat clear window 0.6 mm below a thin rim, so laying MAO face-down never presses it.
- The tail's slack is one long loop, so the face's travel flexes the loop, not a fold (§4a).
- Holding the face during a reset enters the ROM bootloader (GPIO0), as with the LCDkit knob. The reset is TP7 RST,
  Tag-Connect EN, or plugging USB into a board with no cell: with a cell fitted, plugging USB does not reset the chip.

## 4a. Display tail (stock Winstar WF0128BTYAA4DNN0, 70.1 mm)

The panel is the stock part with its full 70.1 ± 0.5 mm tail (spec §8); nothing is cut or re-terminated.

| Step | Path | Geometry |
|---|---|---|
| 1 | leaves the panel at 9 o'clock (image up = 12 o'clock: the GC9A01 turns only in 90° steps, so the exit is cardinal) | 13.05 mm wide near the glass, 9.50 ± 0.1 mm at the contacts |
| 2 | folds back under the panel at its bending area | fold radius ≥ 1 mm |
| 3 | one long loop under the panel: two flat layers (2 × 0.11 mm) in a 0.4 mm deep pocket of the face carrier, inward to about x +8, where the loop turns in a 2.4 mm deep well of the carrier, and back out | takes up the slack (about 45 mm). The turn has room for a ≥ 1 mm radius, and the ±0.5 mm tail tolerance only moves it within the well (x 6 … 12, y ±5.75; no parts under it on F.Cu, `mech_check.py`). A press flexes the long loop, not a crease |
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
| ToF VL53L4CD (U402) | 11 o'clock, r 21.2 | Clear aperture Ø3, no paint. The sensor top is 3.9 mm below the window, so the air gap is bridged by a **black light-blocking gasket**: closed-cell foam (PORON or EPDM) 4.2 mm free, about 3.9 mm fitted, with separate Ø1.2 openings over the emitter and the receiver, bonded to the window underside and resting on the sensor cap. This is ST AN5231's configuration for sub-1 m ranging (gasket required, window ≤ 1.5 mm, no open air gap). The 0.25 mm press travel only compresses the foam. Run crosstalk calibration at the factory station |
| Light OPT3004 (U403) | 1 o'clock, r 21.2 | Clear aperture Ø2; or a 50 % ink dot so it reads "dim" like an eye. The aperture, 4.25 mm above the die, limits the view to about ±17°: enough for room level and "covered"; calibrate the lux scale at bring-up |
| IR receiver IRM-H638T (U503) | 3 o'clock, r 21.4 | IR-transparent (visible-black) ink acceptable. The dome top (4.0 ± 0.3 mm) sits 0.6–0.9 mm under the window (0.35–0.65 mm pressed) |
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
2. Bond the window to the display carrier, fit the display onto the carrier with its tail laid as one loop in the carrier pocket (turning in the well) and out through the rim channel at 9 o'clock (§4a), fit the ToF and mic gaskets under the window border. Pin the carrier's three flexure arms to the top shell's bosses.
3. Thread the tail down through the board's slot as the board goes into the top shell on the peg and inserts, fold it inward under the board, open J301's back-flip actuator (bottom side), insert the tail (contacts either way up) and close the actuator. The display can be lifted out and replaced the same way.
4. Insulator, cell (plug J102), speaker in its cradle (contacts up, under LS501), LRA. Close the base with 2 × M2.
