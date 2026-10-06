# MAO_MAIN A1 firmware

Firmware for the MAO_MAIN A1 board (ESP32-S3-MINI-1-N8) next to the
ESP32-C3-LCDkit, from one tree. The tree is the M4.1 firmware
(`feat/mao-m5-rev-a`: M3 link security and relationships, M3.2 multi-device,
the M4.0 ODD contract, the M4.1 character, UI, motion and sleep ladder) with
the MAO_MAIN A0 board firmware (`feat/mao-main-a0`) ported onto it and
adapted to the A1 hardware. Where the two disagreed, M4.1 won.

Hardware facts come from `hardware/mao/design/pinmap.py` through the
generated `components/mao_board/boards/main_a1/mao_board_pins.h` (never
edited by hand). Anything that could not be checked without a board is
marked `VERIFY AT BRING-UP` in the source and listed in §9.

## 1. Board profiles and building

| | ESP32-C3-LCDkit | MAO_MAIN A1 |
|---|---|---|
| Target | `esp32c3` | `esp32s3` |
| Kconfig | `CONFIG_MAO_BOARD_LCDKIT` | `CONFIG_MAO_BOARD_MAIN_A1` (default for esp32s3) |
| Sources | `components/mao_board/boards/lcdkit/` | `components/mao_board/boards/main_a1/` |
| Target defaults | `sdkconfig.defaults.esp32c3`: 4 MB, 160 MHz | `sdkconfig.defaults.esp32s3`: 8 MB, **no PSRAM** (`CONFIG_SPIRAM=n`), 240 MHz |
| Partitions | `partitions.csv` | `partitions_8mb.csv` (2 x 3 MB OTA, 1.5 MB assets) |
| Component lock | `dependencies.lock.esp32c3` | `dependencies.lock.esp32s3` |

`sdkconfig.defaults` keeps every shared M4.1 line (including
`CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM=10` and
`CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP=n`); only flash size, partition
table and CPU clock moved to the per-target files. The C3 sdkconfig this
generates is byte-identical to the M4.1 one.

```powershell
.\tools\idf.ps1 build              # LCDkit dev      -> build/          (unchanged)
.\tools\idf.ps1 release build      # LCDkit release  -> build-release/  (unchanged)
.\tools\idf.ps1 factory build      # LCDkit factory  -> build-factory/
.\tools\idf.ps1 s3-dev build       # A1 dev          -> build-s3-dev/
.\tools\idf.ps1 s3-release build   # A1 release      -> build-s3-release/
.\tools\idf.ps1 s3-factory build   # A1 factory (self-test at boot) -> build-s3-factory/
```

