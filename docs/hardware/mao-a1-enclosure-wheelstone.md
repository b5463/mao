# MAO_MAIN A1: the wheel stone enclosure

Owner decision 2026-10-06: the **wheel stone** replaces the rock as the A1 enclosure direction. The owner found the rock's
small ring not prominent enough. In the wheel stone the whole upper shoulder of a round river stone is the dial: you turn
the stone's top, and the face sits in a shallow well in the middle. The rock (`mao-a1-enclosure-rock.md`) stays in the
tree as the earlier study.

| Assembled | Cut-away |
|---|---|
| ![iso](renders/mao-wheelstone-iso.png) | ![cut](renders/mao-wheelstone-cut.png) |
| ![underside](renders/mao-wheelstone-under.png) | ![exploded](renders/mao-wheelstone-exploded.png) |

![side](renders/mao-wheelstone-side.png)

## Form

- **Size:** Ø82 × 20 mm. The stone is widest at z 7 and has an elliptical shoulder up to the crown, with a flat underside
  and an 8 mm round-over.
- **Dial:** the wheel is everything above z 12. It is 78.9 mm across where it meets the base and 8.2 mm tall at the
  outside, so the thumb turns the stone's own shoulder. 120 shallow grip grooves (0.18 mm) run on the slope only; the top
  stays smooth.
- **Face well:** the bezel lip sits 1.9 mm below the wheel's inner edge, so the wheel turns without catching the face.
- **The mark:** debossed 0.5 mm on the underside (18 mm wide).

## Press (P3, owner decision 2026-10-07)

The whole top presses: wheel, bezel, window, panel, carrier, frame and the board slide 0.45 mm down on the base as
one piece. The base keeps the cell and a steel finger. One switch on the board's back (SW301, ALPS SKQGADE010,
2.55 N, at (8.0, 3.8), B.Cu) rests on the finger's boss. This replaces the face tripod (three switches on F under
the carrier). It needs one switch instead of three. The face no longer moves against the board, so the face stack has
no press gaps, and the click is the same wherever the stone is pressed.

- **Guide:** three M2 screws come up from under the base. Each runs through a Ø3 brass spacer that slides in a column
  of the base (bore Ø3.3, r 32.4 at 0°, 150° and 300°) and threads into a heat-set insert in a frame boss. The
  spacers guide the top, the screw heads under the columns keep it on, and three columns stop it turning. The
  columns' tops are the hard stop: the frame bosses sit 0.45 mm above them at rest. The frame wall is slotted
  round each column (0.15 mm clearance).
- **Finger:** 1.5 mm stainless (301), laser cut from `mao-wheelstone-finger.dxf`. Its root is held by two M2
  screws on two base posts at 12 o'clock, past the cell. It reaches 24.7 mm over the cell to a 0.3 mm dimple under
  the stem. It is 2.8 mm wide over its length and a 5.2 mm pad at the switch. Stiffness about 30 N/mm, so it gives
  0.08 mm at the click.
- **Numbers:** the dome carries the top at rest. The click comes after 0.33 mm (0.25 mm switch travel plus 0.08 mm
  finger give), and the stop after 0.45 mm. The switch sees about 6.1 N at the stop, within its rating (the domes
  take a static load well above their click force; check against ALPS at the fit print).
- **Clearances:** the finger is 0.35 mm above the cell. Its nearest B parts (U104, D101) clear it by 0.80 mm at
  rest and 0.35 mm at the stop.

## Parts

