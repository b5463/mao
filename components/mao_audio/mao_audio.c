#include "mao_audio.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
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
    SOUND_BUMP1,
    SOUND_BUMP2,
    SOUND_THUNK,
    SOUND_DEPART,
    SOUND_SHUTTER,
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
/* Wall contact: three escalating thuds. Low but not so low the tiny speaker
 * loses them; the last one gets a short lower tail (the "THUNK"). */
static const tone_seg_t kBump1[] = {
    { .freq_hz = 380, .dur_ms = 26, .attack_ms = 2, .release_ms = 22, .amp_q15 = 9500 },
};
static const tone_seg_t kBump2[] = {
    { .freq_hz = 330, .dur_ms = 38, .attack_ms = 2, .release_ms = 32, .amp_q15 = 13500 },
};
static const tone_seg_t kThunk[] = {
    { .freq_hz = 290, .dur_ms = 46, .attack_ms = 1, .release_ms = 36, .amp_q15 = 17500 },
    { .freq_hz = 210, .dur_ms = 70, .attack_ms = 2, .release_ms = 62, .amp_q15 = 11000 },
};
/* A frame taken: a mechanical shutter's two quick clicks - the blades
 * open high and bright, close lower. Short enough never to crowd a burst. */
static const tone_seg_t kShutter[] = {
    { .freq_hz = 2600, .dur_ms = 9,  .attack_ms = 1, .release_ms = 7,  .amp_q15 = 12000 },
    { .freq_hz = 1500, .dur_ms = 14, .attack_ms = 1, .release_ms = 12, .amp_q15 = 10000 },
};
/* Departure: a tiny soft rising pair, quieter than confirm. */
static const tone_seg_t kDepart[] = {
    { .freq_hz = 740,  .dur_ms = 30, .attack_ms = 3, .release_ms = 18, .amp_q15 = 7000 },
    { .freq_hz = 1180, .dur_ms = 45, .attack_ms = 3, .release_ms = 36, .amp_q15 = 6000 },
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
    [SOUND_BUMP1]   = SOUND(kBump1),
    [SOUND_BUMP2]   = SOUND(kBump2),
    [SOUND_THUNK]   = SOUND(kThunk),
    [SOUND_DEPART]  = SOUND(kDepart),
    [SOUND_SHUTTER] = SOUND(kShutter),
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

/* Suspend / resume (M4.1 sleep): the stream is parked at the floor (no PDM
 * pulses: the line low, the amplifier silent) before the channel stops, and
 * the DMA is preloaded with the floor before it starts again, so neither
 * edge can reach the speaker as a click. The task waits on s_resume while
 * suspended; it never writes to a disabled channel. */
static SemaphoreHandle_t s_parked, s_resume;
static volatile bool s_suspend;

static void park_if_asked(void)
{
    if (!s_suspend) {
        return;
    }
    for (int i = 0; i < CHUNK_FRAMES; i++) {
        s_chunk[i] = FLOOR;
    }
    for (int k = 0; k < 4; k++) {
        write_chunk(CHUNK_FRAMES);            /* the whole DMA ring is floor now */
    }
    xSemaphoreGive(s_parked);
    xSemaphoreTake(s_resume, portMAX_DELAY);
}

static void start_on_floor(void)
{
    for (int i = 0; i < CHUNK_FRAMES; i++) {
        s_chunk[i] = FLOOR;
    }
    size_t loaded = 1;
    for (int k = 0; k < 8 && loaded; k++) {   /* the DMA ring holds the floor before it runs */
        loaded = 0;
        i2s_channel_preload_data(s_tx, s_chunk, CHUNK_FRAMES * sizeof(int16_t), &loaded);
    }
    mao_board_audio_line_rise();              /* the line glides up to the floor's level ... */
    i2s_channel_enable(s_tx);
    vTaskDelay(pdMS_TO_TICKS(20));            /* ... the stream runs on its floor ... */
    mao_board_audio_line_attach();            /* ... and takes the line */
}

static void audio_task(void *arg)
{
    (void)arg;
    /* Once, at start: the line glides up from still to the floor's level
     * (the stream would otherwise begin at mid scale - a jump the speaker
     * hears as a click), and the stream takes it over on its floor. */
    start_on_floor();
    play_t p;
    for (;;) {
        /* Keep the DMA fed with the floor: if it ever ran dry it would play
         * mid-scale zeros, a jump the speaker hears. */
        if (xQueueReceive(s_queue, &p, 0) != pdTRUE) {
            park_if_asked();
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
    /* The channel runs for the lifetime of the firmware (the task starts it
     * on its floor, then keeps it fed between sounds; only a rest stops it). */

    s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(play_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    s_parked = xSemaphoreCreateBinary();
    s_resume = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_parked && s_resume, ESP_ERR_NO_MEM, TAG, "sems");
    if (xTaskCreate(audio_task, "mao_audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "tone engine ready: %d Hz mono, %d ms chunks, sounds: tick touch release confirm back notice warm",
             SAMPLE_RATE_HZ, CHUNK_FRAMES * 1000 / SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t mao_audio_suspend(void)
{
    if (!s_tx || s_suspend) {
        return ESP_OK;
    }
    xQueueReset(s_queue);                     /* nothing new starts; a sound playing finishes */
    s_suspend = true;
    if (xSemaphoreTake(s_parked, pdMS_TO_TICKS(1000)) != pdTRUE) {
        s_suspend = false;
        return ESP_ERR_TIMEOUT;
    }
    mao_board_audio_line_rest();              /* the line glides from the floor's level to still ... */
    return i2s_channel_disable(s_tx);         /* ... and the stream stops behind it, off the pin */
}

/* DEV: the PDM path's gain stages (i2s_pdm_sig_scale_t: 0 /2, 1 x1, 2 x2,
 * 3 x4), reconfigured live - to find where the floor reaches a still line. */
esp_err_t mao_audio_debug_scale(int hp, int sd)
{
    ESP_RETURN_ON_ERROR(mao_audio_suspend(), TAG, "park");
    i2s_pdm_tx_slot_config_t slot = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    slot.hp_en = false;
    slot.sd_dither = 0;
    slot.sd_dither2 = 0;
    slot.hp_scale = (i2s_pdm_sig_scale_t)hp;
    slot.sd_scale = (i2s_pdm_sig_scale_t)sd;
    const esp_err_t err = i2s_channel_reconfig_pdm_tx_slot(s_tx, &slot);
    mao_audio_resume();
    return err;
}

esp_err_t mao_audio_resume(void)
{
    if (!s_tx || !s_suspend) {
        return ESP_OK;
    }
    start_on_floor();
    s_suspend = false;
    xSemaphoreGive(s_resume);
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

void mao_audio_bump(uint8_t strength)
{
    enqueue(strength >= 3 ? SOUND_THUNK : (strength == 2 ? SOUND_BUMP2 : SOUND_BUMP1));
}

void mao_audio_shutter(void)
{
    enqueue(SOUND_SHUTTER);
}

void mao_audio_depart(void)
{
    enqueue(SOUND_DEPART);
}