`idf.ps1` passes `-DIDF_TARGET` on every call, so no `set-target` is needed
(it would reset that profile's sdkconfig). Factory station:
`tools/factory_test.py` (from the A0; probe-pad names provisional, §9).

## 2. What came from the A0

| A0 piece | In this tree |
|---|---|
| `mao_board` structure: Kconfig board choice, `mao_board_common.c` ("mao board" report), one directory per board, caps | kept; `boards/main_a0` became `boards/main_a1` (rewritten for the A1, §3) |
| Per-target sdkconfig / lock files, 8 MB table, `sdkconfig.factory`, factory script | kept (A1 has no PSRAM) |
| `mao_power` (gauge, charger, charge-temperature pause, events) | renamed **`mao_battery`**; its ACTIVE / IDLE / DROWSY state machine and drowsy loop dropped (§4) |
| `mao_haptics` (DRV2605L, vocabulary, NVS calibration) | kept; power hook replaced by `mao_haptics_suspend()` |
| `mao_sense` | kept for IMU + ToF; **new ICM-42670-P driver** replaces the LSM6DSOX; OPT3004, touch and PDM mic dropped |
| `mao_perception` (engine + glue) | engine unchanged (host-tested); glue fed by IMU, ToF, dial, press, USB, battery, IR |
| `mao_selftest` (boot check + factory test) | kept; A1 step list (§5) |
| `mao_ir` NEC TX + RX | kept; RX on GPIO36 (`MAO_PIN_IR_RX`), receiver supply AUX_PWR_EN |
| `mao_audio` tsk, test chirp, busy-until, board gain | kept, plus an I2S path (§3) |
| `mao_input` edge / direction / press counters | kept |
| `mao_display` rotation (default 90), rail control, test plate, refusing lock | kept (rail re-init on power-up; no expander-reset recovery: no expander) |
| `mao_ui_fault` (service screen) | kept; `skip_wordmark` / `resume` dropped for M4.1's `mao_ui_wake_boot` |
| `mao_app_percept.c` | ported onto the M4.1 character (§6) |
| `mao_state` fields | the A0's mood fields were for A0's own fiddling and room senses; neither exists here (§6), so only what the reactions need is kept, inside `mao_app_percept.c` |
| A0 `mao_app_power.c`, `mao_power_continuity` | dropped; M4.1's `mao_app_power.c` is the only power code; the useful continuity field (time asleep) folded into its RTC marker |
| `tests/host` | kept (§8) |
| AW9364 driver and tests, board-ID divider, TCA6408A expander and its reset recovery | dropped (not on the A1) |

## 3. What changed for the A1 (`boards/main_a1`)

| Area | A1 |
|---|---|
| Module | ESP32-S3-MINI-1-N8, no PSRAM; chip checked at `mao_board_init()` |
| Revision | NVS `mao`/`hw_rev`, written at manufacturing (`mao board rev A1`, dev console); `"A1"` when absent. The boot check's own record moved to `mao`/`chk_rev` |
| Rails / enables | plain GPIOs, each with a hardware pull-down (off from reset): LCD_PWR_EN 6, LCD_RST_N 7, AMP_SD 9, HAPTIC_EN 1, TOF_XSHUT 15, AUX_PWR_EN 38 (IR receiver), IR_TX 17, CHG_CE_N 18. Latched low as outputs at init (input+output, so the self-test reads them back). Held in light and deep sleep |
| Display | GC9A01 on SPI2 IO_MUX pads (SCLK 12, MOSI 11, CS 10), DC 13, TE 35 (input, unused yet; internal pull-down on while the panel rail is off, released when the rail comes up, held through deep sleep: design review N3); rail + reset sequence as on the A0; rotation `CONFIG_MAO_A1_LCD_ROTATION` (default 90), SPI clock `CONFIG_MAO_A1_LCD_PCLK_MHZ` (default 80) |
| Backlight | LEDC on GPIO8 into the analogue current sink: the LCDkit's LEDC code (30 kHz, 9 bit, RC_FAST, kept alive in light sleep, hardware fades), so the M4.1 3 % rest level and the fades work unchanged. No AW9364 |
| Dial / press | Hall A GPIO21 / B GPIO37, rest mask 00\|11, 2 transitions per detent, 30 per revolution; press GPIO14 (off every strap). HALL_FAST GPIO5 high while awake, low (low-power sampling) in sleep (`mao_input_sleep`) |
| Light-sleep wake | the knob (`mao_board_knob_wake_arm`: Hall A / B at the level they are not at, press low) plus, from the rest loop, USB_PRESENT_N at its other level |
| Deep-sleep wake | ext1 ANY_LOW: press GPIO14, IMU INT1 GPIO4, USB_PRESENT_N GPIO2 (plugging USB in; armed only while it idles high, i.e. on battery); ext0: Hall A GPIO21 at its other level. Design review 2026-10-06, finding 1: the VBUS divider drives a 2N7002 whose drain (100 k pull-up) is the RTC pad GPIO2; GPIO39 is a spare |
| I2C | I2C0 SDA 47 / SCL 48, 4.7 k, 400 kHz: ICM-42670-P 0x68, VL53L4CD 0x29, MAX17048 0x36, DRV2605L 0x5A; ToF and haptic powered for the boot probe and a scan |
| Charger | BQ25185: STAT1 33 / STAT2 34 (H/H done or no input, H/L charging, L/H recoverable fault, L/L latched fault: `mao_board_charger_status()`), /CE = CHG_CE_N (high pauses), USB_PRESENT_N 2 (VBUS divider -> 2N7002, low = present) |
| Audio | MAX98357A on I2S0 standard TX (BCLK 40, LRCLK 41, DIN 42), SD_MODE = AMP_SD |
| IR | TX GPIO17 (RMT, 38 kHz), RX GPIO36 (RMT, active low), receiver supply AUX_PWR_EN (1 ms settle) |
| IMU | ICM-42670-P; INT1 GPIO4 open drain, active low, latched, 100 k pull-up; **INT2 not wired** (wake-on-motion on INT1, everything else polled) |
| Not on the A1 | RGB LED, touch, microphone, ambient-light sensor, board-ID divider, I/O expander |

### Audio (`mao_audio`)

One engine, two paths, chosen at build time (`CONFIG_MAO_BOARD_LCDKIT`):

- **LCDkit (PDM)**: exactly M4.1: the stream idles on the -32768 floor,
  the LEDC line glide at rest, `mao_audio_debug_scale()`.
- **A1 (I2S)**: floor 0, a symmetric waveform (-a..+a, the same peak-to-peak
  as the PDM swing). The stream starts at silence; the first sound raises
  SD_MODE and waits 8 ms (MAX98357A turn-on 7-7.5 ms) before it plays;
  SD_MODE drops after 3 s of silence and always before the stream stops
  (suspend: SD low, park, channel disable; resume: channel enable at
  silence, SD rises with the next sound). The line-glide and duty-probe board
  calls are no-ops; `mao_audio_debug_scale()` returns ESP_ERR_NOT_SUPPORTED.

### ICM-42670-P driver (`mao_sense_imu.c`, from DS-000451 rev 1.0)

Soft reset (SIGNAL_PATH_RESET), WHO_AM_I 0x67, RC oscillator on (IDLE) and
MCLK_RDY before the MREG1 writes (ACCEL_WOM_X/Y/Z_THR, 98 mg), INT_CONFIG
0x04 (INT1 latched, open drain, active low), accel ±4 g (8192 LSB/g), 100 Hz
low-noise with the 53 Hz UI filter while awake; 25 Hz low-power on the
wake-up oscillator with 4x averaging at rest; wake-on-motion (compare with the
previous sample, second over-threshold sample, OR of the axes) routed to INT1
in every mode; INT_STATUS2 read with every sample (clears the latch) and
before a deep sleep; die temperature TEMP_DATA / 128 + 25 (the charge
limit's board thermometer). The part has no tap detector, so KNOCK never
happens on the A1; 6D orientation and free fall are derived from the
samples.

## 4. Power: one state machine

The M4.1 ladder (`mao_app/mao_power.c`, pure, `tests/power`; hardware in
`mao_app/mao_app_power.c`) is the only power state machine: ACTIVE ->
SLEEP_DISPLAY (light sleep, 3 % backlight) -> NIGHT; DEV deep sleep; the
USB-host stay-awake rule, the RTC watchdog breadcrumb and the light-rest
first-touch rules are untouched. Added around it, all no-ops on the LCDkit:

- **Rest of the peripherals**: while the chip rests, haptics are suspended,
  the IR receiver is unpowered, the sensors drop to their REST level (IMU
  low-power, ToF off), perception is told (`mao_perception_rest`).
- **Charge limit in light sleep**: on USB the light sleep wakes at least
  every 10 s and calls `mao_battery_poll()` (USB, STAT lines, charge limit,
  gauge when due), then sleeps on. A USB plug / unplug ends one sleep of the
  loop as housekeeping only (it does not wake MAO). With USB present MAO never
  deep-sleeps (`deep_rest` and the critical sleep fall back to the light rest,
  where the charge limit runs: the charger's own NTC window ends near 60 C,
  the cell is rated to 45 C).
- **Deep sleep** (DEV `deepsleep <s>`): wake sources are cleared first, then
  `mao_board_deep_sleep_hold(true)` arms the board's own (A1: press, dial,
  motion, USB plugged in; LCDkit: none, as before), then the timer. The sensors go to DEEP
  (INT1 released), the time asleep is kept in RTC memory next to the M4.1
  marker and handed to perception at the wake (LONG_ABSENCE spans sleep).
- **Critical battery**: `mao_battery` posts `MAO_EVENT_BATTERY_CRITICAL`
  after three readings on battery below `CONFIG_MAO_BATTERY_CRITICAL_MV`
  (3550 mV provisional, from Gate C) or at SOC <= 2 %. The app shows the
  sleeping frame and enters a deep sleep that only the press, plugging USB
  in (USB_PRESENT_N, ext1) or a recheck timer
  (`CONFIG_MAO_BATTERY_CRITICAL_RECHECK_MIN`, 60 min, kept as a fallback)
  ends; a recheck that still finds the cell critical on battery goes
  back to sleep before the screen lights. Not on USB; a FORGET or pairing
  gets up to 30 s. A cell that turns critical during light sleep is acted on
  from the rest loop.
- **Busy**: a running factory self-test keeps MAO awake (counted as a
  pending action).

`mao_battery` (was A0's `mao_power`): `mao_battery_*`, Kconfig
`MAO_BATTERY_*`; gauge polled every 30 s (5 s when low; ALRT is not wired,
the alert flags are read and cleared with each reading); charging from the
STAT lines (the A0's gauge-based estimate stays for boards without them);
charge pause at >= 43 C, resume at <= 40 C on /CE from the IMU die
temperature, fail-safe to enabled; `mao battery [charge off|on]` (dev).

## 5. Self-test and boot check (`mao_selftest`)

Boot check only on MAO hardware (the LCDkit gets none: no faults, nothing in
NVS). Fault bits: display, input (fatal), imu, proximity, fuel_gauge,
haptic, ir, audio, radio, nvs, battery, charger (latched charger fault). A
fatal fault shows the name and `SERVICE nn` (`mao_ui_fault`) instead of the
character and ignores input and percepts.

Factory steps on the A1: `i2c_bus`, `imu_id` (0x67), `imu_motion`, `tof_id`,
`tof_ranging`, `gauge_id`, `gauge_voltage`, **`vbus`**, **`charger_stat`**,
**`charge_pause`** (/CE high must stop a charge in progress; SKIP when the
cell is not charging), `haptic_id`, `haptic_cal`, `rail_lcd`, `rail_ir_rx`,
`pad_lcd`, `pad_irv`, `speaker` (by ear: no mic), `ir_loopback`, `radio`,
`nvs`; operator: `lcd`, `backlight`, **`backlight_sink`** (3 % evenness and
smooth fades), `dial`, `press`, `tof_near`. Removed with their hardware:
board_id, expander, expander_reset, als_*, rail_mic, pad_mic,
touch_baseline, mic_level, touch_zones.

## 6. Application (`mao_app`)

- `mao_app_percept.c` uses only M4.1 vocabulary: `NOTICE`, `ATTEND`,
  `mao_character_look(x, y, true)` (screen px). No "happy", no CONTENT, no
  DIZZY reaction, no yellow; being shaken or dropped is a NOTICE plus a
  haptic tremor. Controller first: a percept never moves the eyes within
  2.5 s of knob input, and only HOME reacts. Presence percepts (approach,
  pick-up, USB plug, IR, long absence, cover) count as input for the idle
  clock and wake MAO.
- **One fiddling detector**: M4.1's `mao_fiddle` (HOME's light).
  Perception's engine still computes its fiddling score (host-tested) but the
  glue never posts `FIDDLING_ESCALATION`.
- **Haptic feel (A0)**: one LRA tick per dial detent (thinned to one per
  90 ms when FAST, none when VERY_FAST) and one click per face press,
  including the press that only wakes MAO.
- Perception does not run on the LCDkit (no sensors): on the dial alone it
  would only have invented LONG_ABSENCE.

## 7. API changes

```c
/* mao_board.h */
caps: audio_pdm_line, deep_wake_knob   (gone: expander, mic, touch, als)
esp_err_t mao_board_charger_status(mao_charger_status_t *out);
const char *mao_board_charger_status_name(mao_charger_status_t s);
esp_err_t mao_board_light_sleep_prepare(const mao_board_wake_t *want, mao_board_wake_t *armed); /* holds pads (A1) */
void mao_board_wake_decode(mao_board_wake_t *out);   /* deep: ext0/ext1 status; light: the levels noted before */
mao_board_wake_t: press, dial, motion, usb, proximity   (gone: expander)
MAO_LINE_USB_PRESENT, MAO_LINE_CHARGING              (gone: MAO_LINE_SENSE_ALERT)
MAO_IRQ_IMU_INT1, MAO_IRQ_TOF, MAO_IRQ_USB_PRESENT    (gone: IMU_INT2, EXPANDER; USB is active low on the A1: USB_PRESENT_N)
MAO_RAIL_DISPLAY/AMP/HAPTIC/TOF/IR_RX                 (gone: MIC)
gone: mao_board_revision_mv, _display_sleep (M4.1's SLPIN/SLPOUT path is the
      one), _display_reinit, _expander_*, _touch_*, _mic_init
kept from M4.1: backlight_fade, knob_wake_arm, switch_down, audio_hold,
      audio_line_*, audio_duty_permille, deep_sleep_hold

/* mao_system.h: ONE devcmd signature (M4.1's) */
typedef void (*mao_devcmd_handler_t)(char *arg);       /* arg NULL if none */
esp_err_t mao_devcmd_register(const char *name, mao_devcmd_handler_t fn);
esp_err_t mao_system_report_optional(const char *name, esp_err_t err);
esp_err_t mao_system_report_status(const char *name);
/* events: USB_CONNECTED, USB_DISCONNECTED, CHARGING_STARTED, CHARGING_DONE,
 * BATTERY_LOW, BATTERY_CRITICAL, IR_RECEIVED, PERCEPT (appended) */
/* settings: display_rotation ("rot"), check_revision ("chk_rev"), hw_faults ("hw_flt") */

/* mao_battery.h (was mao_power.h of the A0) */
mao_battery_init / _available / _get_status / _usb_present / _poll / _refresh /
_gauge_version_get / _set_temp_source
mao_battery_policy.h: mao_battery_charge_pause, _charging_estimate, _critical_reading

/* mao_sense.h */
MAO_OBS_IMU, MAO_OBS_TOF only; mao_sense_set_level(AWAKE|REST|DEEP|OFF)

/* others */
mao_haptics_suspend(bool); mao_ir_suspend(bool);
mao_perception_rest(bool); mao_perception_deep_sleep(); mao_perception_woke(uint64_t slept_ms);
mao_display_ok / _set_rotation / _get_rotation / _test_show / _test_clear / _rail
mao_ui_fault(const char *code);
mao_audio_tsk / _test_chirp / _busy_until_ms
mao_input_stats_t: edges_a, edges_b, detents_cw, detents_ccw, presses
```

Dev commands registered by components (dev builds): `board [rev X]`,
`rotate`, `battery [charge off|on]`, `sense`, `haptic`, `ir`, `percept`,
`selftest`, plus M4.1's `quirk`, `deepsleep`, `power`, `rellayout`, `link`,
`rel`. The A0's `power` became `battery` (M4.1 owns `power`); A0's `sleep`
and `backlight` (AW9364) are gone.

## 8. The LCDkit

Behaviour is M4.1's. What differs, and why it does not change behaviour:

- A1-only components (`mao_battery`, `mao_sense`, `mao_haptics`,
  `mao_perception`, `mao_selftest`, IR NEC start) return
  ESP_ERR_NOT_SUPPORTED at compile time on the LCDkit, so their code and RAM
  are not in its image; the image is 12 KB larger than M4.1's (board common
  code, the rotate / board dev commands, stubs).
- Boot log: `[--] haptics / battery / sense / perception / self-test not
  fitted`. The health line lists the M4.1 tasks exactly as before.
- Rotation 0 is not applied (the panel keeps its own state), the event
  queue keeps 16 entries, the audio PDM path is compiled as before.
- `deep_rest` clears the wake sources before `mao_board_deep_sleep_hold()`
  (on the LCDkit the hold arms nothing, so the order makes no difference).

Tests (all green, `CC` = zig cc): `tests/{accent, character_harness
(identical to expected.txt), fiddle, hold, power, quirks, relationships,
odd_contract, link_security}` and `tests/host/check.sh` (perception engine
with three A1 scenarios, battery policy; 188 checks; `run.sh` / make adds
the sanitizers).

## 9. Verify at bring-up

- **Display**: rotation (90; 270 is the other candidate: `mao rotate 270`,
  `mao rotate save`), colour order / inversion / mirror flags, 80 MHz SPI
  (else `CONFIG_MAO_A1_LCD_PCLK_MHZ` 40), TE unused.
- **Backlight sink**: polarity (more duty = brighter), LED current at 100 %
  and 3 %, evenness at 3 %, no audible whine at 30 kHz, keep-alive in light
  sleep on the S3.
- **Dial**: direction (`A1_DIAL_REVERSE`), Hall wake latency at
  HALL_FAST low, ext0 dial wake from deep sleep.
- **Audio**: gain (`A1_AUDIO_GAIN` 0.58, by ear), MAX98357A turn-on (8 ms)
  without a pop, SD_MODE off after 3 s, no click at suspend / resume.
  `mao_board_audio_gain()` never returns more than `A1_AUDIO_GAIN_MAX`
  0.70 (design review N5): with GAIN_SLOT open a full-scale sine is
  2.1 dBV + 9 dB = 3.59 Vrms, and on USB power (VSYS 4.5 V, ~3.0 Vrms before
  clipping) only the gain limits the speaker: 0.70 x 3.59 = 2.51 Vrms =
  0.79 W into 8 ohm, under the CMS-150803-088S-X8's 0.8 W (0.58: 2.08 Vrms,
  0.54 W). Tune by ear below the ceiling; on battery the amplifier clips
  lower.
