/*
 * LAMP 01 development console. Reads lines from the USB-Serial/JTAG FIFO
 * (boards with native USB) and from UART0 (boards with a USB-UART bridge).
 *
 *   lamp status | lamp offline | lamp online | lamp power <0|1> |
 *   lamp level <0-100> | lamp junk
 *
 * Test controls (laboratory only):
 *   lamp drop_ack <n> | lamp delay_ack <ms> | lamp flood <hz> <s> [state|announce] | lamp reboot
 */
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "hal/usb_serial_jtag_ll.h"
#include "lamp.h"

static const char *TAG = "LAMP01";

typedef struct {
    char buf[48];
    size_t len;
} line_t;

static void run(char *line)
{
    char *cmd = strstr(line, "lamp ");
    if (cmd) {
        cmd += 5;
    } else {
        cmd = strstr(line, "cam ");
        if (!cmd) {
            return;
        }
        cmd += 4;
    }
    char *arg = strchr(cmd, ' ');
    if (arg) {
        *arg++ = '\0';
    }
    const int32_t v = arg ? atoi(arg) : 0;
    if (!strcmp(cmd, "status")) {
        lamp_post_command(LAMP_CMD_STATUS, 0);
    } else if (!strcmp(cmd, "offline")) {
        lamp_post_command(LAMP_CMD_OFFLINE, 0);
    } else if (!strcmp(cmd, "online")) {
        lamp_post_command(LAMP_CMD_ONLINE, 0);
    } else if (!strcmp(cmd, "power")) {
        lamp_post_command(LAMP_CMD_SET_POWER, v);
    } else if (!strcmp(cmd, "level")) {
        lamp_post_command(LAMP_CMD_SET_LEVEL, v);
    } else if (!strcmp(cmd, "junk")) {
        lamp_post_command(LAMP_CMD_JUNK, 0);
    } else if (!strcmp(cmd, "drop_ack")) {
        lamp_post_command(LAMP_CMD_DROP_ACK, v);
    } else if (!strcmp(cmd, "delay_ack")) {
        lamp_post_command(LAMP_CMD_DELAY_ACK, v);
    } else if (!strcmp(cmd, "flood") && arg) {
        /* flood <hz> <seconds> [state|announce] */
        const char *a2 = strchr(arg, ' ');
        int secs = a2 ? atoi(a2 + 1) : 10;
        const char *a3 = a2 ? strchr(a2 + 1, ' ') : NULL;
        const int mode = (a3 && strstr(a3, "announce")) ? LAMP_FLOOD_ANNOUNCE : LAMP_FLOOD_STATE;
        const int hz = v < 1 ? 1 : (v > 100 ? 100 : v);
        secs = secs < 1 ? 1 : (secs > 255 ? 255 : secs);
        lamp_post_command(LAMP_CMD_FLOOD, hz | (secs << 8) | (mode << 24));
    } else if (!strcmp(cmd, "reboot")) {
        lamp_post_command(LAMP_CMD_REBOOT, 0);
    } else if (!strcmp(cmd, "ready") && arg) {
        lamp_post_command(LAMP_CMD_SET_READY, v);
    } else if (!strcmp(cmd, "storage") && arg) {
        lamp_post_command(LAMP_CMD_SET_STORAGE, v);
    } else if (!strcmp(cmd, "capture_delay") && arg) {
        lamp_post_command(LAMP_CMD_CAPTURE_DELAY, v);
    } else if (!strcmp(cmd, "identify")) {
        lamp_post_command(LAMP_CMD_IDENTIFY, 0);
    } else if (!strcmp(cmd, "action") && arg) {
        /* action status | delay <ms> | busy on|off | fail on|off |
         * drop_result <n> | duplicate_result */
        char *a2 = strchr(arg, ' ');
        if (a2) {
            *a2++ = '\0';
        }
        const int av = a2 ? (strstr(a2, "on") ? 1 : atoi(a2)) : 0;
        if (!strcmp(arg, "status")) {
            lamp_post_command(LAMP_CMD_ACT_STATUS, 0);
        } else if (!strcmp(arg, "delay")) {
            lamp_post_command(LAMP_CMD_ACT_DELAY, av);
        } else if (!strcmp(arg, "busy")) {
            lamp_post_command(LAMP_CMD_ACT_BUSY, av);
        } else if (!strcmp(arg, "fail")) {
            lamp_post_command(LAMP_CMD_ACT_FAIL, av);
        } else if (!strcmp(arg, "drop_result")) {
            lamp_post_command(LAMP_CMD_ACT_DROP_RESULT, av);
        } else if (!strcmp(arg, "duplicate_result")) {
            lamp_post_command(LAMP_CMD_ACT_DUP_RESULT, 0);
        } else {
            ESP_LOGW(TAG, "action status|delay <ms>|busy on/off|fail on/off|drop_result <n>|duplicate_result");
        }
    } else if (!strcmp(cmd, "session")) {
        lamp_post_command(LAMP_CMD_SESSION, 0);
    } else if (!strcmp(cmd, "drop_session")) {
        lamp_post_command(LAMP_CMD_DROP_SESSION, 0);
    } else if (!strcmp(cmd, "inject_prev") && arg) {
        /* inject_prev <cap> <value> */
        const char *a2 = strchr(arg, ' ');
        lamp_post_command(LAMP_CMD_INJECT_PREV, v * 1000 + (a2 ? atoi(a2 + 1) : 0));
    } else {
        ESP_LOGW(TAG, "unknown command '%s'", cmd);
    }
}

static void feed(line_t *l, const uint8_t *data, int n)
{
    for (int i = 0; i < n; i++) {
        const char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            l->buf[l->len] = '\0';
            if (l->len) {
                run(l->buf);
            }
            l->len = 0;
        } else if (c >= 0x20 && c < 0x7F && l->len < sizeof(l->buf) - 1) {
            l->buf[l->len++] = c;   /* printable only: see MAO's mao_devcmd.c */
        }
    }
}

static void console_task(void *arg)
{
    (void)arg;
    const bool uart_ok = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) == ESP_OK;
    line_t usj = { 0 }, uart = { 0 };
    for (;;) {
        uint8_t buf[32];
        int n = usb_serial_jtag_ll_read_rxfifo(buf, sizeof(buf));
        if (n > 0) {
            feed(&usj, buf, n);
        }
        if (uart_ok) {
            n = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), 0);
            if (n > 0) {
                feed(&uart, buf, n);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void lamp_console_start(void)
{
    xTaskCreate(console_task, "lamp_console", 3072, NULL, 2, NULL);
}
