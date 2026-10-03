# MAO current hardware audit

Phase 1 of MAO_MAIN A0. Source of truth: the firmware at `feat/mao-controller-m2`
(`f213d87`, stage **M2**, `PROJECT_VER 0.2.0-m2`). Where documentation and code
disagree, the code wins. Every claim below cites the file it comes from.

## 1. Platform

| Item | Value | Source |
|---|---|---|
| Board | Espressif **ESP32-C3-LCDkit** | `mao_board.h` `MAO_BOARD_NAME` |
| MCU | ESP32-C3 (rev v0.4), single RISC-V core, 160 MHz | `sdkconfig.defaults`, README |
| Chip check | `mao_board_init()` refuses anything but `CHIP_ESP32C3` | `mao_board.c:28` |
| Flash | 4 MB, in-package (XMC), QIO @ 80 MHz | `sdkconfig.defaults` |
| PSRAM | none | README |
| Framework | ESP-IDF **v6.0.3**, LVGL **9.4.0** via `esp_lvgl_port` 2.9.0, `esp_lcd_gc9a01` 2.0.4, `led_strip` 3.0.3 | `dependencies.lock` |
| Console | USB-Serial/JTAG only (UART0 GPIO20/21 unused) | `sdkconfig.defaults` |
| Build | `tools/idf.ps1` (Windows PowerShell wrapper), dev/release overlays | README |

Baseline build (this audit, Docker `espressif/idf:v6.0.3`, dev profile): app
image **1 113 776 B** (39 % of the 1.75 MB OTA slot free); static DRAM
105 864 B of 321 296 B.

## 2. Canonical GPIO table (ESP32-C3-LCDkit)

The firmware has exactly one GPIO table, `components/mao_board/mao_board_pins.h`,
private to `mao_board`. No other component references a GPIO number (verified by
`grep -rn "GPIO\|gpio_num"`; the only hits outside `mao_board` read pins from the
board descriptor).

| GPIO | Function | Dir | Peripheral | Notes |
|---|---|---|---|---|
| 0 | LCD MOSI | out | SPI2 | write-only bus, no MISO |
| 1 | LCD SCLK | out | SPI2 | 80 MHz |
| 2 | LCD DC | out | GPIO | **strapping pin**, driven only after boot |
| 3 | Speaker PDM out | out | I2S0 PDM TX | → RC → NS4150 class-D, no enable pin |
| 4 | IR (shared TX/RX) | — | (none) | jumper selects TX or RX; **never configured** in M2 |
| 5 | LCD backlight | out | LEDC ch0, 5 kHz, 10-bit | active high |
| 6 | Encoder B | in, pull-up | GPIO any-edge ISR | |
| 7 | LCD CS | out | SPI2 | |
| 8 | WS2812B-Mini data | out | RMT | **strapping pin**, configured in `app_main` only |
| 9 | Encoder push switch | in, pull-up | GPIO any-edge ISR | **BOOT strap**: input only, never driven. Holding the knob at reset enters download mode |
| 10 | Encoder A | in, pull-up | GPIO any-edge ISR | |
| 11–17 | in-package flash | — | — | reserved |
| 18 / 19 | USB D− / D+ | — | USB-Serial/JTAG | console + flashing |
| 20 / 21 | UART0 RX / TX | — | — | unused |
| — | LCD reset | — | — | **not connected**: software reset only |

Pin usage is complete: 11 of the 15 usable GPIOs are used, and none is free
except UART0. That is why the A0 cannot add sensing on a C3 (§9).

## 3. Subsystems as implemented

### Display (`mao_display`, `mao_board`)
- GC9A01, 240 × 240 round, RGB565, colour inversion on, BGR order, `mirror_x`,
  bytes swapped. Values chosen for the LCDkit panel. On a new panel they may need
  re-checking.
