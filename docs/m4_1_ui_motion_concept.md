# M4.1 UI / motion concept (for review, before any redesign code)

Deliverables §88 (architecture audit), §110 (surface concept), §111
(transition map) and §112 (motion grammar). Written from the code at
`42616cb` and from screenshots of the real 240 × 240 display. Nothing here is
implemented yet. The decisions marked **D1–D6** need your answer first.

---

## 0. What exists (audit, §88)

**Good foundation, keep it:**

* One spring engine (`mao_spring.h`, SOFT / SNAP / HEAVY) drives every UI and
  character channel on a shared tick. Every motion is already interruptible
  and retargets instantly; nothing is a fixed-duration tween.
* LVGL objects are created once and only moved or faded, dirty-checked
  (`mao_ui_text_place`). There is no per-frame object churn and no heap use
  per frame.
* The app owns state and the UI renders models (`mao_ui_device_t`,
  `mao_ui_devices_t`). Animation never drives business state.
* The character pipeline is layered: BASE (dial physics), REACTION, transfer,
  ATTENTION (controller feedback + expression + mind), MOTION, RENDER, with
  a priority of NAV > PRESS > DIAL > SYSTEM > IDLE. Reactions do not reset
  the base.
* The pseudo-3D head exists (sphere yaw/pitch, foreshortening, lids drawn in
  the background colour, pupils, two catchlights).
* The audio stream never stops (PDM floor, soft envelopes). Nine cues: tick,
  touch, release, warm, notice, confirm, back, bump, depart.

**What prevents one cohesive system:**

| # | Finding | Effect |
|---|---|---|
| A1 | Layout is per-surface constants (`PANEL_*_Y`, `LIST_*`, `MENU_*`), no circle-aware geometry | long words collide with the rim, and NOT READY collides with STORAGE (both on one line at x −58 / +50) |
| A2 | Focus is opacity only (255 vs 110) | readable in a screenshot, weak on the panel, and nothing moves when focus moves |
| A3 | Presence/show-delay logic is duplicated in menu, list and panel; the choreography lives in scattered delays (`MENU_ENTER_DELAY`, `DEVICES_ENTER_DELAY`, `HOME_RETURN_DELAY`) | transitions cannot be reasoned about as a map |
| A4 | The UI cannot tell the eyes where to look. The character has gaze channels, but no "attend (x, y)" input from the UI | "attention is the hierarchy" is impossible today |
| A5 | Character presence is binary: HOME, or gone. It leaves on MENU, DEVICES and device pages (a compact "peek" lost a hardware comparison, because it collided with the value) | utility screens are characterless, and the eyes reappear only for transfer / failure |
| A6 | Tool feedback exists only as the centre word's offset (press 3 px, pending 1.5 px, done overshoot, failed lateral) | CAPTURE has no landing, and colour events are unused on pages |
| A7 | Fonts are **Montserrat** 14 / 20 / 28 plus 48 for values, marked "development placeholder". **BIZ UDPGothic is not in the repository** | see D3 |
| A8 | The frame rate is 10–16 fps while active (from the `perf 10s` logs) | smoothness budget, to be measured per phase (§93) |

**Conflicts with the M4.1 brief (need a decision, not a silent fix):**

* **C1 Grammar.** On HOME a *double* press opens a MENU and a single press
  only squashes the eyes. The brief's grammar says double press has no
  action. → D1.
* **C2 MENU.** The menu holds DEVICES plus three placeholders (ACTIONS,
  TOOLS, SETUP → "NOT YET"): the most developer-looking surface left. → D2.
* **C3 Mouth.** A tiny "o" mouth ring appears when MAO leaves HOME (§6: eyes
  only). I would remove it.
* **C4 Direct anime references.** "Cat mode" (`mao_lark_states_cat.c`)
  implements Maomao's cat-ear gag: almond eyes and slit pupils, no ears
  drawn. There is also a "poison-greed gold" pupil tint. §5 says take the
  temperament only, never her expressions. → D4.
* **C5 Transfer failure** plays three escalating attempts, about 3.5 s. §79
  allows one. I would reduce it to one push, a rebound, a skeptical look and a
  settle (about 1.4 s).

