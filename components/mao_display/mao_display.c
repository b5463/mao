#include "mao_display.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mao_board.h"
#include "mao_lvgl.h"
#include "mao_power.h"
#include "mao_settings.h"
#include "mao_system.h"

static const char *TAG = "MAO_DISPLAY";

#define POWER_HOOK_LOCK_MS  500
#define API_LOCK_MS         1000
#define RECOVERY_CHECK_MS   250

static mao_lvgl_t s_lvgl;
static uint8_t s_brightness;
static bool s_panel_asleep;
static uint16_t s_rotation;
static bool s_rail_off;             /* switched off on request (self-test): rendering paused */
static uint32_t s_resets_seen;      /* mao_board_expander_resets() already handled */
static bool s_reinit_pending;       /* panel was reset while asleep */

/* Test layer (factory self-test): a full-screen plate above every view. */
static lv_obj_t *s_test_plate;
static lv_obj_t *s_test_big;
static lv_obj_t *s_test_small;

/* ------------------------------------------------------------------------ */
/* Frame performance probe (development builds)                             */
/*                                                                          */
/* A "frame" is one LVGL refresh that actually rendered something           */
/* (RENDER_START fired). Frame time = RENDER_START -> REFR_READY, i.e.      */
/* drawing all dirty areas plus pushing them over SPI. Idle screens cost    */
/* nothing and are not counted.                                             */
/* ------------------------------------------------------------------------ */
#if CONFIG_MAO_PERF_PROBE

typedef struct {
    int64_t window_start_us;
    int64_t frame_start_us;
    bool in_frame;
    uint32_t frames;
    int64_t total_us;
    int64_t worst_us;
    int64_t sec_index;       /* current 1 s bucket */
    uint32_t sec_frames;
    uint32_t active_secs;    /* seconds with >= 1 frame */
    uint32_t peak_sec_frames;
} perf_t;

static perf_t s_perf;

static void perf_close_second(void)
{
    if (s_perf.sec_frames > 0) {
        s_perf.active_secs++;
        if (s_perf.sec_frames > s_perf.peak_sec_frames) {
            s_perf.peak_sec_frames = s_perf.sec_frames;
        }
    }
    s_perf.sec_frames = 0;
}

static void perf_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    const int64_t now = esp_timer_get_time();

    if (code == LV_EVENT_RENDER_START) {
        s_perf.frame_start_us = now;
        s_perf.in_frame = true;
    } else if (code == LV_EVENT_REFR_READY && s_perf.in_frame) {
        s_perf.in_frame = false;
        const int64_t dt = now - s_perf.frame_start_us;
        s_perf.frames++;
        s_perf.total_us += dt;
        if (dt > s_perf.worst_us) {
            s_perf.worst_us = dt;
        }
        const int64_t sec = now / 1000000;
        if (sec != s_perf.sec_index) {
            perf_close_second();
            s_perf.sec_index = sec;
        }
        s_perf.sec_frames++;
    }
}

static void perf_report_cb(lv_timer_t *t)
{
    (void)t;
    const int64_t now = esp_timer_get_time();
    perf_close_second();
    s_perf.sec_index = now / 1000000;

    if (s_perf.frames > 0) {
        const float window_s = (float)(now - s_perf.window_start_us) / 1e6f;
        const float avg_ms = (float)s_perf.total_us / (float)s_perf.frames / 1000.0f;
        const float busy = (float)s_perf.total_us / (float)(now - s_perf.window_start_us) * 100.0f;
        const float active_fps = s_perf.active_secs ? (float)s_perf.frames / (float)s_perf.active_secs : 0.0f;
        ESP_LOGI(TAG, "perf %.0fs: frames=%" PRIu32 " fps(active)=%.1f peak=%" PRIu32
                 " frame avg=%.2fms worst=%.2fms render-busy=%.1f%% heap_min=%" PRIu32,
                 (double)window_s, s_perf.frames, (double)active_fps, s_perf.peak_sec_frames,
                 (double)avg_ms, (double)s_perf.worst_us / 1000.0, (double)busy,
                 esp_get_minimum_free_heap_size());
    }
    s_perf = (perf_t) {
        .window_start_us = now,
        .sec_index = now / 1000000,
    };
}

