# MAO firmware

Firmware for **MAO**, the ODD JOBS handheld controller. Current stage: **M2** (in progress)
(ODD BUS v1 over ESP-NOW: discovery and generic capability control).

- Boards (one tree, chosen by target, see below):
  - Espressif **ESP32-C3-LCDkit** (ESP32-C3 rev v0.4, 4 MB flash, no PSRAM): the M0-M2 platform
  - **MAO_MAIN A0** (ESP32-S3-WROOM-1-N8R2: 8 MB flash, 2 MB PSRAM): the custom PCB,
    see [docs/firmware/mao-a0-firmware.md](docs/firmware/mao-a0-firmware.md)
- Framework: native ESP-IDF **v6.0.3**
- UI: LVGL 9.4 via `esp_lvgl_port`, GC9A01 240×240 round display

## Build and flash

The project uses a private ESP-IDF install at `%USERPROFILE%\esp\v6.0.3\esp-idf`
(tools in `%USERPROFILE%\.espressif`). `tools/idf.ps1` activates it for one
PowerShell process and forwards arguments to `idf.py`. Run it from
PowerShell, not Git Bash; ESP-IDF refuses to run under MSYS.

```powershell
.\tools\idf.ps1 build
.\tools\idf.ps1 -p COM13 flash monitor     # check the port first: VID:PID 303A:1001
```

Override the IDF location with `MAO_IDF_PATH` if needed.

The target selects the board (Kconfig "MAO board" follows it):

| Target | Board | Target defaults | Partition table |
|---|---|---|---|
| `esp32c3` | ESP32-C3-LCDkit | `sdkconfig.defaults.esp32c3` | `partitions.csv` (4 MB) |
| `esp32s3` | MAO_MAIN A0 | `sdkconfig.defaults.esp32s3` | `partitions_8mb.csv` (8 MB) |

`set-target` switches a build directory to another board. It deletes that
directory's generated `sdkconfig`, and each profile has its own, so run it once
per profile:

```powershell
.\tools\idf.ps1 set-target esp32s3            # dev profile -> MAO_MAIN A0
.\tools\idf.ps1 release set-target esp32s3    # release profile too
.\tools\idf.ps1 set-target esp32c3            # back to the LCDkit
```

IDF applies `sdkconfig.defaults`, then `sdkconfig.defaults.<target>`, then
the profile file. Component versions are pinned per target in
`dependencies.lock.<target>`. Without the wrapper (Linux, CI, Docker):

```sh
idf.py -B build-s3 -D SDKCONFIG=build-s3/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.dev" set-target esp32s3 build
```

Build profiles layer over `sdkconfig.defaults`:

| Profile | Command | Output | Differences |
|---|---|---|---|
| dev (default) | `.\tools\idf.ps1 build` | `build/` | dev console, perf probe, INFO logs |
| release | `.\tools\idf.ps1 release build` | `build-release/` | no console/probe, WARN logs |
| factory | `.\tools\idf.ps1 factory build` | `build-factory/` | dev + the board self-test at every boot (assembled-board validation only, never ship) |

## Layout

Data flow: drivers (input, power, IR) → event bus → `mao_app` (state) → `mao_ui` / `mao_character` / `mao_audio` / `mao_haptics` / `mao_led`.
Sensor observations (`mao_sense`) go to `mao_perception`, which posts interpreted percepts (APPROACH_NEAR, PICKED_UP,
GENTLE_PET, ...) on the event bus; the character never reads hardware and no sensor maps straight to an animation.

