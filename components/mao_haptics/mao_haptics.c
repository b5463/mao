/*
 * MAO haptic vocabulary on the DRV2605L ROM library (LRA library 6).
 *
 * Why ROM effects: they are tuned for closed-loop LRAs (overdrive and braking
 * are built in), cost no flash or I2C bandwidth, and the closed loop scales
 * them to the calibrated actuator. Each touch has three variants for
 * strength; a touch is a short sequence of effects and waits.
 *
 * Effects used (DRV2605L waveform library numbers):
 *   1-3   Strong Click 100/60/30 %        confirm
 *   7-9   Soft Bump 100/60/30 %           heartbeat
 *   13    Soft Fuzz 60 %                  tremor (strong)
 *   21-23 Medium Click 1-3 100/80/60 %    short_pulse
 *   24-26 Sharp Tick 1-3 100/80/60 %      tick
 *   27-29 Short Double Click Strong 1-3   double_tap
 *   47-51 Buzz 1-5 100..20 %              annoyed_buzz, tremor (lighter)
 *   84,85 Transition Ramp Up Medium Smooth 1/2, 0->100 %   wake_pulse
 *   108   Transition Ramp Up Medium Smooth 1, 0->50 %      wake_pulse (light)
 * VERIFY AT BRING-UP: the feel of each touch on the mounted LRA.
 *
 * One task owns the driver: plays, stops, calibration and the idle
 * shutdown (EN low IDLE_OFF_MS after the last touch ends).
 */
#include "mao_haptics.h"
#include "mao_haptics_priv.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "mao_board.h"
#include "mao_power.h"
#include "mao_system.h"

static const char *TAG = "MAO_HAPTICS";

#define TASK_STACK          3072
#define TASK_PRIO           5
#define QUEUE_LEN           4
#define POLL_MS             20          /* GO-bit poll while a touch plays */
#define IDLE_OFF_MS         1500        /* driver off this long after the last touch */
#define MAX_PLAY_MS         3000        /* safety stop for a sequence that never ends */

#define NVS_NAMESPACE       "mao"
#define NVS_KEY_CAL         "hap_cal"
#define CAL_BLOB_VERSION    1

#define WAIT_MS(ms)         (0x80 | ((ms) / 10))
#define TIERS               3           /* strong, medium, light */

typedef enum { CMD_PLAY = 0, CMD_STOP, CMD_CALIBRATE, CMD_IDENTIFY } cmd_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t haptic;
} cmd_t;

typedef struct {
    uint8_t version;
    mao_drv_cal_t cal;
} cal_blob_t;

static const char *const kNames[MAO_HAPTIC_COUNT] = {
    [MAO_HAPTIC_TICK] = "tick",
    [MAO_HAPTIC_DOUBLE_TAP] = "double_tap",
    [MAO_HAPTIC_HEARTBEAT] = "heartbeat",
    [MAO_HAPTIC_SHORT_PULSE] = "short_pulse",
    [MAO_HAPTIC_TREMOR] = "tremor",
    [MAO_HAPTIC_ANNOYED_BUZZ] = "annoyed_buzz",
    [MAO_HAPTIC_WAKE_PULSE] = "wake_pulse",
    [MAO_HAPTIC_CONFIRM] = "confirm",
};

/* [touch][tier] -> up to 8 sequencer slots, 0-terminated. */
static const uint8_t kVocab[MAO_HAPTIC_COUNT][TIERS][8] = {
    [MAO_HAPTIC_TICK]         = { { 24 }, { 25 }, { 26 } },
    [MAO_HAPTIC_DOUBLE_TAP]   = { { 27 }, { 28 }, { 29 } },
    [MAO_HAPTIC_HEARTBEAT]    = { { 7, WAIT_MS(150), 8 }, { 8, WAIT_MS(150), 9 }, { 9, WAIT_MS(150), 9 } },
    [MAO_HAPTIC_SHORT_PULSE]  = { { 21 }, { 22 }, { 23 } },
    [MAO_HAPTIC_TREMOR]       = { { 13, 13 }, { 50, 50 }, { 51, 51 } },
    [MAO_HAPTIC_ANNOYED_BUZZ] = { { 47, WAIT_MS(60), 47 }, { 48, WAIT_MS(60), 48 }, { 49, WAIT_MS(60), 49 } },
    [MAO_HAPTIC_WAKE_PULSE]   = { { 84 }, { 85 }, { 108 } },
    [MAO_HAPTIC_CONFIRM]      = { { 1 }, { 2 }, { 3 } },
};

