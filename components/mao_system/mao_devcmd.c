/*
 * Development console: line commands over the USB-Serial/JTAG console.
 *
 * Polls the USB-Serial/JTAG RX FIFO directly. The console VFS cannot be used
 * for input here: without the USJ driver installed, IDF 6.0 always reports
 * zero readable bytes, so the FIFO would never drain and the host's writes
 * would stall. Installing the driver would move console TX onto it as well,
 * which can block when no host is attached; polling the FIFO avoids both.
 * Nothing else in MAO reads this FIFO.
 *
 * Compiled only with CONFIG_MAO_DEV_CONSOLE. Commands that concern
 * application behaviour are forwarded as MAO_EVENT_DEV_COMMAND so they run in
 * the dispatcher like any other event.
 *
 * Send e.g. "mao reset-first-boot" from idf.py monitor or tools/mao_cmd.py.
 */
#include "sdkconfig.h"

#if CONFIG_MAO_DEV_CONSOLE && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hal/usb_serial_jtag_ll.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mao_events.h"
#include "mao_settings.h"
#include "mao_system.h"

static const char *TAG = "MAO_SYSTEM";

#define DEVCMD_TASK_STACK  3072
#define DEVCMD_TASK_PRIO   1
#define DEVCMD_POLL_MS     50
#define DEVCMD_LINE_MAX    48