| Path | Purpose |
|---|---|
| `main/` | `app_main()`: bring-up sequencing only |
| `components/mao_board` | The only code that knows GPIOs, I2C addresses and board wiring. One implementation per board under `boards/` (`lcdkit/`, `main_a0/`), picked by Kconfig; pin headers are private (the A0 one is generated from `hardware/mao/design/pinmap.py`) |
| `components/mao_display` | GC9A01 + LVGL runtime, display lock, backlight, frame-performance probe |
| `components/mao_input` | Quadrature decoder (EC11 on the LCDkit, Hall ring on the A0; 30 detents/rev) and push-switch state machine, which emit MAO events |
| `components/mao_audio` | Non-blocking synthesised UI sounds: tick, notice, confirm, back |
| `components/mao_led` | WS2812 status LED: off at rest, brief pulses (A0: not fitted, calls are no-ops) |
| `components/mao_ir` | NEC IR TX/RX on the A0 (RMT); disabled on the LCDkit until its jumper is verified |
| `components/mao_haptics` | DRV2605L LRA driver and the haptic vocabulary (A0) |
| `components/mao_power` | Fuel gauge, charger/USB events, power states and sleep, deep-sleep continuity (A0) |
| `components/mao_sense` | IMU, ToF (ST VL53L4CD ULD vendored), light, touch and mic-level observations (A0) |
| `components/mao_perception` | Perception: observations + input → percepts (filters, hysteresis, fusion, confidence); platform-independent engine in `core/` |
| `components/mao_selftest` | Boot check (hardware faults, first boot of a board) and the factory / board self-test |
| `components/mao_system` | Boot banner, event bus + dispatcher, idle timer, settings (NVS `mao`), dev console |
| `components/mao_app` | Application behaviour and the authoritative app state (`mao_state.c`); reactions to percepts (`mao_app_percept.c`); power policy feed (`mao_app_power.c`) |
| `components/mao_ui` | Views: HOME, menu shell, placeholder pages, first encounter; transitions |
| `components/mao_character` | Procedural character: reaction states, idle behaviour, spring motion |
| `components/mao_radio` | Wi-Fi STA (never associated) + ESP-NOW transport |
| `components/odd_bus` | ODD BUS v1: shared, product-agnostic protocol (also used by device firmware) |
| `components/mao_devices` | ODD BUS controller: registry, discovery, liveness, confirmed value control |
| `devices/lamp_01_test` | LAMP 01: minimal ODD BUS test light (POWER, LEVEL) for a second ESP32-C3 |
| `tests/host` | Host unit tests of the perception engine and the power policy (`tests/host/run.sh`) |
| `assets/`, `tools/` | Assets (unused so far), helper scripts (incl. `factory_test.py`) |

## Development tools

With `CONFIG_MAO_DEV_CONSOLE=y` (default) the firmware accepts line commands
on the USB console. Release builds should disable it, along with
`CONFIG_MAO_PERF_PROBE`.

```powershell
python tools\mao_cmd.py COM13 help
python tools\mao_cmd.py COM13 status              # view, dial, encoder counters, heap
python tools\mao_cmd.py COM13 reset-first-boot    # clears only MAO's first-boot key
python tools\mao_cmd.py COM13 reboot
python tools\mao_cmd.py COM13 stress 20 --listen 25
python tools\mao_smoke.py COM13 --cycles 5        # scripted walk through every view and gesture
python tools\mao_cmd.py COM13 odd-selftest        # ODD BUS codec self-test on target
python tools\mao_cmd.py COM13 odd-flood 30        # 50 Hz radio load (coexistence testing)
python tools\mao_cmd.py COM13 board               # caps, revision, I2C scan, rails
python tools\mao_cmd.py COM13 sense               # A0: latest observation of every sensor
python tools\mao_cmd.py COM13 haptic heartbeat    # A0: tick double_tap heartbeat short_pulse tremor annoyed_buzz wake_pulse confirm
python tools\mao_cmd.py COM13 ir send 0x10 0x2A   # A0: one NEC frame
python tools\mao_cmd.py COM13 power               # A0: battery, charger, power state, sleep continuity
python tools\mao_cmd.py COM13 sleep 60            # A0: deep sleep, timer wake after 60 s (also: sleep light 30)
python tools\mao_cmd.py COM13 percept             # perception status; "percept inject GENTLE_PET" tries a reaction
python tools\mao_cmd.py COM13 rotate 270          # display rotation live (bring-up); "rotate save" keeps it
python tools\factory_test.py COM13                # board self-test with operator prompts (docs/hardware/mao-factory-test.md)
sh tests/host/run.sh                              # host unit tests (perception engine, power policy)
python tools\odd_m2_test.py --mao COM13 --lamp COMx   # end-to-end with LAMP 01
```

ESP-NOW runs on the fixed development channel `ODD_BUS_DEV_CHANNEL` (1) with
unencrypted peers: **development network only**.

## Partition map (4 MB, LCDkit)

| Name | Offset | Size |
|---|---|---|
| bootloader | 0x000000 | 32 KB |
| partition table | 0x008000 | 4 KB |
| nvs | 0x009000 | 24 KB |
| otadata | 0x00F000 | 8 KB |
| phy_init | 0x011000 | 4 KB |
| coredump | 0x012000 | 56 KB |
| ota_0 | 0x020000 | 1792 KB |
| ota_1 | 0x1E0000 | 1792 KB |
| assets | 0x3A0000 | 384 KB |

## Partition map (8 MB, MAO_MAIN A0)

The first 128 KB are identical to the 4 MB table.

| Name | Offset | Size |
|---|---|---|
| nvs / otadata / phy_init / coredump | 0x009000 … 0x01FFFF | as above |
| ota_0 | 0x020000 | 3072 KB |
| ota_1 | 0x320000 | 3072 KB |
| assets | 0x620000 | 1536 KB |
| (unallocated reserve) | 0x7A0000 | 384 KB |

## Recovery

A verified dump of the original factory firmware lives in `../backup/`
(SHA-256 `a15e09ff…41c10`). To restore it:

```powershell
python -m esptool --port COM13 write-flash 0x0 ..\backup\mao_flash_full_20260923.bin
```
