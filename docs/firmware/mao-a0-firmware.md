> **SUPERSEDED** by [mao-a1-firmware.md](mao-a1-firmware.md): the A0 board firmware was ported onto the M4.1 tree for the MAO_MAIN A1 (2026-10-06). Kept for history; do not apply.

# MAO_MAIN A0 firmware

Firmware support for the MAO_MAIN A0 board (ESP32-S3-WROOM-1-N8R2) next to
the ESP32-C3-LCDkit, from one tree. Hardware facts come from
`hardware/mao/design/pinmap.py` (via the generated
`components/mao_board/boards/main_a0/mao_board_pins.h`) and the A0 design
brief. Everything that could not be checked without a board is a named
constant marked `VERIFY AT BRING-UP` in the source and listed in §13.

Stage 1 added the board profiles and the drivers (§1-§4). Stage 2 added
perception (§5), the application's reactions to it (§6), automatic power
states (§4.1), the boot experience (§7), the boot check and the factory
self-test (§8, with `docs/hardware/mao-factory-test.md`), and the display
rotation setting (§3.1).

The A0 board revision (AW9364 backlight, charger /CE, GPIO38/39 swap, IMU
INT pulls) is followed as described below; what changed and how to port it
onto another tree is in `docs/firmware/mao-a0-rev-merge.md`.

## 1. Board profiles

| | ESP32-C3-LCDkit | MAO_MAIN A0 |
|---|---|---|
| Target | `esp32c3` | `esp32s3` |
| Kconfig | `CONFIG_MAO_BOARD_LCDKIT` | `CONFIG_MAO_BOARD_MAIN_A0` |
| Sources | `components/mao_board/boards/lcdkit/` | `components/mao_board/boards/main_a0/` |
| Target defaults | `sdkconfig.defaults.esp32c3`: 4 MB, 160 MHz | `sdkconfig.defaults.esp32s3`: 8 MB QIO 80 MHz, PSRAM quad 80 MHz, 240 MHz |
| Partitions | `partitions.csv` | `partitions_8mb.csv`: 2 × 3 MB OTA, 1.5 MB assets, 384 KB reserve |
| Component lock | `dependencies.lock.esp32c3` | `dependencies.lock.esp32s3` |

- `menu "MAO board"` (`components/mao_board/Kconfig`) is a choice; each
  option depends on its target and is the default there. `MAO_BOARD_NAME`
  is a hidden Kconfig string that follows the choice.
- `components/mao_board/CMakeLists.txt` compiles `mao_board_common.c` plus
  the selected board's files. The requirement list is the union of both
  boards, because IDF expands requirements before Kconfig is evaluated.
- `mao_board_init()` checks the chip model per board (`CHIP_ESP32C3` /
  `CHIP_ESP32S3`). `mao_system` prints the chip from `esp_chip_info()`.
- `mao_board.h` is board-agnostic. A call for hardware the board does not
  have returns `ESP_ERR_NOT_SUPPORTED` and touches no pin.
  `mao_board_get_caps()` says what exists. On the LCDkit everything new is
  "not fitted", so it behaves as in M2. The only visible difference is extra
  `[--] … not fitted` boot lines.

### Building

Four configurations (board × profile), each in its own build directory;
the factory profile is the dev profile plus the self-test at boot:

```sh
# in espressif/idf:v6.0.3 (Docker) or any IDF v6.0.3 shell, from the repo root
idf.py -B build-s3-dev -D SDKCONFIG=build-s3-dev/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.dev" set-target esp32s3 build
#   s3 = A0 (esp32s3), c3 = LCDkit (esp32c3); profile file sdkconfig.dev | sdkconfig.release | sdkconfig.factory
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.3 \
       idf.py -B build-c3-release -D SDKCONFIG=build-c3-release/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" set-target esp32c3 build
```

Windows: `.\tools\idf.ps1 [dev|release|factory] build` (§1 of the README).
Host unit tests (no IDF): `tests/host/run.sh` (§11).

## 2. A0 HAL (`mao_board.h`)

| Area | API | A0 implementation |
|---|---|---|
| Identity | `mao_board_init`, `_get_caps`, `_revision`, `_revision_mv` | chip check, release sleep holds, HALL_FAST high, mic off, IRQ inputs, board ID (ADC1_CH7 on GPIO8, one-shot, curve-fitting cal, 8 samples, 1.40–1.90 V = "A0"; 1M/1M of the 3.18 V rail = 1.59 V nominal), I2C + expander |
| I2C | `_i2c_bus`, `_i2c_device`, `_i2c_add`, `_i2c_scan`, `_i2c_name` | I2C0, SDA 7 / SCL 15, 400 kHz, internal pull-ups off. Every device is probed at boot; ToF and haptic rails are powered for the probe or scan only |
| Rails | `_rail_set`, `_rail_is_on`, `_rail_readback` (`DISPLAY`, `AMP`, `HAPTIC`, `TOF`, `IR_RX`, `MIC`) | expander P1/P2/P3/P4/P5; MIC = GPIO35 (input+output, so it can be read back). Readback = the pin level (expander input register / GPIO), not the commanded one |
| Diagnostics | `_expander_test` | CONFIG readback, POLARITY round trip 0x2A / 0x15 on the output pins (polarity only acts on inputs: no rail moves), output pins read back their commanded level |
| Expander reset | `_expander_resets`, `_expander_reset_test`, `_display_reinit` | GPIO39 = EXP_RST_N (open drain, R22 10 k pull-up, TP11): automatic recovery of a wedged expander and the self-test pulse; a reset counter tells the rail owners to re-initialise (below) |
| Lines | `_line_get` (`USB_PRESENT`, `CHARGING`, `SENSE_ALERT`) | GPIO3 low; `CHARGING` = `ESP_ERR_NOT_SUPPORTED` (the BQ24073 /CHG is not wired; `mao_power` estimates charging, §3.4); P7 low (reading the input register clears the expander INT) |
| Charger | `_charge_enable` (caps `charge_control`) | expander P6 = BQ24073 /CE: 0 = charging enabled (also the 100 k pull-down default), 1 = paused. Owned by the charge temperature limit (§3.4) |
| IRQs | `_irq_get` (`IMU_INT1/2`, `TOF`, `EXPANDER`, `USB_PRESENT`) | GPIO 14 / 47 / 48 / 21 / 3, all active low, already inputs. No internal pulls: the IMU drives push-pull, and an LSM6DSOX that sees INT1 high at its power-up locks itself into I3C-only mode |
| Display | `_display_init`, `_backlight_set`, `_display_sleep`, `_display_get_resolution` | §3.1; the descriptor carries the panel-native orientation and the mounting `rotation` |
| Input | `_input_init`, `_hall_fast_set` | Hall A 41 / B 42, press GPIO0 (input only), same decoder parameters as the EC11 |
| Touch | `_touch_get`, `_touch_zone_name` | RIGHT T1, LEFT T4, TOP T5, REAR T6; wake zone TOP |
| Audio | `_audio_init`, `_audio_gain`, `_mic_init` | §3.2 |
| IR | `_ir_get` | TX GPIO38 (no reset pull on that pad; 100 k gate pull-down: LEDs dark from reset), RX GPIO40 (active low), 38 kHz / 33 %, RX settle 1 ms |
| LED | `_led_init` | `ESP_ERR_NOT_SUPPORTED` (no LED on the A0) |
| Sleep | `_deep_sleep_prepare`, `_light_sleep_prepare`, `_light_sleep_done`, `_wake_decode` | §4 |

