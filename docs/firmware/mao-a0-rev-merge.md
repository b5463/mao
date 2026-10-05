# MAO_MAIN A0 board revision: firmware merge guide

How to carry the firmware changes for the revised MAO_MAIN A0 board onto a
newer MAO firmware tree. The changes were made on top of commit `b17187e`
(2026-10-05). The full diff is in `docs/firmware/mao-a0-rev.patch` (components,
tests and docs/firmware; new files included; `hardware/` and `docs/hardware/`
left out). Behaviour is described in `docs/firmware/mao-a0-firmware.md`
(§2, §3.1, §3.4, §4, §8, §10, §11, §13).

Hardware source of truth: `hardware/mao/design/pinmap.py` and `circuit.py`.
The pin header is generated from `pinmap.py`; never hand-merge it.

## 1. Hardware changes and what the firmware does about them

| # | Hardware change | Why (hardware reason) | Firmware response |
|---|---|---|---|
| 1 | GPIO38 = IR_TX (IR LED gate, RMT carrier), GPIO39 = EXP_RST_N (expander reset, open drain, 10 k pull-up R22) — swapped | GPIO39 is MTCK and has a weak pull-up at reset, which would have switched the IR LEDs on; on EXP_RST_N it agrees with the 10 k pull-up. GPIO38 has no reset pull | Regenerated pin header; code uses `MAO_PIN_*` only. Comments, log text and docs that named GPIO38 for EXP_RST_N / GPIO39 for IR_TX fixed |
| 2 | Backlight: LEDC PWM into a low-side MOSFET (GPIO45 `LCD_BL_PWM`, 80 % cap) replaced by an Awinic AW9364 constant-current sink; GPIO45 = `LCD_BL_CTRL` = its EN | Constant current, no PWM flicker, and 2 × 20 mA = 40 mA (the panel rating) is the hardware maximum, so no firmware cap is needed. The panel LEDs now hang from VSYS, not from the switched 3V3_LCD rail | New AW9364 1-wire pulse-count driver behind the unchanged `mao_board_backlight_set(percent)`; `A0_BACKLIGHT_MAX_PCT` removed; LEDC no longer used on the A0 |
| 3 | Expander P6: input `CHG_N` (charger /CHG) became output `CHG_CE_N` (charger /CE), 100 k pull-down at the charger | Firmware must be able to pause charging (item 4); /CHG is no longer connected | P6 is an output driven 0 (charging enabled) from expander init on; `MAO_LINE_CHARGING` is `ESP_ERR_NOT_SUPPORTED` on the A0 and `mao_power` estimates charging from PGOOD + the fuel gauge |
| 4 | Charge thermal limit (new) | The LiPo cell may be charged only at 0–45 °C, but the BQ24073's pack-NTC window ends at 50 °C | `mao_power` reads the IMU die temperature every 10 s while USB is present; /CE = 1 (pause) at ≥ 43 °C, back to 0 at ≤ 40 °C; transitions logged; fail-safe = charging enabled |
| 5 | IMU INT1/INT2 (GPIO14/47) without MCU pull-ups | An LSM6DSOX that sees INT1 high at its power-up selects I3C-only mode (datasheet §5.3, "INT1 must be set to '0' or left unconnected during power-on") | Internal pull-ups removed in `config_irq_inputs()`; the IMU's deep-sleep wake is armed only if the IMU answered at boot (a missing IMU leaves the pad floating) |
| 6 | +3V3 = 3.18 V (3.10–3.28 V worst case) | Panel VCI maximum 3.3 V | Board-ID window 1400–1900 mV still holds (1.59 V nominal, ~1.51–1.68 V worst case): unchanged. The self-test's tighter `board_id` limits were re-centred: 1550–1750 → 1480–1700 mV |
| 7 | Amplifier SD_MODE through 2.2 k series resistor | MAX98357A datasheet case VDDIO > VDD | None (SD_MODE high still selects the left channel) |
| 8 | Display orientation | — | None: the panel tail still leaves at 9 o'clock (`CONFIG_MAO_A0_LCD_ROTATION` default 90) |

## 2. Changed files