| Part | Moves | What it does |
|---|---|---|
| base | – | Floor (1.2 mm under the cell, 2 mm elsewhere). It carries the cell rails, the three guide columns (pockets for the screw heads below them), the finger's two root posts with M2 inserts, the USB-C cable tunnel at 12 o'clock, IR windows at 11 and 1 o'clock, the speaker grille at 9 o'clock and the mark. |
| finger | – | 1.5 mm stainless, screwed to the base; the switch's stem rests on its dimple. |
| frame | presses | A bearing wall round the board (slotted round the guide columns) and the 0.6 mm deck the wheel rides on, which also passes over the Hall pair. Four ribs above the board rim and snap ledges under it hold the board. Two pins go down through H1 / H2 to locate it. The three guide bosses carry the M2 inserts. |
| wheel | turns, presses | Solid (10.5 cm³, about 13 g in PETG: a little flywheel). Its flat underside rides the deck with 0.15 mm clearance and carries the 30-pole strip at r 26.3 in a groove open to the bore (the strip is self-adhesive). A skirt hides the joint, with room for the travel. The inner edge steps down under the bezel lip, which holds the wheel down. |
| bezel | presses | Fitted last. A 0.9 mm tube inside the deck, with a lip that holds the wheel down at its outer edge and stops the window at its inner edge. Three snap tongues (70°, 190°, 300°, where nothing sits under the deck edge) catch 0.55 mm under the deck, and a lead-in on each barb guides it past. A notch clears the IR receiver; a slot takes the carrier's key. |
| carrier | presses | A tray under the panel standing on the board on three feet (on bare mask, where the tripod's switches were), with the tail pocket and the well (x 6 … 12) from `mechanical.py`. The tail drops at 9 o'clock past the panel's glass ledge. Six posts up to the window sit between the sensors and the tail: none over the IR receiver (3 o'clock), the ToF (≈ 242°) or the tail. A key into the bezel stops it rotating. |
| window | presses | 1 mm clear PMMA disc, Ø45.8, laser cut and bonded to the posts, held under the bezel lip with 0.5 mm under the lip. |

The face stack follows the board's: panel rear 2.7 mm and window underside 4.9 mm above F.Cu, fixed (the press moves
the board with it). A 30-pole strip face sits 1.35 mm above the Hall tops, the spec gap being 1.5 mm.

**Assembly:**
1. The panel goes on the carrier and its tail through the board slot into J301.
2. The board, with the carrier on its face, snaps into the frame from below: the pins through H1 / H2, the ledges
   under its rim.
3. The finger is screwed to its two posts in the base, and the cell goes in its rails.
4. The base goes on from below: 3 × M2 screws through the brass spacers in its columns into the frame's inserts.
5. The wheel drops onto the deck.
6. The window is bonded to the carrier posts.
7. The bezel snaps in.

**Opening it:** remove the three screws and lift the base off. The board then comes out of the frame's ledges from
below, which exposes the barbs.

## Checks (`wheelstone.py`)

- **Clash check, 0 findings at 0.2 mm, at rest and pressed.** Every enclosure part is tested against every other part and
  against the board itself. That covers 105 component bodies (from the board's 3D models and Fab outlines, dumped by
  `board_parts.py`), the cell and the speaker.
  - Anything overlapping by more than 0.06 mm counts. The only designed overlap is SW301's stem on the finger's dimple.
  - "Pressed" moves the top, the board and its parts 0.45 mm down; the base, the finger and the cell stay.
  - A planted fault (the finger raised 0.6 mm) is caught on U104 and D101, pressed.
- **This review found and fixed three faults in the first wheel-stone draft:**
  - The frame's hub lip blocked fitting the wheel, and nothing held the face in. Both are now retained by the bezel.
  - The face carrier's solid ring sat in the sensors' view and on the IR receiver. It is now posts with gaps.
  - The base posts cut 57 mm³ into the cell.
- **P3 review:** the base's guide columns ran through the frame's wall (31 mm³ at rest, 36 mm³ pressed). The wall is
  now slotted round them.
- **STL export:** every mesh is closed and a single body (trimesh), then decimated to about 0.2 mm where it stays closed.