### TCA6408A expander (0x20)
The power-up state is all inputs with the output register at 0xFF. The
driver writes OUTPUT = 0x00 first, then POLARITY = 0x00, then CONFIG = 0x80
(P7 the only input; P0-P5 rails and resets, P6 the charger /CE driven 0 =
charging enabled), and reads CONFIG back. If CONFIG were written first, every
rail would glitch on and charging would pause. Writes are read-modify-write
on a shadow register under a mutex. A missing expander is reported
`[!!] I2C expander` and boot continues, but rails, display power and the
charge temperature limit are then unavailable (charging stays enabled by the
/CE pull-down).

| Bit | Net | Dir | Default (pull) |
|---|---|---|---|
| P0 | LCD_RST_N | out | 100 k down: panel in reset |
| P1 | LCD_PWR_EN | out | 100 k down: panel logic rail off |
| P2 | AMP_SD_N | out | 100 k down: amplifier shut down (2.2 k series into SD_MODE) |
| P3 | HAPTIC_EN | out | 100 k down |
| P4 | TOF_XSHUT | out | 100 k down |
| P5 | IR_RX_PWR | out | 100 k down |
| P6 | CHG_CE_N | out | 100 k down: charging enabled |
| P7 | SENSE_ALRT_N | in | 100 k up (gauge ALRT + light INT, wired-OR) |

**Recovery through GPIO39 (EXP_RST_N).** GPIO39 drives the expander's RESET
pin (open drain, released high from the first instruction of
`a0_i2c_init()`; R22 10 k pull-up holds it high through boot and deep
sleep; GPIO39 = MTCK has a reset pull-up, which agrees with it). The 4-layer board uses every module pin except the strap GPIO46
(see `mao-pin-map.md`). When an expander write or input read fails, the driver resets the I2C
bus (`i2c_master_bus_reset()`), pulses RESET low for 10 µs, reprograms the
expander from the shadow registers (output before direction, as at power-up)
and retries the access once. A wedged expander no longer needs a power
cycle.

A reset is not free: from the pulse until the output register is rewritten
(~0.2 ms) every expander pin is an input, so the rails fall to their
pull-down defaults. The panel sees LCD_RST_N low and a supply dip, the ToF
its XSHUT, the haptic driver its EN; each loses its configuration. A paused
charger may run for that moment (/CE pull-down); the shadow restores the
pause. Every
pulse increments `mao_board_expander_resets()`, and the owners re-initialise:

| Owner | Notices | Re-initialises |
|---|---|---|
| `mao_display` | LVGL timer, every 250 ms | `mao_board_display_reinit()` (reset wait, full GC9A01 init with the kept MADCTL, so the rotation survives), display on, everything invalidated, backlight restored; deferred to the wake-up if the panel is asleep |
| `mao_sense` | sense task, every loop | `sense_tof_recover()`: the VL53L4CD is booted again into the current mode |
| `mao_haptics` | haptics task, every loop | the DRV2605L is powered down cleanly; the next touch powers it up and configures it |

The amplifier enable and the IR receiver supply come back by themselves;
the IMU, light sensor, gauge and mic are not on the expander.
`i2c_master_bus_reset()` does not take the driver's bus lock: a transaction
another task has in flight at that moment fails once and is retried by its
owner. The self-test's `expander_reset` step uses
`mao_board_expander_reset_test()`: pulse, check CONFIG is back at its 0xFF
default (the line really reset the chip), reprogram, check CONFIG 0x80 and
every output pin. The expander test's POLARITY patterns are 0x6A / 0x15, so
every output bit P0-P6 is set and cleared once.

## 3. Drivers

### 3.1 Display: 1.28" round GC9A01 panel in the J301 FPC connector (240 × 240)
- The panel plugs in: an 18-pin 0.5 mm tail (Winstar WF0128BTYAA4DNN0 or any
  panel of that 18-pin standard) in the J301 back-flip connector, so it can be
  replaced without soldering.
- SPI2, 80 MHz by default (`CONFIG_MAO_A0_LCD_PCLK_MHZ`, 10–80, menuconfig "MAO
  board"; the signals run through the panel's stock 70 mm tail, so bring-up
  confirms 80 MHz and falls back to 40 if the picture glitches), mode 0,
  write-only: SCLK 12 and MOSI 11 on SPI2's IO_MUX pads,
  CS 13 through the GPIO matrix, DC 10 a plain GPIO; the module pins follow the
  connector's pin order (no crossing on the board). `unpark()` restores the same
  routing after a rail power cycle. TE (tearing effect) is GPIO9, an input with
  the internal pull-down; the flush does not use it yet.
- **Backlight: Awinic AW9364** constant-current sink, EN = `MAO_PIN_LCD_BL_CTRL`
  (GPIO45). The panel LEDs hang from VSYS (not from the switched 3V3_LCD rail)
  into two 20 mA channels: 40 mA, the panel's rating, is the hardware maximum,
  so there is no firmware cap any more (the old `A0_BACKLIGHT_MAX_PCT` 80 %
  PWM ceiling is gone). EN low is the only thing that darkens the LEDs, so
  every rail-off and sleep path sets 0 % first.
  - 1-wire pulse-count dimming (datasheet V2.3): the enable edge is edge 1 =
    20 mA per channel; edge n = (17 − n)/16 × 20 mA, 16 steps down to
    1.25 mA; ready time > 20 µs after the enable edge, then TLO 0.5–500 µs and
    THI > 0.5 µs per edge; the current holds while EN stays high; EN low longer
    than TOFF (0.8–2.5 ms) shuts it down.
  - `mao_board_backlight_set(percent)`: 0 = EN low; otherwise step
    n = 17 − ceil(16 × percent / 100), clamped 1..16 (per channel: 1 % →
    1.25 mA, 50 % → 10 mA, 94–100 % → 20 mA). The current is never below the
    request and less than one step above it.
  - Dimmer: (n_new − n_cur) more edges. Brighter: EN low ≥ 3 ms (shutdown,
    measured with `esp_timer`, sleeping for the bulk), then the enable edge
    and n − 1 edges. The datasheet does not say whether edge 17 wraps, so the
    driver never counts past 16; the price is a ~3 ms dark gap on every
    brighter step (VERIFY AT BRING-UP whether it is visible in a fade-in).
    Each pulse train (≤ 16 edges, 2 µs low / 2 µs high, 30 µs ready: under
    100 µs) runs in a critical section with `esp_rom_delay_us`, so no
    interrupt can stretch a low phase towards TOFF. A mutex serialises callers
    (LVGL task, power hook, app, self-test); an unchanged step sends nothing.
  - The percent → step math and the edge plan are pure C
    (`boards/main_a0/aw9364_dimming.c`), host-tested (§11). `mao backlight`
    (§10) drives the steps directly for bring-up.
  - GPIO45 is the VDD_SPI strap. On this module the flash voltage comes from
    eFuse, and the AW9364's 150 k EN pull-down holds the pin low through reset
    anyway (dark from power-on). The pin becomes a plain GPIO output, latched
    low first, only in `mao_board_display_init()`.
- Power-up sequence: RST_N low, PWR_EN high, 10 ms, 10 ms with reset low,
  RST_N high, 120 ms, then `esp_lcd` GC9A01 init with `reset_gpio = -1`. The
  driver's software reset is kept; it is redundant but harmless.
