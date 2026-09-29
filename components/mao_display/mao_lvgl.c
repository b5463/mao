#include "mao_lvgl.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "mao_board.h"

static const char *TAG = "MAO_DISPLAY";

/* Partial rendering: two 240 x 40-line RGB565 buffers (19200 B each) in
 * DMA-capable internal RAM. LVGL renders into one while the other is being
 * sent over SPI. A full frame would be 115200 B, so this is a third of that.
 * The LCDkit's panel has no TE line, so a write can never be synced to the
 * panel's scan: the fewer strips a moving area takes, the smaller the chance
 * the scan catches it half-written (a torn band when the eyes swing fast) -
 * 40 lines puts the eyes in about half as many strips as 20 did. */
#define MAO_LVGL_BUF_LINES      40
#define MAO_LVGL_DOUBLE_BUFFER  true

#define MAO_LVGL_TASK_STACK     6144
#define MAO_LVGL_TASK_PRIO      3
#define MAO_LVGL_TICK_MS        5
#define MAO_LVGL_MAX_SLEEP_MS   500

esp_err_t mao_lvgl_start(mao_lvgl_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    uint16_t h_res = 0;
    mao_board_display_get_resolution(&h_res, NULL);
    /* SPI max transfer = one draw buffer (lines x width x 2 bytes). */
    const size_t buf_px = (size_t)h_res * MAO_LVGL_BUF_LINES;

    mao_board_display_t bd;
    ESP_RETURN_ON_ERROR(mao_board_display_init(buf_px * sizeof(uint16_t), &bd), TAG, "board display");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = MAO_LVGL_TASK_PRIO;
    port_cfg.task_stack = MAO_LVGL_TASK_STACK;
    port_cfg.timer_period_ms = MAO_LVGL_TICK_MS;
    port_cfg.task_max_sleep_ms = MAO_LVGL_MAX_SLEEP_MS;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = bd.io,
        .panel_handle = bd.panel,
        .buffer_size = buf_px,
        .double_buffer = MAO_LVGL_DOUBLE_BUFFER,
        .hres = bd.h_res,
        .vres = bd.v_res,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = bd.swap_xy,
            .mirror_x = bd.mirror_x,
            .mirror_y = bd.mirror_y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = bd.swap_bytes,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "lvgl_port_add_disp failed");

    ESP_LOGI(TAG, "LVGL %d.%d.%d: %s partial buffers, %u px x %d = %u B each (%u B total, DMA internal)",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             MAO_LVGL_DOUBLE_BUFFER ? "2" : "1",
             (unsigned)bd.h_res, MAO_LVGL_BUF_LINES,
             (unsigned)(buf_px * sizeof(uint16_t)),
             (unsigned)(buf_px * sizeof(uint16_t) * (MAO_LVGL_DOUBLE_BUFFER ? 2 : 1)));

    out->disp = disp;
    out->panel = bd.panel;
    out->io = bd.io;
    return ESP_OK;
}
