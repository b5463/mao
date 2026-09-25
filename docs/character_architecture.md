# MAO character architecture

The character (`components/mao_character`) is a presentation layer. It
reports what the controller is doing; it never decides, gates or delays it.

## Pipeline

One LVGL timer (`TICK_MS` = 25 ms) runs `tick_cb()` in `mao_character.c`.
Every tick executes the same stages in the same order:

| # | Stage | Where | What it does |
|---|---|---|---|
| 1 | INTAKE | `mao_character.c` `apply()` | Drain the command queue (public API calls from other tasks), dev dial injection |
| 2 | BASE / dial physics | `mao_character_dial.c` | Detents -> smoothed speed, acceleration, reversal disturbance -> gaze, face, tilt, lean, stretch, orbit targets |
| 3 | REACTION | `mao_character_react.c` | Timed ends of notice / attend / warm / leave; view choreography (appear, leave, return, peek, sleepy) |
| 4 | Transfer | `mao_character_transfer.c` | Exit / enter / failed-escape choreography; owns the pose while active |
| 5 | State | `mao_character_react.c` `update_state()` | Derive the reported `mao_character_state_t` (IDLE, FOLLOW, AWAY, EXIT, ...) |
| 6 | ATTENTION | `mao_character_attention.c` | Play queued controller feedback; mix the Lark expression layer (`mao_lark*.c`) and the mind's layer (`mao_life.c`), weighted by priority |
| 7 | MOTION | `mao_character_motion.c` | Step every spring channel, compose the pseudo-3D head pose |
| 8 | RENDER | `mao_character_draw.c` | Write the pose into LVGL objects (dirty-checked; no per-frame heap) |

Priority (`mao_char_current_prio()`), highest first: NAV (transfer, away,
view change) > PRESS > DIAL > SYSTEM reaction > IDLE. It decides which
layer owns the face; the expression layer is faint while the user is in
charge and at full gain only in idle or while controller feedback plays.

## State ownership

All character state lives in one `mao_char_t` (`mao_character_internal.h`),
owned by `mao_character.c` and passed explicitly to every stage. There are no
other mutable globals in the core. The context is zero-initialised (`.bss`);
`mao_character_create()` sets the few non-zero defaults.

Only `include/mao_character.h` is public. Every public function except the
name/count getters posts a command to the queue, so all state changes happen
on the LVGL task.

## Boundaries (M2.6 freeze)

- No ODD BUS, device or transport knowledge in the character. The app maps
  controller outcomes to `mao_character_react()` (ACK, BUSY, DONE, FAIL, ...).
- The character is never in front of ACTION transmission: `mao_app` sends
  first, then tells the character.
- Capture/tool microstates (REST, PRESS, PENDING, DONE, BUSY, FAILED) belong
  to the UI (`mao_ui_device_feedback()`), not the character.
- Transfer protocol state lives in `mao_app_transfer.c`; the character only
  plays the choreography it is told to.
- Audio stays in `mao_audio`. The only calls from the character are the
  transfer choreography's fire-and-forget cues (`mao_audio_depart()`,
  `mao_audio_bump()`), timed to the motion.

## DEVICE page input grammar (frozen)

| Gesture | Effect |
|---|---|
| Turn | Move focus across centre, words, CONNECT; while editing a LEVEL, change the value |
| Single press | Activate the focused word: toggle, action, LEVEL edit on/off, or CONNECT |
| Long press | BACK to DEVICES |
| Double press | Nothing |
| Press < 350 ms after the page opens | Ignored (the second tap of the opening double tap) |

After a secondary action resolves and the dial has been quiet for 1.5 s,
focus returns to the primary control (CAPTURE, or the LEVEL value).

## Verifying a refactor

`tests/character_harness/` builds the whole component on the PC against
recording LVGL / FreeRTOS / ESP stubs and a seeded RNG, drives 12 scenarios
on a simulated clock, and hashes every frame (object order, geometry,
colours, opacity, custom draw primitives, log lines). `check.sh` rebuilds
and diffs against `expected.txt`. A behaviour-preserving change must keep
every hash. The hashes depend on the compiler and libm, so compare builds made
by the same toolchain (the baseline uses `zig cc` 0.16, `-O1`).