- Colour handling and the panel-native orientation (`A0_LCD_PANEL_MIRROR_X`
  true, `_MIRROR_Y` false, `_SWAP_XY` false, BGR, inverted) are properties of
  the 1.28" IPS glass + GC9A01 and equal the LCDkit's (FPC towards 6 o'clock =
  rotation 0); VERIFY AT BRING-UP against the fitted panel.
- **Rotation.** On the A0 the panel's tail leaves at 9 o'clock, a 90° turn. The
  mounting is a Kconfig choice, `MAO_MAIN A0: display rotation`
  (`CONFIG_MAO_A0_LCD_ROTATION`, 0 / 90 / 180 / 270, default **90**, VERIFY
  AT BRING-UP: the sense of the turn as the controller sees it is unknown
  until the face is on screen; 270 is the other candidate). The board passes
  it in `mao_board_display_t.rotation` (LCDkit: 0); `mao_display` applies it
  with `lv_display_set_rotation()`, which `esp_lvgl_port` turns into the
  panel's MADCTL (hardware rotation: no extra buffer, no cost per frame).
  The panel orientation is set only at the first init; after a rail power
  cycle the GC9A01 driver re-sends its last MADCTL, so the rotation
  survives. `mao rotate 0|90|180|270` changes it live, `mao rotate save`
  stores it in NVS (`mao`/`rot`, it then overrides Kconfig at boot),
  `mao rotate default` removes the override. API:
  `mao_display_set_rotation()` / `_get_rotation()`.
- Test helpers for the self-test: `mao_display_test_show()` (a full-screen
  plate with big / small text on `lv_layer_top()`), `_test_clear()`,
  `mao_display_rail(on)`: off pauses LVGL rendering (`lvgl_port_stop()`, so
  nothing is flushed into the unpowered panel) and switches the rail off for
  as long as the fixture needs to measure TP13; on re-initialises the panel,
  redraws and resumes rendering. The UI keeps running meanwhile (the LVGL
  lock is not held).
- After an expander reset the panel is re-initialised automatically (§2,
  "Recovery through GPIO39").
- With the rail off (`MAO_RAIL_DISPLAY` off), DC, CS, MOSI and SCLK are
  driven low as GPIOs. For deep sleep they are isolated (high-Z, held). When
  the rail is switched on again, the panel is re-initialised and the pins are
  returned to SPI2 (`gpio_iomux_output`, an IDF private API).
- DROWSY / deep sleep: `mao_display` sends sleep-in (0x10) under the LVGL lock
  and turns the backlight off. Wake sends sleep-out (0x11) + 120 ms and
  restores the brightness.

### 3.2 Audio
- Speaker: MAX98357A on I2S1 standard Philips TX (BCLK 17, WS 18, DOUT 16),
  16 kHz mono 16-bit. Each sample goes to both slots. SD_MODE = expander P2
  at 3.3 V selects the left channel; low is shutdown. `mao_audio` powers
  the amp only around sounds: on before a sound with 2 ms wake, off after 3 s
  of silence. The vocabulary is unchanged. `A0_AUDIO_GAIN` = 0.58 (the
  LCDkit value, VERIFY by ear; gain pin open = 9 dB).
- Mic: SPH0641LU4H-1 on I2S0 PDM RX (CLK 37, DATA 36), hardware PDM→PCM,
  16 kHz, DSR 8S = 1.024 MHz PDM clock. **Slot choice:** SELECT is tied low,
  and IDF defines `I2S_PDM_SLOT_LEFT` as "the device whose select pin is
  pulled down". The mono default (left slot) is used, with no clock
  inversion (`A0_MIC_CLK_INVERT`, VERIFY). The mic supply is GPIO35
  (`MAO_RAIL_MIC`), switched together with the clock.

- Added in stage 2: `mao_audio_tsk()` (two dry clicks, 3.9 / 3.3 kHz, 55 ms
  apart: the "tsk" of fiddling escalation), `mao_audio_test_chirp(level)`
  (self-test only: 700 Hz … 4.2 kHz steps, 520 ms, fixed level independent
  of the volume) and `mao_audio_busy_until_ms()` (end of the current sound
  + 150 ms room tail), which perception uses to ignore MAO's own sounds.

### 3.3 Haptics: TI DRV2605L (0x5A), LRA LD0832AA-0099F
- EN = expander P3. IN/TRIG is grounded, so playback uses the internal
  trigger (GO bit). Mode: LRA, closed loop, auto-resonance, ROM library 6.
- Registers: MODE 0x01, LIBRARY 0x03, SEQ 0x04–0x0B, GO 0x0C,
  RATED_VOLTAGE 0x16 = 70, OD_CLAMP 0x17 = 130, A_CAL_COMP 0x18,
  A_CAL_BEMF 0x19, FEEDBACK 0x1A = 0xB4|BEMF_GAIN, CONTROL1 0x1B = 0x90
  (boost, DRIVE_TIME 16 = 2.1 ms), CONTROL2 0x1C = 0xF5 (SAMPLE_TIME 300 µs),
  CONTROL3 0x1D = 0xA0, CONTROL4 0x1E = 0x30 (AUTO_CAL_TIME 1.0–1.2 s).
- Math (in `mao_drv2605.c`): RATED_VOLTAGE = 1.8 V × √(1 − (4·300 µs +
  300 µs) × 235 Hz) / 20.58 mV = 70.4 → 70. OD_CLAMP = 2.5 V / (21.32 mV ×
  √(1 − 235 Hz × 800 µs)) = 130.1 → 130.
- Calibration runs in the background on first boot (the device should be
  still). Results go to NVS `mao`/`hap_cal` and are loaded on later boots.
  `mao haptic calibrate` re-runs it. The driver is reconfigured on every EN
  rise.
- Vocabulary (strong / medium / light, chosen by strength 75 % / 40 %):
  tick 24/25/26 Sharp Tick, double_tap 27/28/29 Short Double Click Strong,
  heartbeat 7·150 ms·8 Soft Bump, short_pulse 21/22/23 Medium Click,
  tremor 13+13 / 50+50 / 51+51, annoyed_buzz 47·60 ms·47 Buzz,
  wake_pulse 84 / 85 / 108 ramp-up, confirm 1/2/3 Strong Click.
  Calls are non-blocking and a new touch interrupts the current one. EN goes
  low 1.5 s after the last touch, and whenever haptics are disabled, drowsy or
  asleep.
- Input feel (`mao_app.c`): every dial step plays `tick` with the sound tick
  (`dial_tick`, thinned to one per 90 ms when spun fast, none when very fast):
  the ring's ferrite pole strip has no mechanical detent, so this is its
  click. Every face press plays `confirm`: the face rocks on its lip, so rim
  presses travel further and lighter than centre presses, and the click makes
  them feel the same (`docs/hardware/mao-mechanical.md` §3, §4).

- Added in stage 2: `mao_haptics_identify()` (STATUS[7:5] read in the
  haptics task with EN raised as needed), `mao_haptics_get_cal_info()` (runs,
  last result, A_CAL_COMP / A_CAL_BEMF / BEMF_GAIN, resonance from
  LRA_PERIOD) and `mao_haptics_busy_until_ms()` (playing + 250 ms ring-down,
  1.6 s for a calibration) so perception ignores MAO's own vibration.

### 3.4 Power: MAX17048 (0x36) + BQ24073
- Gauge: VCELL 0x02 (78.125 µV), SOC 0x04 (1/256 %), VERSION 0x08
  (0x001x), HIBRT 0x0A = 0x8030, CONFIG 0x0C (RCOMP kept, ATHD = 27 → empty
  alert at 5 %, ALSC off), VALRT 0x14 = 0xA5FF (3.30 V low), CRATE 0x16
  (0.208 %/h), STATUS 0x1A (flags cleared after reading).