static void perf_attach(lv_display_t *disp)
{
    s_perf.window_start_us = esp_timer_get_time();
    lv_display_add_event_cb(disp, perf_event_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, perf_event_cb, LV_EVENT_REFR_READY, NULL);
    lv_timer_create(perf_report_cb, CONFIG_MAO_PERF_PERIOD_S * 1000, NULL);
    ESP_LOGI(TAG, "perf probe on (summary every %d s while rendering)", CONFIG_MAO_PERF_PERIOD_S);
}

#endif /* CONFIG_MAO_PERF_PROBE */

/* ------------------------------------------------------------------------ */
/* Power states (boards with power management)                              */
/* ------------------------------------------------------------------------ */

/* Drowsy / deep sleep: backlight off and the panel in sleep-in (it keeps its
 * frame memory). Waking restores the last brightness. Holding the LVGL lock
 * keeps a flush from racing the sleep commands. */
/* LVGL lock held. The panel lost its configuration (expander reset under
 * power): initialise it again and redraw everything. */
static void reinit_panel(void)
{
    if (mao_board_display_reinit() != ESP_OK) {
        return;
    }
    esp_lcd_panel_disp_on_off(s_lvgl.panel, true);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    mao_board_backlight_set(s_brightness);
    ESP_LOGW(TAG, "panel re-initialised after an expander reset");
}

static void power_hook(const mao_power_transition_t *t, void *ctx)
{
    (void)ctx;
    const bool sleep = t->to == MAO_POWER_DROWSY || t->to == MAO_POWER_DEEP_SLEEP;
    if (!s_lvgl.disp || sleep == s_panel_asleep || !lvgl_port_lock(POWER_HOOK_LOCK_MS)) {
        return;
    }
    if (sleep) {
        mao_board_backlight_set(0);
        mao_board_display_sleep(true);
    } else if (s_reinit_pending) {
        s_reinit_pending = false;
        s_panel_asleep = false;
        reinit_panel();             /* includes sleep-out */
    } else {
        mao_board_display_sleep(false);
        mao_board_backlight_set(s_brightness);
    }
    s_panel_asleep = sleep;
    lvgl_port_unlock();
}

/* LVGL timer: an expander reset (a wedged expander recovered, or the
 * self-test) pulled the panel's reset line and dipped its supply. */
static void recovery_cb(lv_timer_t *t)
{
    (void)t;
    const uint32_t n = mao_board_expander_resets();
    if (n == s_resets_seen) {
        return;
    }
    s_resets_seen = n;
    if (s_rail_off || !mao_board_rail_is_on(MAO_RAIL_DISPLAY)) {
        return;                     /* powering it up again initialises it anyway */
    }
    if (s_panel_asleep) {
        s_reinit_pending = true;    /* on waking */
        return;
    }
    reinit_panel();
}

/* ------------------------------------------------------------------------ */
/* Rotation                                                                 */
/* ------------------------------------------------------------------------ */

static bool rotation_valid(int deg)
{
    return deg == 0 || deg == 90 || deg == 180 || deg == 270;
}

/* LVGL lock held. LVGL swaps its logical axes and esp_lvgl_port turns the
 * rotation into the panel's MADCTL (hardware rotation, no extra buffer). */
static void apply_rotation(uint16_t deg)
{
    static const lv_display_rotation_t kRot[] = {
        LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270,
    };
    lv_display_set_rotation(s_lvgl.disp, kRot[(deg / 90) & 3]);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    s_rotation = deg;
}

esp_err_t mao_display_set_rotation(uint16_t degrees)
{
    ESP_RETURN_ON_FALSE(rotation_valid(degrees), ESP_ERR_INVALID_ARG, TAG, "rotation must be 0/90/180/270");
    ESP_RETURN_ON_FALSE(s_lvgl.disp, ESP_ERR_INVALID_STATE, TAG, "display not initialised");
    ESP_RETURN_ON_FALSE(lvgl_port_lock(API_LOCK_MS), ESP_ERR_TIMEOUT, TAG, "lvgl lock");
    apply_rotation(degrees);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "rotation %u deg", degrees);
    return ESP_OK;
}

