#include "mao_audio.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/i2s_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mao_board.h"

static const char *TAG = "MAO_AUDIO";

#define SAMPLE_RATE_HZ       16000
#define AUDIO_TASK_STACK     3072
#define AUDIO_TASK_PRIO      6
#define AUDIO_QUEUE_LEN      4
#define CHUNK_FRAMES         160            /* 10 ms per i2s write */
#define TICK_MIN_GAP_MS      35             /* hard ceiling for tick rate */

/* Gain at 100 % volume comes from the board (mao_board_audio_gain): 0.58 on
 * the LCDkit, so the 60 % default equals the M0 level (0.35). The NS4150 is
 * loud; restraint is deliberate. */

/* Boards with a switchable amplifier (A0: MAX98357A shutdown via SD_MODE)
 * power it only around sounds. VERIFY AT BRING-UP: enable-to-sound time and
 * whether switching is audible. */
#define AMP_WAKE_MS          2
#define AMP_IDLE_OFF_MS      3000

#define SINE_LUT_BITS        8
#define SINE_LUT_SIZE        (1 << SINE_LUT_BITS)

typedef enum {
    SOUND_TICK,
    SOUND_NOTICE,
    SOUND_CONFIRM,
    SOUND_BACK,
    SOUND_TSK,
    SOUND_TEST,
    SOUND_COUNT,
} sound_id_t;

/* One tone segment with a linear attack and release (never starts or ends
 * at non-zero amplitude, so no DC steps / pops). */
typedef struct {
    uint16_t freq_hz;
    uint16_t dur_ms;
    uint16_t attack_ms;
    uint16_t release_ms;
    uint16_t amp_q15;
} tone_seg_t;

typedef struct {
    const tone_seg_t *segs;
    uint8_t count;
} sound_t;

static const tone_seg_t kTick[] = {
    { .freq_hz = 2600, .dur_ms = 7,  .attack_ms = 1, .release_ms = 5,  .amp_q15 = 9000 },
};
static const tone_seg_t kNotice[] = {
    { .freq_hz = 1319, .dur_ms = 45, .attack_ms = 2, .release_ms = 32, .amp_q15 = 12000 },
};
static const tone_seg_t kConfirm[] = {
    { .freq_hz = 1047, .dur_ms = 50, .attack_ms = 3, .release_ms = 20, .amp_q15 = 16000 },
    { .freq_hz = 1568, .dur_ms = 80, .attack_ms = 3, .release_ms = 50, .amp_q15 = 16000 },
};
static const tone_seg_t kBack[] = {
    { .freq_hz = 1568, .dur_ms = 40, .attack_ms = 3, .release_ms = 16, .amp_q15 = 13000 },
    { .freq_hz = 1047, .dur_ms = 60, .attack_ms = 3, .release_ms = 40, .amp_q15 = 13000 },
};

/* "tsk": two dry, very short high clicks (the second a touch lower) with a
 * gap: disapproval without a melody. freq 0 = silence. */
static const tone_seg_t kTsk[] = {
    { .freq_hz = 3900, .dur_ms = 9,  .attack_ms = 1, .release_ms = 7,  .amp_q15 = 11000 },
    { .freq_hz = 0,    .dur_ms = 55, .attack_ms = 0, .release_ms = 0,  .amp_q15 = 0 },
    { .freq_hz = 3300, .dur_ms = 12, .attack_ms = 1, .release_ms = 9,  .amp_q15 = 12000 },
};
/* Self-test chirp: stepped sweep across the speaker's useful band, loud
 * enough for the microphone to hear it (factory acoustic loopback). */
static const tone_seg_t kTest[] = {
    { .freq_hz = 700,  .dur_ms = 70, .attack_ms = 3, .release_ms = 5, .amp_q15 = 20000 },
    { .freq_hz = 1200, .dur_ms = 70, .attack_ms = 3, .release_ms = 5, .amp_q15 = 20000 },
    { .freq_hz = 2000, .dur_ms = 70, .attack_ms = 3, .release_ms = 5, .amp_q15 = 20000 },
    { .freq_hz = 3000, .dur_ms = 70, .attack_ms = 3, .release_ms = 5, .amp_q15 = 20000 },
    { .freq_hz = 4200, .dur_ms = 70, .attack_ms = 3, .release_ms = 5, .amp_q15 = 20000 },
    { .freq_hz = 2000, .dur_ms = 120, .attack_ms = 3, .release_ms = 40, .amp_q15 = 20000 },
};

#define SOUND(arr) { arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])) }
static const sound_t kSounds[SOUND_COUNT] = {
    [SOUND_TICK]    = SOUND(kTick),
    [SOUND_NOTICE]  = SOUND(kNotice),
    [SOUND_CONFIRM] = SOUND(kConfirm),
    [SOUND_BACK]    = SOUND(kBack),
    [SOUND_TSK]     = SOUND(kTsk),
    [SOUND_TEST]    = SOUND(kTest),
};

static i2s_chan_handle_t s_tx;
static QueueHandle_t s_queue;
static int16_t s_sine[SINE_LUT_SIZE];
static int16_t s_chunk[CHUNK_FRAMES];
static int64_t s_last_tick_us;
static volatile int32_t s_gain_q15 = (int32_t)(0.35f * 32767);
static float s_gain_full = 0.58f;
static bool s_amp_switch;
static bool s_amp_on;
static volatile uint32_t s_busy_until_ms;   /* the speaker sounds until then (+ room tail) */
static volatile uint8_t s_test_gain_pct;    /* self-test: fixed level, independent of volume */

#define ROOM_TAIL_MS         150            /* reverb / mechanical ring-out after a sound */