- Charger: BQ24073, USB present = PGOOD on GPIO3 low (both edges). Charge
  current 207 mA (ISET 4.3 k; the cell allows 250 mA) with a USB500 input
  limit, set in hardware; the pack NTC on TS stops charging outside 0–50 °C.
  /CHG is **not wired** on this revision; expander P6 is the charger's /CE.
- **Charging (estimated).** `mao_board_line_get(MAO_LINE_CHARGING)` returns
  `ESP_ERR_NOT_SUPPORTED` on the A0, so `mao_power` derives it
  (`mao_policy_charging()`, host-tested): USB present AND not paused by the
  temperature limit AND (gauge CRATE > +1 %/h OR SOC < 100 %). Without a
  gauge: USB present and not paused. Honest limits: right after plug-in it
  says "charging" before CRATE has turned positive; a charger that terminates
  while the gauge reads below 100 % (or a load above the USB500 limit that
  draws on the cell) still reads as charging until SOC reaches 100 %. CRATE
  is the gauge's approximate SOC rate (0.208 %/h per LSB, "not for conversion
  to ampere"). The estimate is refreshed with every gauge reading (30 s) and
  at once when USB comes or goes (the gauge is re-read then).
- **Charge temperature limit.** The LiPo cell may be charged at 0–45 °C, but
  the charger's NTC window ends at 50 °C. While USB is present, `mao_power`
  reads the board temperature every 10 s (and at once on plug-in) and
  pauses charging (`mao_board_charge_enable(false)`, /CE = 1) at ≥ 43 °C,
  resumes at ≤ 40 °C (`mao_policy_charge_pause()`, host-tested); transitions
  are logged (`charging paused: board 43.4 C >= 43 C` / `charging resumed`).
  The source is the IMU die temperature (`mao_sense_imu_temperature()`,
  LSM6DSOX OUT_TEMP 0x20/0x21, 256 LSB/°C, 0 = 25 °C), registered by
  `mao_sense` with `mao_power_set_temp_source()`. Fail-safe: no source, a
  failed read or a NaN leaves charging **enabled** (logged once); the pack NTC
  still stops the charger at 50 °C. The IMU die is a board-temperature proxy,
  not the cell, and its offset is only specified as ±15 °C (VERIFY AT
  BRING-UP against a thermocouple). Unplugging USB returns /CE to 0. In
  DROWSY on a charger the light-sleep timer is shortened to 10 s so the check
  keeps running (the IMU converts at its 12.5 Hz low-power ODR). Deep sleep
  writes every expander output 0, so charging is enabled while the chip
  sleeps (nothing watches the temperature then; deep sleep on USB is off by
  default, `MAO_POWER_DEEP_ON_USB`). Cold is left to the NTC (0 °C).
- Events: `USB_CONNECTED` / `USB_DISCONNECTED`, `CHARGING_STARTED`,
  `CHARGING_DONE` (charging estimate went false while USB is still present
  and nothing holds the charger off; a temperature pause posts no event),
  `BATTERY_LOW` (SOC ≤ 15 %, re-armed above 20 %), `BATTERY_CRITICAL`
  (on battery with SOC ≤ 2 % or VCELL < 3.35 V for 3 readings in a row),
  `POWER_STATE`. After a critical alert, a 5 s grace period follows, then
  deep sleep with USB as the only wake source
  (`CONFIG_MAO_POWER_CRITICAL_SHUTDOWN`). This keeps clear of the ~2.96 V
  UVLO. The gauge is polled every 30 s (every 5 s when low) and also read on
  every alert.

- Added in stage 2: `mao_power_refresh()` (read the gauge now),
  `mao_power_gauge_version()`, `mao_power_last_wake()` (what ended the last
  DROWSY), `mao_power_policy_config()` (§4.1), sleep reason
  `MAO_SLEEP_REASON_IDLE`.
- Added with the board revision: `mao_power_set_temp_source()`, and
  `mao_power_status_t` fields `charge_paused`, `temp_valid`, `temp_c`.
- The expander INT (GPIO21) now carries only the gauge / light alert: every
  expander interrupt reads `MAO_LINE_SENSE_ALERT`, which clears the INT even
  without a gauge.

### 3.5 Sense (`mao_sense`): observations only
| Sensor | Part / addr | Configuration | Observation |
|---|---|---|---|
| IMU | LSM6DSOX 0x6A (WHO_AM_I 0x6C; LSM6DS3TR-C 0x6A accepted) | CTRL3_C 0x64 (BDU, H_LACTIVE, IF_INC, push-pull), XL 104 Hz normal mode ±4 g, gyro off (`mao_sense_imu_gyro`). INT1 = wake-up (125 mg) + tap + double tap; INT2 = 6D (60°) + free-fall (312 mg, 58 ms). LIR latched. DSOX: TAP_CFG0/1/2 0x56–0x58, I3C off; DS3TR-C: TAP_CFG 0x58. INT lines without MCU pulls (INT1 high at IMU power-up = I3C-only). Die temperature OUT_TEMP 0x20/0x21 on request (`mao_sense_imu_temperature`, for the charge limit) | accel g, gyro dps, event flags, 6D orientation |
| ToF | VL53L4CD 0x29, ST ULD 2.2.2 (vendored, `vl53l4cd/README.md`) | XSHUT = P4; autonomous 20 ms budget every 200 ms (ACTIVE) / 500 ms (IDLE); DROWSY: interrupt only below 150 mm; off in deep sleep | distance, range status, signal, sigma, threshold flag |
| ALS | OPT3004 0x44 (0x7E = 0x5449, 0x7F = 0x3001) | CONFIG 0xCE11: auto range, 800 ms, continuous, latched window ±25 % (min ±2 lx) re-centred each read; reading CONFIG clears the latch; off when drowsy / asleep | lux |
| Touch | S3 touch v2, 4 zones | default sample config (500 charges, 0.5–2.2 V), threshold = 2 % of benchmark, hardware filter, active/inactive callbacks; TOP = deep-sleep wake channel. LEFT holds its last reading while the speaker amplifier is on and 150 ms after: the speaker lies partly over that electrode and the class-D outputs switch at ~330 kHz | raw, baseline, delta 0..1 (1 = 2 × threshold), touched, changed mask |
| Mic | SPH0641 via I2S0 PDM | 32 ms blocks, DC removed | RMS / peak dBFS, onset (+10 dB over a tracked floor, > −75 dBFS, 250 ms refractory) |

The sense task wakes on the IMU and ToF interrupt lines, on touch callbacks,
or when a periodic sample is due (rates in `mao_sense.c`). Subscribers use a
callback or a queue, with a per-kind rate limit; observations that carry an
event bypass the limit. `mao_sense_latest()` returns the last observation of
each kind. Nothing in `mao_sense` interprets the data.

Added in stage 2: `mao_sense_identify(kind, &id)` reads the identity
registers under the sense device lock (IMU WHO_AM_I, ToF model id, ALS
manufacturer << 16 | device), and `mao_sense_mic_enable()` switches the mic
clock and supply together (never a clock into an unpowered mic).

### 3.6 IR (`mao_ir`)
- TX: RMT 1 MHz, 38 kHz carrier at 33 % duty, and a simple encoder that
  writes an NEC frame as 34 symbols. `mao_ir_send(addr, cmd)`: an address
  ≤ 0xFF is sent as standard NEC (addr, ~addr), a larger one as extended
  16-bit. The call blocks for about 70 ms.
