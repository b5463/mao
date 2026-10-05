/*
 * Capacitive touch zones on the ESP32-S3 touch sensor (IDF v6
 * esp_driver_touch_sens). Electrodes: right / left rim, top window border,
 * rear base, each with 510 R series and a 0.42 pF ESD part.
 *
 * The hardware scans continuously, filters the data and keeps a benchmark
 * (baseline) per channel; it raises active / inactive callbacks itself, so
 * the CPU only polls for the analogue values. Thresholds are set relative to
 * each channel's benchmark after an initial scan.
 *
 * The board's wake zone (top) is also the deep-sleep wake channel; every
 * zone can wake the chip from light sleep.
 *
 * The speaker sits under the board partly over the LEFT rim electrode, and
 * the filterless class-D amplifier switches at ~330 kHz whenever it is
 * enabled: the LEFT zone holds its last reading while the amplifier runs and
 * for AMP_SETTLE_US after (the hardware benchmark keeps tracking).
 */
#include "mao_sense_priv.h"

#include "soc/soc_caps.h"

#if SOC_TOUCH_SENSOR_SUPPORTED

#include "driver/touch_sens.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mao_board.h"

static const char *TAG = "MAO_SENSE";

/* VERIFY AT BRING-UP: touch threshold as a fraction of the benchmark, for
 * the A0 electrodes behind the enclosure. */
#define THRESH_RATIO            0.02f
/* delta = 1.0 at this multiple of the threshold ("firm touch"). */
#define FULL_SCALE_THRESHOLDS   2.0f
#define INIT_SCANS              3
#define SCAN_TIMEOUT_MS         2000
#define AMP_SETTLE_US           (150 * 1000)

static touch_sensor_handle_t s_sens;
static touch_channel_handle_t s_chan[MAO_TOUCH_ZONE_COUNT];
static int s_chan_id[MAO_TOUCH_ZONE_COUNT];
static uint32_t s_thresh[MAO_TOUCH_ZONE_COUNT];
static volatile uint32_t s_active_mask;      /* by channel id, from the driver callbacks */
static uint8_t s_prev_touched;               /* by zone */
static mao_obs_touch_zone_t s_left_held;     /* LEFT while the speaker amplifier runs */
static int64_t s_amp_quiet_at_us;

static bool on_change(touch_sensor_handle_t sens, const touch_base_event_data_t *event, void *ctx)
{
    (void)sens;
    (void)ctx;
    s_active_mask = event->status_mask;
    return sense_notify_from_isr(SENSE_BIT_TOUCH);
}

static bool on_active(touch_sensor_handle_t sens, const touch_active_event_data_t *event, void *ctx)
{
    return on_change(sens, event, ctx);
}

static bool on_inactive(touch_sensor_handle_t sens, const touch_inactive_event_data_t *event, void *ctx)
{
    return on_change(sens, event, ctx);
}

esp_err_t sense_touch_read(mao_obs_touch_t *out)
{
    ESP_RETURN_ON_FALSE(s_sens && out, ESP_ERR_INVALID_STATE, TAG, "touch not ready");
    *out = (mao_obs_touch_t) { 0 };
    uint8_t touched = 0;
    const int64_t now = esp_timer_get_time();
    if (mao_board_rail_is_on(MAO_RAIL_AMP)) {
        s_amp_quiet_at_us = now + AMP_SETTLE_US;
    }
    const bool hold_left = now < s_amp_quiet_at_us;
    for (int z = 0; z < MAO_TOUCH_ZONE_COUNT; z++) {
        if (!s_chan[z]) {
            continue;
        }
        if (z == MAO_TOUCH_LEFT && hold_left) {
            out->zone[z] = s_left_held;
            touched |= s_left_held.touched ? (uint8_t)(1u << z) : 0;
            continue;
        }
        uint32_t smooth = 0, bench = 0;
        touch_channel_read_data(s_chan[z], TOUCH_CHAN_DATA_TYPE_SMOOTH, &smooth);
        touch_channel_read_data(s_chan[z], TOUCH_CHAN_DATA_TYPE_BENCHMARK, &bench);
        float delta = 0.0f;
        if (smooth > bench && s_thresh[z] > 0) {
            delta = (float)(smooth - bench) / ((float)s_thresh[z] * FULL_SCALE_THRESHOLDS);
            delta = delta > 1.0f ? 1.0f : delta;
        }
        const bool is_touched = (s_active_mask & (1u << s_chan_id[z])) != 0;
        out->zone[z] = (mao_obs_touch_zone_t) {
            .raw = smooth,
            .baseline = bench,
            .delta = delta,
            .touched = is_touched,
        };
        touched |= is_touched ? (uint8_t)(1u << z) : 0;
        if (z == MAO_TOUCH_LEFT) {
            s_left_held = out->zone[z];
        }
    }
    out->changed = touched ^ s_prev_touched;
    s_prev_touched = touched;
    return ESP_OK;
}