static void run_command(char *line)
{
    /* Accept "mao <cmd>" and "<cmd>"; ignore anything before "mao " (stray
     * bytes that happened to be printable). */
    char *cmd = strstr(line, "mao ");
    cmd = cmd ? cmd : line;
    if (strncmp(cmd, "mao ", 4) == 0) {
        cmd += 4;
    }
    char *arg = strchr(cmd, ' ');
    if (arg) {
        *arg++ = '\0';
    }

    if (strcmp(cmd, "help") == 0) {
        ESP_LOGI(TAG, "dev commands: help | status | snap | anim <name> | view <home|menu|page> | "
                 "dial <dps> [s] | look <n> | state [n] | stress <s> | key <cw|ccw|press|release|click|double|long> [n] | "
                 "reset-first-boot | reboot");
    } else if (strcmp(cmd, "key") == 0 && arg) {
        /* Inject a synthetic input event: indistinguishable from the knob for
         * everything above the input driver (scripted UI tests). */
        char *count_s = strchr(arg, ' ');
        int count = 1;
        if (count_s) {
            *count_s++ = '\0';
            count = atoi(count_s);
        }
        static const struct { const char *name; mao_event_type_t type; } kKeys[] = {
            { "cw", MAO_EVENT_INPUT_CW }, { "ccw", MAO_EVENT_INPUT_CCW },
            { "press", MAO_EVENT_INPUT_PRESS }, { "release", MAO_EVENT_INPUT_RELEASE },
            { "click", MAO_EVENT_INPUT_CLICK }, { "double", MAO_EVENT_INPUT_DOUBLE_CLICK },
            { "long", MAO_EVENT_INPUT_LONG_PRESS },
        };
        bool found = false;
        for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
            if (strcmp(arg, kKeys[i].name) == 0) {
                const bool dial = kKeys[i].type == MAO_EVENT_INPUT_CW || kKeys[i].type == MAO_EVENT_INPUT_CCW;
                mao_event_post(kKeys[i].type, dial ? (count > 0 ? count : 1) : 0);
                found = true;
            }
        }
        if (!found) {
            ESP_LOGW(TAG, "dev: unknown key '%s'", arg);
        }
    } else if (strcmp(cmd, "reset-first-boot") == 0) {
        esp_err_t err = mao_settings_set_first_boot_done(false);
        ESP_LOGW(TAG, "dev: first-boot flag cleared (%s); reboot to see the first encounter",
                 esp_err_to_name(err));
    } else if (strcmp(cmd, "reboot") == 0) {
        ESP_LOGW(TAG, "dev: rebooting");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else if (strcmp(cmd, "status") == 0) {
        mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_STATUS);
    } else if (strcmp(cmd, "replay-boot") == 0) {
        mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_REPLAY_BOOT);
    } else if (strcmp(cmd, "snap") == 0) {
        mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_SNAP);
    } else if (strcmp(cmd, "anim") == 0 && arg) {
        /* Order matches mao_character_preview_t. */
        static const char *const kAnims[] = {
            "idle", "blink", "follow", "fast", "vfast", "dizzy", "press", "happy", "sleepy", "leave", "hide",
        };
        int found = -1;
        for (int i = 0; i < (int)(sizeof(kAnims) / sizeof(kAnims[0])); i++) {
            if (strcmp(arg, kAnims[i]) == 0) {
                found = i;
            }
        }
        if (found < 0) {
            ESP_LOGW(TAG, "dev: anim idle|blink|follow|fast|vfast|dizzy|press|happy|sleepy|leave");
        } else {
            mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_ANIM_BASE + found);
        }
    } else if (strcmp(cmd, "view") == 0 && arg) {
        /* Order matches mao_view_t (INTRO is not reachable this way). */
        const int v = !strcmp(arg, "home") ? 1 : !strcmp(arg, "menu") ? 2 : !strcmp(arg, "page") ? 3 : -1;
        if (v < 0) {
            ESP_LOGW(TAG, "dev: view home|menu|page");
        } else {
            mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_VIEW_BASE + v);
        }
    } else if (strcmp(cmd, "state") == 0) {
        mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_EXPR_BASE + (arg ? atoi(arg) : 999));
    } else if (strcmp(cmd, "look") == 0 && arg) {
        mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_LOOK_BASE + atoi(arg));
    } else if (strcmp(cmd, "dial") == 0 && arg) {
        char *sec_s = strchr(arg, ' ');
        int seconds = 3;
        if (sec_s) {
            *sec_s++ = '\0';
            seconds = atoi(sec_s);
        }
        int dps = atoi(arg);
        dps = dps < -400 ? -400 : (dps > 400 ? 400 : dps);
        seconds = seconds < 1 ? 1 : (seconds > 120 ? 120 : seconds);
        mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_DIAL_BASE + (dps + 500) * 1000 + seconds);
    } else if (strcmp(cmd, "stress") == 0) {
        int seconds = arg ? atoi(arg) : 10;
        if (seconds < 1) {
            seconds = 1;
        } else if (seconds > 600) {
            seconds = 600;
        }
        mao_event_post(MAO_EVENT_DEV_COMMAND, MAO_DEVCMD_STRESS_BASE + seconds);
    } else if (cmd[0] != '\0') {
        ESP_LOGW(TAG, "dev: unknown command '%s' (try 'mao help')", cmd);
    }
}

static void devcmd_task(void *arg)
{
    (void)arg;
    char line[DEVCMD_LINE_MAX];
    size_t len = 0;
    for (;;) {
        uint8_t buf[16];
        const int n = usb_serial_jtag_ll_read_rxfifo(buf, sizeof(buf));
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(DEVCMD_POLL_MS));
            continue;
        }
        for (int i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                if (len > 0) {
                    run_command(line);
                }
                len = 0;
            } else if (c >= 0x20 && c < 0x7F && len < sizeof(line) - 1) {
                /* Printable ASCII only. The C3's USB-Serial/JTAG FIFO can
                 * receive stray USB control bytes (e.g. a host's periodic
                 * GET_DESCRIPTOR setup packet, 80 06 .. 00); a NUL among them
                 * would otherwise truncate the real command on this line. */
                line[len++] = c;
            }
        }
    }
}

esp_err_t mao_devcmd_start(void)
{
    if (xTaskCreate(devcmd_task, "mao_devcmd", DEVCMD_TASK_STACK, NULL, DEVCMD_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "dev console enabled ('mao help')");
    return ESP_OK;
}

#else

#include "mao_system.h"

esp_err_t mao_devcmd_start(void)
{
    return ESP_OK;
}

#endif