- RX: RMT, glitch filter 1.25 µs, 12 ms idle, input inverted (active-low
  receiver), 25 % timing tolerance, inverted command checked, repeat codes
  supported. The receiver supply is P5 with a 1 ms settle. Frames are posted
  as `MAO_EVENT_IR_RECEIVED` (`(addr << 16) | cmd`) and passed to an optional
  callback. Frames caught while MAO itself transmits are flagged `echo`. The
  receiver is off while drowsy or asleep.
- LCDkit: unchanged; IR stays `[--] IR disabled`.
- Added in stage 2: `mao_ir_get_stats()` (frames, echoes, last frame and its
  time) for the factory loopback, `mao_ir_rx_is_on()`. The single RX callback
  belongs to mao_perception.

### 3.7 Input
Stage 2 extends `mao_input_stats_t` with per-channel edge counters
(`edges_a` / `edges_b`: both Hall latches toggle), detents by direction
(`detents_cw` / `detents_ccw`, after the board's reversal) and debounced
`presses`, for the self-test. Events are unchanged.

## 4. Power states and wake sources

| State | Display | Sensors | Other |
|---|---|---|---|
| ACTIVE | on | rates above, mic on | HALL_FAST high |
| IDLE | on (app may dim) | slower rates | HALL_FAST high |
| DROWSY | panel sleep-in, backlight off | IMU wake-on-motion only (12.5 Hz LP), ToF approach threshold, ALS off, mic off | amp, haptics and IR RX off; HALL_FAST low; CPU in light sleep |
| DEEP_SLEEP | rail off, bus isolated | IMU wake-on-motion (off on a critical battery), ToF / ALS / mic off | every expander rail off, HALL_FAST low and held |

- Deep-sleep wake: ext1 ANY_LOW on GPIO0 (press), GPIO14 (IMU INT1, only if
  the IMU answered at boot: the pad has no pull), GPIO3 (USB plugged) and
  GPIO21 (expander: gauge / light alert). A line that is already low is not
  armed. With USB present, ext0 waits on GPIO3 going high
  (unplug) instead. Touch wakes on the TOP zone, and a timer wakes every
  `CONFIG_MAO_POWER_WAKE_INTERVAL_MIN` (30 min). On a critical battery only
  USB can wake MAO.
- Light-sleep (DROWSY) wake: the same lines plus GPIO48 (ToF threshold, through the GPIO wake source: GPIO48 is not an RTC pad), any
  touch zone, and a 60 s housekeeping timer (10 s on USB power, for the charge
  temperature limit, §3.4). The timer only refreshes the battery state (and
  on USB checks the temperature) and sleeps again; any other source returns to ACTIVE. Ring
  rotation alone is not a wake source (GPIO 41/42 are not RTC pads), but
  touching the ring usually trips the IMU or a touch zone. ext1 moves its
  pads to RTC IO, so they are given back to the GPIO matrix after every
  light sleep and at boot.
- Continuity: a record in RTC memory (`RTC_DATA_ATTR`) holds the sleep
  count, reason and RTC time at entry. After a deep-sleep wake,
  `mao_power_continuity()` gives the reason, the wake source and the time
  asleep (`esp_rtc_get_time_us()`). The default RTC clock is the internal
  RC oscillator; its accuracy is VERIFY.
- Hooks: `mao_power_register_hook()`. Display, haptics, sense and IR register
  hooks. Hooks run in the `mao_power` task and finish their work before
  sleep starts.

### 4.1 Power policy (automatic states)

`components/mao_power/mao_power_policy.c` is the decision, in plain C and
host-tested; `mao_app` (`mao_app_power.c`) feeds it once a second
(`MAO_EVENT_TICK`) and asks `mao_power` for the state; `mao_power` carries it
out and itself goes from a long DROWSY to deep sleep.

```
activity ──IDLE_AFTER_S──> IDLE ──DROWSY_AFTER_S──> DROWSY ──DEEP_AFTER_MIN──> DEEP SLEEP
   ^  (input, or a presence percept: touch, approach, pick-up, knock, ...)        |
   └──── press, touch, motion, USB, proximity, charger ── wake ───────────────────┘
```

| Kconfig (`MAO power`) | Default | Meaning |
|---|---|---|
| `MAO_POWER_AUTO` | y | automatic states at all (boards with power management) |
| `MAO_POWER_IDLE_AFTER_S` | 30 | IDLE: sensors at their slower rates, character at ~15 Hz |
| `MAO_POWER_DROWSY_AFTER_S` | 180 | DROWSY: panel sleep-in, light sleep (the sleepy look, `MAO_SLEEPY_TIMEOUT_S` = 45 s, comes first) |
| `MAO_POWER_DEEP_AFTER_MIN` | 30 | DROWSY this long → deep sleep (0 = never) |
| `MAO_POWER_REDROWSE_S` | 20 | woken from DROWSY and nothing follows: look around this long, then doze again |
| `MAO_POWER_SLEEP_WITH_CONSOLE` | n | otherwise never below IDLE while a USB host is attached (`usb_serial_jtag_is_connected()`, SOF-based; light/deep sleep would drop the console) |
| `MAO_POWER_DEEP_ON_USB` | n | otherwise no deep sleep on USB power (DROWSY only) |
| `MAO_POWER_QUIET_WAKE_S` | 8 | after a housekeeping-timer wake: dark look-around time, then straight back to deep sleep |

- **Activity** is input or a presence percept (§6). NUDGED, SUDDEN_NOISE,
  QUIET_ROOM, DARK_ROOM, WITHDRAWN are not activity: a bumped desk or a door
  slam does not keep MAO up.
- **Hold awake**: the first encounter, an open DEVICES / DEVICE view (the
  user is controlling a lamp), the self-test, `mao stress`.
- **Waking from DROWSY** (`MAO_EVENT_POWER_STATE` ACTIVE after DROWSY,
  source from `mao_power_last_wake()`): a peek, not a full wake-up. USB wakes
  fully; press, touch, approach follow as input / percepts and wake MAO.
  Without them it dozes off again after `REDROWSE_S`.
- **DROWSY → DEEP**: inside the `mao_power` light-sleep loop
  (`mao_policy_drowsy_to_deep()`); the light-sleep timer is shortened so the
  deadline is met. Reason `idle` in the continuity record.
- **IDLE** also sets `mao_character_set_reduced_rate(true)` (66 ms motion
  tick); ACTIVE restores 33 ms.
- `mao sleep` / `mao sleep light` still work for tests (reason `dev`).

## 5. Perception (`mao_perception`)

```
mao_sense observations ─┐                      core/percept_engine.c (plain C, host-tested)
dial, press, USB,       ├─> mao_percept task ─> filters · hysteresis · debounce · windows ·
battery, power state    │   (queue, 50 ms tick)  fusion · confidence · cooldowns · history
IR frames (not echoes) ─┘                                      │
                                         MAO_EVENT_PERCEPT (percept, confidence, detail)
                                                               v
                                                 mao_app (§6) ─> character / audio / haptics
```

The character never reads hardware and no sensor maps to an animation:
sensors become **percepts** (what MAO noticed), and only the application
decides what MAO does about them. The engine has no clock and no IDF: every
input carries its time, so it runs identically on the host (§11).

- **Event**: `MAO_EVENT_PERCEPT`, value = `mao_percept_pack(percept,
  confidence 0..100, detail)` (bits 0-7 / 8-14 / 16-30, never negative);
  `mao_percept_from_event()` unpacks. Vocabulary in
  `components/mao_perception/include/mao_percept.h`.
- **Self-gating**: while MAO's own speaker (`mao_audio_busy_until_ms`) or LRA
  (`mao_haptics_busy_until_ms`) is active, the mic and the motion detectors
  ignore what that does. A knock on MAO is felt and heard: it is a KNOCK,
  never also a SUDDEN_NOISE.
- **Rate limits**: per-percept cooldowns and a global limit of 6 event
  percepts per second; state percepts (enter / leave) use hysteresis only.
- **Missing sensors** are never fed; fusion counts their evidence as neutral.
  On the LCDkit only the dial, the press (and no power events) exist, so it
  produces FIDDLING_ESCALATION and LONG_ABSENCE and nothing else.
- **Continuity**: at deep sleep entry the time since anyone was around is
  kept in RTC memory; after a wake it is added to the time asleep, so
  LONG_ABSENCE spans sleeps.

| Percept | Evidence (and fusion) | Filtering | Detail |
|---|---|---|---|
| APPROACH_STARTED | ToF track closing faster than 120 mm/s for 2 samples, ≥ 80 mm closer than where the episode began, within 700 mm | median-of-3 + EMA distance, smoothed velocity; once per episode; 5 s cooldown; not while held / upside down / covered | - |
| APPROACH_NEAR | 2 consecutive filtered samples < `MAO_PERCEPT_NEAR_MM` (200); confidence higher if an approach preceded it; DROWSY threshold interrupt also counts (confidence 60) | leaves only above 1.5 × near for 400 ms (hysteresis) | - |
| WITHDRAWN | after NEAR, nothing in range for 1.2 s | suppressed right after an uncover | - |
| TOUCH | debounced (40 ms) touch begin; rim LEFT/RIGHT wait 150 ms: both sides = a grip, not a touch; nothing while held | 300 ms cooldown | zone |
| TOUCH_HOLD | one zone ≥ 1.2 s, not a pet, not a grip | once per contact | zone |
| TOUCH_REPEAT | ≥ 3 touch begins on one zone in 2.5 s | 3 s cooldown; feeds fiddling | zone |
| GENTLE_PET | TOP contact ≥ 700 ms or two slow strokes in 4 s (0.4) + low motion (0.2) + quiet room, no recent noise (0.2) + hand near or covered (0.2); -0.25 if annoyed (level ≥ 2); emitted at ≥ 70 % | missing sense counts 0.1; 5 s cooldown; soothes fiddling (halves it) | - |
| PICKED_UP | motion from rest ≥ 350 ms with tilt ≥ 12°, or a grip / base touch, or ≥ 0.9 s of vigorous motion; confidence from tilt, grip, duration | motion = change between samples or \|a\|-1 g, EMA 300 ms | - |
| PUT_DOWN / HARD_PUT_DOWN | held, then still ≥ 700 ms and upright (z > 0.8); hard if a shock (\|a\|-1 g > 0.8) or tap was the landing; free fall before it = dropped | | 1 = dropped |
| UPSIDE_DOWN | gravity z < -0.7 g for 800 ms; back above -0.2 for 600 ms | hysteresis on both edges | 1 / 0 |
| SHAKE | ≥ 4 strong samples (> 0.5 g) in 1.5 s | 2.5 s cooldown; feeds fiddling | - |
| NUDGED | short motion from rest (< 1.5 s, peak < 0.45 g, no tap) that settles by itself | 4 s cooldown | - |
| KNOCK | IMU tap / double tap on a resting MAO, nobody touching it, not a landing; +25 confidence if the mic heard it | 0.8 s cooldown | taps |
| QUIET_ROOM | room level (dB EMA, rises slowly, falls faster) < -58 dBFS for `MAO_PERCEPT_QUIET_ROOM_S` (120 s); leaves above -50 dBFS for 4 s | mic not running = no judgement | 1 / 0 |
| SUDDEN_NOISE | mic onset ≥ 15 dB above the room and ≥ -50 dBFS; decided 180 ms later so a knock can claim it; not while MAO is handled | 3 s cooldown | - |
| COVERED | ToF < 35 mm (0.5) + light < 30 % of the recent level (0.4) + TOP touch (0.2), ≥ 70 % of the available weights for 1.2 s; uncovered at ≤ 30 % for 0.8 s | not while held | 1 / 0 |
| DARK_ROOM | light < 3 lx for `MAO_PERCEPT_DARK_ROOM_S` (20 s) and not covered; light again > 12 lx for 2 s | | 1 / 0 |
| USB_CONNECTED | power events, debounced 400 ms | | 1 / 0 |
| LOW_BATTERY | BATTERY_LOW / _CRITICAL | low repeated at most every 10 min | 1 / 2 |
| REMOTE_SIGNAL | someone else's NEC frame (echoes and repeats ignored) | 1.5 s cooldown | command |
| LONG_ABSENCE | first presence after ≥ `MAO_PERCEPT_LONG_ABSENCE_MIN` (120 min) with nobody, sleep included; emitted before the percept that ended it | | minutes |
| FIDDLING_ESCALATION | score = dial (reversals +0.6, spinning) + touch (repeats, touching while dialling) + motion (shake, hard landing, dialling while moved) + press (≥ 3 in 2 s) + residue; each part capped, decays with a 6 s half-life. Levels at 3 / 6 / 9.5, one step at a time ≥ 1.2 s apart. Reaching level 2 adds 1.5 of **residue** (60 s half-life) once per episode: the next fiddling escalates sooner. The dial alone tops out at level 2; level 3 needs a second kind of fiddling or a recent episode | level falls back quietly (1.5 hysteresis); fully calm (score < 1) is announced once | level 1-3, 0 = calmed |

Kconfig `MAO perception`: `MAO_PERCEPT_NEAR_MM` (200, VERIFY through the
window), `MAO_PERCEPT_QUIET_ROOM_S` (120), `MAO_PERCEPT_DARK_ROOM_S` (20),
`MAO_PERCEPT_LONG_ABSENCE_MIN` (120), `MAO_PERCEPT_IMU_Z_DOWN` (n: U401 sits
on the face side at 0°, so by ST AN5192 fig. 1 the IMU's +X is board +x, +Y
is board −y (towards 12 o'clock) and +Z points out of the face; the axis glyph
is on the silkscreen; confirm the sign at bring-up), `MAO_PERCEPT_LOG`
(y: one INFO line per percept). Everything else is in `pe_config_default()`.

## 6. Application: percepts → behaviour (`mao_app_percept.c`)

The cause/state side. Reactions use the existing vocabulary (NOTICE,
SURPRISED, HAPPY, WAKE, ATTEND, DIZZY, sleepy, the dial-follow motion) plus
a small extension of the character: `mao_character_look(x, y, hold)` (gaze
somewhere), SUSPICIOUS (narrowed side-eye), ANNOYED ("tsk": narrowed, flat
mouth, a head shake), CROSS (narrowed, turns away; the dial is not followed
until it passes), CONTENT (soft squint, slow blink), and DIZZY as a reaction.
Nothing was removed; the dial, press, menu and device behaviour is as before.

Context decides, not a table:
- only HOME shows reactions; menus and device views stay undisturbed; the
  first encounter ignores percepts;
- **company** percepts (approach near, touch, pet, pick-up, put-down, knock,
  cover, USB plugged, IR, long absence, fiddling) count as activity: they
  wake a sleepy MAO and keep it awake (§4.1);
- **habituation**: each percept has an interest that a reaction spends
  (× 0.55) and that recovers over a minute; minor variants (a look instead of
  a reaction) cover the gap;
- a big reaction (pick-up surprise, pet, cross, peekaboo, long absence) is
  not trampled by small ones for 1.2 s;
- **mood**: annoyance follows FIDDLING_ESCALATION (1 suspicious, 2 tsk +
  `mao_audio_tsk` + a tick, 3 cross + annoyed buzz); level 2/3 leave a
  **grudge** (30 / 90 s) during which a pet only gets a suspicious look (and
  halves the grudge) and happy reactions are withheld; calming down is a
  small sigh (look down).

| Percept | MAO (HOME) |
|---|---|
| APPROACH_STARTED | eyes lift towards the visitor (sleepy: only a little) |
| APPROACH_NEAR | wakes; NOTICE (or a look up when habituated) |
| WITHDRAWN | if it was just with someone: watches them go (60 %) |
| TOUCH | towards the touched side; TOP: NOTICE; REAR: SURPRISED |
| TOUCH_HOLD | TOP: HAPPY (if not annoyed); sides: a longer look that way |
| TOUCH_REPEAT | SUSPICIOUS |
| GENTLE_PET | CONTENT + a light heartbeat touch; with a grudge: suspicious, grudge halved |
| PICKED_UP | SURPRISED + short pulse (habituated: NOTICE) |
| PUT_DOWN / HARD_PUT_DOWN | settles with a glance down / SURPRISED, or DIZZY + tremor when dropped |
| UPSIDE_DOWN | DIZZY; the right way up again: HAPPY |
| SHAKE | DIZZY (+ tremor) |
| NUDGED | awake: a glance aside; asleep: nothing (not company) |
| KNOCK | NOTICE + "who's there?" look up; double knock: SURPRISED |
| SUDDEN_NOISE | SURPRISED; sleepy: a startled peek without waking |
| QUIET_ROOM / DARK_ROOM | sleepy sooner (after 15 s without company); dark room: brightness × 60 %; lights back on while sleepy: the lids lift for a moment |
| COVERED | looks up at the hand; uncovered: HAPPY (peekaboo), habituates |
| USB_CONNECTED | NOTICE + notice sound + short pulse; unplugged: a glance down |
| LOW_BATTERY | a tired look; critical: sleepy at once (deep sleep follows) |
| REMOTE_SIGNAL | ATTEND (glance at the rim) |
| LONG_ABSENCE | HAPPY + wake pulse + confirm sound ("you're back") |

`mao percept inject <NAME> [detail] [confidence]` posts any percept, so the
reactions can be tried on the LCDkit or before the sensors are tuned.

## 7. Boot experience

| Situation | What the person sees |
|---|---|
| Power-on / reset | as before: "MAO" wordmark, then the eyes open (first encounter on a fresh MAO) |
| Wake from deep sleep (press, motion, touch, USB, charger) | no wordmark: the eyes simply open again (`mao_ui_skip_wordmark()` before the first frame, `mao_ui_resume()`); LONG_ABSENCE follows if it slept long |
| Housekeeping timer wake | nothing: the backlight stays at 0 and the character sleepy; someone showing up within `MAO_POWER_QUIET_WAKE_S` lights it and opens the eyes; otherwise back to deep sleep |
| Fatal hardware fault (no display or no input) | the wordmark with a dim service code (`SERVICE nn`) instead of the character; the console explains |

No raw debug text appears in normal operation. Diagnostics are on the
console (`[OK]` / `[!!]` / `[--]` lines, `mao board`, `mao selftest boot`).

## 8. Boot check and factory self-test (`mao_selftest`)

- **Boot check** (every boot, `mao_selftest_init()` after all drivers): reads
  the bring-up reports (`mao_system_report_status()`), the board revision and
  the fitted devices and builds a fault mask (display, input = fatal;
  expander, IMU, proximity, light, gauge, haptic, touch, mic, IR, audio,
  board ID, radio, NVS, power). It is logged once clearly, kept in NVS
  (`mao`/`hw_rev`, `mao`/`hw_flt`) when it changes, and on the first boot of
  a board or revision a `BOOTCHECK first ...` line is printed.
  Graceful degradation (brief §44): a missing ToF, mic, IMU or gauge only
  removes those percepts; MAO boots and behaves normally otherwise.
- **Factory self-test**: `mao selftest` (interactive), `mao selftest auto`
  (operator steps skipped), or at every boot with `CONFIG_MAO_SELFTEST_AT_BOOT`
  (profile `sdkconfig.factory`). On the A0: 24 automatic steps (including
  `expander_reset`, which pulses GPIO39), 3 probe-pad steps (`pad_lcd`,
  `pad_mic`, `pad_irv`: each rail off then on, measured at TP13 / TP14 /
  TP15 by the fixture DMM or the operator) and 6 operator steps; 7 steps on
  the LCDkit. Answers: `mao selftest yes|no|skip`, a face press = yes,
  `mao selftest mv <millivolts>` for a pad reading; `abort`, `clear` (remove
  the result plate), `boot` (boot-check report); `nopads` after `run` /
  `auto` skips the pad steps. Output: `SELFTEST STEP …`, `SELFTEST PROMPT …`,
  `SELFTEST MEASURE <step> <pad> <net> on|off <min> <max> …`, verdict
  `SELFTEST PASS 33/33` / `FAIL …` / `INCOMPLETE …`, `SELFTEST_JSON {…}`.
  Station script: `tools/factory_test.py`. Steps, limits, fixture, test
  pads: `docs/hardware/mao-factory-test.md`.
- Board revision changes: `board_id` limits 1480 … 1720 mV (3.18 V rail:
  1.59 V nominal, 1.51–1.70 V over every tolerance; was 1550 … 1750 for 3.3 V); `charger` passes on PGOOD and,
  where the board limits the charge temperature, a readable board temperature;
  it reports the estimated charging state, a pause and the temperature
  (`PGOOD(usb)=1 chg=1, board 29.4 C`). `docs/hardware/mao-factory-test.md`
  still lists the old values (owned by the hardware docs).

## 9. Bring-up order (`main/mao_main.c`)
system → board (chip, pads, board ID, I2C, expander with every rail off) →
LED (`[--]` on A0) → display (rotation applied) → input → audio → haptics →
power → sense → IR → devices → **perception** → **self-test (boot check)** →
app (boot style from the continuity record and the boot check). Every step
reports `[OK]` / `[!!]` / `[--]`, and a failure never stops the boot.
`mao_display_lock()` refuses (rather than asserting) if the display failed, so
the UI degrades instead of crashing.

## 10. Development console

| Command | Board | Effect |
|---|---|---|
| `mao board` | both | name, revision (mV), caps, I2C devices fitted/present, live scan, rails, lines |
| `mao sense` | A0 | latest observation of every sensor |
| `mao haptic <name>` | A0 | play a touch; also `calibrate`, `on`, `off`, `strength N` |
| `mao ir send <addr> <cmd>` | A0 | one NEC frame (hex with `0x`); `mao ir rx on\|off` |
| `mao power` | A0 | battery V / % / rate, USB, charging (estimated), temperature pause, charge-limit board temperature, state, continuity |
| `mao backlight <0..100>` | A0 | AW9364 bring-up, bypassing `mao_display`: set a level; `step <1..16>` one driver step; `edge` one raw extra edge (at step 16: does edge 17 wrap to full?); the next set restarts the driver |
| `mao sleep [s]` | A0 | deep sleep, timer wake after s (default 30 min); `mao sleep light [s]` = DROWSY test |
| `mao percept` | both | senses in use, held / near, room level, absence, fiddling level and score; `log on\|off`; `inject <NAME> [detail] [confidence]` |
| `mao rotate [deg]` | both | display rotation: show, set live (0/90/180/270), `save` (NVS), `default` |
| `mao selftest [run\|auto] [nopads]` | both | factory self-test; `yes`, `no`, `skip`, `mv <mV>`, `abort`, `clear`, `boot` |
| `mao help` | both | lists the built-in and registered commands |

Components register their own commands (`mao_devcmd_register`, up to 16), so
`mao_system` knows nothing about them. All commands are compiled out
without `CONFIG_MAO_DEV_CONSOLE` (release).

## 11. Host unit tests

The perception engine, the power policy (states, the charge temperature
limit and the charging estimate) and the A0 backlight's AW9364 step logic
(percent → step, the edge plan between any two steps against a driver model
that never counts past edge 16) are plain C. `tests/host/` builds
them with the host compiler (C99, `-Wall -Wextra -Werror`, AddressSanitizer
and UBSan) together with scenario tests: a small world simulator feeds every
sense at the A0's rates (IMU 100 ms, ToF 200 ms, light 1 s, touch 100 ms,
mic 32 ms, tick 50 ms) and checks which percepts come out, how often and in
which order (no spam on a quiet desk or in a static scene, pick-up / put-down,
hard landing and drop, nudge vs pick-up, upside-down hysteresis, shake
cooldown, own vibration ignored, approach / near / withdrawn, ToF outlier and
hovering, touch debounce, grip vs touch, repeat, pet fusion and its absence
in a loud room, peekaboo, dark room vs cover, noise vs knock vs own sound,
quiet room, fiddling escalation dial-only (LCDkit) and multi-modal, pets
soothing, long absence across sleep, USB debounce, global rate limit,
battery and IR, drowsy resets) plus the policy table.

```sh
tests/host/run.sh            # local cc + make (macOS / Linux)
tests/host/run.sh --docker   # gcc inside espressif/idf:v6.0.3
```

## 12. Known hardware / source discrepancies
- Resolved: the generated header contains `MAO_PIN_USB_PRESENT_N` (GPIO3 since the 4-layer pin map)
  and the old `#ifndef` fallback is gone; the board-ID divider reads
  1 M / 1 M everywhere; the pull-up notes in `pinmap.py` now match
  `circuit.py` (TOF_INT_N 10 k, SENSE_ALRT_N 100 k); the `pinmap.py`
  docstring names `hardware/mao/design/gen_pinmap.py`.
- Hardware changes after stage 2 that the firmware follows: TP13-TP15 expose
  the switched rails; the IR receiver output has a 10 k pull-up (R506) to its
  switched supply.
- Board revision (`docs/firmware/mao-a0-rev-merge.md`): GPIO38 = IR_TX and
  GPIO39 = EXP_RST_N (swapped: GPIO39 = MTCK has a reset pull-up that would
  have lit the IR LEDs); GPIO45 = LCD_BL_CTRL (AW9364 EN) instead of the LEDC
  backlight PWM; expander P6 = CHG_CE_N output instead of the CHG_N input;
  +3V3 = 3.18 V; the amplifier's SD_MODE has a 2.2 k series resistor (no
  firmware change); the panel tail still leaves at 9 o'clock.