Generated (re-run the generator, do not merge by hand):

| File | Change |
|---|---|
| `components/mao_board/boards/main_a0/mao_board_pins.h` | `python3 hardware/mao/design/gen_pinmap.py` from the repo root (also rewrites `docs/hardware/mao-pin-map.md`). New/renamed symbols: `MAO_PIN_IR_TX` 39 → 38, `MAO_PIN_EXP_RST_N` 38 → 39, `MAO_PIN_LCD_BL_PWM` → `MAO_PIN_LCD_BL_CTRL` (45), `MAO_EXP_CHG_N` → `MAO_EXP_CHG_CE_N` (6) |

Board support (`components/mao_board`):

| File | What changed | Why |
|---|---|---|
| `boards/main_a0/aw9364_dimming.h`, `.c` (new) | Pure C, no IDF: `aw9364_step_from_percent()`, `aw9364_step_current_ua()`, `aw9364_plan(from, to)` (shutdown first? how many rising edges?), AW9364 timing constants | Testable on the host; one place for the datasheet facts (§3) |
| `boards/main_a0/mao_board_a0_display.c` | LEDC backlight replaced: GPIO45 configured as a plain output, latched low, inside `mao_board_display_init()`; `mao_board_backlight_set()` = mutex + plan + shutdown wait (`esp_timer`, sleeps the bulk) + pulse train in a critical section with `esp_rom_delay_us`; unchanged step = nothing sent. Dev command `mao backlight <0..100> \| step <n> \| edge` (dev builds only) | Item 2 |
| `boards/main_a0/mao_board_a0_priv.h` | `A0_EXP_INPUT_MASK` = P7 only (was P6 + P7); `A0_BACKLIGHT_MAX_PCT` removed; board-ID comment for 3.18 V | Items 2, 3, 6 |
| `boards/main_a0/mao_board_a0.c` | `config_irq_inputs()`: no pulls on IMU INT1/INT2; `caps.charge_control`; `mao_board_line_get(MAO_LINE_CHARGING)` → `ESP_ERR_NOT_SUPPORTED` (no I2C access); new `mao_board_charge_enable()` (P6 = !enable) | Items 3, 4, 5 |
| `boards/main_a0/mao_board_a0_i2c.c` | Header comment: GPIO39, R22, P6 output and what a reset does to /CE; reset-test comment (stuck reset now leaves 0x80); expander-test POLARITY patterns 0x2A/0x15 → 0x6A/0x15 so P6 is exercised too. Init order unchanged: OUTPUT (0x00 = charging enabled) before CONFIG | Items 1, 3 |
| `boards/main_a0/mao_board_a0_sleep.c` | IMU INT1 armed for ext1 wake only if the IMU answered at boot; comments (P6 low in deep sleep = charging enabled; GPIO21 carries only the gauge / light alert) | Items 3, 5 |
| `boards/lcdkit/mao_board_lcdkit_ext.c` | `mao_board_charge_enable()` stub → `ESP_ERR_NOT_SUPPORTED` | API completeness |
| `include/mao_board.h` | `caps.charge_control`; `mao_board_charge_enable()`; `MAO_LINE_CHARGING` and `mao_board_backlight_set()` documentation | New API |
| `mao_board_common.c` | `mao board` prints `charge_control`, and `charging=-` where the line is not wired | Item 3 |
| `CMakeLists.txt` | `aw9364_dimming.c` in the A0 sources; `esp_timer` in PRIV_REQUIRES | Item 2 |

Power (`components/mao_power`):

