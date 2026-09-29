/*
 * MAO Rev A: the deep-sleep wake architecture, on a bare ESP32-S3 dev board
 * (M5.0 Gate B.1 §9-11). Not product firmware.
 *
 * Wiring on the dev board (Rev A pins):
 *   GPIO1  "PRESS"   a button to GND, external pull-up to 3V3 (value under test)
 *   GPIO2  "ENC A"   a switch or an EC11's A to GND, external pull-up (value under test)
 *   GPIO4  "IMU INT" a button to GND (stands in for the ICM-42670-P's open-drain INT1),
 *                    external pull-up
 *
 * What it does: awake for AWAKE_S seconds (printing the pins), then deep
 * sleep with
 *   ext1: GPIO1 | GPIO4, ANY_LOW        (press, motion)
 *   ext0: GPIO2 at the opposite of its level now   (a turn in either state)
 * On every boot it prints the wake cause, which ext1 pin fired, the ext0 level
 * it had armed, and a wake counter kept in RTC memory; it deinitialises the
 * RTC IOs so they are plain GPIOs again. Measure the board current while it
 * sleeps (the module's 3V3 in series with a meter).
 */
#include <inttypes.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_PRESS  GPIO_NUM_1
#define PIN_ENC_A  GPIO_NUM_2
#define PIN_IMU    GPIO_NUM_4
#define AWAKE_S    5

static const char *TAG = "S3_WAKE";

static RTC_DATA_ATTR uint32_t s_wakes;
static RTC_DATA_ATTR int s_armed_a;       /* the level ext0 waited for */

static const char *cause_name(uint32_t causes)
{
    if (causes & (1u << ESP_SLEEP_WAKEUP_EXT0)) {
        return "EXT0 (encoder turn)";
    }
    if (causes & (1u << ESP_SLEEP_WAKEUP_EXT1)) {
        return "EXT1 (press or motion)";
    }
    if (causes & (1u << ESP_SLEEP_WAKEUP_TIMER)) {
        return "TIMER";
    }
    return "cold boot / reset";
}

void app_main(void)
{
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    const uint64_t ext1 = esp_sleep_get_ext1_wakeup_status();
    ESP_LOGI(TAG, "boot: wake #%" PRIu32 ", causes 0x%" PRIx32 " = %s", s_wakes, causes, cause_name(causes));
    if (causes & (1u << ESP_SLEEP_WAKEUP_EXT1)) {
        ESP_LOGI(TAG, "  ext1 status 0x%" PRIx64 ":%s%s", ext1, (ext1 & (1ULL << PIN_PRESS)) ? " PRESS" : "",
                 (ext1 & (1ULL << PIN_IMU)) ? " IMU" : "");
    }
    if (causes & (1u << ESP_SLEEP_WAKEUP_EXT0)) {
        ESP_LOGI(TAG, "  ext0 had waited for ENC A = %d", s_armed_a);
    }
    /* back to plain GPIOs (an RTC IO left in RTC mode does not read as GPIO) */
    rtc_gpio_deinit(PIN_PRESS);
    rtc_gpio_deinit(PIN_ENC_A);
    rtc_gpio_deinit(PIN_IMU);
    const gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_PRESS) | (1ULL << PIN_ENC_A) | (1ULL << PIN_IMU),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,       /* the external pulls are what is under test */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&in);

    for (int s = 0; s < AWAKE_S * 4; s++) {
        ESP_LOGI(TAG, "awake: PRESS=%d ENC_A=%d IMU=%d", gpio_get_level(PIN_PRESS), gpio_get_level(PIN_ENC_A),
                 gpio_get_level(PIN_IMU));
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    /* a press or motion still held would wake at once: wait for release (no re-sleep loop) */
    while (gpio_get_level(PIN_PRESS) == 0 || gpio_get_level(PIN_IMU) == 0) {
        ESP_LOGW(TAG, "a wake line is low: waiting for it to be released");
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    const int a_now = gpio_get_level(PIN_ENC_A);
    s_armed_a = !a_now;
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io((1ULL << PIN_PRESS) | (1ULL << PIN_IMU), ESP_EXT1_WAKEUP_ANY_LOW));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(PIN_ENC_A, s_armed_a));
    /* the internal RTC pulls stay off: the external high-value pulls hold the levels */
    rtc_gpio_pullup_dis(PIN_PRESS);
    rtc_gpio_pulldown_dis(PIN_PRESS);
    rtc_gpio_pullup_dis(PIN_ENC_A);
    rtc_gpio_pulldown_dis(PIN_ENC_A);
    rtc_gpio_pullup_dis(PIN_IMU);
    rtc_gpio_pulldown_dis(PIN_IMU);
    s_wakes++;
    ESP_LOGI(TAG, "deep sleep: ext1 PRESS|IMU any-low, ext0 ENC_A == %d (it is %d now)", s_armed_a, a_now);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}