- SPI2 at 80 MHz, mode 0, write-only. No TE line, so tearing is not synchronised.
- LVGL partial rendering: 2 × 240 × 20-line buffers (9 600 B each), DMA-capable
  internal RAM. Refresh period 16 ms.
- Panel stays dark until the first real frame is rendered
  (`mao_display_start`), and the backlight starts at 0 %. **Boot never shows garbage.**
- Brightness preference 5–100 % (NVS), sleepy dims to 35 % of the preference.

### Rotary input (`mao_input`)
- EC11 with push switch. 30 detents/rev, 15 quadrature cycles/rev,
  **2 transitions per detent**, rest states AB = 00 and AB = 11 (measured in M1).
- Quadrature decoding by a transition table in the ISR (the table is the
  debouncer). It commits a detent on arrival at rest and recovers from one
  missed edge. There is no hardware debounce on the LCDkit.
- Switch: 20 ms debounce, long press 600 ms, double click 350 ms.
- Events: CW/CCW (coalesced detents), PRESS, RELEASE, CLICK, LONG_PRESS,
  DOUBLE_CLICK.
- Dial velocity classes (`mao_state.c`): SLOW / NORMAL ≥ 12 /s / FAST ≥ 28 /s /
  VERY_FAST ≥ 56 /s, plus "reversing" (≥ 3 reversals in 900 ms → DIZZY).
  **These thresholds are in detents/s for a 30-detent encoder.** A different
  encoder changes the feel unless `detents_per_rev` is kept.

### Audio (`mao_audio`)
- I2S0 **PDM TX**, mono 16-bit, 16 kHz, DMA ring 3 × 10 ms. Channel always
  enabled with `auto_clear`, so it plays silence between sounds (no start/stop pops).
- On the LCDkit, the PDM bitstream is RC-filtered into an **NS4150** analog
  class-D amplifier with **no shutdown pin**, so the amplifier is always powered.
- Synthesised vocabulary (no assets): `tick` (2.6 kHz, 7 ms), `notice`,
  `confirm` (rising), `back` (falling). Gain capped at 0.58 because "the NS4150
  is loud". Volume 0–100 % in NVS, default 60 %.
- Ticks are rate-limited to 35 ms and thinned at FAST dial speed. There are no
  ticks at VERY_FAST.

### Status LED (`mao_led`)
- One WS2812B-Mini on RMT. Dim blue while booting, then off. Brief 90 ms
  pulses: warm (interaction) and cool (notice / device found).
- Kept very dim "because it sits next to the display".

### IR (`mao_ir`)
- **Not functional.** The GPIO is shared between the IR LED and the 38 kHz
  receiver through a jumper whose position was never verified. The firmware
  records the resource and reports `[--] IR disabled`. No protocol code exists.

### Wireless / ODD BUS (`mao_radio`, `odd_bus`, `mao_devices`)
- Wi-Fi STA, never associated, power save **off**, fixed channel 1
  (`ODD_BUS_DEV_CHANNEL`). ESP-NOW v1 frames (≤ 250 B), unencrypted (dev network).
- ODD BUS v1: header + CRC, DISCOVER / ANNOUNCE / DESCRIBE / SET_VALUE / ACK.
  Device identity = `0x0DD0` + factory MAC.
- Controller side: RAM-only registry of 8 devices. Discovery every 10 s at idle,
  1.5 s while device views are open. Offline after 32 s / 5 s. Optimistic value
  control with ≤ 25 ms coalescing, sequence-matched ACKs, bounded retries,
  latency statistics.
- RSSI is recorded per device but not used for behaviour.
- **Pairing:** there is none. Any ODD device on channel 1 is discovered.
- **KINO:** the device class `ODD_DEVICE_DISPLAY` ("KINO D4 (future)") exists.
  No KINO-specific code exists; control is purely capability-driven, so a
  KINO exposing POWER/LEVEL would work unchanged.
- LAMP 01 test device (`devices/lamp_01_test`) runs on a second ESP32-C3.

