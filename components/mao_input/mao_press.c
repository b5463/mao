/*
 * The press gesture (see mao_press.h). Plain C, no ESP-IDF runtime: compiled
 * unchanged into the host tests (tests/press).
 */
#include "mao_press.h"

#include <stdlib.h>

void mao_press_init(mao_press_t *p, const mao_press_cfg_t *cfg, bool pressed_now,
                    mao_press_emit_t emit, void *ctx)
{
    *p = (mao_press_t) {
        .cfg = *cfg,
        .emit = emit,
        .ctx = ctx,
        .pressed = pressed_now,
        .heard = pressed_now,          /* held through boot: a press from time 0, as before */
        .last_detent_us = INT64_MIN,
    };
}

static void hear(mao_press_t *p, int64_t now_us)
{
    p->heard = true;
    p->pressed_at_us = now_us;
    p->long_fired = p->turned_early;   /* already a hold-and-turn */
    p->turned_early = false;
    p->emit(MAO_EVENT_INPUT_PRESS, 0, p->ctx);
}

static bool in_turn(const mao_press_t *p, int64_t now_us)
{
    return p->cfg.turn_guard_ms != 0 && p->last_detent_us != INT64_MIN &&
           now_us - p->last_detent_us < (int64_t)p->cfg.turn_guard_ms * 1000;
}

void mao_press_level(mao_press_t *p, int64_t now_us, bool pressed)
{
    if (pressed == p->pressed) {
        return;
    }
    p->pressed = pressed;
    if (pressed) {
        if (in_turn(p, now_us)) {
            p->heard = false;          /* pressure from turning the wheel: part of the turn */
            p->turned_early = false;
            p->turn_presses++;
            return;
        }
        hear(p, now_us);
        return;
    }
    if (!p->heard) {
        return;                        /* a turn press ends unheard */
    }
    p->heard = false;
    p->emit(MAO_EVENT_INPUT_RELEASE, 0, p->ctx);
    if (!p->long_fired) {
        p->emit(MAO_EVENT_INPUT_CLICK, 0, p->ctx);
        if (p->last_click_us != 0 && now_us - p->last_click_us <= (int64_t)MAO_PRESS_DOUBLE_MS * 1000) {
            p->emit(MAO_EVENT_INPUT_DOUBLE_CLICK, 0, p->ctx);
            p->last_click_us = 0;
        } else {
            p->last_click_us = now_us;
        }
    }
}

int32_t mao_press_detents(mao_press_t *p, int64_t now_us, int32_t detents, bool pressing)
{
    if (detents == 0) {
        return 0;
    }
    const bool turn_press = p->pressed && !p->heard;
    p->last_detent_us = now_us;
    if (p->cfg.turn_guard_ms != 0 && (turn_press || pressing)) {
        p->jiggle = 0;
        return detents;                /* the wheel turning under a thumb: plain turns */
    }
    const bool held = p->pressed && p->heard;
    const bool fresh = pressing || (held && now_us - p->pressed_at_us < (int64_t)MAO_PRESS_JIGGLE_MS * 1000);
    if (!fresh) {
        p->jiggle = 0;
    } else {
        p->jiggle += detents;
        if (abs(p->jiggle) < MAO_PRESS_JIGGLE_DETENTS) {
            return 0;                  /* a nudge from pushing the knob */
        }
        detents = p->jiggle;
        p->jiggle = 0;
    }
    if (held || pressing) {
        /* Turned while held (M4.1: hold-and-turn): this press is a gesture of
         * its own - it must not also become a CLICK or a LONG PRESS. */
        p->long_fired = true;
        p->last_click_us = 0;
        p->turned_early = pressing;
    }
    return detents;
}

void mao_press_tick(mao_press_t *p, int64_t now_us)
{
    if (p->pressed && !p->heard && p->cfg.turn_guard_ms != 0 && p->last_detent_us != INT64_MIN &&
        now_us - p->last_detent_us >= (int64_t)p->cfg.turn_settle_ms * 1000) {
        hear(p, now_us);               /* the turning stopped and the press stayed: a press */
    }
    if (p->pressed && p->heard && !p->long_fired &&
        now_us - p->pressed_at_us >= (int64_t)MAO_PRESS_LONG_MS * 1000) {
        p->long_fired = true;
        p->last_click_us = 0;
        p->emit(MAO_EVENT_INPUT_LONG_PRESS, 0, p->ctx);
    }
}

int64_t mao_press_deadline(const mao_press_t *p)
{
    if (p->pressed && !p->heard && p->cfg.turn_guard_ms != 0 && p->last_detent_us != INT64_MIN) {
        return p->last_detent_us + (int64_t)p->cfg.turn_settle_ms * 1000;
    }
    if (p->pressed && p->heard && !p->long_fired) {
        return p->pressed_at_us + (int64_t)MAO_PRESS_LONG_MS * 1000;
    }
    return INT64_MAX;
}