| Item | Value |
|---|---|
| Size | Ø82 × 20.0 mm |
| Wheel OD at the joint, bore, step under the lip | 78.9 / 49.2 / 51.4 mm |
| Wheel height at the outside | 8.2 mm |
| Face well (wheel inner edge above the lip) | 1.9 mm |
| Strip face to Hall top | 1.35 mm |
| Window | Ø45.8, 0.5 mm under the lip, underside 4.9 mm over F.Cu |
| Snap barb under the deck | 0.55 mm |
| Top travel to the hard stop | 0.45 mm |
| Finger: free length, stiffness, give at the click | 24.7 mm, 30 N/mm, 0.08 mm |
| Press to the click / switch force at the stop | 0.33 mm / 6.1 N |

## Files

All files are in `hardware/mao/enclosure/wheelstone/`.

**Generator:**
- `wheelstone.py`: the parts as voxel fields, the report and the clash check.
- `board_parts.py`: dumps the board's part bodies to `board_parts.json`. Run it with KiCad's Python and rerun it after
  any placement change.
- `export_wheelstone.py`: STL output.
- `render_wheelstone.py`: renders.
- `section_wheelstone.py`: radial sections with the board drawn in.

![sections](renders/mao-wheelstone-sections.png)

**Print files (`stl/`).** The STL frame is +x right, +y towards 12 o'clock, z up.

| File | Volume | Print |
|---|---:|---|
| `mao-wheelstone-base.stl` | 10.6 cm³ | Mark side down; supports under the round-over and in the cable tunnel. The guide bores want SLA or MJF precision (Ø3.3 for a Ø3 spacer); on FDM, ream them. |
| `mao-wheelstone-frame.stl` | 4.5 cm³ | Deck down. |
| `mao-wheelstone-wheel.stl` | 10.5 cm³ | Underside down; supports under the 0.75 mm step to the skirt (hidden). |
| `mao-wheelstone-bezel.stl` | 0.9 cm³ | Lip down. Snap tongues: MJF PA12 or PETG, not brittle resin. |
| `mao-wheelstone-carrier.stl` | 1.5 cm³ | Small and precise: SLA resin, posts up. |
| `mao-wheelstone-finger.dxf` | – | The finger's flat pattern for laser cutting: 1.5 mm 301 stainless, two Ø2.2 holes, the dimple's centre marked (0.3 mm, punched). `mao-wheelstone-finger.stl` is for fit checks only. |
| `mao-wheelstone-feel-model.stl` | 83.7 cm³ | One solid piece; print it first to judge size and grip. |

Bought parts: 3 × M2 screws with 3 × Ø3 brass spacers (the guide; length set at the fit print so the top sits on
the dome with no play) and 3 × M2 heat-set inserts in the frame, plus 2 × M2 screws and 2 × inserts for the finger.

JLCPCB covers all of these: MJF PA12 for the bezel and SLA resin for the carrier from its 3D-printing service, and
the finger from its sheet-metal service, in keeping with the one-supplier sourcing goal. The window is a laser-cut
PMMA disc.

## Open

1. **Wheel feel:** the wheel spins freely on PETG-on-PETG. Grease, or a TPU drag ring in the deck, would give damping; it
   is not modelled.
2. **Antenna:** a solid wheel puts more plastic over the antenna at 6 o'clock than the puck did. Do the RSSI A/B check at
   bring-up.
3. **IR windows:** the LEDs at 11 and 1 o'clock fire through Ø3 holes in the base and frame. Check the range against the
   puck.
4. **Snap force:** the 0.55 mm barbs on 3 mm-wide, 0.9 mm-thick tongues (3.75 mm free length) are sized by eye. The fit print decides.
5. **Fit print:** the checks are numeric only. Print the feel model first, then a full set.
6. **Press feel (P3):** check that the top slides on its three spacers without rocking or binding, wherever the
   stone is pressed. Set the spacers so the top rests on the dome with no play. Confirm with ALPS that the ~6 N at the
   stop is within the SKQGADE010's static rating, or shorten the travel.