### Character (`mao_character`)
- Procedural two-eye face (plus an optional minimal mouth) from plain LVGL
  objects, ~30 Hz spring-damper motion on 12 pose channels.
- States: IDLE, NOTICE, FOLLOW, DIZZY, SLEEPY, SURPRISED, HAPPY.
- Reactions: NOTICE, SURPRISED, HAPPY, WAKE, ATTEND (look towards the edge
  when a device appears).
- Idle behaviour: randomised glances, blinks and breathing (`esp_random`).
  Fast dial spins make the face **orbit following the knob's real angle**,
  which uses `detents_per_rev`.
- The character never reads hardware. It is driven only by commands from
  `mao_app`. This is the property the new perception layer must preserve.

### Application (`mao_app`)
- One authoritative state (`mao_state.c`), mutated only in the dispatcher task.
- Views: INTRO (first encounter "MAO / TURN"), HOME, MENU (DEVICES, ACTIONS,
  TOOLS, SETUP), PLACEHOLDER, DEVICES, DEVICE.
- Sleepy after `CONFIG_MAO_SLEEPY_TIMEOUT_S` (45 s) without input. **Purely
  visual: the chip never sleeps** and the radio stays at full power.

### System (`mao_system`)
- Event bus: one 16-deep queue, ≤ 8 subscribers, dispatcher task.
- Boot report `[OK]/[!!]/[--]` per subsystem. A failed subsystem does not stop
  boot ("MAO READY (with subsystem errors)"), so graceful degradation already
  exists.
- Health log every 60 s (heap, stack high-water marks, dropped events).
- Dev console over USB-Serial/JTAG (FIFO polled): `status`, `key …`
  injection, `stress`, `odd-selftest`, `odd-flood`, `reset-first-boot`,
  `reboot`.

## 4. Persistent state

| Store | Keys | Notes |
|---|---|---|
| NVS namespace `mao` (24 KB partition) | `ver` (schema 1), `fb_done`, `volume`, `bright` | Never erases foreign namespaces. A corrupt partition gives defaults, never an automatic erase |

Volatile by design: character state, view, dial position, device registry.
**No character memory exists yet** (no familiarity, annoyance residue or
interaction history). See §8.

## 5. Partition map (4 MB)

`nvs 24K · otadata 8K · phy 4K · coredump 56K · ota_0 1792K · ota_1 1792K · assets 384K`.
`assets` (SPIFFS) is reserved and unused. Coredumps go to flash in ELF format.

## 6. Power assumptions

- USB-powered only (LCDkit). **No battery, charger, fuel gauge or power
  switching.** No sleep modes are used. Wi-Fi power save is disabled on purpose
  for ESP-NOW latency.
- Backlight is the only power control (LEDC PWM).

## 7. Boot, recovery and debug

- Normal boot: LED dim blue → panel initialised dark → first frame → backlight
  → "MAO" wordmark gives way to the character (`mao_ui_boot`).
- Recovery: hold the knob (GPIO9 = BOOT) while powering, or flash a factory dump
  with esptool (README). Native USB flashing auto-resets.
- No JTAG configuration beyond the built-in USB-Serial/JTAG. UART0 unused.
- Crash data: coredump partition.

## 8. Behaviours named in the A0 brief that are **not in this repository**

The brief asks to preserve behaviours from "M1.5/M2/M4/M4.1". This repository's
history ends at M2 (`854f7a9` M1 baseline → `f213d87`). The following are
**absent from the code** and cannot be audited:

| Brief item | Status in repo |
|---|---|
| escalating suspicious → tsk → mad | absent: no such states or counters |
| lingering emotional residue | absent: no persistent or decaying mood |
| rare reactions | absent: only randomised idle timing |
| fiddling behaviour | partial: DIZZY on rapid reversals only |
| secrets / easter eggs | absent |
| KINO interaction | generic only: capability-driven control would cover it |
| pairing | absent: open discovery on channel 1 |

