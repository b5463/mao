# MAO M5.0 — Gate B
# ESP32-S3 Rev A Architecture Review

- **Branch:** `feat/mao-m5-rev-a`, from `8a4f67b` (M4.1 frozen).
- **Status:** for review. There is no schematic, layout or placement yet.
- **Markings:** values checked against vendor documents in this pass are
  plain. **(verify)** means not yet checked against a datasheet. **(est.)**
  is an estimate.

## 1. Exact MCU / module recommendation

**Espressif ESP32-S3-MINI-1-N8**
- **Flash:** 8 MB quad-SPI, in package.
- **PSRAM:** none.
- **Size:** 15.4 × 20.5 × 2.4 mm.
- **Antenna:** on-board PCB antenna.
- **Temperature:** −40 to 85 °C.

That's from the ESP32-S3-MINI-1/MINI-1U datasheet v1.7, table 1-1. The
MINI-1U-N8 is the same with an antenna connector, at 15.4 × 15.4 mm. It's
the fallback if the enclosure forces an external antenna.

**Why this exact part:**
- It's certified with a known-good antenna, so RF risk and layout effort are
  low.
- It's the smallest S3 module with 8 MB and no PSRAM, which matches the brief.
- IO26 is free on the N8. On the N4R2 variant it goes to PSRAM.
- There are no octal flash or PSRAM pins taken. WROOM variants with octal
  PSRAM lose IO35–37.

**MINI vs WROOM:** the WROOM-1 is larger (18 × 25.5 mm class *(verify)*). It
only buys PSRAM and octal options, which MAO doesn't need, so it isn't
chosen.

## 2. S3 vs C3

| | ESP32-C3 (prototype) | ESP32-S3-MINI-1-N8 |
|---|---|---|
| CPU | RISC-V, 1 core, 160 MHz | Xtensa LX7, 2 cores, 240 MHz |
| SRAM | 400 KB | 512 KB (+16 KB RTC) |
| Flash | 4 MB (LCDkit) | 8 MB |
| Usable GPIO on the module | 15 | 39 (IO0–21, 26, 33–48) |
| Deep-sleep wake GPIOs | IO0–5 | **IO0–21 (all RTC GPIOs)** |
| USB | USB-Serial-JTAG | USB-Serial-JTAG + USB OTG (IO19/20) |
| Deep sleep | 5 µA | 7 µA (RTC memory), 8 µA (RTC peripherals on) |
| Light sleep | 130 µA | 240 µA |
| Wi-Fi RX / TX peak | 82 / 350 mA | 95 / 355 mA |
| Modem-sleep (CPU at 80 MHz) | ~13–22 mA | ~22–47 mA |

**Firmware implications:**
- The same ESP-IDF and the same component split. Only `mao_board` and the
  low-level sleep and audio pieces change.
- The second core can take LVGL, which gives frame-rate headroom.
- Deep-sleep wake moves from the C3 GPIO wake to the S3's **ext0/ext1** RTC
  wake.
- The ESP-NOW encrypted-peer limit of 10 is set explicitly and carries over.
- **The honest cost:** active current is about 15–25 % higher (the RX and
  modem-sleep figures above).

## 3. S3 vs C6 / C5

| | S3-MINI-1 | C6-MINI-1 | C5 |
|---|---|---|---|
| CPU | 2 × LX7, 240 MHz | RISC-V, 160 MHz | RISC-V single core |
| SRAM | 512 KB | 512 KB HP + 16 KB LP | *(verify)* |
| GPIO | 39 | 22 | 29 (chip) |
| Radio | 2.4 GHz Wi-Fi 4 + BLE 5 | 2.4 GHz Wi-Fi 6 + BLE 5 + 802.15.4 | 2.4 + 5 GHz Wi-Fi 6 + BLE 5 + 802.15.4 |
| USB | Serial/JTAG + OTG | Serial/JTAG | Serial/JTAG |
| ESP-NOW to the C3 endpoints | yes | yes | yes, on 2.4 GHz |
| Port effort from C3 | moderate (Xtensa, new pins) | low–moderate (RISC-V) | moderate (newest silicon) |

