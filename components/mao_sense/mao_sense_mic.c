/*
 * Microphone level meter: Knowles SPH0641LU4H-1 (PDM) through the board's
 * I2S0 PDM RX channel, 16 kHz PCM from the hardware PDM->PCM filter.
 *
 * Per 32 ms block (512 samples): DC removed (the S3 filter has no
 * high-pass), RMS and peak in dBFS (0 dBFS = full-scale square wave), and
 * an onset flag when the RMS jumps ONSET_DB above a slowly tracked floor.
 * Only levels leave this file, never audio.
 *
 * Stopping = clock off (the mic sleeps) and, on boards that switch it, its
 * supply off too: the SPH0641 still draws ~80 uA with the clock stopped.
 * Its own task ("mao_mic") does the blocking reads; start / stop requests
 * are acknowledged so a power hook knows the mic is really off.
 */
#include "mao_sense_priv.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2s_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mao_board.h"

static const char *TAG = "MAO_SENSE";

#define MIC_RATE_HZ         16000
#define BLOCK_SAMPLES       512          /* 32 ms */
#define TASK_STACK          3072
#define TASK_PRIO           4
#define READ_TIMEOUT_MS     100
#define SETTLE_BLOCKS       4            /* discarded after the clock starts (mic wake-up, filter settling) */
#define SUPPLY_SETTLE_MS    1            /* 100 R / 1 uF supply filter: ~0.1 ms */
#define ACK_TIMEOUT_MS      300
#define DBFS_FLOOR          (-120.0f)
/* VERIFY AT BRING-UP: onset sensitivity in a quiet room and with speech. */
#define ONSET_DB            10.0f        /* jump above the tracked floor */
#define ONSET_MIN_DBFS      (-75.0f)     /* ignore jumps that stay this quiet */
#define ONSET_REFRACTORY_MS 250
#define FLOOR_ALPHA         0.05f        /* per block: ~0.6 s time constant */

static i2s_chan_handle_t s_rx;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_ack;
static volatile bool s_run = true;
static bool s_running;
static int16_t s_buf[BLOCK_SAMPLES];

static float to_dbfs(float level)
{
    if (level <= 0.0f) {
        return DBFS_FLOOR;
    }
    const float db = 20.0f * log10f(level / 32768.0f);
    return db < DBFS_FLOOR ? DBFS_FLOOR : db;
}

static void apply_run_state(void)
{
    if (s_run == s_running) {
        return;
    }
    if (s_run) {
        /* Not every board switches the mic supply (NOT_SUPPORTED is fine). */
        if (mao_board_rail_set(MAO_RAIL_MIC, true) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(SUPPLY_SETTLE_MS) + 1);
        }
        if (i2s_channel_enable(s_rx) == ESP_OK) {
            s_running = true;
            size_t got = 0;
            for (int i = 0; i < SETTLE_BLOCKS; i++) {
                i2s_channel_read(s_rx, s_buf, sizeof(s_buf), &got, READ_TIMEOUT_MS);
            }
        }
    } else {
        i2s_channel_disable(s_rx);
        mao_board_rail_set(MAO_RAIL_MIC, false);
        s_running = false;
    }
    xSemaphoreGive(s_ack);
}

static void mic_task(void *arg)
{
    (void)arg;
    float floor_db = DBFS_FLOOR;
    uint32_t last_onset_ms = 0;
    for (;;) {
        apply_run_state();
        if (!s_running) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        size_t got = 0;
        if (i2s_channel_read(s_rx, s_buf, sizeof(s_buf), &got, READ_TIMEOUT_MS) != ESP_OK || got != sizeof(s_buf)) {
            continue;
        }

        int32_t sum = 0;
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            sum += s_buf[i];
        }
        const float mean = (float)sum / BLOCK_SAMPLES;
        float energy = 0.0f;
        float peak = 0.0f;
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            const float v = (float)s_buf[i] - mean;
            energy += v * v;
            const float a = fabsf(v);
            peak = a > peak ? a : peak;
        }
        const float rms_db = to_dbfs(sqrtf(energy / BLOCK_SAMPLES));
        const uint32_t now = sense_now_ms();

        const bool onset = rms_db > floor_db + ONSET_DB && rms_db > ONSET_MIN_DBFS &&
                           now - last_onset_ms >= ONSET_REFRACTORY_MS;
        if (onset) {
            last_onset_ms = now;
        }
        /* The floor follows quiet stretches quickly and loud ones slowly, so
         * a sustained sound does not keep re-triggering onsets. */
        floor_db = floor_db <= DBFS_FLOOR + 1.0f ? rms_db
                   : floor_db + FLOOR_ALPHA * (rms_db - floor_db) * (rms_db < floor_db ? 4.0f : 1.0f);

        const mao_obs_t obs = {
            .kind = MAO_OBS_MIC,
            .time_ms = now,
            .event = onset,
            .mic = { .rms_dbfs = rms_db, .peak_dbfs = to_dbfs(peak), .onset = onset },
        };
        sense_publish(&obs);
    }
}

void sense_mic_run(bool run)
{
    if (!s_task) {
        return;
    }
    s_run = run;
    xSemaphoreTake(s_ack, 0);              /* drop a stale acknowledgement */
    xTaskNotifyGive(s_task);
    xSemaphoreTake(s_ack, pdMS_TO_TICKS(ACK_TIMEOUT_MS));
}

esp_err_t sense_mic_init(void)
{
    ESP_RETURN_ON_ERROR(mao_board_mic_init(MIC_RATE_HZ, &s_rx), TAG, "board mic");
    s_ack = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_ack, ESP_ERR_NO_MEM, TAG, "sem");
    if (xTaskCreate(mic_task, "mao_mic", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "mic: level meter, %d ms blocks, onset +%.0f dB over the floor",
             BLOCK_SAMPLES * 1000 / MIC_RATE_HZ, (double)ONSET_DB);
    return ESP_OK;
}
