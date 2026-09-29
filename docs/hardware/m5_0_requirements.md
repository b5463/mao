# MAO M5.0: Rev A hardware requirements (Gate A draft)

> **Superseded in part by Gate B** (`m5_0_gate_b.md`). The owner chose
> the ESP32-S3 for Rev A. The ESP32-C3 decision, the I/O expander and the
> C3 GPIO budget below are kept only as the record of the Gate A reasoning.
> The subsystem requirements still apply.

Status: **draft for review, Gate A**. There is no schematic and no layout yet.

The software baseline is `feat/mao-m4.1-ui-motion` at `8a4f67b` (M4.1, frozen).
The hardware exists to support that experience unchanged.

This document was written for:
- the owner, deciding what goes on the Rev A board;
- whoever draws the schematic next.

## Sources

The facts below were checked against these. A value marked *(verify)* has
not been checked yet and must be confirmed at Gate B.

- **ESP32-C3-MINI-1 / MINI-1U datasheet v2.2** (Espressif). Used for the pin
  table, strapping pins, boot modes, the RTC power domain and current
  consumption.
- **ESP-IDF sleep modes (ESP32-C3).** Only RTC IOs (the VDD3P3_RTC domain,
  GPIO0–5) can wake the chip from deep sleep. The level is low or high per
  pin, and a pulse must last at least 3 RTC slow-clock cycles.
- **ESP32-C3-LCDkit user guide.** This is the prototype, used as the
  baseline pin map.
- **The M4.1 bench.** These findings came from the prototype
  (docs/m4_1_ui_motion_concept.md §10):
  - with CPU power-down on, light sleep hangs;
  - USB-Serial-JTAG does not survive light sleep;
  - the PDM floor is a 38 % pulse train;
  - the panel has no TE line.

## Facts that drive the design

