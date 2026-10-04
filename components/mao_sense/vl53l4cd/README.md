# VL53L4CD Ultra Lite Driver (vendored)

ST's official VL53L4CD ULD, used by `mao_sense` for the proximity sensor.

| | |
|---|---|
| Source | https://github.com/STMicroelectronics/x-cube-tof1, tag `v3.4.3` (commit `c12709c`), `Drivers/BSP/Components/vl53l4cd/modules/` |
| Driver version | ULD 2.2.2 (`VL53L4CD_IMPLEMENTATION_VER_*` in `vl53l4cd_api.h`) |
| License | BSD-3-Clause (ST SLA0103), see `LICENSE.md`; copyright headers kept |
| Files taken | `vl53l4cd_api.c`, `vl53l4cd_api.h` (unmodified), `LICENSE.md` |
| MAO files | `platform.h` (replaces ST's example), implementation in `../mao_sense_tof_platform.c` (ESP-IDF `i2c_master`) |

Not taken: ST's calibration module (offset / crosstalk calibration), the
STM32 BSP wrapper (`vl53l4cd.c/.h`) and ST's example platform layer.

To update: replace the two `vl53l4cd_api.*` files from a newer ST release,
keep `platform.h`, and update the version line above.