#define BUSY_TAIL_MS        250         /* the LRA rings down after GO clears */
#define CAL_BUSY_MS         1600

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_identify_done;
static uint8_t s_identify_id;
static esp_err_t s_identify_err;
static mao_haptics_cal_info_t s_cal_info = { .result = ESP_ERR_INVALID_STATE };
static volatile uint32_t s_busy_until_ms;
static bool s_ready;
static volatile bool s_enabled = true;
static volatile bool s_suspended;          /* MAO drowsy / asleep */
static volatile uint8_t s_strength = 100;

const char *mao_haptic_name(mao_haptic_t haptic)
{
    return haptic < MAO_HAPTIC_COUNT ? kNames[haptic] : "?";
}

bool mao_haptic_from_name(const char *name, mao_haptic_t *out)
{
    for (int i = 0; name && i < MAO_HAPTIC_COUNT; i++) {
        if (strcmp(name, kNames[i]) == 0) {
            if (out) {
                *out = (mao_haptic_t)i;
            }
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------------ */
/* Calibration persistence                                                  */
/* ------------------------------------------------------------------------ */

static bool cal_load(mao_drv_cal_t *cal)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    cal_blob_t blob;
    size_t len = sizeof(blob);
    const bool ok = nvs_get_blob(h, NVS_KEY_CAL, &blob, &len) == ESP_OK && len == sizeof(blob) &&
                    blob.version == CAL_BLOB_VERSION;
    nvs_close(h);
    if (ok) {
        *cal = blob.cal;
    }
    return ok;
}

static esp_err_t cal_store(const mao_drv_cal_t *cal)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h), TAG, "nvs open");
    const cal_blob_t blob = { .version = CAL_BLOB_VERSION, .cal = *cal };
    esp_err_t err = nvs_set_blob(h, NVS_KEY_CAL, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void run_calibration(void)
{
    mao_drv_cal_t cal;
    uint8_t period = 0;
    s_busy_until_ms = now_ms() + CAL_BUSY_MS;
    const esp_err_t err = mao_drv_calibrate(&cal, &period);
    mao_drv_power(false);
    s_cal_info.runs++;
    s_cal_info.result = err;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "calibration failed (%s): using defaults, will retry next boot", esp_err_to_name(err));
        return;
    }
    s_cal_info.from_nvs = false;
    s_cal_info.comp = cal.comp;
    s_cal_info.bemf = cal.bemf;
    s_cal_info.bemf_gain = cal.bemf_gain;
    /* LRA_PERIOD: 98.46 us per LSB. */
    s_cal_info.resonance_hz = period ? (uint16_t)(1000000u / (period * 9846u / 100u)) : 0;
    const esp_err_t st = cal_store(&cal);
    ESP_LOGI(TAG, "calibration stored in NVS: %s", esp_err_to_name(st));
}

/* ------------------------------------------------------------------------ */
/* Task                                                                     */
/* ------------------------------------------------------------------------ */

static int tier_for_strength(uint8_t strength)
{
    return strength >= 75 ? 0 : (strength >= 40 ? 1 : 2);
}

static void play(mao_haptic_t haptic)
{
    if (!s_enabled || s_suspended || s_strength == 0 || haptic >= MAO_HAPTIC_COUNT) {
        return;
    }
    if (mao_drv_power(true) != ESP_OK) {
        return;
    }
    const uint8_t *seq = kVocab[haptic][tier_for_strength(s_strength)];
    size_t len = 0;
    while (len < 8 && seq[len] != 0) {
        len++;
    }
    const esp_err_t err = mao_drv_play(seq, len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "play %s: %s", kNames[haptic], esp_err_to_name(err));
    }
}

