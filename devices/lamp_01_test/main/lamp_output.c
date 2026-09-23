/* LAMP 01 output: always logs (in lamp_main.c); optionally drives an LED. */
#include "sdkconfig.h"
#include "esp_check.h"
#include "lamp.h"

#if CONFIG_LAMP_OUTPUT_WS2812
#include "led_strip.h"
static led_strip_handle_t s_strip;
#elif CONFIG_LAMP_OUTPUT_PWM
#include "driver/ledc.h"
#endif

static const char *TAG = "LAMP01";

esp_err_t lamp_output_init(void)
{
#if CONFIG_LAMP_OUTPUT_WS2812
    const led_strip_config_t strip = {
        .strip_gpio_num = CONFIG_LAMP_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = { .resolution_hz = 10 * 1000 * 1000 };
    return led_strip_new_rmt_device(&strip, &rmt, &s_strip);
#elif CONFIG_LAMP_OUTPUT_PWM
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "timer");
    const ledc_channel_config_t c = {
        .gpio_num = CONFIG_LAMP_LED_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0, .duty = 0,
#if CONFIG_LAMP_LED_ACTIVE_LOW
        .flags.output_invert = 1,
#endif
    };
    return ledc_channel_config(&c);
#else
    (void)TAG;
    return ESP_OK;
#endif
}

void lamp_output_apply(bool power, int32_t level)
{
    /* Perceptual (squared) curve so low levels are distinguishable. */
    const uint32_t lin = power ? (uint32_t)level : 0;   /* 0..100 */
    const uint32_t perceived = lin * lin;               /* 0..10000 */
#if CONFIG_LAMP_OUTPUT_WS2812
    const uint8_t v = (uint8_t)(perceived * 255 / 10000);
    led_strip_set_pixel(s_strip, 0, v, (uint8_t)(v * 7 / 10), (uint8_t)(v * 4 / 10));   /* warm white */
    led_strip_refresh(s_strip);
#elif CONFIG_LAMP_OUTPUT_PWM
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, perceived * 1023 / 10000);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#else
    (void)perceived;
#endif
}