static void render_segment(const tone_seg_t *seg)
{
    const uint32_t total = (uint32_t)seg->dur_ms * SAMPLE_RATE_HZ / 1000;
    const uint32_t attack = (uint32_t)seg->attack_ms * SAMPLE_RATE_HZ / 1000;
    const uint32_t release = (uint32_t)seg->release_ms * SAMPLE_RATE_HZ / 1000;
    /* 32-bit phase accumulator; top SINE_LUT_BITS index the table. */
    const uint32_t phase_inc = (uint32_t)(((uint64_t)seg->freq_hz << 32) / SAMPLE_RATE_HZ);
    const int32_t gain = s_gain_q15;
    uint32_t phase = 0;

    uint32_t n = 0;
    while (n < total) {
        uint32_t frames = total - n;
        if (frames > CHUNK_FRAMES) {
            frames = CHUNK_FRAMES;
        }
        for (uint32_t i = 0; i < frames; i++, n++) {
            int32_t env = seg->amp_q15;
            if (attack && n < attack) {
                env = env * (int32_t)n / (int32_t)attack;
            } else if (release && n >= total - release) {
                env = env * (int32_t)(total - n) / (int32_t)release;
            }
            int32_t s = s_sine[phase >> (32 - SINE_LUT_BITS)];
            s = (s * env) >> 15;
            s = (s * gain) >> 15;
            s_chunk[i] = (int16_t)s;
            phase += phase_inc;
        }
        size_t written = 0;
        i2s_channel_write(s_tx, s_chunk, frames * sizeof(int16_t), &written, portMAX_DELAY);
    }
}

static void amp_power(bool on)
{
    if (!s_amp_switch || s_amp_on == on) {
        return;
    }
    if (mao_board_rail_set(MAO_RAIL_AMP, on) == ESP_OK) {
        s_amp_on = on;
        if (on) {
            vTaskDelay(pdMS_TO_TICKS(AMP_WAKE_MS));
        }
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    sound_id_t id;
    for (;;) {
        const TickType_t wait = s_amp_on ? pdMS_TO_TICKS(AMP_IDLE_OFF_MS) : portMAX_DELAY;
        if (xQueueReceive(s_queue, &id, wait) != pdTRUE) {
            amp_power(false);     /* quiet for a while: shut the amplifier down */
            continue;
        }
        if (id < SOUND_COUNT) {
            uint32_t dur = 0;
            for (uint8_t i = 0; i < kSounds[id].count; i++) {
                dur += kSounds[id].segs[i].dur_ms;
            }
            /* Tell listeners (perception) this sound is MAO's own. */
            s_busy_until_ms = (uint32_t)(esp_timer_get_time() / 1000) + AMP_WAKE_MS + dur + ROOM_TAIL_MS;
            amp_power(true);
            const int32_t saved = s_gain_q15;
            if (id == SOUND_TEST) {
                s_gain_q15 = (int32_t)(s_gain_full * 32767.0f * (float)s_test_gain_pct / 100.0f);
            }
            for (uint8_t i = 0; i < kSounds[id].count; i++) {
                render_segment(&kSounds[id].segs[i]);
            }
            s_gain_q15 = saved;
        }
    }
}

esp_err_t mao_audio_init(void)
{
    for (int i = 0; i < SINE_LUT_SIZE; i++) {
        s_sine[i] = (int16_t)lrintf(32767.0f * sinf(2.0f * (float)M_PI * (float)i / SINE_LUT_SIZE));
    }

    ESP_RETURN_ON_ERROR(mao_board_audio_init(SAMPLE_RATE_HZ, &s_tx), TAG, "board audio");
    mao_board_caps_t caps;
    mao_board_get_caps(&caps);
    s_amp_switch = caps.amp_switch;
    s_gain_full = mao_board_audio_gain();
    /* The channel stays enabled for the lifetime of the firmware; with
     * auto_clear the DMA plays silence between sounds. */
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");

    s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(sound_id_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    if (xTaskCreate(audio_task, "mao_audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "tone engine ready: %d Hz mono, %d ms chunks, sounds: tick notice confirm back",
             SAMPLE_RATE_HZ, CHUNK_FRAMES * 1000 / SAMPLE_RATE_HZ);
    return ESP_OK;
}

void mao_audio_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_gain_q15 = (int32_t)(s_gain_full * 32767.0f * (float)percent / 100.0f);
}

static void enqueue(sound_id_t id)
{
    if (s_queue) {
        xQueueSend(s_queue, &id, 0);
    }
}

void mao_audio_tick(void)
{
    if (!s_queue) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (now - s_last_tick_us < TICK_MIN_GAP_MS * 1000) {
        return;
    }
    /* Ticks never queue behind other sounds. */
    if (uxQueueMessagesWaiting(s_queue) > 0) {
        return;
    }
    const sound_id_t id = SOUND_TICK;
    if (xQueueSend(s_queue, &id, 0) == pdTRUE) {
        s_last_tick_us = now;
    }
}

void mao_audio_notice(void)
{
    enqueue(SOUND_NOTICE);
}

void mao_audio_confirm(void)
{
    enqueue(SOUND_CONFIRM);
}

void mao_audio_back(void)
{
    enqueue(SOUND_BACK);
}

void mao_audio_tsk(void)
{
    enqueue(SOUND_TSK);
}

void mao_audio_test_chirp(uint8_t level_percent)
{
    s_test_gain_pct = level_percent > 100 ? 100 : level_percent;
    enqueue(SOUND_TEST);
}

uint32_t mao_audio_busy_until_ms(void)
{
    return s_busy_until_ms;
}