| File | What changed | Why |
|---|---|---|
| `include/mao_power_policy.h`, `mao_power_policy.c` | `MAO_CHARGE_PAUSE_C` 43, `MAO_CHARGE_RESUME_C` 40, `MAO_CHARGE_RATE_MIN_PCT_H` 1.0; `mao_policy_charge_pause(usb, paused, temp_valid, temp_c)`; `mao_charge_input_t` + `mao_policy_charging()` | Pure, host-tested decisions (items 3, 4) |
| `include/mao_power.h` | `mao_power_temp_source_t`, `mao_power_set_temp_source()`; `mao_power_status_t.charge_paused`, `.temp_valid`, `.temp_c`; header comment | Items 3, 4 |
| `mao_power.c` | `charging_now()` / `update_charging()` (board line if the board has one, else the estimate; `CHARGING_DONE` not posted for a temperature pause); `charge_limit()` every 10 s on USB and at once on plug / unplug; USB change re-reads the gauge; `check_alert()` reads the alert line on every expander interrupt (it used to rely on the CHG read to clear the INT); DROWSY light-sleep timer capped at 10 s on USB; first check 10 s after init (the source registers later); `mao power` prints the limit | Items 3, 4 |

Sense (`components/mao_sense`):

| File | What changed | Why |
|---|---|---|
| `mao_sense_imu.c`, `mao_sense_priv.h` | `sense_imu_temperature()`: OUT_TEMP_L/H 0x20/0x21, 25 °C + raw / 256; `ESP_ERR_INVALID_STATE` while powered down (stale register); quiet on failure | Item 4 (LSM6DS3TR-C fallback has the same registers and scale) |
| `include/mao_sense.h`, `mao_sense.c` | Public `mao_sense_imu_temperature()` under the sense device lock; registered with `mao_power_set_temp_source()` when the IMU runs | Item 4, without a mao_power → mao_sense dependency (mao_sense already depends on mao_power) |

Self-test (`components/mao_selftest/mao_selftest.c`): `board_id` limits 1480–1700 mV
(item 6); `charger` step no longer reads `MAO_LINE_CHARGING` (it would fail with
`ESP_ERR_NOT_SUPPORTED`): passes on PGOOD and, with `caps.charge_control`, a
readable board temperature; reports estimated charging, pause and temperature;
`expander_reset` comment GPIO39.

Host tests (`tests/host`): `test_backlight.c` (new: percent → step table, every
percent 1–100 never below the request and < 1 step above it, step currents vs
datasheet table 1, all 17 × 17 step transitions through a driver model that
refuses edge 17, plan cases); `test_policy.c` (charge pause hysteresis,
fail-safe incl. NaN, a heat/cool sequence with exactly two transitions, the
charging estimate); `Makefile` (sources + include path), `mini_test.h`,
`test_main.c`, `run.sh`. 160 → 240 checks.

Docs: `docs/firmware/mao-a0-firmware.md`, this file.

## 3. API changes

New:

```c
/* mao_board.h */
bool charge_control;                                  /* in mao_board_caps_t */
esp_err_t mao_board_charge_enable(bool enable);       /* A0: expander P6 = !enable; others: ESP_ERR_NOT_SUPPORTED */

/* mao_power.h */
typedef esp_err_t (*mao_power_temp_source_t)(float *celsius);
void mao_power_set_temp_source(mao_power_temp_source_t source);
/* mao_power_status_t: bool charge_paused; bool temp_valid; float temp_c; */

/* mao_power_policy.h (pure) */
bool mao_policy_charge_pause(bool usb_present, bool paused, bool temp_valid, float temp_c);
bool mao_policy_charging(const mao_charge_input_t *in);

/* mao_sense.h */
esp_err_t mao_sense_imu_temperature(float *celsius);
```

Changed behaviour, same signature:
- `mao_board_backlight_set(percent)`: A0 now 16 current steps (rounded up), no
  80 % cap; a brighter step goes dark for ~3 ms; safe from any task.
- `mao_board_line_get(MAO_LINE_CHARGING)`: `ESP_ERR_NOT_SUPPORTED` on the A0.
  Anything outside `mao_power` that read it must use
  `mao_power_get_status()->charging` instead (in this tree: the self-test and
  `mao board`).
- `mao_power_status_t.charging` on the A0 is an estimate (see
  `mao_policy_charging()` for its limits); `MAO_EVENT_CHARGING_DONE` means the
  estimate went false with USB present and no temperature pause.
- Expander CONFIG readback is 0x80 (was 0xC0).

Removed: `MAO_PIN_LCD_BL_PWM`, `MAO_EXP_CHG_N`, `A0_BACKLIGHT_MAX_PCT`.

