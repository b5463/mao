# MAO_MAIN A0: factory / board test

How an assembled MAO_MAIN A0 is validated: what the fixture provides, the
procedure, what the firmware self-test checks with which limits, and how the
test pads are used. Firmware side: `components/mao_selftest`; host side:
`tools/factory_test.py`; build profile: `sdkconfig.factory`.

## 1. Overview

| Stage | Where | What |
|---|---|---|
| 0 | AOI / visual | assembly, polarity, solder |
| 1 | pads, no firmware | rails at the test pads (§4), shorts, supply current |
| 2 | USB | flash the factory build |
| 3 | USB console (+ DMM on TP13-TP15) | firmware self-test: 24 automatic, 3 probe-pad and 6 operator steps (§3) |
| 4 | USB | flash the release firmware (NVS kept: haptic calibration, hardware record) |

The self-test is the same code in every build: `mao selftest` on any dev
console, or automatically two seconds after boot in the factory build
(`CONFIG_MAO_SELFTEST_AT_BOOT`). The factory build must never ship.

## 2. Fixture

| Item | Purpose |
|---|---|
| USB-C cable to the station PC (data) | power (VBUS 5 V), USB-Serial/JTAG console, flashing. The self-test requires USB present (PGOOD) |
| Battery or battery simulator on the cell connector (J102) | gauge and charger steps. A simulator at 3.8 V with a current limit of 1 A is ideal; a real cell also works |
| Pogo plate on the B-side probe pads (service field at 1-2 o'clock plus the pads at their sources), §4 | rail checks before firmware, reset / BOOT control, I2C sniffing, the switched rails TP13-TP15 |
| DMM (scriptable) on TP13 / TP14 / TP15 against TP1 / TP2 | proves the three switched rails really switch (`--dmm` in `factory_test.py`); without one the operator measures with a handheld DMM |
| Tag-Connect TC2030-NL cable on J201 (optional) | UART0 recovery and flashing without USB |
| Lid, matte white inside, ~30 mm above the window | IR loopback reflector (TX at the back edge, RX at 3 o'clock) and a fixed target for the proximity sensor |
| Quiet enclosure (< 45 dBA) | microphone level and the speaker -> mic acoustic loopback |
| Operator | looks (LCD, backlight), turns the ring, presses the face, touches the four zones, holds a hand over the window |

The board is tested complete with display, LRA, speaker, springs and the
ring's pole strip (in the printed carrier or a fixture carrier), because several steps
are functional, not electrical.

## 3. Self-test steps and limits

Automatic steps run first (no operator), then the operator steps. Every step
prints `SELFTEST STEP <n>/<total> <id> PASS|FAIL|SKIP <detail>`.

| # | id | Checks | Pass limits |
|---:|---|---|---|
| 1 | `board_id` | board-ID divider (GPIO8, 1M/1M, ADC1 calibrated, measured at boot) | 1480 … 1700 mV (A0 nominal 1590 on the 3.18 V rail) |
| 2 | `i2c_bus` | live scan of the bus (switchable devices powered for it) | 0x20, 0x29, 0x36, 0x44, 0x5A, 0x6A all answer |
| 3 | `expander` | TCA6408A CONFIG readback (0x80: P0-P6 outputs, P7 the alert input), POLARITY round trip 0x6A / 0x15 on the output pins, every output pin reads back its commanded level | all match |
| 4 | `expander_reset` | pulses EXP_RST_N (GPIO39, TP11 XRST): the expander must really reset (CONFIG back at its 0xFF default), then is reprogrammed from the shadow registers: CONFIG re-read, every output pin at its commanded level. The panel, ToF and haptic driver are re-initialised by their owners afterwards | CONFIG 0xFF after the pulse; 0x80 and all outputs restored after |
| 5 | `imu_id` | WHO_AM_I | 0x6C (LSM6DSOX); 0x6A (LSM6DS3TR-C) accepted, noted |
| 6 | `imu_motion` | latest accelerometer vector | 0.85 g ≤ \|a\| ≤ 1.15 g, data < 1.5 s old |
| 7 | `tof_id` | VL53L4CD model id | 0xEBAA |
| 8 | `tof_ranging` | new ranging results | ≥ 3 in 1.5 s |
| 9 | `als_id` | OPT3004 manufacturer / device | 0x5449 / 0x3001 |
| 10 | `als_lux` | latest reading | 0 ≤ lux < 120 000, < 3 s old |
| 11 | `gauge_id` | MAX17048 VERSION | 0x001x |
| 12 | `gauge_voltage` | fresh VCELL / SOC read | 3000 … 4400 mV, SOC 0 … 100 % |
| 13 | `charger` | PGOOD (GPIO3), the board temperature the charge limit uses (IMU), the charging estimate (gauge) and any pause (/CE, expander P6) | PGOOD asserted (the test runs on USB) and the board temperature readable; charging and pause reported |
| 14 | `haptic_id` | DRV2605L STATUS[7:5] through the haptics task | 7 (DRV2605L; 3 = DRV2605 accepted) |
| 15 | `haptic_cal` | auto-calibration (≈1.2 s buzz; keep the board still) | DIAG_RESULT ok, resonance 190 … 285 Hz (LRA nominal 235 Hz). Result stored in NVS |
| 16 | `rail_lcd` | display load switch off/on through the expander, enable line read back; panel re-initialised and redrawn | enable reads 0 off, 1 on |
| 17 | `rail_mic` | mic clock + supply (GPIO35) off and on, line read back, audio after power-up | supply 0 then 1; level > -100 dBFS after power-up |
| 18 | `rail_ir_rx` | receiver supply (expander P5) off/on, read back; receiver output (GPIO40, 10 k pull-up R506 to the switched supply) | supply 0 then 1; output **low while off**, idle high when powered |
| 19 | `pad_lcd` | **TP13 3V3_LCD** measured with the rail off, then on (§4.1) | off ≤ 0.30 V, on 2.85 … 3.40 V |
| 20 | `pad_mic` | **TP14 MIC_VDD** (GPIO35 through 100 R), off then on | off ≤ 0.30 V, on 2.85 … 3.35 V |
| 21 | `pad_irv` | **TP15 IR_RX_VCC** (expander P5 through 100 R), off then on | off ≤ 0.30 V, on 2.85 … 3.40 V |
| 22 | `touch_baseline` | benchmark of the four zones, untouched | each 1 000 … 4 000 000 counts **and** within ×4 of the median of the four; none reads touched |
| 23 | `mic_level` | 1 s of 32 ms blocks | ≥ 10 blocks, mean > -100 dBFS, max < -15 dBFS, max-min ≥ 0.3 dB (not stuck) |
| 24 | `speaker` | test chirp (fixed 70 % level) heard by the mic | rise ≥ 12 dB over the quiet level; else the operator confirms |
| 25 | `ir_loopback` | NEC frame 0xA5/0x3C, received as MAO's own echo | echo within 150 ms (3 tries); else the operator holds a card over the puck |
| 26 | `radio` | ESP-NOW transmit counters (discovery broadcast), MAC | ≥ 1 frame sent, 0 rejected, MAC non-zero |
| 27 | `nvs` | write / read / erase in a test namespace (`mao_st`) | value matches |
| 28 | `lcd` | red, green, blue, white, black full screen, then "MAO" text | operator: even colours, no dead pixels / tint, text upright (rotation) |
| 29 | `backlight` | 100 → 0 → 100 % ramp through the AW9364's 16 steps (U303) | operator: even steps, no flash; dark at 0 % |
| 30 | `dial` | turn clockwise ≥ 3 detents, then counter-clockwise ≥ 3 | both directions in the right sense; ≥ 6 edges on each Hall channel |
| 31 | `press` | press the face once | one debounced press |
| 32 | `touch_zones` | touch RIGHT, LEFT, TOP, REAR in turn | each zone reports touched |
| 33 | `tof_near` | a hand about 5 cm above the window at 11 o'clock | a valid reading < 120 mm |

Limits marked VERIFY AT BRING-UP in the source (`mao_selftest.c`): touch
benchmark window, LRA resonance window, speaker loopback rise, mic levels.
Tighten them once the first ten boards have been measured; the JSON records
(§6) carry the measured values for exactly that.

On the ESP32-C3-LCDkit the same command runs the subset that exists
(`speaker` by operator confirmation, `radio`, `nvs`, `lcd`, `backlight`,
`dial`, `press`): 7 steps.

### Probe-pad measurements (steps 19-21)
For each switched rail the firmware switches the rail off, prints
`SELFTEST MEASURE <step> <pad> <net> off 0 300 <text>` and waits; then
switches it on and asks again with the on-limits. The answer is
`mao selftest mv <millivolts>` (or `skip`). `factory_test.py` answers with
the fixture DMM when started with `--dmm "<command>"` (the command prints the
voltage in volts; `{pad}`, `{net}`, `{state}` are filled in), otherwise it
asks the operator to measure the pad to GND (TP1 / TP2) and type the volts.
Waits: twice the prompt timeout with an operator, 5 s in automatic mode.
`mao selftest auto nopads` / `factory_test.py --no-pads` skip the three
steps (the verdict is then INCOMPLETE). Why: the firmware can only read back
the enable lines; only a measurement at the pad proves that the load switch,
the GPIO-fed mic supply and the expander-fed receiver supply really switch.

### Verdicts
- `SELFTEST PASS 33/33`: every step passed.
- `SELFTEST FAIL 31/33 failed=tof_id,tof_ranging [skipped=...]`: at least one failure.
- `SELFTEST INCOMPLETE 27/33 skipped=lcd,backlight,dial,...`: nothing failed, something was not run
  (automatic mode, no DMM, or the operator skipped). **Only PASS passes the board.**

Then one line `SELFTEST_JSON {...}` with board, revision, ID voltage,
firmware version, MAC and every step's result and detail. The screen shows the
verdict (green PASS, red FAIL, amber INCOMPLETE) with the count.

## 4. Test pads and Tag-Connect

Every probe pad is on the B side, placed where its signal already is
(`hardware/mao/design/placement.py`), so no probe branch crosses the board.
Positions in board mm from the puck axis, y towards 6 o'clock, viewed from the
face (the pads are on B: mirror x when looking at the back). Supplies and
grounds use 1.2 mm pads, signals and the switched rails 1.0 mm. The fixture's
pogo plate carries one pin per pad; the layout is asymmetric (one grid
position holds the I2C pull-ups instead of a pad, and pads sit outside the
field), so the board cannot sit in it rotated (ODD JOBS 158).

Service field, between the charger and the module (2.8 mm grid, three rows;
each name stands upright beside its pad, on a via-free spot of its own; SCL
sits on the other side, where the I2C pull-ups take its grid spot). The rails come straight down from the power
section; every rail has a ground pad beside it for a spring-tip probe
(ODD JOBS 39):

```
   x:        6.0        8.8        11.6       14.3
   y -6.1:  TP2 GND    TP3 3V3    TP4 SYS    TP5 BAT
   y -3.3:  TP10 SCL   (R214/R213 I2C pull-ups)  TP1 GND    TP16 GND
   y -0.5:  TP9 SDA    TP11 XRST  TP8 BOOT   TP7 RST
```

At their sources:

| Pad | Where | Position |
|---|---|---|
| TP6 VBUS | on the VBUS bus between the TVS D101 and the charger; D101's ground pad beside it | (5.5, −16.95) |
| TP13 LCDV | on the 3V3_LCD rail, beside the backlight driver U303 | (−6.4, −7.2) |
| TP14 MIC | at the end of the microphone supply (C407 / C406) | (22.3, 12.1) |
| TP15 IRV | under the IR receiver's supply pin | (22.35, 0.9) |
| J201 Tag-Connect | at the module's UART pins (RXD0/TXD0 straight across from pins 36/37) | (15.2, 16.35) |

| Pad | Label | Net | Size | Use in the fixture | Expected |
|---|---|---|---|---|---|
| TP1, TP2, TP16 | GND | GND | 1.2 mm | pogo ground reference (three pads for low impedance); DMM reference for TP13-TP15 | 0 V |
| TP3 | 3V3 | +3V3 | 1.2 mm | rail check before firmware; supply current through the USB / VBAT source | 3.10 … 3.32 V (3.18 V nominal; the top includes the TPS63802 FB bias current) |
| TP4 | SYS | VSYS | 1.2 mm | charger output | ≈ 4.4 V on USB (BQ24073 DPPM), ≈ VBAT on battery |
| TP5 | BAT | VBAT | 1.2 mm | cell / simulator voltage after the reverse-polarity FET | the source voltage minus < 50 mV |
| TP6 | VBUS | VBUS | 1.2 mm | USB input after the protection | 5.0 V ± 5 % |
| TP7 | RST | MCU_EN | 1.0 mm | reset the module: pull low ≥ 1 ms (open drain) | 3.2 V idle |
| TP8 | BOOT | PRESS_N (GPIO0) | 1.0 mm | hold low while releasing RST: ROM download mode (USB or UART) | 3.2 V idle (10 k pull-up); 0 V while the face is pressed |
| TP9, TP10 | SDA, SCL | I2C_SDA / I2C_SCL | 1.0 mm | logic analyser on the shared bus while debugging a failing I2C step; never drive | 3.2 V idle (2.2 k pull-ups) |
| TP11 | XRST | EXP_RST_N (GPIO39) | 1.0 mm | observe the expander reset: a pulse during `expander_reset`; may be pulled low (open drain) to reset the TCA6408A by hand while debugging. Never hold it low while the firmware runs: every expander rail drops (and the charger's /CE pull-down R117 re-enables charging) | 3.2 V idle (10 k pull-up R205) |
| TP13 | LCDV | 3V3_LCD | 1.0 mm | `pad_lcd`: the display load switch output (TPS22919, U105) | off ≤ 0.30 V, on 2.85 … 3.40 V |
| TP14 | MIC | MIC_VDD | 1.0 mm | `pad_mic`: the mic supply from GPIO35 through 100 R / 1 µF | off ≤ 0.30 V, on 2.85 … 3.35 V |
| TP15 | IRV | IR_RX_VCC | 1.0 mm | `pad_irv`: the IR receiver supply from expander P5 through 100 R / 4.7 µF | off ≤ 0.30 V, on 2.85 … 3.40 V |

J201 (Tag-Connect TC2030-IDC-NL, no part fitted): 1 GND, 2 EN, 3 UART0 TX
(GPIO43), 4 +3V3, 5 UART0 RX (GPIO44), 6 GPIO0. TXD0 and RXD0 run straight across
from the module's pins 36/37 into the connector's module-side column; the cable adapter maps these six pins to the
programmer's EN / IO0 / TXD / RXD / 3V3 / GND. Use it to flash and recover a
board whose USB path is broken: `esptool --port <uart> --before default_reset`
drives EN and GPIO0 through the cable's DTR/RTS adapter. The USB console is
the normal path; the UART is silent in MAO's firmware (console on USB only).

Before any firmware: with USB connected and no cell, TP6 5 V, TP4 ≈ 4.4 V,
TP3 3.18 V, TP7, TP8 and TP11 high, TP13-TP15 low (every switched rail off at
reset: pull-downs on the enables, GPIO35 pulled down). With a 3.8 V source on
the cell connector and no USB: TP5 ≈ 3.8 V, TP4 ≈ 3.8 V, TP3 3.18 V. A board
that fails here is not flashed.

### 4.1 The switched rails and their pads

| Rail | Switch | Enable | Firmware readback | Pad |
|---|---|---|---|---|
| 3V3_LCD (panel logic; the backlight runs from VSYS through U303) | TPS22919 load switch (U105), QOD discharge 100 R (R115) | expander P1 `LCD_PWR_EN` | P1 pin level (expander input register) | TP13 LCDV |
| MIC_VDD | GPIO35 through 100 R / 1 µF | GPIO35 `MIC_PWR` | GPIO35 input | TP14 MIC |
| IR_RX_VCC | expander P5 through 100 R / 4.7 µF | expander P5 `IR_RX_PWR` | P5 pin level; GPIO40 low while off (R506) | TP15 IRV |

The readback proves the enable pin moves; the pad measurement proves the rail
itself follows (a failed load switch, an open 100 R or a shorted capacitor
only shows there).

### 4.2 IR receiver output
The receiver output (GPIO40) has a 10 k pull-up, R506, to IR_RX_VCC, the
switched receiver supply (not +3V3, so the unpowered receiver is never
back-powered through its output). With the supply off GPIO40 reads a defined
low; powered and idle it reads high. `rail_ir_rx` checks both.

### 4.3 Expander reset (XRST)
GPIO39 drives the TCA6408A RESET (open drain, R205 10 k pull-up). The
firmware pulses it (10 µs) in two cases: to recover a wedged expander (an
I2C write or input read fails: bus reset, pulse, reprogram from the shadow
registers, one retry) and in the `expander_reset` self-test step. During the
pulse and the reprogram (~0.2 ms) every expander output is an input, so the
rails fall to their pull-down defaults; the firmware then re-initialises the
panel, the ToF and the haptic driver. On TP11 a scope shows the pulse.

## 5. Procedure

1. Put the board in the fixture, connect USB (and the battery simulator).
2. Rail check at the pads (§4).
3. Flash the factory build:
   ```sh
   idf.py -B build-factory -D SDKCONFIG=build-factory/sdkconfig \
          -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.factory" set-target esp32s3 build
   idf.py -B build-factory -p <port> flash
   ```
   (Windows: `.\tools\idf.ps1 factory build` / `.\tools\idf.ps1 factory -p COM13 flash`.)
4. Run the station script; it resets the board and follows the test that starts at boot:
   ```sh
   python tools/factory_test.py <port> --reset --wait-boot --serial <label> --record results/ --log results/console.log \
          --dmm "<fixture DMM command> {pad}"
   ```
   It prints every step, shows the operator instructions (turn, press,
   touch, hand over the window, card over the puck), takes the TP13-TP15
   readings (from the DMM command, or asks the operator for the volts) and
   asks the look / listen questions (`y` / `n` / `s` = show again or skip).
   A press of the face also answers "yes". Exit status 0 = PASS.
5. Close the lid for the IR and proximity steps when asked (or keep it closed:
   the IR loopback then passes without a prompt).
6. On PASS: flash the release firmware **app only** (`idf.py -B build-release ... app-flash`), so the
   NVS partition keeps the haptic calibration and the hardware record.
   On FAIL: the JSON record names the failing steps; see §7.

With a dev build instead of the factory build, step 4 is
`python tools/factory_test.py <port>` (it sends `mao selftest`).
`--auto` runs only the automatic steps (result INCOMPLETE at best).

## 6. Records and boot check

- `--record DIR` writes `<serial or MAC>-<time>.json` per run: verdict, every step with
  its measured detail, every probe-pad reading (`pads`: pad, net, state, mV,
  limits), and the device's own JSON (board, revision, ID mV, firmware, MAC).
- Every boot also runs a **boot check** (no extra I/O): which fitted devices
  failed bring-up, and whether the board revision is known. On the first boot
  of a board (or a new revision) it prints
  `BOOTCHECK first board="MAO_MAIN A0" rev=A0 id_mv=1652 faults=0x0000 none`.
  Faults are stored in NVS (`mao`/`hw_rev`, `mao`/`hw_flt`) and shown with
  `mao selftest boot`. A fatal fault (no display, no input) makes MAO show
  its wordmark with a service code (`SERVICE nn`, nn = lowest fault bit + 1)
  instead of the character; anything else degrades gracefully.

| Fault bit | Name | Bit | Name |
|---:|---|---:|---|
| 0 | display (fatal) | 8 | touch |
| 1 | input (fatal) | 9 | mic |
| 2 | expander | 10 | ir |
| 3 | imu | 11 | audio |
| 4 | proximity | 12 | board_id |
| 5 | light | 13 | radio |
| 6 | fuel_gauge | 14 | nvs |
| 7 | haptic | 15 | power |

## 7. Failure hints

| Failing step | First suspects |
|---|---|
| `board_id` | R (1 M) values / placement on GPIO8, missing 100 nF |
| `i2c_bus` missing 0x20 | expander U202 (B side, centre), its ADDR strap; then almost everything else fails too |
| `i2c_bus` missing 0x29 / 0x5A | XSHUT (P4) / HAPTIC_EN (P3) lines; the device itself |
| `expander` bad output pins | short on a rail enable line (LCD_PWR_EN, AMP_SD_N, HAPTIC_EN, TOF_XSHUT, IR_RX_PWR, LCD_RST_N, CHG_CE_N) |
| `imu_motion` | IMU orientation / soldering (LGA-14) |
| `tof_ranging` | XSHUT, GPIO1 interrupt line (GPIO48, 10 k pull-up), window crosstalk |
| `gauge_voltage` | VBAT path (Q101, R108 link), cell connector polarity |
| `charger` | PGOOD pull-up (100 k R106 on GPIO3), USB-C CC resistors; no board temperature: the IMU (U401) |
| `haptic_cal` | LRA leads on J501, LRA not free (glue), DRV2605L supply |
| `expander_reset` CONFIG not 0xFF | EXP_RST_N open / stuck high (GPIO39, R205, TP11), expander RESET pin solder |
| `expander_reset` not restored | expander or bus after reset (see `i2c_bus`), a rail enable shorted |
| `rail_lcd` | TPS22919 (U105), LCD_PWR_EN, its pull-down R116 |
| `pad_lcd` on low / off high | TPS22919 (U105) or its ON pin; QOD discharge path (R115) |
| `pad_mic` | R402 100 R open, C406 / C407 short, GPIO35 drive |
| `pad_irv` | R505 100 R open, C506 short, expander P5 |
| `rail_mic`, `mic_level` | GPIO35 supply RC (100 R / 1 µF), mic port hole, PDM CLK/DATA |
| `rail_ir_rx` | P5 supply RC (100 R / 4.7 µF), receiver U503, output pull-up R506 (output not low while off, or not high when powered) |
| `speaker` | speaker contacts on LS501 (cradle pressure), MAX98357A SD_MODE (P2), I2S lines |
| `ir_loopback` | IR LEDs D501/D502 orientation, Q501, the receiver; no reflector |
| `touch_baseline` | spring contacts (TOP J302, REAR J303), rim electrodes, 510 R series parts |
| `dial` direction | `A0_DIAL_REVERSE` (firmware) or a swapped Hall pair |
| `lcd` text not upright | `CONFIG_MAO_A0_LCD_ROTATION` (try `mao rotate 270`, then `mao rotate save`) |