**Result: the S3.** The C6 has too few GPIOs to leave real spares once
everything is direct (22 vs about 30 needed). The C5's dual band doesn't help
ODD BUS on 2.4 GHz, and it's the least proven module path. The C6's 802.15.4
radio is unused. Nothing here overturns the S3.

## 4. Complete preliminary GPIO map

**Rules:**
- straps (GPIO0, 3, 45, 46) carry nothing user-accessible;
- USB (IO19/20) stays reserved;
- the display SPI sits on the **IO_MUX FSPI pins** (IO10–12), because full
  80 MHz needs the IO_MUX path *(verify the S3 GPIO-matrix SPI limit)*;
- wake sources sit on RTC GPIOs;
- every enable has a hardware default-off pull.

| GPIO | Signal | Dir | RTC / wake | Strap | Reset / default | Pull | Deep sleep | Notes |
|---|---|---|---|---|---|---|---|---|
| 0 | — (BOOT pad) | — | RTC | **strap** (1 = SPI boot) | weak pull-up | ext 10 k up | — | fixture pad for download; nothing else |
| 1 | ENC_SW | in | RTC, **ext1 wake (low)** | — | input | ext 1 M up | armed | press wake |
| 2 | ENC_A | in | RTC, **ext0 wake (level)** | — | input | ext 1 M up | armed at the opposite level | turn wake |
| 3 | — | — | RTC | **strap** (JTAG source) | floating | — | — | left unused |
| 4 | IMU_INT1 | in | RTC, **ext1 wake (low)** | — | input | ext 100 k up (open-drain) | armed | wake-on-motion |
| 5 | IMU_INT2 | in | RTC | — | input | ext 100 k up | — | FIFO / data-ready |
| 6 | ENC_B | in | RTC | — | input | ext 1 M up (see §13) | pull-up off | not a wake source |
| 7 | LCD_PWR_EN | out | RTC | — | Hi-Z | **ext 100 k down** | low (panel off) | load switch |
| 8 | LCD_BL | out (LEDC) | RTC | — | Hi-Z | **ext 100 k down** | low | backlight driver gate |
| 9 | LCD_TE | in | RTC | — | input | — | — | TE interrupt |
| 10 | LCD_CS | out | RTC | — | Hi-Z | ext 100 k up | — | IO_MUX FSPICS0 |
| 11 | LCD_MOSI | out | RTC | — | — | — | — | IO_MUX FSPID |
| 12 | LCD_SCLK | out | RTC | — | — | — | — | IO_MUX FSPICLK |
| 13 | LCD_DC | out | RTC | — | — | — | — | |
| 14 | LCD_RST | out | RTC | — | Hi-Z | ext 100 k down | low | held in reset while unpowered |
| 15 | BOARD_ID | ADC2 | RTC | — | input | divider | — | read once at boot, before Wi-Fi |
| 16 | CHG_STAT | in | RTC | — | input | ext pull-up | — | charger status (open-drain *(verify)*) |
| 17 | I2S_BCLK | out | RTC | — | — | — | — | amplifier |
| 18 | I2S_WS | out | RTC | — | — | — | — | amplifier |
| 19 / 20 | USB D− / D+ | — | — | — | — | — | — | native USB, reserved |
| 21 | I2S_DOUT | out | RTC | — | — | — | — | amplifier |
| 33 | RGB_DATA | out (RMT) | — | — | — | — | — | WS2812-class |
| 34 | IR_RX | in (RMT) | — | — | — | — | — | demodulated receiver output |
| 35 | IR_TX | out (RMT) | — | — | Hi-Z | ext 100 k down | low | IR LED driver gate |
| 36 | HAPTIC_TRIG | out | — | — | Hi-Z | ext 100 k down | low | DRV2605L IN/TRIG |
| 37 | HAPTIC_EN | out | — | — | Hi-Z | ext 100 k down | low | DRV2605L EN |
| 38 | AMP_SD | out | — | — | Hi-Z | **ext 100 k down** | low | amplifier shutdown (off by default) |
| 39 | RGB_PWR_EN | out | — | — | Hi-Z | ext 100 k down | low | the RGB LED's idle current must go |
| 40 | IR_RX_PWR_EN | out | — | — | Hi-Z | ext 100 k down | low | the IR receiver's idle current must go |
| 41 | *spare* | — | — | — | — | — | — | test pad |
| 42 | *spare* | — | — | — | — | — | — | test pad |
| 43 / 44 | *spare* (UART0 TX/RX) | — | — | — | — | — | — | factory log pads |
| 45 | — | — | — | **strap** (VDD_SPI voltage) | weak pull-down | — | — | must stay 0 for the 3.3 V flash: leave unused |
| 46 | — | — | — | **strap** (boot / ROM print) | weak pull-down | — | — | left unused |
| 47 | I2C_SDA | io | — | — | — | ext 4.7 k up | — | IMU, fuel gauge, haptic driver. GPIO47/48 run on VDD_SPI, which is 3.3 V with this module's quad flash *(verify)* |
| 48 | I2C_SCL | out | — | — | — | ext 4.7 k up | — | |
| 26 | *spare* (N8 only) | — | — | — | — | — | — | goes to PSRAM on the R2 variants |