If later work (M3–M4.1) exists elsewhere, it has not been pushed to
`b5463/mao`. The A0 HAL keeps the character interface unchanged (commands in,
no hardware reads), so that work can be merged on top. The perception layer
adds the inputs those behaviours need.

## 9. Prototype assumptions the A0 must not inherit

| Assumption | Why it cannot carry over |
|---|---|
| ESP32-C3: 15 usable GPIOs, all used | the A0 needs ~33 signals; no native touch; no PDM RX for a microphone |
| no LCD reset line | a hung panel can only be recovered by power-cycling the whole device |
| NS4150 without shutdown, PDM → RC filter | always-on amplifier idle current; no mute; no power gating |
| shared IR TX/RX pin with jumper | not product-like; IR never worked |
| WS2812 next to the display | the brief bans gratuitous RGB; see the architecture doc for its replacement |
| USB-only power, no sleep | the A0 is battery-powered with real power states |
| chip check hard-coded to `CHIP_ESP32C3` | must become per-board |
| 30-detent EC11 thresholds | keep 30 detents/rev on the A0 so dial feel is unchanged |

## 10. Tests and CI

- No unit tests (`tests/.gitkeep` only). No CI configuration in the repository.
- On-target tools: `tools/mao_smoke.py` (scripted walk of views/gestures over
  the console), `tools/odd_m2_test.py` (MAO ↔ LAMP end to end),
  `tools/mao_cmd.py`, `odd-selftest` (on-target codec test).

## 11. Hardware cross-check against Espressif's LCDkit documentation

Sources: LCDkit user guide, schematics `SCH_ESP32-C3-C6-LCDkit-MB_V1.1` and
`SCH_ESP32-C3-LCDkit-DB_V1.0`, PCB drawings, esp-bsp `esp32_c3_lcdkit`.
The firmware's 11 GPIO assignments all match the schematic.

| Item | LCDkit hardware | Consequence for A0 |
|---|---|---|
| Module | ESP32-C3-MINI-1, 4 MB flash (ordering variant unconfirmed) | — |
| Physical form | **Rectangular** 61 × 54 mm main board + 40 × 40 mm display board on 2.54 mm headers. The EC11 knob sits **beside** the display, not around it | MAO's form is not inherited from the kit. The A0 defines its own (architecture doc) |
| Panel | **Limito LH128R-IG01**: GC9A01, 4-line SPI, 35.6 × 38.1 × 1.55 mm outline, Ø32.4 mm active area. 12-pin FPC **soldered directly** to pads (footprint named 0.7 mm pitch) | A0 needs a panel whose FPC mates a connector, or a hot-bar land; pinout must come from the panel datasheet |
| LCD reset | 10 kΩ pull-up only, not on a GPIO | A0 wires RESX |
| Backlight | NPN low-side switch, 10 Ω from 3.3 V. No current regulation. **Defaults ON** at power-up | firmware hides this by setting PWM 0 first. A0 must default the backlight OFF in hardware |
| Audio | GPIO3 PDM → 3-stage RC (1 kΩ; 4.7/2.2/1 nF) → NS4150, gain 3×, **always enabled**. Speaker 8 Ω / 1 W, 24 × 15 mm | A0 adds a shutdown line; the 8 Ω / 1 W class of speaker is kept |
| IR | IRM-H638TTR2 receiver (38 kHz) and IR67-21C 940 nm emitter share GPIO4 through a 3-pin jumper | A0 separates RX and TX |
| RGB LED | WS2812-family on 5 V via a BSS138 level shifter (exact variant inconsistent across Espressif documents) | — |
| Power | USB-C → Schottky → SGM2212 LDO. No battery | — |

Not verifiable from public documents: EC11 part number and whether it has
external pull-ups (the firmware enables internal pull-ups, and the measured
30 detents / 15 cycles are what matter), the speaker connector type, and the
real backlight and IR currents.
