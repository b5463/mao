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
/* The line idles at the bottom of the PDM range: an almost static signal.
 * PDM silence at mid scale is a dense bit pattern that the NS4150 (no
 * enable pin) turns into an audible ring, and stopping / starting the
 * stream cracks. So the stream never stops, idles at this floor, and every
 * sound rides up from it inside its own soft envelope. */
#define FLOOR                (-32768)       /* the very bottom: no PDM pulses at all */
#define BOOT_RAMP_MS         600            /* mid scale -> floor, once, at start */
#define TICK_MIN_GAP_MS      35             /* hard ceiling for tick rate */

/* volume 100 % -> gain 0.58, so the 60 % default equals the M0 level (0.35).
 * The NS4150 is loud; restraint is deliberate. */
#define GAIN_AT_FULL_VOLUME  0.58f

#define SINE_LUT_BITS        8
#define SINE_LUT_SIZE        (1 << SINE_LUT_BITS)

typedef enum {
    SOUND_TICK,
    SOUND_TOUCH,
    SOUND_RELEASE,
    SOUND_WARM,
    SOUND_NOTICE,
    SOUND_CONFIRM,
    SOUND_BACK,
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
    /* A rounder tick: lower, with soft edges (a 1 ms attack cracks). */
    { .freq_hz = 1850, .dur_ms = 10, .attack_ms = 3, .release_ms = 7,  .amp_q15 = 7500 },
};
/* Touch: a low, soft contact. Release: a slightly higher lift. */
static const tone_seg_t kTouch[] = {
    { .freq_hz = 520,  .dur_ms = 18, .attack_ms = 3, .release_ms = 13, .amp_q15 = 9000 },
};
static const tone_seg_t kRelease[] = {
    { .freq_hz = 880,  .dur_ms = 22, .attack_ms = 2, .release_ms = 17, .amp_q15 = 8000 },
};
/* Warm: two low soft notes rising a fourth, long tail. */
static const tone_seg_t kWarm[] = {
    { .freq_hz = 587,  .dur_ms = 55, .attack_ms = 6, .release_ms = 20, .amp_q15 = 11000 },
    { .freq_hz = 784,  .dur_ms = 110, .attack_ms = 4, .release_ms = 90, .amp_q15 = 11000 },
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

#define SOUND(arr) { arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])) }
static const sound_t kSounds[SOUND_COUNT] = {
    [SOUND_TICK]    = SOUND(kTick),
    [SOUND_TOUCH]   = SOUND(kTouch),
    [SOUND_RELEASE] = SOUND(kRelease),
    [SOUND_WARM]    = SOUND(kWarm),
    [SOUND_NOTICE]  = SOUND(kNotice),
    [SOUND_CONFIRM] = SOUND(kConfirm),
    [SOUND_BACK]    = SOUND(kBack),
};

static i2s_chan_handle_t s_tx;
static QueueHandle_t s_queue;
static int16_t s_sine[SINE_LUT_SIZE];
static int16_t s_chunk[CHUNK_FRAMES];
static int64_t s_last_tick_us;

typedef struct {
    uint8_t id;          /* sound_id_t */
    uint8_t level;       /* 0..255 per-play level (ticks get softer at speed) */
} play_t;
static volatile int32_t s_gain_q15 = (int32_t)(0.35f * 32767);

static void render_segment(const tone_seg_t *seg, uint8_t level)
{
    const uint32_t total = (uint32_t)seg->dur_ms * SAMPLE_RATE_HZ / 1000;
    const uint32_t attack = (uint32_t)seg->attack_ms * SAMPLE_RATE_HZ / 1000;
    const uint32_t release = (uint32_t)seg->release_ms * SAMPLE_RATE_HZ / 1000;
    /* 32-bit phase accumulator; top SINE_LUT_BITS index the table. */
    const uint32_t phase_inc = (uint32_t)(((uint64_t)seg->freq_hz << 32) / SAMPLE_RATE_HZ);
    const int32_t gain = (s_gain_q15 * level) >> 8;
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
            /* Peak amplitude a, riding on the floor: floor .. floor + 2a. */
            const int32_t a = (env * gain) >> 15;
            const int32_t s = FLOOR + a + ((s_sine[phase >> (32 - SINE_LUT_BITS)] * a) >> 15);
            s_chunk[i] = (int16_t)s;
            phase += phase_inc;
        }
        size_t written = 0;
        i2s_channel_write(s_tx, s_chunk, frames * sizeof(int16_t), &written, portMAX_DELAY);
    }
}