---

## 1. Decisions

**Answered 2026-09-28:** D1 single press → DEVICES, MENU removed ·
D3 BIZ UDPGothic · D4 **keep cat mode as MAO's own rare state, drop the gold
tint** · D5 UNREADABLE · D6 keep the palette (default).


| # | Question | My recommendation |
|---|---|---|
| **D1** | HOME entry gesture | **Single press on HOME → DEVICES.** Double press does nothing anywhere. Long press on HOME stays the WARM acknowledgement. This matches the grammar used everywhere else |
| **D2** | The MENU layer | **Remove it for now.** HOME ↔ DEVICES directly; ACTIONS / TOOLS / SETUP return when they exist. The menu code stays in the tree, unused |
| **D3** | Typeface | **BIZ UDPGothic** (SIL OFL). I need to download it from Google Fonts and convert an uppercase + digits subset with `lv_font_conv` (network use). The alternative is to stay on Montserrat, whose geometric roundness reads as "embedded default" |
| **D4** | Cat mode, gold tint | **Remove both** (they copy a recognisable character gag). Keep the underlying behaviours (rare intense interest, locked tracking), expressed with MAO's normal eyes |
| **D5** | The INVALID word | **UNREADABLE**: true (MAO cannot read what the device says it is) and distinct from INCOMPATIBLE and OFFLINE. The alternatives, UNSUPPORTED and UNAVAILABLE, collide with those two |
| **D6** | Iris palette (blue iris, cyan lower catchlight, from the `docs/design` studies) | **Keep.** It is MAO's identity, not the reference's. Refine volume and lids only (§7) |

---

## 2. Motion grammar (§112): six rules, nothing else

1. **ANCHOR.** The thing you chose survives into the next state. A device
   name becomes the page title, INFO becomes the sheet heading, and CONNECT
   becomes the exit edge. Nothing enters from nowhere if something on screen
   caused it.
2. **ATTEND.** MAO's gaze leads a change by one beat (≈ 120 ms): it looks
   toward where something will appear, or after what is leaving. Gaze is
   composition, not a pointer; it lands near, not on, a word.
3. **YIELD.** When utility needs the screen, the eyes step down *below the
   rim*: partly clipped by the circle, still alive. They never shrink into a
   corner. They come back the way they left.
4. **CARRY.** Energy from the dial is carried by the thing it moves (the
   focus line stretches, a value drifts with the turn), then settles. There
   is never free inertial scrolling, and fast turns compress instead of
   running on.
5. **LAND.** An outcome finishes in a stable place, with at most one colour
   event and one sound *at the same instant*. Colour arrives, does its job
   in ≤ 300 ms, and leaves.
6. **RECEDE.** Something unavailable loses presence: opacity, wider tracking
   (distance), position toward the rim. It never turns grey-boxed or red.

**One spatial rule.** The world of devices is *to the right of* MAO. The
transfer already exits right. Changing context moves sideways (HOME ↔
DEVICES, MAO ↔ a device); choosing inside a context moves vertically (the
list, the focus line, values).

**Timing vocabulary (§91).** Existing spring profiles, named by role. The
milliseconds are ≈ 90 % settle times, tuned on the panel.

| Token | Spring | ≈ | Used for |
|---|---|---|---|
| TAP | k 700 ζ 0.55 | 120 ms | press compression, word feedback |
| SELECT | k 260 ζ 0.78 | 200 ms | focus line, list position |
| PAGE | HEAVY k 190 ζ 0.86 | 300 ms | surfaces entering / leaving, anchors travelling |
| SHEET | k 320 ζ 0.95 | 220 ms | sheets (no overshoot: text never wobbles) |
| LAND | SNAP k 520 ζ 0.42 | 150 ms + overshoot | outcomes landing |
| SETTLE | SOFT k 110 ζ 0.62 | 450 ms | idle, presence returning, magnetism |
| BEAT | 120 ms delay | — | the one hand-off delay (gaze → arrival, clear → enter) |

Springs are for things with mass: the character, the focus line, dial-driven
values, the transfer. Text presence uses PAGE and SHEET, which are
critically damped, so type never bounces.

---

## 3. Surfaces (§110)