static void haptics_task(void *arg)
{
    (void)arg;
    bool playing = false;
    int64_t play_started_us = 0;
    int64_t off_at_us = 0;
    uint32_t resets_seen = mao_board_expander_resets();
    for (;;) {
        /* An expander reset dropped EN behind the driver's back: its
         * registers are gone. Power it down cleanly; the next touch powers
         * it up and configures it again. */
        const uint32_t resets = mao_board_expander_resets();
        if (resets != resets_seen) {
            resets_seen = resets;
            if (mao_drv_is_powered()) {
                mao_drv_power(false);
                playing = false;
            }
        }
        TickType_t wait = portMAX_DELAY;
        if (playing) {
            wait = pdMS_TO_TICKS(POLL_MS);
        } else if (mao_drv_is_powered()) {
            const int64_t left_us = off_at_us - esp_timer_get_time();
            wait = left_us > 0 ? pdMS_TO_TICKS(left_us / 1000) + 1 : 0;
        }

        cmd_t cmd;
        if (xQueueReceive(s_queue, &cmd, wait) == pdTRUE) {
            switch (cmd.kind) {
            case CMD_PLAY:
                play((mao_haptic_t)cmd.haptic);
                playing = mao_drv_is_powered();
                play_started_us = esp_timer_get_time();
                if (playing) {
                    s_busy_until_ms = now_ms() + 400;    /* refreshed while GO stays set */
                }
                break;
            case CMD_IDENTIFY:
                s_identify_err = mao_drv_device_id(&s_identify_id);
                xSemaphoreGive(s_identify_done);
                break;
            case CMD_STOP:
                mao_drv_stop();
                mao_drv_power(false);
                playing = false;
                break;
            case CMD_CALIBRATE:
                run_calibration();
                playing = false;
                break;
            default:
                break;
            }
            continue;
        }

        const int64_t now = esp_timer_get_time();
        if (playing) {
            bool busy = false;
            if (mao_drv_busy(&busy) != ESP_OK || !busy || now - play_started_us > MAX_PLAY_MS * 1000LL) {
                playing = false;
                off_at_us = now + IDLE_OFF_MS * 1000LL;
            }
            s_busy_until_ms = (uint32_t)(now / 1000) + BUSY_TAIL_MS;
        } else if (mao_drv_is_powered() && now >= off_at_us) {
            mao_drv_power(false);   /* full shutdown: EN low */
        }
    }
}

static void send(cmd_kind_t kind, mao_haptic_t haptic)
{
    if (s_queue) {
        const cmd_t cmd = { .kind = (uint8_t)kind, .haptic = (uint8_t)haptic };
        xQueueSend(s_queue, &cmd, 0);   /* touches are disposable when busy */
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

void mao_haptics_get_cal_info(mao_haptics_cal_info_t *out)
{
    if (out) {
        *out = s_cal_info;
    }
}

esp_err_t mao_haptics_identify(uint8_t *device_id, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_ready && device_id, ESP_ERR_INVALID_STATE, TAG, "haptics not running");
    xSemaphoreTake(s_identify_done, 0);
    const cmd_t cmd = { .kind = CMD_IDENTIFY };
    ESP_RETURN_ON_FALSE(xQueueSend(s_queue, &cmd, pdMS_TO_TICKS(timeout_ms)) == pdTRUE, ESP_ERR_TIMEOUT, TAG,
                        "queue");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_identify_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE, ESP_ERR_TIMEOUT,
                        TAG, "no answer");
    *device_id = s_identify_id;
    return s_identify_err;
}

uint32_t mao_haptics_busy_until_ms(void)
{
    return s_busy_until_ms;
}

