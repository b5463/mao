/*
 * LAMP 01 development console. Reads lines from the USB-Serial/JTAG FIFO
 * (boards with native USB) and from UART0 (boards with a USB-UART bridge).
 *
 *   lamp status | lamp offline | lamp online | lamp power <0|1> |
 *   lamp level <0-100> | lamp junk
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
    if (!cmd) {
        return;
    }
    cmd += 5;
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