esp_err_t sense_touch_init(void)
{
    mao_board_touch_t bt;
    ESP_RETURN_ON_ERROR(mao_board_touch_get(&bt), TAG, "no touch on this board");

    touch_sensor_sample_config_t sample[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2),
    };
    const touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample);
    ESP_RETURN_ON_ERROR(touch_sensor_new_controller(&sens_cfg, &s_sens), TAG, "controller");

    touch_channel_config_t chan_cfg = {
        .active_thresh = { 2000 },               /* placeholder until the benchmark is known */
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };
    for (int z = 0; z < MAO_TOUCH_ZONE_COUNT; z++) {
        s_chan_id[z] = bt.channel[z];
        if (bt.channel[z] >= 0) {
            ESP_RETURN_ON_ERROR(touch_sensor_new_channel(s_sens, bt.channel[z], &chan_cfg, &s_chan[z]),
                                TAG, "channel %d", bt.channel[z]);
        }
    }
    const touch_sensor_filter_config_t filter = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_RETURN_ON_ERROR(touch_sensor_config_filter(s_sens, &filter), TAG, "filter");

    /* A few one-shot scans give each channel a valid benchmark; thresholds
     * are then set relative to it (the controller must be disabled). */
    ESP_RETURN_ON_ERROR(touch_sensor_enable(s_sens), TAG, "enable");
    for (int i = 0; i < INIT_SCANS; i++) {
        ESP_RETURN_ON_ERROR(touch_sensor_trigger_oneshot_scanning(s_sens, SCAN_TIMEOUT_MS), TAG, "scan");
    }
    ESP_RETURN_ON_ERROR(touch_sensor_disable(s_sens), TAG, "disable");
    for (int z = 0; z < MAO_TOUCH_ZONE_COUNT; z++) {
        if (!s_chan[z]) {
            continue;
        }
        uint32_t bench = 0;
        ESP_RETURN_ON_ERROR(touch_channel_read_data(s_chan[z], TOUCH_CHAN_DATA_TYPE_BENCHMARK, &bench), TAG, "bm");
        s_thresh[z] = (uint32_t)((float)bench * THRESH_RATIO);
        s_thresh[z] = s_thresh[z] ? s_thresh[z] : 1;
        chan_cfg.active_thresh[0] = s_thresh[z];
        ESP_RETURN_ON_ERROR(touch_sensor_reconfig_channel(s_chan[z], &chan_cfg), TAG, "threshold");
    }

    /* Deep-sleep wake on the board's wake zone only (RTC peripherals may
     * power down); all zones still wake the chip from light sleep. */
    const mao_touch_zone_t wz = bt.wake_zone;
    if (s_chan[wz]) {
        const touch_sleep_config_t slp = TOUCH_SENSOR_DEFAULT_DSLP_PD_CONFIG(s_chan[wz], s_thresh[wz]);
        ESP_RETURN_ON_ERROR(touch_sensor_config_sleep_wakeup(s_sens, &slp), TAG, "sleep wakeup");
    }

    const touch_event_callbacks_t cbs = {
        .on_active = on_active,
        .on_inactive = on_inactive,
    };
    ESP_RETURN_ON_ERROR(touch_sensor_register_callbacks(s_sens, &cbs, NULL), TAG, "callbacks");
    ESP_RETURN_ON_ERROR(touch_sensor_enable(s_sens), TAG, "enable");
    ESP_RETURN_ON_ERROR(touch_sensor_start_continuous_scanning(s_sens), TAG, "scanning");
    ESP_LOGI(TAG, "touch: %d zones (thresholds %lu/%lu/%lu/%lu), deep-sleep wake on %s",
             MAO_TOUCH_ZONE_COUNT, (unsigned long)s_thresh[0], (unsigned long)s_thresh[1],
             (unsigned long)s_thresh[2], (unsigned long)s_thresh[3], mao_board_touch_zone_name(wz));
    return ESP_OK;
}

#else  /* no touch sensor on this chip */

esp_err_t sense_touch_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sense_touch_read(mao_obs_touch_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