void mao_haptics_play(mao_haptic_t haptic)
{
    if (s_ready && s_enabled && !s_suspended && s_strength > 0) {
        send(CMD_PLAY, haptic);
    }
}

void mao_haptics_set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (!enabled) {
        send(CMD_STOP, MAO_HAPTIC_TICK);
    }
}

bool mao_haptics_is_enabled(void)
{
    return s_enabled;
}

void mao_haptics_set_strength(uint8_t percent)
{
    s_strength = percent > 100 ? 100 : percent;
}

void mao_haptics_stop(void)
{
    send(CMD_STOP, MAO_HAPTIC_TICK);
}

esp_err_t mao_haptics_calibrate(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "haptics not running");
    send(CMD_CALIBRATE, MAO_HAPTIC_TICK);
    return ESP_OK;
}

/* Drowsy / asleep: nothing may buzz, and the driver goes fully off. */
static void power_hook(const mao_power_transition_t *t, void *ctx)
{
    (void)ctx;
    s_suspended = t->to == MAO_POWER_DROWSY || t->to == MAO_POWER_DEEP_SLEEP;
    if (s_suspended) {
        send(CMD_STOP, MAO_HAPTIC_TICK);
    }
}

#if CONFIG_MAO_DEV_CONSOLE

static void devcmd_haptic(const char *args)
{
    mao_haptic_t h;
    if (strcmp(args, "calibrate") == 0) {
        ESP_LOGI(TAG, "dev: auto-calibration (keep the device still)");
        mao_haptics_calibrate();
    } else if (strncmp(args, "strength ", 9) == 0) {
        mao_haptics_set_strength((uint8_t)atoi(args + 9));
        ESP_LOGI(TAG, "dev: strength %u %%", s_strength);
    } else if (strcmp(args, "off") == 0 || strcmp(args, "on") == 0) {
        mao_haptics_set_enabled(args[1] == 'n');
        ESP_LOGI(TAG, "dev: haptics %s", s_enabled ? "on" : "off");
    } else if (mao_haptic_from_name(args, &h)) {
        mao_haptics_play(h);
    } else {
        ESP_LOGW(TAG, "dev: haptic <tick|double_tap|heartbeat|short_pulse|tremor|annoyed_buzz|wake_pulse|confirm"
                 "|calibrate|on|off|strength N>");
    }
}

static void register_devcmds(void)
{
    mao_devcmd_register("haptic", "haptic <name>|calibrate|on|off|strength N  (names: tick double_tap heartbeat "
                        "short_pulse tremor annoyed_buzz wake_pulse confirm)", devcmd_haptic);
}

#else

static void register_devcmds(void)
{
}

#endif

esp_err_t mao_haptics_init(void)
{
    mao_board_caps_t caps;
    mao_board_get_caps(&caps);
    if (!caps.haptic) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_RETURN_ON_ERROR(mao_drv_init(), TAG, "drv2605l");

    mao_drv_cal_t cal;
    const bool have_cal = cal_load(&cal);
    if (have_cal) {
        mao_drv_set_cal(&cal);
        s_cal_info.from_nvs = true;
        s_cal_info.comp = cal.comp;
        s_cal_info.bemf = cal.bemf;
        s_cal_info.bemf_gain = cal.bemf_gain;
    }

    s_queue = xQueueCreate(QUEUE_LEN, sizeof(cmd_t));
    s_identify_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_queue && s_identify_done, ESP_ERR_NO_MEM, TAG, "queue");
    if (xTaskCreate(haptics_task, "mao_haptics", TASK_STACK, NULL, TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    if (!have_cal) {
        /* First boot: calibrate in the background (~1.2 s of buzzing). */
        send(CMD_CALIBRATE, MAO_HAPTIC_TICK);
    }
    mao_power_register_hook("haptics", power_hook, NULL);
    register_devcmds();
    ESP_LOGI(TAG, "haptics: DRV2605L, LRA closed loop, ROM library 6, %s",
             have_cal ? "calibration loaded from NVS" : "calibrating");
    return ESP_OK;
}
