#include "mao_display.h"

#include <inttypes.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mao_board.h"
#include "mao_lvgl.h"
#include "mao_system.h"

static const char *TAG = "MAO_DISPLAY";

static mao_lvgl_t s_lvgl;

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
    lvgl_port_unlock();

    mao_system_log_heap(TAG, "after display init");
    return ESP_OK;
}

esp_err_t mao_display_start(uint8_t brightness_percent)
{
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

bool mao_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void mao_display_unlock(void)
{
    lvgl_port_unlock();
}

esp_err_t mao_display_set_brightness(uint8_t percent)
{
    return mao_board_backlight_set(percent);
}

esp_err_t mao_display_fade_brightness(uint8_t percent, uint32_t fade_ms)
{
    return mao_board_backlight_fade(percent, fade_ms);
}
