#include "mao_input.h"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mao_board.h"
#include "mao_events.h"

static const char *TAG = "MAO_INPUT";

#define INPUT_TASK_STACK   3072
#define INPUT_TASK_PRIO    5

#define NOTIFY_ENCODER     (1u << 0)
#define NOTIFY_BUTTON      (1u << 1)

/*
 * Quadrature decoding
 * -------------------
 * Both encoder lines interrupt on any edge. The ISR reads A/B, looks up the
 * (previous, current) state pair in a transition table and accumulates +1/-1
 * for valid Gray-code steps. Invalid (double) transitions contribute 0 and
 * contact bounce on one line produces +1/-1 pairs that cancel, so the table is
 * itself the debouncer.
 *
 * Detents are committed only when the lines arrive at one of the board's rest
 * states (AB values where the mechanism settles: 00 only for full-cycle
 * encoders, 00 and 11 for the LCDkit's 30-detent / 15-pulse EC11). Arriving
 * at rest with at least half a detent travelled in one direction counts as a
 * detent, which tolerates one missed edge during very fast spins; bounce
 * around a rest position nets to zero and is ignored. If the knob is not on
 * a detent at boot, the first arrival at rest only synchronises.
 */
static const int8_t kQuadTable[16] = {
    /* index = (prev << 2) | curr, AB bits */
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

static mao_board_encoder_t s_enc;
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* ISR-owned state */
static uint8_t s_prev_ab;
static bool s_synced;     /* false until the lines have been seen at rest */
static int8_t s_accum;
/* Shared ISR -> task (protected by s_lock) */
static int32_t s_pending_detents;
/* Diagnostics, written by the ISR only */
static mao_input_stats_t s_stats;

uint8_t mao_input_detents_per_rev(void)
{
    return s_enc.detents_per_rev;
}

void mao_input_get_stats(mao_input_stats_t *out)
{
    if (out) {
        portENTER_CRITICAL(&s_lock);
        *out = s_stats;
        portEXIT_CRITICAL(&s_lock);
    }
}

static inline uint8_t read_ab(void)
{
    return (uint8_t)((gpio_get_level(s_enc.gpio_a) << 1) | gpio_get_level(s_enc.gpio_b));
}

static void encoder_isr(void *arg)
{
    (void)arg;
    const uint8_t ab = read_ab();
    const int8_t step = kQuadTable[(s_prev_ab << 2) | ab];
    if (step == 0 && ab != s_prev_ab) {
        s_stats.invalid_transitions++;   /* both lines changed: edge(s) missed */
    }
    s_accum += step;
    s_prev_ab = ab;

    int32_t detent = 0;
    if (s_enc.rest_mask & (1u << ab)) {
        const int8_t full = (int8_t)s_enc.transitions_per_detent;
        const int8_t half = full / 2;
        if (!s_synced) {
            s_synced = true;                 /* started between detents */
        } else if (s_accum >= half) {
            detent = 1;
        } else if (s_accum <= -half) {
            detent = -1;
        } else if (s_accum != 0) {
            s_stats.rest_bounces++;          /* partial move that returned: ignored */
        }
        if (detent != 0 && s_accum != full && s_accum != -full) {
            s_stats.recovered_detents++;     /* committed despite a missed edge */
        }
        s_accum = 0;
    }

    if (detent != 0) {
        portENTER_CRITICAL_ISR(&s_lock);
        s_stats.detents++;
        s_pending_detents += detent;
        portEXIT_CRITICAL_ISR(&s_lock);

        BaseType_t woken = pdFALSE;
        xTaskNotifyFromISR(s_task, NOTIFY_ENCODER, eSetBits, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static void button_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(s_task, NOTIFY_BUTTON, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ---------------------------------------------------------------------- */
/* Button state machine (task context)                                    */
/* ---------------------------------------------------------------------- */

typedef struct {
    bool pressed;            /* debounced state */
    bool debouncing;
    bool long_fired;
    int64_t debounce_until_us;
    int64_t pressed_at_us;
    int64_t last_click_us;
    bool turned_early;       /* turned deliberately while the press was still debouncing */
} button_t;

static button_t s_btn;

/* Pushing an EC11 often nudges it by a detent. In the first moments of a
 * press, turning counts only once it reaches two detents: a nudge is not a
 * turn, and must neither move anything nor cancel the press. */
#define PRESS_JIGGLE_MS      200
#define PRESS_JIGGLE_DETENTS 2
static int32_t s_jiggle;             /* detents held back at the start of a press */

static bool button_raw_pressed(void)
{
    const int level = gpio_get_level(s_enc.gpio_switch);
    return s_enc.switch_active_low ? (level == 0) : (level != 0);
}

static void button_on_edge(int64_t now)
{
    /* Any edge (including bounce) restarts the stability window. */
    s_btn.debouncing = true;
    s_btn.debounce_until_us = now + MAO_INPUT_DEBOUNCE_MS * 1000;
}

static void button_process(int64_t now)
{
    if (s_btn.debouncing && now >= s_btn.debounce_until_us) {
        s_btn.debouncing = false;
        const bool pressed = button_raw_pressed();
        if (pressed != s_btn.pressed) {
            s_btn.pressed = pressed;
            if (pressed) {
                s_btn.pressed_at_us = now;
                s_btn.long_fired = s_btn.turned_early;   /* already a hold-and-turn */
                s_btn.turned_early = false;
                mao_event_post(MAO_EVENT_INPUT_PRESS, 0);
            } else {
                mao_event_post(MAO_EVENT_INPUT_RELEASE, 0);
                if (!s_btn.long_fired) {
                    mao_event_post(MAO_EVENT_INPUT_CLICK, 0);
                    if (s_btn.last_click_us != 0 &&
                        now - s_btn.last_click_us <= MAO_INPUT_DOUBLE_CLICK_MS * 1000) {
                        mao_event_post(MAO_EVENT_INPUT_DOUBLE_CLICK, 0);
                        s_btn.last_click_us = 0;
                    } else {
                        s_btn.last_click_us = now;
                    }
                }
            }
        }
    }

    if (s_btn.pressed && !s_btn.long_fired &&
        now - s_btn.pressed_at_us >= MAO_INPUT_LONG_PRESS_MS * 1000) {
        s_btn.long_fired = true;
        s_btn.last_click_us = 0;
        mao_event_post(MAO_EVENT_INPUT_LONG_PRESS, 0);
    }
}

/* Ticks until the next button deadline, or portMAX_DELAY when idle. */
static TickType_t button_next_timeout(int64_t now)
{
    int64_t deadline = INT64_MAX;
    if (s_btn.debouncing) {
        deadline = s_btn.debounce_until_us;
    }
    if (s_btn.pressed && !s_btn.long_fired) {
        const int64_t lp = s_btn.pressed_at_us + MAO_INPUT_LONG_PRESS_MS * 1000;
        if (lp < deadline) {
            deadline = lp;
        }
    }
    if (deadline == INT64_MAX) {
        return portMAX_DELAY;
    }
    const int64_t wait_us = deadline - now;
    if (wait_us <= 0) {
        return 0;
    }
    return pdMS_TO_TICKS((wait_us + 999) / 1000) + 1;
}

/* ---------------------------------------------------------------------- */

static void encoder_flush(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    int32_t detents = s_pending_detents;
    s_pending_detents = 0;
    portEXIT_CRITICAL(&s_lock);

    if (detents == 0) {
        return;
    }
    const bool pressing = s_btn.debouncing && !s_btn.pressed && button_raw_pressed();
    const bool fresh = pressing || (s_btn.pressed && now - s_btn.pressed_at_us < PRESS_JIGGLE_MS * 1000);
    if (!fresh) {
        s_jiggle = 0;
    } else {
        s_jiggle += detents;
        if (abs(s_jiggle) < PRESS_JIGGLE_DETENTS) {
            return;                        /* a nudge from pushing the knob */
        }
        detents = s_jiggle;
        s_jiggle = 0;
    }
    if (s_btn.pressed || pressing) {
        /* Turned while held (M4.1: hold-and-turn): this press is a gesture of
         * its own - it must not also become a CLICK or a LONG PRESS. */
        s_btn.long_fired = true;
        s_btn.last_click_us = 0;
        s_btn.turned_early = pressing;
    }
    if (s_enc.reverse) {
        detents = -detents;
    }
    const mao_event_type_t type = detents > 0 ? MAO_EVENT_INPUT_CW : MAO_EVENT_INPUT_CCW;
    mao_event_post(type, abs(detents));
}

static void input_task(void *arg)
{
    (void)arg;
    TickType_t timeout = portMAX_DELAY;
    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, timeout);
        const int64_t now = esp_timer_get_time();

        if (bits & NOTIFY_ENCODER) {
            encoder_flush();
        }
        if (bits & NOTIFY_BUTTON) {
            button_on_edge(now);
        }
        button_process(now);
        timeout = button_next_timeout(now);
    }
}

esp_err_t mao_input_init(void)
{
    ESP_RETURN_ON_ERROR(mao_board_input_init(&s_enc), TAG, "board input");
    ESP_RETURN_ON_FALSE(s_enc.transitions_per_detent == 2 || s_enc.transitions_per_detent == 4,
                        ESP_ERR_INVALID_ARG, TAG, "unsupported transitions/detent");

    s_prev_ab = read_ab();
    s_synced = (s_enc.rest_mask & (1u << s_prev_ab)) != 0;
    if (!s_synced) {
        ESP_LOGW(TAG, "encoder not at a detent at boot (AB=%u%u); resyncing at the next detent",
                 (s_prev_ab >> 1) & 1, s_prev_ab & 1);
    }
    s_btn.pressed = button_raw_pressed();

    if (xTaskCreate(input_task, "mao_input", INPUT_TASK_STACK, NULL, INPUT_TASK_PRIO, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "isr service");

    ESP_RETURN_ON_ERROR(gpio_set_intr_type(s_enc.gpio_a, GPIO_INTR_ANYEDGE), TAG, "enc a intr");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(s_enc.gpio_b, GPIO_INTR_ANYEDGE), TAG, "enc b intr");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(s_enc.gpio_switch, GPIO_INTR_ANYEDGE), TAG, "sw intr");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(s_enc.gpio_a, encoder_isr, NULL), TAG, "enc a isr");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(s_enc.gpio_b, encoder_isr, NULL), TAG, "enc b isr");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(s_enc.gpio_switch, button_isr, NULL), TAG, "sw isr");

    ESP_LOGI(TAG, "encoder: quadrature ISR decoder, %u transitions/detent, %u detents/rev, "
             "rest states mask 0x%02x%s; switch debounce %d ms, long press %d ms, double click %d ms",
             s_enc.transitions_per_detent, s_enc.detents_per_rev, s_enc.rest_mask,
             s_enc.reverse ? ", reversed" : "",
             MAO_INPUT_DEBOUNCE_MS, MAO_INPUT_LONG_PRESS_MS, MAO_INPUT_DOUBLE_CLICK_MS);
    return ESP_OK;
}
