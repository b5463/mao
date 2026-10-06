/*
 * MAO platform layer for ST's VL53L4CD Ultra Lite Driver (ULD).
 *
 * The ULD (vl53l4cd_api.c/.h, ST, BSD-3-Clause, unmodified) talks to the
 * sensor only through the functions declared here. This file replaces ST's
 * example platform.h; the implementation is mao_sense_tof_platform.c, on top
 * of the ESP-IDF i2c_master driver. Register indices are 16-bit, MSB first.
 */
#pragma once

#include <math.h>
#include <stdint.h>
#include "driver/i2c_types.h"

/* The ULD uses float_t; newlib provides it through math.h. */

typedef struct {
    i2c_master_dev_handle_t i2c;
} mao_vl53l4cd_dev_t;

/* ULD device handle. */
typedef mao_vl53l4cd_dev_t *Dev_t;

/* All return 0 on success, non-zero on an I2C failure. */
uint8_t VL53L4CD_RdByte(Dev_t dev, uint16_t RegisterAdress, uint8_t *p_value);
uint8_t VL53L4CD_WrByte(Dev_t dev, uint16_t RegisterAdress, uint8_t value);
uint8_t VL53L4CD_RdWord(Dev_t dev, uint16_t RegisterAdress, uint16_t *p_value);
uint8_t VL53L4CD_WrWord(Dev_t dev, uint16_t RegisterAdress, uint16_t value);
uint8_t VL53L4CD_RdDWord(Dev_t dev, uint16_t RegisterAdress, uint32_t *p_value);
uint8_t VL53L4CD_WrDWord(Dev_t dev, uint16_t RegisterAdress, uint32_t value);
uint8_t VL53L4CD_WaitMs(Dev_t dev, uint32_t TimeMs);