## 5. Spare GPIO count

**Five genuinely usable spares**: IO41, IO42, IO43, IO44, and IO26 (N8 only).
IO5 (IMU_INT2) is optional. The JTAG function isn't needed, because
debugging runs over USB-Serial-JTAG. The brief's target was at least 4.

## 6. Display architecture
- **Bus:** SPI at 80 MHz with DMA and 40-line double buffers, unchanged from
  M4.1.
- **Signals:** a real **CS**, **RESET**, **TE** to an interrupt pin, and
  **power enable**.
- **Panel power:** a TI **TPS22916** load switch. It handles 1–5.5 V at 2 A,
  with 60–100 mΩ on-resistance, 0.5 µA quiescent when on and **10 nA off**.
  Its "B" and "C" versions set the rise time, and it has 150 Ω quick output
  discharge in a 0.78 × 0.78 mm package. The display is then fully off in
  deep sleep.
- **Backlight:** a low-side N-MOSFET (or a small LED driver) switching the
  LED string at ≥ 20 kHz, 30 kHz as today. A pull-down keeps it off through
  reset. The backlight is on a separate supply branch from audio.
- **Panel:** **not locked.** It must expose TE on its FPC. The common 8-pin
  breakouts don't; an 18-pin FPC GC9A01 variant with TE (pin 14) exists.
  Datasheets and drawings are to be requested from **Winstar**,
  **DisplayModule (DM-TFTR128-466)** and **Raystar**. The PCB isn't locked
  until the FPC pinout is confirmed in writing. For quality, judge black
  level, IPS viewing angle, the lowest usable brightness, and the LED count
  and current.

## 7. Encoder / wake architecture
- **Lines:** A (IO2), B (IO6) and PRESS (IO1), all direct and none on a
  strap.
- **Press wake:** ext1 on IO1 and the IMU on IO4, set to wake when either
  goes **low**.
- **Turn wake:** ext0 on IO2, armed at the opposite of A's current level
  before sleeping. Any detent flips A within the first half-step.
- **Wake cause:** read from `esp_sleep_get_wakeup_causes()` and the ext1
  status, which says which pin fired. That gives press, turn, motion, timer
  or cold boot.
- **What the waking touch does:** M4.1's `mao_wake_eat` rules are unchanged.
  - **Press wake:** the press, its release and its click are eaten.
  - **Turn wake:** the detents during the wake boot are never decoded, and
    the 350 ms quiet window eats the rest. There's no level change, no
    phantom detent and no wrong direction.
- **Leakage:** an EC11 can rest with A and B low, at half the detents. With
  10 kΩ pull-ups that's 330 µA per line, far over the whole deep-sleep
  budget. So:
  - A and SW get **1 MΩ** pull-ups (3.3 µA worst case each), plus a small
    capacitor footprint;
  - B's pull-up is switched off, or B is left floating-safe, in deep sleep.
  - The awake debounce behaviour must be re-checked with 1 MΩ *(verify on
    the prototype: swap the LCDkit resistors)*.

## 8. IMU selection