## 4. Porting onto a newer tree

1. Bring `hardware/mao/design/pinmap.py` (and `circuit.py`) of the revised
   board into the tree, then run `python3 hardware/mao/design/gen_pinmap.py`
   from the repo root. Do not apply the patch's `mao_board_pins.h` hunk by hand.
2. Apply the rest: `git apply --3way --exclude='*mao_board_pins.h' docs/firmware/mao-a0-rev.patch`
   (or `patch -p1`). Resolve conflicts with the tables in §2 as the intent.
3. If the newer tree moved things, check by symbol rather than by line:
   - `grep -rn "MAO_PIN_LCD_BL_PWM\|MAO_EXP_CHG_N\|A0_BACKLIGHT_MAX_PCT\|ledc" components/mao_board/boards/main_a0` → nothing left.
   - `grep -rn "MAO_LINE_CHARGING" components` → only `mao_board.h`, the board
     implementations, `mao_board_common.c` (tolerates "not supported") and
     `mao_power.c` (`s_chg_line`). Any new reader must handle
     `ESP_ERR_NOT_SUPPORTED` or use `mao_power_get_status()`.
   - `grep -rn "GPIO ?38\|GPIO ?39" components docs/firmware` → GPIO38 only for
     IR_TX, GPIO39 only for EXP_RST_N.
   - Every path that turns the panel rail off or sleeps must call
     `mao_board_backlight_set(0)` first: the LEDs are on VSYS now, so the rail
     no longer darkens them.
   - Every expander bulk write (`a0_expander_write_all`) must keep P6 at 0 unless
     the charge limit wants it high; deep sleep writes 0x00 on purpose.
   - New calls of `mao_board_backlight_set()` from an ISR are not allowed
     (mutex, possible 3 ms wait).
4. If the newer `mao_power` restructured its task: keep (a) a 10 s
   `charge_limit()` while USB is present, also inside the DROWSY light-sleep
   loop, (b) `charge_limit(true)` on every USB change, (c) reading
   `MAO_LINE_SENSE_ALERT` on every expander interrupt (it clears the
   TCA6408A INT; nothing else does now), (d) charging re-evaluated after every
   gauge reading.
5. If the newer `mao_sense` starts before `mao_power`: fine,
   `mao_power_set_temp_source()` may be called before `mao_power_init()`.
6. Build all profiles (`build_all.sh s3-dev s3-factory s3-release c3-dev
   c3-release` or `idf.py` per README) with zero warnings, and
   `tests/host/run.sh` (240 checks at the time of writing, plus whatever the
   newer tree adds).

## 5. Bring-up checks on the revised board

**AW9364 steps and wrap.** With a dev build on the console:
1. `mao backlight step 1` … `mao backlight step 16`: 16 visibly distinct,
   monotonic levels; `mao backlight 0` dark.
2. Measure the LED current (ammeter in series with the panel's VLED+, panel
   pin 7 = J301 pad 12, e.g. through an FPC breakout; or the drop across a
   temporary shunt in that feed): `mao backlight 100` ≈ 40 mA (datasheet
   2 × 20 mA, 33–47 mA spread), `mao backlight 50` ≈ 20 mA, `step 16`
   ≈ 2.5 mA. Record the values. Check down to the lowest VSYS of interest
   (full current is specified to VSYS ≈ 3.25 V).
3. Wrap test: `mao backlight step 16` (dimmest), then `mao backlight edge`
   (edge 17). If the panel jumps to full brightness, edge 17 wraps to step 1;
   if nothing changes, it saturates. The firmware never relies on either, but
   if it wraps, `aw9364_plan()` could reach a brighter step by
   (16 − n_cur + n_new) more edges instead of the 3 ms shutdown. The next
   `mao backlight` set restarts the driver from a known state.
4. Run the self-test's `backlight` step (fade 100 → 0 → 100 in 10 % steps):
   look for the 3 ms dark gap on each brighter step during the fade-in. If it
   is visible and the part wraps, use the wrap path (step 3).
5. Scope EN (GPIO45) once: enable edge, ≥ 20 µs high, then ~2 µs low / ~2 µs
   high per edge; shutdown gaps ≥ 3 ms; nothing on EN between changes.

