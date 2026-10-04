#include "mao_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mao_board.h"

static const char *TAG = "MAO_LED";

#define PULSE_MS 90

typedef struct {
    uint8_t r, g, b;
} rgb_t;

/* Kept very dim: the WS2812 is bright and sits next to the display. */
static const rgb_t kStateColor[] = {
    [MAO_LED_STATE_OFF]     = { 0, 0, 0 },
    [MAO_LED_STATE_BOOTING] = { 0, 0, 6 },   /* dim blue */
};
static const rgb_t kPulseColor[] = {
    [MAO_LED_PULSE_INTERACTION] = { 10, 7, 3 },   /* warm */
    [MAO_LED_PULSE_NOTICE]      = { 4, 7, 10 },   /* cool */
};

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_mutex;
static esp_timer_handle_t s_pulse_timer;
static mao_led_state_t s_state = MAO_LED_STATE_OFF;

static void show(rgb_t c)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (c.r == 0 && c.g == 0 && c.b == 0) {
        led_strip_clear(s_strip);
    } else {
        led_strip_set_pixel(s_strip, 0, c.r, c.g, c.b);
        led_strip_refresh(s_strip);
    }
    xSemaphoreGive(s_mutex);
}

static void pulse_end_cb(void *arg)
{
    (void)arg;
    show(kStateColor[s_state]);
}

esp_err_t mao_led_init(void)
{
    mao_board_caps_t caps;
    mao_board_get_caps(&caps);
    if (!caps.rgb_led) {
        /* No LED on this board (A0: removed on purpose). Every call below
         * stays a harmless no-op because s_strip remains NULL. */
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex, ESP_ERR_NO_MEM, TAG, "mutex");
    ESP_RETURN_ON_ERROR(mao_board_led_init(&s_strip), TAG, "board led");

    const esp_timer_create_args_t args = {
        .callback = pulse_end_cb,
        .name = "mao_led_pulse",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_pulse_timer), TAG, "pulse timer");

    mao_led_set_state(MAO_LED_STATE_BOOTING);
    return ESP_OK;
}

void mao_led_set_state(mao_led_state_t state)
{
    if (!s_strip || state > MAO_LED_STATE_BOOTING) {
        return;
    }
    s_state = state;
    if (!esp_timer_is_active(s_pulse_timer)) {
        show(kStateColor[state]);
    }
}

void mao_led_pulse(mao_led_pulse_t kind)
{
    if (!s_strip || kind > MAO_LED_PULSE_NOTICE) {
        return;
    }
    show(kPulseColor[kind]);
    esp_timer_stop(s_pulse_timer);  /* restart window if already pulsing */
    esp_timer_start_once(s_pulse_timer, PULSE_MS * 1000);
}