| | TDK ICM-42670-P | Bosch BMI270 | ST LSM6DSOX |
|---|---|---|---|
| 6-axis low-noise current | **0.55 mA** | 685 µA (full ODR) | *(verify)* |
| Accel low-power / WoM | **4.4 µA at 1.56 Hz**, wake-on-motion | any- / no-motion, significant motion *(current: verify)* | activity / wake-up *(verify)* |
| Sleep | 3.5 µA | *(verify)* | *(verify)* |
| Interrupts | 2 | 2 | 2 *(verify)* |
| Noise | *(verify)* | accel 160 µg/√Hz, gyro 0.007 dps/√Hz | *(verify)* |
| Interface | I2C 1 MHz / I3C / SPI | I2C / SPI | I2C / SPI |
| Package | 2.5 × 3.0 × 0.76 mm | 2.5 × 3.0 × 0.8 mm LGA-14 | *(verify)* |
| ESP-IDF | **official Espressif component `espressif/icm42670`** (I2C; data, sensitivity and power-down; no wake-on-motion or interrupt setup yet) | Bosch API (C) | ST C driver |

- **Recommendation: TDK ICM-42670-P** on I2C. It has the lowest verified
  wake-on-motion current (4.4 µA), an official ESP-IDF driver, and a small
  package. INT1 on IO4 does the wake, INT2 on IO5 the FIFO.
- **What we have to write:** the wake-on-motion and interrupt register setup,
  in `mao_motion`.
- **Alternative:** BMI270, for its richer motion features on chip.
- **Axis convention:** +X screen right, +Y screen top, +Z out through the
  display. It will be recorded in the schematic, PCB silkscreen, board file
  and docs.
- **Placement:** a stable central area, away from the speaker, the LRA, the
  regulator's switching node and board edges.

## 9. Haptic architecture
- **Driver: TI DRV2605L.**
  - Drives both LRA and ERM, with auto-resonance tracking for LRAs and a
    licensed effect library.
  - Controlled over I2C, PWM or a hardware trigger.
  - Runs on 2–5.2 V, draws **4 µA shut down**, and starts in 0.7 ms.
  - Packages: DSBGA-9 or VSSOP-10.
- **Pins:** EN on IO37 and IN/TRIG on IO36, both direct, since the trigger is
  latency-sensitive.
- **Power:** the driver runs from 3.3 V or SYS *(verify the peak current
  against the rail)*.
- **LRA criteria:**
  - an 8–10 mm coin or small bar LRA;
  - resonance matched to the driver's range;
  - rated at or below 2 V RMS;
  - fast start and stop, suited to taps rather than buzzes.
  - **Candidates:** Vybronics or Jinlong coin LRAs *(verify)*.
- **Mechanics:** bonded to a structural wall of the enclosure, not floating.
  Kept away from the IMU. The IMU will sense its pulses; firmware marks
  "self-haptic" windows so they aren't read as motion.

## 10. Audio
- **Recommendation: an I2S class-D amplifier with shutdown,
  MAX98357A-class** *(verify: pops, shutdown current, SD_MODE levels)*.
- **Why it changes from the prototype:** a digital-input amplifier makes
  silence simply zero samples. There's no PDM floor at 38 % and no DC step,
  so **the root cause of M4.1's clicks and the whir goes away**. The C3 had
  no pins for I2S; the S3 has room.
- **Pins:** BCLK on IO17, WS on IO18, DOUT on IO21, SD on IO38 with a
  pull-down.
- **Startup:** power, then the I2S clock running with zero samples, then
  release SD, then play. **Shutdown** is the reverse. SD is held low through
  reset, boot and deep sleep.
- **Firmware:** `mao_audio` moves from PDM TX to I2S standard TX. The sound
  vocabulary is unchanged.
- **Speaker:** about 8 Ω and about 1 W class, in a small sealed cavity. The
  priority is clean cues, not volume.

## 11. IR
- **Separate lines:** TX on IO35 and RX on IO34, both on RMT.
- **TX:** an N-MOSFET or NPN driving the IR LED from SYS or 3.3 V, with a
  pull-down on its gate.
- **RX:** a 38 kHz demodulating receiver (TSOP-class *(verify part)*),
  powered through IO40 so it's **off in deep sleep**. It isn't a wake source
  in Rev A.

## 12. RGB
The addressable RGB LED (WS2812-class, on RMT) stays, on IO33. Its supply is
**gated by IO39**, because an idle addressable LED draws current that would
break the ≤ 50 µA target. Whether the enclosure shows it is a later decision.