static void write_chunk(uint32_t frames)
{
    size_t written = 0;
    i2s_channel_write(s_tx, s_chunk, frames * sizeof(int16_t), &written, portMAX_DELAY);
}

static void audio_task(void *arg)
{
    (void)arg;
    /* Once: glide from mid scale (where the stream starts) down to the floor. */
    const uint32_t ramp = BOOT_RAMP_MS * SAMPLE_RATE_HZ / 1000;
    for (uint32_t n = 0; n < ramp;) {
        uint32_t i = 0;
        for (; i < CHUNK_FRAMES && n < ramp; i++, n++) {
            const float u = 0.5f - 0.5f * cosf((float)M_PI * (float)n / (float)ramp);
            s_chunk[i] = (int16_t)(FLOOR * u);
        }
        write_chunk(i);
    }
    play_t p;
    for (;;) {
        /* Keep the DMA fed with the floor: if it ever ran dry it would play
         * mid-scale zeros, a jump the speaker hears. */
        if (xQueueReceive(s_queue, &p, 0) != pdTRUE) {
            for (int i = 0; i < CHUNK_FRAMES; i++) {
                s_chunk[i] = FLOOR;
            }
            write_chunk(CHUNK_FRAMES);
            continue;
        }
        if (p.id < SOUND_COUNT) {
            for (uint8_t i = 0; i < kSounds[p.id].count; i++) {
                render_segment(&kSounds[p.id].segs[i], p.level);
            }
        }
    }
}

esp_err_t mao_audio_init(void)
{
    for (int i = 0; i < SINE_LUT_SIZE; i++) {
        s_sine[i] = (int16_t)lrintf(32767.0f * sinf(2.0f * (float)M_PI * (float)i / SINE_LUT_SIZE));
    }

    ESP_RETURN_ON_ERROR(mao_board_audio_init(SAMPLE_RATE_HZ, &s_tx), TAG, "board audio");
    /* The channel stays enabled for the lifetime of the firmware and the
     * task keeps it fed with the idle floor between sounds. */
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");

    s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(play_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    if (xTaskCreate(audio_task, "mao_audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "tone engine ready: %d Hz mono, %d ms chunks, sounds: tick touch release confirm back notice warm",
             SAMPLE_RATE_HZ, CHUNK_FRAMES * 1000 / SAMPLE_RATE_HZ);
    return ESP_OK;
}

void mao_audio_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_gain_q15 = (int32_t)(GAIN_AT_FULL_VOLUME * 32767.0f * (float)percent / 100.0f);
}

static void enqueue(sound_id_t id)
{
    if (s_queue) {
        const play_t p = { .id = (uint8_t)id, .level = 255 };
        xQueueSend(s_queue, &p, 0);
    }
}

void mao_audio_tick(uint8_t intensity)
{
    if (!s_queue) {
        return;
    }
    /* Faster turning = softer and sparser ticks: a texture, never a buzz. */
    const int64_t now = esp_timer_get_time();
    const int64_t gap_us = (TICK_MIN_GAP_MS + (int64_t)intensity * 70 / 255) * 1000;
    if (now - s_last_tick_us < gap_us || uxQueueMessagesWaiting(s_queue) > 0) {
        return;   /* ticks never queue behind other sounds */
    }
    const play_t p = { .id = SOUND_TICK, .level = (uint8_t)(255 - intensity * 115 / 255) };
    if (xQueueSend(s_queue, &p, 0) == pdTRUE) {
        s_last_tick_us = now;
    }
}

void mao_audio_touch(void)
{
    enqueue(SOUND_TOUCH);
}

void mao_audio_release(void)
{
    enqueue(SOUND_RELEASE);
}

void mao_audio_warm(void)
{
    enqueue(SOUND_WARM);
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