- `pinmap.py` text the firmware does not use but that is now stale: the
  BOARD_ID note says 1.65 V (1.59 V with the 3.18 V rail), and the EXP_INT_N
  note still mentions charger status (only the gauge / light alert is left).
- The brief listed GPIO13 as a spare. The 4-layer pin map uses GPIO13 for LCD_CS and GPIO35
  as `MIC_PWR` (the mic draws 80 µA even with its clock stopped), and the
  firmware follows the header.

## 13. Verify at bring-up
Stage 1: panel colour order and inversion · dial direction
(`A0_DIAL_REVERSE`) · speaker gain (`A0_AUDIO_GAIN`) and amp wake time ·
mic slot / clock edge and onset levels · touch threshold ratio · IMU tap
thresholds and timing at 104 Hz · ToF approach distance through the window ·
haptic auto-calibration result and feel of each touch · gauge RCOMP for the
actual cell · IR receiver settle time · board-ID voltage · deep-sleep current
with every rail off · light-sleep behaviour with the USB console attached ·
RTC-clock accuracy for "time asleep".

Stage 2:
- **Display rotation** `CONFIG_MAO_A0_LCD_ROTATION` (default 90; 270 is the
  other candidate): `mao rotate 270`, then `mao rotate save` or fix Kconfig.