## 13. Power architecture
- **Battery:** 1S Li-ion or LiPo (3.0–4.2 V) with protection (a protected
  cell or a protection IC). Capacity is chosen after measurement.
- **Charger: TI BQ25185.**
  - 1 A linear charger with a **power path**: SYS is regulated at 4.5 V on
    USB and supports 3.125 A system discharge.
  - Input 3–18 V, tolerating 25 V.
  - **4 µA battery-only quiescent**, and **3.2 µA** in ship/factory mode.
  - Thermal regulation and shutdown, and faults for input over-voltage,
    battery under-voltage and short circuit.
  - WSON-10, 2.2 × 2 mm.
  - Its status and ship-mode pins are *(verify)* against the full datasheet.
- **3.3 V rail: TI TPS63802** buck-boost.
  - Input 1.3–5.5 V, starting above 1.8 V.
  - **2 A at 3.3 V** for inputs of 2.3 V and up, so the S3's 355 mA transmit
    peaks, the haptics and the audio fit with margin.
  - **11 µA quiescent**, with a power-save mode.
  - VSON 3 × 2 mm, with a 0.47 µH inductor and at least 22 µF out.
  - Plus bulk capacitance at the module: 22–47 µF near its 3V3 pins, with
    decoupling per Espressif's guidance.
- **Fuel gauge: MAX17048-class** on I2C *(verify hibernate current)*, with
  ALRT to a spare pin if wanted.
- **Measurement:** a 0 Ω link or jumper between the charger's SYS and the
  regulator input, plus pads on VBAT, SYS, 3V3 and VBUS.

**Power domains:**

| Part | Active | Visible rest | Deep sleep |
|---|---|---|---|
| S3 | on | light sleep (240 µA) or awake | deep, ext0/ext1 (≈ 8 µA) |
| Display | on | on, 3 % backlight | **off** (load switch) |
| Backlight | PWM | 3 % | off |
| IMU | as needed | low-power | **wake-on-motion (4.4 µA)** |
| Amplifier | as needed | shutdown | shutdown |
| Haptics | as needed | shutdown | shutdown (4 µA) |
| RGB / IR RX | as needed | off | **off** (gated) |
| Fuel gauge | on | on | hibernate |
| Charger / regulator | on | on | on (4 + 11 µA) |

**Deep-sleep budget (est.):**

| Item | Current |
|---|---|
| S3 | 8 µA |
| IMU wake-on-motion | 4.4 µA |
| TPS63802 | 11 µA |
| BQ25185 (battery-only) | 4 µA |
| MAX17048 hibernate | ~3 µA *(verify)* |
| DRV2605L shutdown | 4 µA |
| Amplifier shutdown | ~1 µA *(verify)* |
| Encoder pull-ups | ≤ 7 µA |
| Load switch and other leakage | ~1 µA |
| **Total** | **≈ 43 µA**, within the ≤ 50 µA target |

The budget is tight. If it runs over, the first levers are to power the
DRV2605L from a switched rail, and to find a lower-quiescent regulator.

## 14. USB-C
- **One port** for charging, native USB-Serial-JTAG (IO19/20, for console,
  flashing, service and factory programming) and OTG if ever needed.
- **Protection:** 5.1 kΩ on each CC pin, ESD protection on D+, D− and VBUS
  (TPD2E-class *(verify)*), and VBUS into the charger's protected input.
- **Routing:** D+/D− as a 90 Ω differential pair.
- **Policy:** with a data host (SOF packets seen), MAO stays awake for
  service. On a charge-only supply, sleep is normal, including deep sleep.
  M4.1's firmware already makes this distinction.
- **Factory:** fixture pads for USB D+/D−, VBUS, GND and EN, plus the GPIO0
  BOOT pad.

## 15. ODD BUS
- **Transport unchanged:** ESP-NOW over 2.4 GHz Wi-Fi, on the same channel.
  Discovery stays plaintext and minimal, unicast is encrypted, and the link
  stays authenticated at application level.
- **Endpoints unchanged:** CAMERA 01 and LAMP 01 stay on the ESP32-C3 with
  no change. ESP-NOW frames are the same across Espressif chips.
- **Peer limits:** `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM=10` is set
  explicitly (the IDF default is 7), for 8 secure peers plus margin.
- **Frozen:** security, contract, wire v1 and exactly-once ACTION are
  untouched.
