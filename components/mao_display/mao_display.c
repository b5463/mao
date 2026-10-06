#include "mao_display.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mao_board.h"
#include "mao_lvgl.h"
#include "mao_settings.h"
#include "mao_system.h"

static const char *TAG = "MAO_DISPLAY";

#define API_LOCK_MS         1000

static mao_lvgl_t s_lvgl;
static uint8_t s_brightness;        /* the last level asked for (restored after a rail cycle) */
static uint16_t s_rotation;
static bool s_rail_off;             /* switched off on request (self-test): rendering paused */

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

/* Frame-interval burst: answers "is this motion smooth or stepped?" better
 * than fps(active), whose 1 s buckets punish poses that hold still. Records
 * the gap between consecutive rendered frames for a while, then logs the
 * distribution (only gaps < 500 ms count: a settled pose is not a stutter). */
#define BURST_MAX 256
static int64_t s_burst_until_us;
static int64_t s_burst_prev_us;
static uint16_t s_burst_gap_ms[BURST_MAX];
static uint16_t s_burst_n;

static int cmp_u16(const void *a, const void *b)
{
    return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}

static void burst_frame(int64_t now)
{
    if (now >= s_burst_until_us) {
        if (s_burst_until_us && s_burst_n) {
            qsort(s_burst_gap_ms, s_burst_n, sizeof(s_burst_gap_ms[0]), cmp_u16);
            ESP_LOGI(TAG, "cadence: %u moving frames, gap min=%u median=%u p90=%u max=%u ms",
                     (unsigned)s_burst_n, (unsigned)s_burst_gap_ms[0], (unsigned)s_burst_gap_ms[s_burst_n / 2],
                     (unsigned)s_burst_gap_ms[(uint32_t)s_burst_n * 9 / 10],
                     (unsigned)s_burst_gap_ms[s_burst_n - 1]);
            s_burst_until_us = 0;
        }
        s_burst_prev_us = now;
        return;
    }
    const int64_t gap = now - s_burst_prev_us;
    s_burst_prev_us = now;
    if (gap < 500000 && s_burst_n < BURST_MAX) {
        s_burst_gap_ms[s_burst_n++] = (uint16_t)(gap / 1000);
    }
}

esp_err_t mao_display_perf_burst(uint32_t duration_ms)
{
    s_burst_n = 0;
    s_burst_prev_us = esp_timer_get_time();
    s_burst_until_us = s_burst_prev_us + (int64_t)duration_ms * 1000;
    ESP_LOGI(TAG, "cadence burst: recording frame intervals for %" PRIu32 " ms", duration_ms);
    return ESP_OK;
}

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
        burst_frame(now);
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

#else

esp_err_t mao_display_perf_burst(uint32_t duration_ms)
{
    (void)duration_ms;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_MAO_PERF_PROBE */

/* ------------------------------------------------------------------------ */
/* Development snapshot. Needs no extra RAM: a capture request invalidates   */
/* the whole screen, and every band LVGL flushes during the next refresh is  */
/* streamed from the existing partial draw buffer (before the byte swap) as  */
/* run-length-encoded RGB565 in base64. The dump runs in the LVGL task.      */
/* Wire: MAO_SNAP_BEGIN w h / A x1 y1 x2 y2 / S<base64 RLE>... / MAO_SNAP_END */
/* ------------------------------------------------------------------------ */
#if CONFIG_MAO_DEV_CONSOLE

static volatile bool s_snap_requested;
static bool s_snap_active;

static void b64_line(const uint8_t *src, size_t n, char *out)
{
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = ((uint32_t)src[i] << 16) | ((i + 1 < n ? src[i + 1] : 0) << 8) | (i + 2 < n ? src[i + 2] : 0);
        out[o++] = kB64[(v >> 18) & 63];
        out[o++] = kB64[(v >> 12) & 63];
        out[o++] = i + 1 < n ? kB64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? kB64[v & 63] : '=';
    }
    out[o] = '\0';
}

