/*
 * The hold gesture (see mao_hold.h). Plain C, no ESP-IDF runtime: compiled
 * unchanged into the host tests (tests/hold).
 */
#include "mao_hold.h"

void mao_hold_reset(mao_hold_state_t *h)
{
    *h = (mao_hold_state_t) { .sel = -1 };
}

mao_hold_t mao_hold_step(mao_hold_state_t *h, mao_event_type_t type, bool has_right, bool has_left, bool *moved)
{
    if (moved) {
        *moved = false;
    }
    switch (type) {
    case MAO_EVENT_INPUT_PRESS:
        *h = (mao_hold_state_t) { .down = true, .sel = -1 };
        return MAO_HOLD_PASS;
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        if (!h->down) {
            return MAO_HOLD_PASS;
        }
        /* A three-position switch, one detent per step: left option, BACK,
         * right option. It stops at its ends - no hidden travel to wind back. */
        h->menu = true;
        h->acc += type == MAO_EVENT_INPUT_CW ? 1 : -1;
        const int32_t hi = has_right ? 1 : 0, lo = has_left ? -1 : 0;
        h->acc = h->acc > hi ? hi : (h->acc < lo ? lo : h->acc);
        const int8_t sel = h->acc > 0 ? 0 : h->acc < 0 ? 1 : -1;
        if (sel != h->sel) {
            h->sel = sel;
            if (moved) {
                *moved = true;
            }
        }
        return MAO_HOLD_EATEN;
    }
    case MAO_EVENT_INPUT_LONG_PRESS:
        if (!h->down) {
            return MAO_HOLD_BACK;        /* not framed by a press (console): plain BACK */
        }
        if (!h->menu) {
            h->menu = true;
            h->sel = -1;
            if (moved) {
                *moved = true;
            }
        }
        return MAO_HOLD_EATEN;
    case MAO_EVENT_INPUT_RELEASE: {
        const bool menu = h->menu;
        const int8_t sel = h->sel;
        mao_hold_reset(h);
        if (!menu) {
            return MAO_HOLD_PASS;
        }
        return sel == 0 ? MAO_HOLD_RIGHT : sel == 1 ? MAO_HOLD_LEFT : MAO_HOLD_BACK;
    }
    default:
        return MAO_HOLD_PASS;
    }
}