- **After deep sleep:** relationships and credentials stay in NVS, and
  sessions come back fresh. That path is already proven with the DEV deep
  sleep.
- **Identity:** the controller identity isn't the MCU's MAC. A **development
  migration tool** provisions the bench MAO's identity and relationships onto
  Rev A. It's for service and development only; every production unit gets
  its own identity.

## 16. Radio / antenna
- **Placement:** the module at the **board edge**, with the antenna end
  overhanging or flush, and the Espressif keepout on every layer.
- **Keep clear of:** the battery, the display's metal frame, the speaker
  magnet, the LRA and the USB-C shell.
- **Enclosure:** RF-transparent near the antenna.
- **Validation:** RSSI, range and packet loss against the LCDkit, with CAMERA
  and LAMP at once.

## 17. GPIO expander
**Not needed.** Every control and wake signal is direct, with 5 spare GPIOs
left. None is included.

## 18. Preliminary block diagram
```
USB-C ─ESD─┬─ CC 5.1k×2
           ├─ D+/D− ───────────────────────────── S3 IO19/20 (USB-Serial-JTAG)
           └─ VBUS → BQ25185 (power path) ─┬─ 1S LiPo (protected) ─ MAX17048 (I2C)
                                           └─ SYS 4.5 V → [0Ω / meter link] → TPS63802 → 3V3
3V3 ─┬─ ESP32-S3-MINI-1-N8 (bulk + decoupling)
     ├─ TPS22916 ← IO7 → GC9A01 panel (SPI IO10–13, RST IO14, TE → IO9)
     ├─ backlight MOSFET ← IO8 (PWM ≥ 20 kHz)
     ├─ MAX98357A-class (I2S IO17/18/21, SD IO38) → 8 Ω speaker
     ├─ ICM-42670-P (I2C IO47/48, INT1 → IO4 wake, INT2 → IO5)
     ├─ DRV2605L (I2C, EN IO37, TRIG IO36) → LRA
     ├─ IR TX driver ← IO35 ; IR RX (power IO40) → IO34
     ├─ WS2812 (power IO39) ← IO33
     └─ encoder: SW → IO1 (ext1), A → IO2 (ext0), B → IO6
```

## 19. Power budget (all estimates)

| Mode | Estimate | Dominated by |
|---|---|---|
| ACTIVE (HOME, LAMP, CAMERA) | **~130–190 mA** | Radio receiving at ~95 mA (ESP-NOW, power save off), CPU ~25–45 mA, backlight and panel ~15–40 mA *(panel spec)* |
| Speaker event | +~100–300 mA peak | Amplifier *(verify)* |
| Haptic event | +~100–250 mA peak | LRA *(verify)* |
| VISIBLE REST (light sleep, 3 % backlight) | ~2–6 mA | Panel and backlight |
| DEEP SLEEP | ≈ 43 µA | §13 |

**The biggest battery lever is still the radio receiving continuously.** It's
a later firmware decision, a power-save or duty-cycled ODD BUS, and it's out
of M5.0's scope.

## 20. Risks
1. **Panel TE availability:** no panel with TE confirmed in writing yet. It
   blocks Gate C.
2. **The ≤ 50 µA whole-board target:** the estimate is about 43 µA, with
   little margin. The encoder pull-ups and the regulator's quiescent current
   are the sensitive items.
3. **Antenna and enclosure:** no enclosure data, so the antenna edge and
   keepout can't be placed.
4. **Haptic and IMU mechanics:** LRA coupling and IMU isolation need the
   enclosure structure.
5. **Battery volume:** unknown. Capacity depends on measurements, which need
   a meter.
6. **Higher active current on the S3:** about 15–25 % more than the C3, which
   costs runtime. It's the price of the pins and headroom.
7. **1 MΩ encoder pull-ups:** noise immunity and debounce while awake need
   checking on the prototype.
8. **Unverified datasheet values:** the LSM6DSOX, MAX98357A, MAX17048,
   BQ25185 status and ship pins, and the S3 GPIO-matrix SPI speed limit.

## Next (Gate C prerequisites; nothing started)
- The panel datasheet and FPC pinout confirmed.
- Enclosure dimensions.
- A current meter on the bench.
- Your approval of this architecture.