/* Stream one flushed area: runs of (u16 count, u16 pixel). */
static void snap_area(const lv_area_t *a, const lv_draw_buf_t *buf)
{
    const int32_t w = lv_area_get_width(a), h = lv_area_get_height(a);
    const uint32_t stride = buf->header.stride;
    printf("A %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 "\n", a->x1, a->y1, a->x2, a->y2);
    uint8_t pack[96];
    size_t fill = 0;
    char line[4 * 32 + 1];
    uint16_t run_px = 0;
    uint32_t run_len = 0;
    const uint32_t total = (uint32_t)(w * h);
    for (uint32_t i = 0; i <= total; i++) {
        const bool done = i == total;
        const uint16_t px = done ? 0 : *(const uint16_t *)(buf->data + (i / w) * stride + (i % w) * 2);
        if (!done && run_len && px == run_px && run_len < 0xFFFF) {
            run_len++;
            continue;
        }
        if (run_len) {
            pack[fill++] = (uint8_t)run_len;
            pack[fill++] = (uint8_t)(run_len >> 8);
            pack[fill++] = (uint8_t)run_px;
            pack[fill++] = (uint8_t)(run_px >> 8);
        }
        if (fill && (fill == sizeof(pack) || done)) {
            b64_line(pack, fill, line);
            printf("S%s\n", line);
            fill = 0;
        }
        run_px = px;
        run_len = 1;
    }
}

static void snap_event_cb(lv_event_t *e)
{
    lv_display_t *disp = lv_event_get_target(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_START && s_snap_requested) {
        s_snap_requested = false;
        s_snap_active = true;
        lv_obj_invalidate(lv_screen_active());
        printf("\nMAO_SNAP_BEGIN %" PRId32 " %" PRId32 " rle565-areas\n",
               lv_display_get_horizontal_resolution(disp), lv_display_get_vertical_resolution(disp));
    } else if (code == LV_EVENT_FLUSH_START && s_snap_active) {
        snap_area((const lv_area_t *)lv_event_get_param(e), lv_display_get_buf_active(disp));
    } else if (code == LV_EVENT_REFR_READY && s_snap_active) {
        s_snap_active = false;
        printf("MAO_SNAP_END\n");
        fflush(stdout);
    }
}

esp_err_t mao_display_snapshot_dump(void)
{
    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_ERR_TIMEOUT, TAG, "lvgl lock");
    s_snap_requested = true;
    lv_obj_invalidate(lv_screen_active());   /* guarantee a refresh happens */
    lvgl_port_unlock();
    return ESP_OK;
}

static void snap_attach(lv_display_t *disp)
{
    lv_display_add_event_cb(disp, snap_event_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, snap_event_cb, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(disp, snap_event_cb, LV_EVENT_REFR_READY, NULL);
}

#else

esp_err_t mao_display_snapshot_dump(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif

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
            mao_board_backlight_set(s_brightness);
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

/* rotate [0|90|180|270|save|default]  (display rotation, bring-up) */
static void devcmd_rotate(char *arg)
{
    const char *args = arg ? arg : "";
    const int16_t saved = mao_settings_get()->display_rotation;
    if (args[0] == '\0') {
        ESP_LOGI(TAG, "rotation %u deg (board default %u, saved override %s%d)", s_rotation,
                 s_lvgl.board_rotation, saved < 0 ? "none " : "", saved < 0 ? 0 : saved);
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
    mao_devcmd_register("rotate", devcmd_rotate);
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
#if CONFIG_MAO_PERF_PROBE
    perf_attach(s_lvgl.disp);
#endif
#if CONFIG_MAO_DEV_CONSOLE
    snap_attach(s_lvgl.disp);
#endif
    /* Mounting rotation: the board's, unless a bring-up override was saved.
     * Rotation 0 (the LCDkit's mounting) is the panel's own state: nothing
     * to apply. */
    const int16_t override = mao_settings_get()->display_rotation;
    const uint16_t rotation = rotation_valid(override) ? (uint16_t)override : s_lvgl.board_rotation;
    if (rotation != 0) {
        apply_rotation(rotation);
        ESP_LOGI(TAG, "display rotation %u deg (%s)", rotation,
                 rotation_valid(override) ? "saved override" : "board default");
    }
    lvgl_port_unlock();
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
    /* Switch the panel on while still holding the LVGL lock: the esp_lcd SPI
     * panel IO must not be used by two tasks at once. If the LVGL task was
     * free to flush an animating view (e.g. the pulsing first-encounter
     * prompt) while this task sent the display-on command, both would wait on
     * each other's SPI transactions and deadlock. */
    const esp_err_t on = esp_lcd_panel_disp_on_off(s_lvgl.panel, true);
    lvgl_port_unlock();
    ESP_RETURN_ON_ERROR(on, TAG, "display on");
    return mao_display_set_brightness(brightness_percent);
}

/* A display that failed to initialise has no LVGL port: refusing the lock
 * lets the UI skip its work instead of asserting, so the boot continues. */
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
    return mao_board_backlight_set(percent);
}

esp_err_t mao_display_fade_brightness(uint8_t percent, uint32_t fade_ms)
{
    s_brightness = percent;
    return mao_board_backlight_fade(percent, fade_ms);
}

/* The panel's own sleep (M4.1 NIGHT / DEV deep sleep): the GC9A01 keeps its
 * picture in GRAM and stops scanning (SLPIN needs 5 ms before the next command, SLPOUT 120 ms). Call with the display lock held (the
 * SPI panel IO must not be used by two tasks at once). Waking takes ~120 ms. */
esp_err_t mao_display_panel_sleep(bool sleep)
{
    /* the GC9A01 driver has no disp_sleep: SLPIN (0x10) / SLPOUT (0x11) */
    return esp_lcd_panel_io_tx_param(s_lvgl.io, sleep ? 0x10 : 0x11, NULL, 0);
}