- **IMU axis sign** at rest (`mao sense`: accel z ≈ +1 g with the face up,
  as the layout predicts), else `MAO_PERCEPT_IMU_Z_DOWN`.
- Perception thresholds on real hardware: near distance through the window
  (`MAO_PERCEPT_NEAR_MM`), cover distance (35 mm), quiet-room level
  (-58 dBFS), sudden-noise rise (15 dB / -50 dBFS), motion still / move /
  shake levels (0.025 / 0.06 / 0.5 g), pick-up time (350 ms) and tilt
  (12°), dark / light levels (3 / 12 lx), pet weights; look for false
  PICKED_UP from desk vibration and false SUDDEN_NOISE from the speaker's
  ring-out (`ROOM_TAIL_MS` 150 ms) and the LRA (`BUSY_TAIL_MS` 250 ms).
- Power policy timing (30 s / 180 s / 30 min / 20 s re-doze / 8 s quiet
  wake) against battery life and how MAO feels; `usb_serial_jtag_is_connected()`
  on a charger-only cable (expected: no SOF, so sleep is allowed).
- Self-test limits: touch benchmark window (1 000 … 4 000 000, ×4 spread),
  LRA resonance (190 … 285 Hz), speaker → mic loopback rise (12 dB), mic
  alive window (-100 … -15 dBFS), IR loopback via the fixture lid,
  `factory_test.py --reset` (RTS pulse) resetting the S3 over
  USB-Serial/JTAG without entering the ROM loader.