| Fact | Consequence |
|---|---|
| 15 usable GPIOs on the module (IO0–10, IO18/19 USB, IO20/21 UART0) | The pin budget is the main constraint (see "GPIO budget") |
| Only IO0–IO5 can wake deep sleep | The encoder press, one encoder line and the IMU interrupt must sit on IO0–5 |
| Straps: IO9 = 1 for SPI boot (weak pull-up); download needs IO8 = 1 and IO9 = 0; IO2 should be pulled up | Nothing may pull IO8 or IO9 low at reset. The LCDkit's encoder press on IO9 is a boot risk: pressing during reset gives download mode |
| Wi-Fi peaks at 350 mA transmitting (802.11b, 20.5 dBm) and 82 mA receiving | Supply sizing. MAO runs ESP-NOW with power save off, so the radio receives at ~82 mA whenever it is up |
| Light sleep is 130 µA; deep sleep is 5 µA (chip only) | Deep sleep only pays off if the display, amplifier and regulators are switched off too |
| M4.1 image: release 1.227 MB, dev 1.319 MB; 88 KB heap free, no PSRAM | 4 MB flash (the LCDkit's size) is enough with OTA. 8 MB is optional headroom. No PSRAM |

## Requirements

**MUST** means Rev A isn't accepted without it. **SHOULD** is strongly
wanted. **OPTIONAL** is only if space, pins and cost allow.

### MCU and memory
- **MUST:** ESP32-C3 on a certified module, ESP32-C3-MINI-1 (PCB antenna)
  or MINI-1U, with 4 MB flash. This keeps the proven firmware, ODD BUS radio,
  performance and cost. See the MCU decision in the review.
- **SHOULD:** 8 MB flash (the MINI-1 H8 variant *(verify ordering code)*)
  for OTA plus assets headroom.
- **Not wanted:** PSRAM (no requirement for it).

### Display
- **MUST:**
  - a 1.28" 240×240 round panel, GC9A01 (or a validated compatible), over SPI;
  - **the TE output on the FPC, routed to an MCU GPIO**;
  - a deliberate display **reset line**;
  - a **switched display supply** (load switch), so deep sleep isn't
    dominated by the panel;
  - **PWM backlight through a driver** (a low-side MOSFET or a constant-current
    LED driver). It must dim smoothly, go very low (the 3 % sleep level) and
    fully off, with no visible flicker, running PWM at ≥ 20 kHz as M4.1 does
    at 30 kHz;
  - a real, documented commercial panel with an FPC pinout and mechanical
    drawing.
- **SHOULD:** series damping footprints on SCLK and MOSI.

### Input
- **MUST:**
  - an EC11-class rotary encoder with push. Keep M4.1's feel: its detents
    per revolution and quadrature cycles are calibrated in firmware (the
    `mao_input` rest mask);
  - the **encoder press on an RTC GPIO** (deep-sleep wake on press);
  - **encoder line A on an RTC GPIO** (deep-sleep wake on turn);
  - external pull-ups, and ESD protection on the encoder lines;
  - **the encoder press must not be on a strapping pin**.
- **SHOULD:** RC footprints on A, B and SW, populated only if needed. The
  firmware decoder already debounces, and over-filtering hurts fast turns.

### Deep-sleep wake (hard requirement)
- **MUST:** a press wakes MAO from the deepest normal user sleep. There is no
  hidden reset, no USB plug-in and no timer.
- **SHOULD:** turning wakes it too, from line A on an RTC GPIO.
- **SHOULD:** the IMU interrupt (wake-on-motion, open-drain active-low with
  a pull-up) on an RTC GPIO, so MAO wakes when it is picked up.

### Audio
- **MUST:** a small speaker and a class-D amplifier with a **shutdown or
  enable pin under MCU control**. That pin gets a **hardware pull-down**, so
  the amplifier is off through reset and boot. This removes the uncontrolled
  amplifier state behind M4.1's clicks, whir and reset click.
- **MUST:** a power sequence of stream-at-floor, then amplifier enable (and
  the reverse on shutdown), following the amplifier datasheet.
- **SHOULD:** keep the 1-pin PDM → RC filter → analogue amplifier topology.
  It saves pins; an I2S amplifier would need 3.

### Motion and haptics
- **MUST:** a 6-axis IMU (3-axis accelerometer and 3-axis gyroscope) with a
  low-power wake-on-motion interrupt. It goes on I2C.
- **MUST:** at least one IMU interrupt to an MCU GPIO, on an RTC GPIO for
  wake.
- **MUST:** a documented IMU axis convention relative to the enclosure (for
  example +X screen right, +Y screen top, +Z out through the display).
- **MUST:** the IMU mounted on a mechanically stable area, away from the
  speaker, the haptic actuator, the switching regulator and high-current
  traces.
- **MUST:** a haptic actuator with a driver: an LRA (or ERM) and an I2C
  haptic driver with an enable or standby mode.
- **Open:** the rest of the brief's IMU and haptics requirements were cut off
  in the message and are needed before Gate B.

### IR
- **SHOULD:** keep IR, with transmit and receive. The GPIO budget may force
  shared transmit and receive on one pin, as the LCDkit does. See the review.

### Indicator
- **OPTIONAL:** a status LED. An addressable RGB LED costs a GPIO that isn't
  there. A plain LED on the I/O expander is the proposal. Whichever is used
  must have sub-µA standby and no strap conflict.

### Power
- **MUST:**
  - a single-cell Li-ion or LiPo battery (3.0–4.2 V) with protection, sized
    once the power budget is measured;
  - USB-C charging with a power path, so MAO runs while plugged in, with
    thermal regulation and a sane input current;
  - a regulated 3.3 V system rail that holds through the 350 mA radio
    transmit peaks across the whole battery range;
  - quiescent and shutdown current checked for every always-on part
    (charger, regulator, fuel gauge, IMU, I/O expander, amplifier, haptic
    driver);
  - a **deep-sleep target of ≤ 50 µA total** (to be confirmed by
    measurement).
- **MUST:** a battery measurement source, either a fuel gauge on I2C or a
  switched ADC divider. The UI to show it is not part of M5.0.
- **MUST:** a **current-measurement link** (a 0 Ω link or jumper) in the
  battery-to-system path, plus test points.

### USB
- **MUST:**
  - one USB-C port for charging, native USB data (USB-Serial-JTAG on
    IO18/19), development, service and factory programming;
  - 5.1 kΩ pull-downs on both CC pins;
  - ESD protection on D+, D− and VBUS;
  - D+/D− routed as a proper differential pair.
- **MUST, policy:** with a USB data host attached, MAO stays awake, because
  light sleep breaks the console. On a charge-only supply it sleeps normally,
  including deep sleep. The firmware can already tell them apart
  (`usb_serial_jtag_is_connected()` sees the host's SOF packets).

### Service, test and ID
- **MUST:**
  - EN and BOOT (IO9 low) reachable on fixture pads, with no user buttons;
  - pads for GND, 3V3, VBAT, VBUS, backlight, amplifier enable, TE and the
    wake line;
  - factory flashing over USB (the UART0 pins may be taken by other signals).
- **MUST:** a board revision ID readable by firmware. The proposal is two
  I/O-expander pins strapped at build; the fallback is NVS at manufacturing.
- **MUST:** a `mao_board` target, so the same firmware builds for both the
  LCDkit and Rev A, with no UI differences between them.

### PCB and mechanical
- **MUST:**
  - 4 layers (signal / solid GND / power and signals / signal);
  - the module's antenna keepout respected at the board edge, clear of the
    battery, the display metal, the speaker and connectors;
  - a board outline following MAO's enclosure;
  - the ODD JOBS symbol only (never the wordmark) as a small maker's mark,
    plus "MAO REV A".
- **MUST:** ERC, DRC, DFM and the independent net review before ordering
  (brief §104–105).

## GPIO budget (preliminary, see the review)

This is the proposal the review argues for. It uses all 15 pins, so there is
no spare GPIO. The slow signals move to an I2C I/O expander.

| GPIO | Signal | Why this pin |
|---|---|---|
| IO0 | ENC_SW (active low, pull-up) | RTC wake on press |
| IO1 | ENC_A (pull-up) | RTC wake on turn |
| IO2 | IMU_INT1 (open-drain, active low, pull-up) | RTC wake on motion; strap wants a pull-up, satisfied |
| IO3 | AUDIO_PDM | LEDC / I2S output |
| IO4 | ENC_B (pull-up) | Fast input (either line may interrupt) |
| IO5 | LCD_BL (PWM) | LEDC |
| IO6 | LCD_SCLK | FSPICLK native |
| IO7 | LCD_MOSI | FSPID native |
| IO8 | I2C_SDA (pull-up) | Strap wants 1 at boot, satisfied by the pull-up |
| IO9 | I2C_SCL (pull-up) + BOOT pad | Strap wants 1 for SPI boot; the fixture pulls it low for download |
| IO10 | LCD_TE (input) | Interrupt on the panel's TE |
| IO18 / IO19 | USB D− / D+ | Native USB |
| IO20 | LCD_DC | — |
| IO21 | IR (shared TX/RX) *or* LCD_CS | See the review: one of these must give way |

On the I2C expander (8-bit, µA quiescent):
- LCD_RST
- AMP_EN (with a hardware pull-down)
- HAPTIC_EN
- LCD power switch enable
- the status LED
- CHG_STAT
- BOARD_ID0/1