- **IMU**: WHO_AM_I, axis sign at rest (`MAO_PERCEPT_IMU_Z_DOWN`),
  wake-on-motion threshold (98 mg) against desk bumps, INT1 release before
  deep sleep, motion wake from deep sleep, free-fall / 6D thresholds at
  10 Hz, die-temperature offset vs a thermocouple.
- **Charger**: STAT1 / STAT2 truth table on the fitted BQ25185, /CE pause
  (`mao battery charge off`, `charge_pause` step), 43 / 40 C pause / resume
  by warming the board, USB_PRESENT_N edges (any-edge ISR, the light-sleep
  wake, and the deep-sleep wake when USB is plugged in on battery: from the
  DEV `deepsleep` and from the critical sleep).
- **Battery**: the 3550 mV critical floor against the measured discharge
  curve under load, the critical sleep's press and USB wakes and 60 min
  recheck, gauge RCOMP for the cell.
- **Sleep**: light-sleep current with every pad held, the panel staying
  powered and showing the rest frame through light sleep, deep-sleep current
  (rails held off, HALL_FAST low, LCD_TE pulled down and held, USB_PRESENT_N
  at 0 uA on battery), the RC slow clock's accuracy for "time asleep".
- **IR**: receiver off-level with AUX_PWR_EN low, 1 ms settle, loopback.
- **Self-test**: probe-pad names (TP13 / TP15 kept from the A0 until the A1
  board names them) and limits; `factory_test.py --reset` on the A1.
- **Revision**: write `hw_rev` at manufacturing (`mao board rev A1`).

## 10. Open items

- A deep-sleep rung after NIGHT: on the A1 the knob can wake the chip from
  deep sleep, so the ladder could add one; not done (the M4.1 ladder and its
  tests are unchanged).
- The ToF approach-threshold mode is kept in the driver but not used at rest
  (the sensor is off while MAO rests).
- The A0 had touch, a mic and a light sensor; the perception engine still
  contains (and tests) those paths; they are never fed on the A1.
- On battery, light sleep looks at the gauge only at the 10-minute
  heartbeat, so a critical cell is acted on after up to three heartbeats.