Geometry: circle r = 120. The chord width at height y is
2·√(120² − y²): 240 px at the centre, 167 px at ±86, 108 px at ±107. A
small layout helper (`mao_ui_layout.h`) will place and fit words to the chord
of their row (A1).

**The focus line (A2).** One 2 px off-white line under the focused word, as
wide as the word. It is the only focus indicator on every surface (list,
page, sheet, SAS). It travels on SELECT. Its leading edge is stiffer than its
trailing edge, so a fast turn *stretches* it toward the destination and it
compresses on arrival (CARRY). Neighbours stay at context opacity. One LVGL
object.

### HOME: character dominant
* **Composition:** only the eyes, centred slightly above the optical centre
  (−6 px). Nothing permanent: no time, no count, no status.
* **Presence:** stillness is a state. Idle becomes long holds (4–12 s),
  micro gaze drift, a rare inspection (look at an edge, squint, return), and
  a very rare long-interest event. The existing life / Lark layers are
  pruned (D4) and slowed. Variation comes from recency: time since the last
  input, the last device event and the last strong reaction. There is no
  random mood timer (§29).
* **Edge attention:** a device appearing or returning makes MAO glance toward
  the right rim (the world's side) and back. No text.
* **Wake:** the lids lift from half to rest while the gaze arrives from where
  the input came. About 250 ms, silent, and never the boot splash.
* **Boot:** the existing "MAO" wordmark contracting into the eyes stays.
* **Press:** the existing squash plus touch/release sounds. **Single press
  (D1): the eyes yield (below).**
* **Colour:** none at rest. Yellow only for WARM (long press), as today.

### DEVICES: balanced
* **Composition:** names on the existing gentle vertical arc, in stable pair
  order. The selected name is large, with the focus line; neighbours are
  small at context opacity. The **eyes sit below the lower rim**, about 55 %
  visible, looking up toward the selected name (YIELD + ATTEND). They follow
  the selection with a small lag and briefly lean in on a fast turn.
* **No title** ("DEVICES" is obvious) and **no status for normal devices.**
  A state word appears *only under the selected name, only when it is not
  normal*: NEW · OFFLINE · VERIFY · REPAIR · INCOMPATIBLE · UNREADABLE. An
  online, secure, compatible device carries no word at all.
* **Row states without words:** offline names RECEDE (lower opacity, +2
  tracking). NEW names are a step smaller. Everything else is identical.
* **Empty:** the eyes scan slowly left-right at the rim while looking. After
  6 s one quiet word, NOTHING NEARBY, appears.
* **Arrival:** a NEW device's name enters along the arc from the right edge
  (the eyes glance right a beat first); `notice` sound only for a genuinely
  new device. **Departure:** a discovered (unknown) name recedes out; a known
  device stays in place and only recedes. No sound. **Return:** it regains
  presence in place, with no celebration and no sound.
* **Scale:** designed for 2–3 names, and the arc handles 8 unchanged.

### Device page (common): utility dominant
The page is built around the *anchor*: the device name travels from its
list row to the top (y −82, small, tracked) and stays there. Rows, top to
bottom:

| y | Content |
|---|---|
| −82 | device name (anchor) |
| −28 | primary: CAPTURE (word, 30 px) or the LEVEL numeral (64 px) |
| +6 | condition, only when it matters: NOT READY / OFF / NO REPLY |
| +34 | quiet fact: STORAGE 74 |
| +62 | secondary words, small: IDENTIFY · SYNC TEST (LIGHT: POWER · IDENTIFY) |
| +90 | CONNECT · INFO on one line, quietest |

This stacks the condition and the fact on separate rows, which fixes the NOT
READY / STORAGE collision. Everything stays inside the chord of its row, and
the bottom rim is free for the eyes to peek (below). **Focus order is
unchanged**, only the positions move.

**Eyes on a device page:** withdrawn below the rim, invisible. They rise to
about 40 % *only* for the user's own outcomes that deserve it: the first
CAPTURE in a rhythm, a FAIL, a CONNECT. They glance at the primary, then
sink. Never during LEVEL editing, SAS or INFO.

**Focus return (magnetism):** the existing rule, made visible. The focus line
travels back to the primary on SETTLE, so it reads as settling, not a
timer.

### CAMERA
* **CAPTURE** is the one large word, at the optical centre. The dial and
  magnetism always come back to it.
* **Lifecycle** (the app's action states, unchanged):
  * *Anticipation* (press): the word compresses 3 px and the focus line
    thickens to 3 px (TAP).
  * *Execution* (SENDING/ACCEPTED): the focus line contracts to a short dash
    under the centre of the word, like an aperture, and holds. The word holds
    1.5 px low.
  * *Landing* (DONE): the line snaps back to full width in **yellow**, the
    word pops with a small overshoot (LAND), the STORAGE number steps down
    with its settle, and the `confirm` sound plays at this instant (not at
    the press). The yellow fades out within 250 ms.
  * *Return:* focus stays on CAPTURE.
  * *Rhythm:* in a fast series, only the first landing gets the eye peek.
    Later ones are line and sound only, as the existing streak logic does.
* **BUSY:** the line returns without colour and the word barely yields (as
  today). **FAILED:** the line returns red for 200 ms, the word misaligns
  laterally and settles, and the eyes peek with the existing wince.
  **UNKNOWN:** the line returns dim, no colour.
* **NOT READY:** CAPTURE drops to secondary opacity, and NOT READY sits
  directly under it as the reason. The page is otherwise unchanged, with no
  red and no banner. A press still flows through (the device answers BUSY).
* **Offline mid-page:** see OFFLINE.

### LIGHT
* **LEVEL:** a large numeral (64 px, a digits-only subset). It is the primary
  and the dial's home.
* **Editing:** click to enter. The focus line moves under the numeral and
  every other word recedes to ~40 % (the page yields to the value). The
  numeral *carries* the turn: it drifts up to 6 px in the turn's direction
  with speed, tracking tightens slightly on fast turns, and it re-centres on
  SETTLE when the dial stops. The number shown is always the user's intent
  (the optimistic value). Intermediate values of a fast turn are not
  animated one by one.
* **POWER:** a word control. OFF dims the numeral to 40 % and shows OFF in
  the condition row. ON restores it with a short upward settle. There is no
  switch graphic.
* **Colour:** none. A lamp's own light is the feedback.

### Sheets: INFO and FORGET, text dominant
* **Anchor:** the INFO word rises to become the sheet heading. The page
  recedes: it fades to 15 % and lifts 6 px, as if tilting away. BACK reverses
  exactly.
* **INFO** is the workshop underside, dense and small, tracked,
  left-aligned in a narrow central column:

  ```
  CAMERA 01
  CAMERA · PAIRED
  SECURE · CONTRACT 1.0
  0DD0 6055 F923 5324
  FORGET
  ```

  This is the one place protocol words are allowed (§74). FORGET is the only
  focusable word.
* **FORGET?** Two words, NO and YES, with NO focused. YES requires a turn;
  one click on YES confirms. No danger styling. On confirm, the name recedes
  out and the list closes over the gap.

### PAIR / SAS
* **Waiting** (the device must be in pair mode): the eyes rise, look right
  toward the device (ATTEND), and one word, PAIRING, appears. The existing
  failure notes are kept (NOT PAIRING, TIMEOUT...).
* **SAS:** six digits in two groups, `890  331`, in the numeral face at
  44 px with a wide gap. They are the only large thing on screen. The code
  arrives *once*: both groups rise 8 px into place together on PAGE. No
  rolling digits, no per-digit animation. The eyes withdraw completely.
* **CANCEL | MATCH**, left and right, with the focus line under CANCEL by
  default. Colour is never used to mean "safe" or "go".
* **Success:** the digits contract toward the centre and dissolve, the page
  returns with its controls, and a **cobalt** line sweeps once under the
  device name (linked).
* **Failure / cancel:** the digits recede. On failure only, the line flashes
  red for 200 ms and the reason word shows.

### VERIFY / REPAIR / OFFLINE
* **VERIFY and REPAIR** use the same calm layout. The centre word is the
  action (VERIFY / REPAIR, 30 px), with one quiet line under it (REMEMBERED /
  NOT VERIFIED) and INFO below. No values, no facts, no red. REPAIR differs
  from a normal page by having no live controls, not by alarm.
* **OFFLINE is absence.** The name keeps its place, with wider tracking and
  lower presence (RECEDE). One word, OFFLINE, sits in the empty centre, with
  CONNECT · INFO below.
  * **Going offline while the page is open:** the controls drift down 8 px
    and fade on PAGE, and the name recedes.
  * **Coming back:** the controls return from the same place (RETURN).
  * **Mid-action:** the pending line relaxes to dim with no colour, which is
    the app's UNKNOWN / no-result outcome. Nothing freezes.

### LIMITED / INCOMPATIBLE / UNREADABLE
* **LIMITED with controls** looks exactly like a normal page, with no
  warning. INFO can explain the details.
* **LIMITED with no controls:** the centre word NO CONTROLS, quiet, with
  CONNECT · INFO below.
* **INCOMPATIBLE / UNREADABLE:** the name, one quiet centre word
  (INCOMPATIBLE / UNREADABLE), and INFO. No CONNECT, no red, never REPAIR.
  INFO shows the contract details.

### CONNECT / AWAY: character and motion dominant
* **Press CONNECT:** the page leans 4 px toward the right edge (ANCHOR: the
  exit edge). The eyes rise from the rim and look right, a beat of stillness,
  a small back-step, then a committed, accelerating exit through the right
  edge with a stretch (the existing exit, kept). The page recedes behind
  them. `depart` sounds at the moment of commitment.
* **AWAY:** the existing near-black field and dim breathing seam at the exit
  edge. The emptiness is the state. The dial stirs the seam.
* **Return:** the eyes enter from the same edge and settle at the rim, and
  the page returns (RETURN).
* **Failure:** *one* push into the edge, a rebound, a skeptical second look
  at the edge, and a settle (C5). The `bump` sound plays once, and no red is
  used (unreachable is not a catastrophe).

---

## 4. Transition map (§111)

Duration classes: T tap · S select · P page · SH sheet · L land · X
transfer. "Interrupt" says what happens if input arrives mid-transition.

| From → To | Trigger | Anchor | Character | Class | Interrupt | Audio |
|---|---|---|---|---|---|---|
| HOME → DEVICES | press (D1) | — (names enter from right) | glance right, sink below rim (YIELD) | P + BEAT | retarget: BACK mid-way reverses | confirm |
| DEVICES → HOME | long press | selected name recedes right | rise from rim to centre | P | reverses | back |
| DEVICES → DEVICE | press | selected name → title | sink fully | P | BACK reverses from current pose | confirm |
| DEVICE → DEVICES | long press | title → its row | rise to rim | P | reverses | back |
| DEVICE → INFO | press INFO | INFO → sheet heading | none | SH | BACK reverses | tick |
| INFO → DEVICE | long press | heading → INFO | none | SH | — | back |
| INFO → FORGET? | press FORGET | FORGET → question | none | SH | — | tick |
| FORGET? → DEVICES | YES | name recedes out, list closes | rise to rim | P | entry guard only | confirm |
| focus move | turn | focus line | follows at low gain (DEVICES only) | S (stretch on speed) | always | tick, compressed at speed |
| focus return | quiet ≥ 1.5 s after result | line → primary | — | SETTLE | a turn cancels it | none |
| LEVEL edit on / off | press | line → numeral / back | stay withdrawn | S | always | tick |
| LEVEL change | turn while editing | numeral carries | — | S | always | tick (compressed) |
| action press | press primary | the word | — | T | — | touch / release |
| action pending | SENDING / ACCEPTED | line → dash | — | T | — | none |
| action done | DONE | line → full, yellow | peek (first in rhythm) | L | — | confirm |
| action failed | FAILED | line red, word misaligns | peek + wince | L | — | back (dry) |
| action busy / unknown | BUSY / UNKNOWN | line returns dim | — | L | — | none |
| DEVICE → CONNECT → AWAY | press CONNECT | exit edge | look right, back-step, exit | X | committed exit completes; aborts during search | depart |
| AWAY → RETURN | press / result | same edge | enter, settle at rim | X | — | none |
| CONNECT fail | unreachable | exit edge | one push, rebound, look | X (~1.4 s) | press cancels | bump ×1 |
| NEW → PAIRING | press PAIR | PAIR → PAIRING word | rise, look right | P | BACK cancels ceremony | confirm |
| PAIRING → SAS | SAS ready | code rises into place | withdraw fully | P | BACK = cancel | none |
| SAS → success | both confirmed | digits contract into centre | — | L + cobalt sweep | — | confirm |
| SAS → fail / cancel | failure / CANCEL | digits recede | — | P (+ red 200 ms on failure) | — | back |
| VERIFY / REPAIR → ceremony | press | the word → PAIRING | as NEW → PAIRING | P | as above | confirm |
| DEVICE → OFFLINE | async | controls drift down, name recedes | — | P | page stays operable (CONNECT, INFO) | none |
| OFFLINE → return | async | controls return from below | — | P | — | none |
| DEVICE → INCOMPATIBLE / UNREADABLE | async | controls withdraw, word arrives | — | P | — | none |
| DESCRIBING → ready | async | controls rise into place once | — | P | — | none |
| device arrives (NEW) | async, on DEVICES | name enters from right | glance right | P | selection unchanged | notice |
| known device returns / re-keys | async | presence restored in place | none | SETTLE | — | **none** (F3) |
| background event, other page | async | nothing visible | none | — | — | none |
| wake | any input after sleep | — | lids lift, gaze arrives | SETTLE (≈ 250 ms) | the input is consumed as wake only if asleep | none |
| boot | power on | wordmark → eyes | open | existing | — | existing |

**Interruption policy (§98–99):**
* Every spring retargets; input is never queued behind motion.
* Navigation during a transition retargets to the new destination from the
  current pose. There is no backlog.
* **Non-interruptible windows:** the existing 350 ms entry guard, a
  committed transfer exit, and the ceremony, which owns the knob until it
  resolves (as today).
* A press during LAND does not wait: the next action starts, and the landing
  collapses to its end state.

---

## 5. Character changes (bounded)

* **Remove:** the "o" mouth (C3), cat mode and the gold tint (D4), the
  three-attempt bash (C5).
* **Add:** a UI → character **attend target** (`attend(x, y, weight)`,
  A4), used for the rim gaze on DEVICES and ATTEND beats. Add a **rim
  presence** mode: the existing peek, moved below the lower rim and
  scaled ~0.55 (A5).
* **Idle:** longer stillness and fewer small flourishes. Rare events are
  gated by time since the last strong reaction.
* **Background silence:** a known device that disappears and returns within
  a few seconds (a re-key blip) gets no glance and no `notice` (F3).
* **Unchanged:** priority order, the controller-first rule, action-outcome
  reactions, the transfer exit / return, and dial physics.

**Harness plan (§133):** the 36 recorded runs will change by design. Before
touching the baseline I will add explicit invariant scenarios:
* dial use never produces irritation states
* a reaction never resets base / gaze
* no reaction to background device blips
* priority order holds
* rare events stay bounded per hour
* no mouth or extra parts are ever drawn

The new baseline is recorded only after you approve the behaviour on
hardware, with the reason in the commit.

---

## 6. Typography proposal (§13, pending D3)

| Role | Size | Tracking | Where |
|---|---|---|---|
| FACT | 13 px caps | +4 | facts, conditions, INFO body, state words |
| NAME | 18 px caps | +3 | list neighbours, page title (anchor) |
| WORD | 30 px caps | +4 | selected name, CAPTURE, REPAIR, the primary word |
| NUMERAL | 64 px digits | −1 | LEVEL |
| CODE | 44 px digits | +2, group gap 22 px | SAS |

* One family, uppercase subset plus digits and `· % -`. The two numeral
  sizes are digits-only subsets.
* Emphasis comes from size, position, opacity and the focus line, not from
  bold.
* Rough flash cost: ~40–60 KB for all five subsets at 4 bpp, against about
  500 KB free.

---

## 7. Plan after approval (§114–115)

1. **Foundation:** layout helper, focus line, attend target, rim presence,
   timing tokens, and one transition table (A1–A5).
2. **HOME:** idle, wake, removals.
3. **DEVICES**, then **CAMERA**.
4. **→ First hardware look** (flash, photographs, your encoder).
5. LIGHT, navigation transitions, action feedback, CONNECT, PAIR / SAS,
   VERIFY / REPAIR / OFFLINE, LIMITED / INCOMPATIBLE / UNREADABLE, sheets,
   audio, character refinement, physical tuning.

Each phase measures frame time, event drops, heap and stack, and keeps the
security, relationship, link, contract and ODD suites green.

## 8. As built (M4.1 rework from use, 2026-09-28)

This section supersedes sections 3 and 4 where they differ. The user
rejected the type-led phase 1 and gave video references (see the memory
note on the visual direction). After a review from the user's side, the
surfaces were rebuilt as follows.

**Grammar, on every device page (control and relationship):**

| Input | Does |
|---|---|
| turn | the page's one continuous thing (a light's brightness); nothing elsewhere |
| press | the page's one thing: capture, light on / off, PAIR, VERIFY, REPAIR, CONNECT (offline) |
| long press, or turning while held | MAO's options: BACK in the middle, CONNECT to the right, FORGET to the left |
| release while the options are up | chooses (the middle = BACK) |
| double press | nothing |

A long press still means back: it goes back on release. So everyone who
goes back sees the options. The input layer cancels a press's CLICK and
LONG PRESS once the knob turned while it was held (`mao_input.c`). Helper:
`mao_app_hold()`.

**INFO is gone** (relationship layout A). Its details sheet only said
PAIRED and the type. FORGET leads straight to "FORGET <name>?".

**Two-answer questions start with the knob in the middle.** This covers
FORGET / KEEP and the pairing code's CANCEL / MATCH. Neither answer is a
press away, and a press in the middle only makes the answers lean in
("turn"). Before, CANCEL was focused on the code screen, so the natural
press-to-agree cancelled the pairing. The destructive or trusting answer
still needs a deliberate turn and a press. A long press is back, never a
decision. FORGET is on the left, the way the menu went.

**FORGET shows its consequence.** The device's mark breaks while FORGET is
chosen. When it happens, the mark comes apart and the NEW mark forms in its
place (or the page leaves, if the device is offline).

**PAIR:**
- While pairing runs there is nothing to press: the word goes, and a ring
  turns.
- A refusal says what is missing: NOT IN PAIRING MODE, or REFUSED ON THE
  DEVICE.
- The code screen asks "SAME ON <name>?".
- Waiting says "CONFIRM ON <name>" and keeps the code.
- Success lands like a capture: a wave to the rim, and the page becomes the
  device.

**Surfaces:**
- DEVICES: a carousel of device marks on the rim. The chosen one is at the
  top. The centre shows its name in large dot type and a live preview (a
  light as its own field at its brightness, or OFF). A state word appears
  only when the device is not normal.
- A device page grows out of that preview.
- LIGHT: the ODD field is the brightness. A large number shows while it
  changes. OFF is shown when the light is off.
- CAMERA: an iris made of the field's own marks (`odd_field_mark`). It
  looks like the same hand as the lamp, but moves as a shutter:
  - Material: the blade seams and the opening's lip are dense and heavy,
    and the faces are sparse.
  - Motion:
    - The finger narrows it.
    - The frame being taken shuts it (the seams meet in a star).
    - A frame taken lights the whole lens for an instant and snaps the
      iris open past rest.
    - Failure makes it flinch.
  - After a capture, the frame number shows large in the opening.
  - This visit's frames collect as a filmstrip on the lower rim.
  - The carousel preview is the same iris, small.
  - NOT NOW / NOT TAKEN appear when a capture does not work.
- NEW / VERIFY / REPAIR / OFFLINE / INCOMPATIBLE: the device's mark (an
  outline when away, calling when new), a state word, and the press word.
- Sheets (FORGET?, the pairing code, WAITING) are drawn in dots. The page
  collapses under them. Anything underway (pairing, waiting on the device)
  is one short row of dots lighting in sequence, in free space: it never
  covers another element.
- Hidden, because the device handles them itself: IDENTIFY, SYNC TEST,
  READY, STORAGE.
