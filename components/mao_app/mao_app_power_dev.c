/*
 * DEV console tools for MAO's rest (mao_app_power.c): "mao power ..." and
 * "mao deepsleep <s>". Only in a DEV_CONSOLE build; nothing here is on the
 * product's path.
 *
 *   power                    the ladder's state
 *   power rest [force] [s]   the sleeping screen now (then light sleep if no
 *                            USB host, or anyway with force; s = timer wake)
 *   power night ...          the same, the night a minute into the rest
 *   power probe              a bare 2 s light sleep (the platform alone)
 *   power clicks | click <n> which part of going to sleep the speaker hears
 *   power duty               the PDM line's measured density, idle and parked
 *   power scale <hp> <sd>    the PDM gain stages, live
 *   power idle <per mille>   the floor density the line glides from
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "mao_app_priv.h"
#include "mao_audio.h"
#include "mao_board.h"
#include "mao_events.h"
#include "mao_radio.h"
#include "mao_system.h"

#if CONFIG_MAO_DEV_CONSOLE
static const char *TAG = "MAO_POWER";
#endif

#if CONFIG_MAO_DEV_CONSOLE
static void dev_deepsleep(char *arg)
{
    const long s = arg ? strtol(arg, NULL, 10) : 0;
    mao_event_post(MAO_EVENT_POWER_DEEP, (int32_t)s);
}

/* "power rest [force] [s]": the sleeping screen now (then light sleep, if
 * no USB host - or anyway with "force": the console is then lost until a
 * reset); "power night ...": the same, the night a minute in; s = a timer
 * wake in light sleep. "power probe": a bare 2 s light sleep. */
static void dev_power(char *arg)
{
    if (arg && strncmp(arg, "idle ", 5) == 0) {
        mao_board_audio_line_idle(atoi(arg + 5));
        ESP_LOGI(TAG, "the PDM floor's density is taken as %d per mille", atoi(arg + 5));
        return;
    }
    if (arg && strncmp(arg, "scale ", 6) == 0) {
        int hp = 0, sd = 1;
        sscanf(arg + 6, "%d %d", &hp, &sd);
        const esp_err_t e = mao_audio_debug_scale(hp, sd);
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP_LOGI(TAG, "scale hp %d sd %d (%s): the idle line is high %d per mille", hp, sd, esp_err_to_name(e),
                 mao_board_audio_duty_permille());
        return;
    }
    if (arg && strncmp(arg, "duty", 4) == 0) {
        ESP_LOGI(TAG, "duty: the idle PDM line is high %d per mille", mao_board_audio_duty_permille());
        mao_audio_suspend();
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "duty: parked (channel stopped): %d per mille", mao_board_audio_duty_permille());
        mao_audio_resume();
        return;
    }
    if (arg && strncmp(arg, "click ", 6) == 0) {
        /* one stage of "clicks" alone: 1 audio, 2 radio, 3 sleep (audio on), 4 sleep (parked) */
        const int k = atoi(arg + 6);
        ESP_LOGI(TAG, "click stage %d", k);
        vTaskDelay(pdMS_TO_TICKS(50));
        if (k == 1) {
            mao_audio_suspend();
            vTaskDelay(pdMS_TO_TICKS(1500));
            mao_audio_resume();
        } else if (k == 2) {
            mao_radio_sleep(true);
            vTaskDelay(pdMS_TO_TICKS(1500));
            mao_radio_sleep(false);
        } else if (k == 3 || k == 4) {
            if (k == 4) {
                mao_audio_suspend();
                mao_board_audio_hold(true);
            }
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            esp_sleep_enable_timer_wakeup(1500000);
            esp_light_sleep_start();
            if (k == 4) {
                mao_board_audio_hold(false);
                mao_audio_resume();
            }
        }
        ESP_LOGI(TAG, "click stage %d done", k);
        return;
    }
    if (arg && strncmp(arg, "clicks", 6) == 0) {
        /* which part of going to sleep does the speaker hear? four stages, 4 s apart */
        const uint64_t s15 = 1500000;
        ESP_LOGI(TAG, "clicks 1/4: audio parked, then resumed");
        mao_audio_suspend();
        vTaskDelay(pdMS_TO_TICKS(1500));
        mao_audio_resume();
        vTaskDelay(pdMS_TO_TICKS(2500));
        ESP_LOGI(TAG, "clicks 2/4: radio stopped, then restarted");
        mao_radio_sleep(true);
        vTaskDelay(pdMS_TO_TICKS(1500));
        mao_radio_sleep(false);
        vTaskDelay(pdMS_TO_TICKS(2500));
        ESP_LOGI(TAG, "clicks 3/4: light sleep 1.5 s, audio running (the console will drop)");
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_sleep_enable_timer_wakeup(s15);
        esp_light_sleep_start();
        vTaskDelay(pdMS_TO_TICKS(2500));
        mao_audio_suspend();
        mao_board_audio_hold(true);
        esp_sleep_enable_timer_wakeup(s15);
        esp_light_sleep_start();                   /* 4/4: parked and held, as a rest does */
        mao_board_audio_hold(false);
        mao_audio_resume();
        return;
    }
    if (arg && strncmp(arg, "probe", 5) == 0) {
        /* the bare platform: 2 s of light sleep on the timer alone, nothing else touched */
        ESP_LOGI(TAG, "probe: light sleep 2 s (timer only)");
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_sleep_enable_timer_wakeup(2000000);
        const int64_t t0 = esp_timer_get_time();
        const esp_err_t e = esp_light_sleep_start();
        ESP_LOGI(TAG, "probe: back (%s), %" PRId64 " ms, causes 0x%" PRIx32, esp_err_to_name(e),
                 (esp_timer_get_time() - t0) / 1000, esp_sleep_get_wakeup_causes());
        return;
    }
    if (arg && (strncmp(arg, "rest", 4) == 0 || strncmp(arg, "night", 5) == 0)) {
        const char *sp = strpbrk(arg, "0123456789");
        mao_app_power_dev_rest(arg[0] == 'n', strstr(arg, "force") != NULL, sp ? (uint32_t)strtoul(sp, NULL, 10) : 0);
        return;
    }
    mao_app_power_dev_status();
}
#endif

void mao_app_power_register(void)
{
#if CONFIG_MAO_DEV_CONSOLE
    mao_devcmd_register("deepsleep", dev_deepsleep);   /* DEV only: timer / reset wake */
    mao_devcmd_register("power", dev_power);
#endif
}