- Probe-pad limits (off ≤ 0.30 V; on 3.00 … 3.40 V, MIC 3.00 … 3.35 V) and
  the 150 ms discharge wait before the "off" reading (IR_RX_VCC has 4.7 µF
  behind 100 R; check it really falls below 0.3 V in time).
- Expander reset: the 10 µs pulse; that the panel, ToF and haptic driver
  come back cleanly after `expander_reset` (and after a real recovery), and
  how visible the panel re-init is (~0.3 s blank).

Board revision (details in `docs/firmware/mao-a0-rev-merge.md` §5):
- **AW9364 steps**: 16 distinct levels from `mao_board_backlight_set()`;
  LED current at 100 % (≈ 40 mA, 33–47 mA by the datasheet spread) and at 1 %
  (≈ 2.5 mA); whether edge 17 wraps to step 1 (if it does, a brighter step
  could skip the 3 ms shutdown); whether that dark gap shows during the
  self-test's fade-in.
- **Charge pause**: warm the board past 43 °C on USB and watch
  `charging paused` (and the charge current fall), cool below 40 °C for
  `charging resumed`; IMU temperature offset against a thermocouple at room
  temperature (datasheet ±15 °C).
- **IR LEDs dark at boot and reset** (GPIO38: no reset pull; 100 k gate
  pull-down), and `expander_reset` still pulses through GPIO39.
- Board ID at 3.18 V (≈ 1.59 V).
