# MAO firmware

Firmware for **MAO**, the ODD JOBS handheld controller. Current stage: **M2** (in progress)
(ODD BUS v1 over ESP-NOW: discovery and generic capability control).

- Board: Espressif ESP32-C3-LCDkit (ESP32-C3 rev v0.4, 4 MB flash, no PSRAM)
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

Build profiles layer over `sdkconfig.defaults`:

| Profile | Command | Output | Differences |
|---|---|---|---|
| dev (default) | `.\tools\idf.ps1 build` | `build/` | dev console, perf probe, INFO logs |
| release | `.\tools\idf.ps1 release build` | `build-release/` | no console/probe, WARN logs |

## Layout

Data flow: input driver → event bus → `mao_app` (state) → `mao_ui` / `mao_character` / `mao_audio` / `mao_led`.

| Path | Purpose |
|---|---|
| `main/` | `app_main()`: bring-up sequencing only |
| `components/mao_board` | The only code that knows GPIOs and board wiring (`mao_board_pins.h` is private) |
| `components/mao_display` | GC9A01 + LVGL runtime, display lock, backlight, frame-performance probe |
| `components/mao_input` | EC11 quadrature decoder (30 detents/rev) and push-switch state machine, which emit MAO events |
| `components/mao_audio` | Non-blocking synthesised UI sounds: tick, notice, confirm, back |
| `components/mao_led` | WS2812 status LED: off at rest, brief pulses |
| `components/mao_ir` | IR interface. Disabled until the TX/RX jumper is verified |
| `components/mao_system` | Boot banner, event bus + dispatcher, idle timer, settings (NVS `mao`), dev console |
| `components/mao_app` | Application behaviour and the authoritative app state (`mao_state.c`) |
| `components/mao_ui` | Views: HOME, menu shell, placeholder pages, first encounter; transitions |
| `components/mao_character` | Procedural character: reaction states, idle behaviour, spring motion |
| `components/mao_radio` | Wi-Fi STA (never associated) + ESP-NOW transport |
| `components/odd_bus` | ODD BUS v1: shared, product-agnostic protocol (also used by device firmware) |
| `components/mao_devices` | ODD BUS controller: registry, discovery, liveness, confirmed value control |
| `devices/lamp_01_test` | LAMP 01: minimal ODD BUS test light (POWER, LEVEL) for a second ESP32-C3 |
| `assets/`, `tools/`, `tests/` | Assets (unused so far), helper scripts, future tests |

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
python tools\odd_m2_test.py --mao COM13 --lamp COMx   # end-to-end with LAMP 01
```

ESP-NOW runs on the fixed development channel `ODD_BUS_DEV_CHANNEL` (1) with
unencrypted peers: **development network only**.

## Partition map (4 MB)

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

## Recovery

A verified dump of the original factory firmware lives in `../backup/`
(SHA-256 `a15e09ff…41c10`). To restore it:

```powershell
python -m esptool --port COM13 write-flash 0x0 ..\backup\mao_flash_full_20260923.bin
```
