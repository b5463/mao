/*
 * VL53L4CD ULD platform layer on the ESP-IDF i2c_master driver (see
 * vl53l4cd/platform.h). 16-bit register index, MSB first; multi-byte values
 * big-endian. The i2c_master driver serialises access to the shared bus.
 */
#include "vl53l4cd/platform.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

#define I2C_TIMEOUT_MS  50
#define ULD_ERROR       1u

static uint8_t write_reg(Dev_t dev, uint16_t reg, uint32_t value, size_t len)
{
    uint8_t buf[2 + 4];
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)reg;
    for (size_t i = 0; i < len; i++) {
        buf[2 + i] = (uint8_t)(value >> (8 * (len - 1 - i)));
    }
    return i2c_master_transmit(dev->i2c, buf, 2 + len, I2C_TIMEOUT_MS) == ESP_OK ? 0 : ULD_ERROR;
}

static uint8_t read_reg(Dev_t dev, uint16_t reg, uint32_t *value, size_t len)
{
    const uint8_t index[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    uint8_t buf[4] = { 0 };
    if (i2c_master_transmit_receive(dev->i2c, index, sizeof(index), buf, len, I2C_TIMEOUT_MS) != ESP_OK) {
        return ULD_ERROR;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        v = (v << 8) | buf[i];
    }
    *value = v;
    return 0;
}

uint8_t VL53L4CD_RdByte(Dev_t dev, uint16_t RegisterAdress, uint8_t *p_value)
{
    uint32_t v = 0;
    const uint8_t st = read_reg(dev, RegisterAdress, &v, 1);
    *p_value = (uint8_t)v;
    return st;
}

uint8_t VL53L4CD_WrByte(Dev_t dev, uint16_t RegisterAdress, uint8_t value)
{
    return write_reg(dev, RegisterAdress, value, 1);
}

uint8_t VL53L4CD_RdWord(Dev_t dev, uint16_t RegisterAdress, uint16_t *p_value)
{
    uint32_t v = 0;
    const uint8_t st = read_reg(dev, RegisterAdress, &v, 2);
    *p_value = (uint16_t)v;
    return st;
}

uint8_t VL53L4CD_WrWord(Dev_t dev, uint16_t RegisterAdress, uint16_t value)
{
    return write_reg(dev, RegisterAdress, value, 2);
}

uint8_t VL53L4CD_RdDWord(Dev_t dev, uint16_t RegisterAdress, uint32_t *p_value)
{
    return read_reg(dev, RegisterAdress, p_value, 4);
}

uint8_t VL53L4CD_WrDWord(Dev_t dev, uint16_t RegisterAdress, uint32_t value)
{
    return write_reg(dev, RegisterAdress, value, 4);
}

uint8_t VL53L4CD_WaitMs(Dev_t dev, uint32_t TimeMs)
{
    (void)dev;
    vTaskDelay(pdMS_TO_TICKS(TimeMs) > 0 ? pdMS_TO_TICKS(TimeMs) : 1);
    return 0;
}