**IR LEDs dark at boot.** Power-on, reset button, `mao reboot`, a panic
reset and a deep-sleep wake: the IR LEDs (camera on the LED window) stay dark
until `mao ir send`. GPIO38 has no reset pull and the 100 k gate pull-down holds
the MOSFET off. `mao selftest` `expander_reset` must still pass (pulse on GPIO39,
TP11 XRST).

**Charge pause by heating.** On USB with a partly discharged cell:
1. `mao power` shows `charge limit: board xx.x C` and `charging=1`.
2. Compare the IMU temperature with a thermocouple on the board at room
   temperature after 10 min idle: the LSM6DSOX offset is only specified as
   ±15 °C. If the offset is systematic, correct it in
   `sense_imu_temperature()` (or move the 43 / 40 °C thresholds) and record why.
3. Warm the board (hot air from a distance, or a warm plate) past 43 °C: the log
   shows `charging paused: board 43.x C >= 43 C`, `mao power` shows `PAUSED`,
   and the charge current (ammeter in the cell lead) falls to ~0 while MAO keeps
   running from USB. TP-level check: BQ24073 /CE (expander P6) high.
4. Let it cool below 40 °C: `charging resumed`, current back.
5. Unplug USB while paused: /CE returns low (`charging enabled again (USB
   removed)`). Plug in hot: the pause comes back at once (check on plug-in).
6. Fail-safe: with the IMU missing or its read failing, the log shows once
   `charge limit: no board temperature ... charging stays enabled`.

**IMU stays on I2C.** Power-cycle the board ten times (including fast
off/on and a brown-out from a bench supply) and check `mao sense` reports the
IMU every time (I3C-only lock-out would make it vanish until a full power
cycle). Also check the IMU wake from deep sleep still works (no pull on INT1:
the IMU drives it push-pull, active low).

**Other.** Board ID reads ≈ 1590 mV (`mao board`), inside the self-test's
1480–1700 mV; `mao board` shows `charge_control=1` and `charging=- (see mao
power)`; the expander test and reset test pass with CONFIG 0x80.

## 6. Checklist

- [ ] `pinmap.py` of the revised board in the tree; `gen_pinmap.py` run; header not hand-edited
- [ ] Patch applied except the generated header; conflicts resolved per §2
- [ ] No `MAO_PIN_LCD_BL_PWM`, `MAO_EXP_CHG_N`, `A0_BACKLIGHT_MAX_PCT`, A0 LEDC code left
- [ ] `A0_EXP_INPUT_MASK` = P7 only; expander init writes OUTPUT (P6 = 0) before CONFIG
- [ ] No pull-ups on IMU INT1/INT2; IMU wake armed only with the IMU present
- [ ] Every rail-off / sleep path sets the backlight to 0 first
- [ ] Nobody outside `mao_power` relies on `MAO_LINE_CHARGING` succeeding on the A0
- [ ] `charge_limit()` runs every 10 s on USB (also while DROWSY) and on every USB change; fails safe to enabled
- [ ] Every expander interrupt reads `MAO_LINE_SENSE_ALERT`
- [ ] Self-test `board_id` limits 1480–1700 mV; `charger` step uses the power status
- [ ] All five configurations build with zero warnings; host tests pass
- [ ] Bring-up (§5): 16 steps, ~40 mA at 100 %, wrap result recorded, fade-in gap judged, IR dark at boot, pause at 43 °C / resume at 40 °C seen, IMU temperature offset recorded, IMU survives power cycles
- [ ] Follow-ups outside firmware: `docs/hardware/mao-factory-test.md` (`board_id` 1550–1750 mV, `charger` step "CHG (expander P6)", CONFIG 0xC0, GPIO38 for EXP_RST_N/TP11) and `docs/hardware/mao-bringup.md` (R303 backlight measurement and 80 % PWM cap, "CHG reads low while charging", 270–300 mA charge current) describe the old board; `pinmap.py` notes BOARD_ID "1.65 V" and EXP_INT_N "charger status" are stale