uint16_t mao_display_get_rotation(void)
{
    return s_rotation;
}

/* ------------------------------------------------------------------------ */
/* Factory test helpers                                                     */
/* ------------------------------------------------------------------------ */

static void test_plate_create(void)
{
    s_test_plate = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_test_plate);
    lv_obj_set_size(s_test_plate, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_test_plate, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_test_plate, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_test_big = lv_label_create(s_test_plate);
    lv_obj_set_style_text_font(s_test_big, &lv_font_montserrat_48, 0);
    lv_obj_align(s_test_big, LV_ALIGN_CENTER, 0, -12);
    s_test_small = lv_label_create(s_test_plate);
    lv_obj_set_style_text_font(s_test_small, &lv_font_montserrat_20, 0);
    lv_obj_align(s_test_small, LV_ALIGN_CENTER, 0, 34);
}

esp_err_t mao_display_test_show(uint32_t bg_rgb, uint32_t fg_rgb, const char *big, const char *small)
{
    ESP_RETURN_ON_FALSE(s_lvgl.disp, ESP_ERR_INVALID_STATE, TAG, "display not initialised");
    ESP_RETURN_ON_FALSE(lvgl_port_lock(API_LOCK_MS), ESP_ERR_TIMEOUT, TAG, "lvgl lock");
    if (!s_test_plate) {
        test_plate_create();
    }
    lv_obj_set_style_bg_color(s_test_plate, lv_color_hex(bg_rgb), 0);
    lv_obj_set_style_text_color(s_test_big, lv_color_hex(fg_rgb), 0);
    lv_obj_set_style_text_color(s_test_small, lv_color_hex(fg_rgb), 0);
    lv_label_set_text(s_test_big, big ? big : "");
    lv_label_set_text(s_test_small, small ? small : "");
    lv_obj_align(s_test_big, LV_ALIGN_CENTER, 0, small && small[0] ? -12 : 0);
    lv_obj_remove_flag(s_test_plate, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
    return ESP_OK;
}

void mao_display_test_clear(void)
{
    if (s_lvgl.disp && s_test_plate && lvgl_port_lock(API_LOCK_MS)) {
        lv_obj_add_flag(s_test_plate, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }
}

esp_err_t mao_display_rail(bool on)
{
    ESP_RETURN_ON_FALSE(s_lvgl.disp, ESP_ERR_INVALID_STATE, TAG, "display not initialised");
    ESP_RETURN_ON_FALSE(lvgl_port_lock(API_LOCK_MS), ESP_ERR_TIMEOUT, TAG, "lvgl lock");
    esp_err_t err = ESP_OK;
    if (!on && !s_rail_off) {
        /* Rendering pauses first: with the rail off nothing may be flushed,
         * or DC and the bus would back-power the unpowered panel. The lock
         * stays free, so the UI keeps working (it just is not drawn). */
        lvgl_port_stop();
        err = mao_board_rail_set(MAO_RAIL_DISPLAY, false);
        if (err == ESP_OK) {
            s_rail_off = true;
        } else {
            lvgl_port_resume();
        }
    } else if (on && s_rail_off) {
        err = mao_board_rail_set(MAO_RAIL_DISPLAY, true);   /* re-initialises the panel */
        if (err == ESP_OK) {
            esp_lcd_panel_disp_on_off(s_lvgl.panel, true);
            lv_obj_invalidate(lv_screen_active());
            lv_obj_invalidate(lv_layer_top());
            if (!s_panel_asleep) {
                mao_board_backlight_set(s_brightness);
            }
        }
        s_rail_off = false;
        lvgl_port_resume();
    }
    lvgl_port_unlock();
    return err;
}

/* ------------------------------------------------------------------------ */
/* Development console                                                      */
/* ------------------------------------------------------------------------ */

#if CONFIG_MAO_DEV_CONSOLE

static void devcmd_rotate(const char *args)
{
    const int16_t saved = mao_settings_get()->display_rotation;
    if (args[0] == '\0') {
        ESP_LOGI(TAG, "rotation %u deg (board default %u, saved override %s%d)", s_rotation,
                 s_lvgl.board_rotation, saved < 0 ? "none" : "", saved < 0 ? 0 : saved);
        return;
    }
    if (strcmp(args, "save") == 0) {
        const esp_err_t err = mao_settings_set_display_rotation((int16_t)s_rotation);
        ESP_LOGI(TAG, "dev: rotation %u deg saved: %s", s_rotation, esp_err_to_name(err));
        return;
    }
    if (strcmp(args, "default") == 0) {
        mao_settings_set_display_rotation(MAO_SETTINGS_ROTATION_DEFAULT);
        mao_display_set_rotation(s_lvgl.board_rotation);
        ESP_LOGI(TAG, "dev: rotation back to the board default %u deg (override cleared)", s_lvgl.board_rotation);
        return;
    }
    const int deg = atoi(args);
    if (!rotation_valid(deg) || (deg == 0 && args[0] != '0')) {
        ESP_LOGW(TAG, "dev: rotate [0|90|180|270|save|default]");
        return;
    }
    mao_display_set_rotation((uint16_t)deg);
    ESP_LOGI(TAG, "dev: rotation %d deg (live; 'mao rotate save' keeps it)", deg);
}

static void register_devcmds(void)
{
    mao_devcmd_register("rotate", "rotate [0|90|180|270|save|default]  (display rotation, bring-up)", devcmd_rotate);
}

#else

static void register_devcmds(void)
{
}

#endif

/* ------------------------------------------------------------------------ */

esp_err_t mao_display_init(void)
{
    mao_system_log_heap(TAG, "before display init");

    ESP_RETURN_ON_ERROR(mao_lvgl_start(&s_lvgl), TAG, "lvgl");

    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_ERR_TIMEOUT, TAG, "lvgl lock");
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    /* Mounting rotation: the board's, unless a bring-up override was saved. */
    const int16_t override = mao_settings_get()->display_rotation;
    const uint16_t rotation = rotation_valid(override) ? (uint16_t)override : s_lvgl.board_rotation;
    apply_rotation(rotation);
    s_resets_seen = mao_board_expander_resets();
    lv_timer_create(recovery_cb, RECOVERY_CHECK_MS, NULL);
#if CONFIG_MAO_PERF_PROBE
    perf_attach(s_lvgl.disp);
#endif
    lvgl_port_unlock();
    ESP_LOGI(TAG, "display rotation %u deg (%s)", rotation,
             rotation_valid(override) ? "saved override" : "board default");

    mao_power_register_hook("display", power_hook, NULL);
    register_devcmds();
    mao_system_log_heap(TAG, "after display init");
    return ESP_OK;
}

esp_err_t mao_display_start(uint8_t brightness_percent)
{
    ESP_RETURN_ON_FALSE(s_lvgl.disp, ESP_ERR_INVALID_STATE, TAG, "display not initialised");
    /* Draw the first real frame while the panel is still dark, so nothing
     * uninitialised is ever visible, then light it up. */
    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_ERR_TIMEOUT, TAG, "lvgl lock");
    lv_refr_now(s_lvgl.disp);
    lvgl_port_unlock();

    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_lvgl.panel, true), TAG, "display on");
    return mao_display_set_brightness(brightness_percent);
}

/* A display that failed to initialise has no LVGL port: refusing the lock
 * lets the UI skip its work instead of asserting, so boot continues. */
bool mao_display_ok(void)
{
    return s_lvgl.disp != NULL;
}

bool mao_display_lock(uint32_t timeout_ms)
{
    return s_lvgl.disp && lvgl_port_lock(timeout_ms);
}

void mao_display_unlock(void)
{
    if (s_lvgl.disp) {
        lvgl_port_unlock();
    }
}

esp_err_t mao_display_set_brightness(uint8_t percent)
{
    s_brightness = percent;
    if (s_panel_asleep) {
        return ESP_OK;     /* applied when the panel wakes */
    }
    return mao_board_backlight_set(percent);
}
