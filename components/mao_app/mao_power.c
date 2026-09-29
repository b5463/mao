#include "mao_power.h"

#include <string.h>

void mao_power_init(mao_power_t *p)
{
    p->st = MAO_PWR_ACTIVE;
}

bool mao_power_busy(const mao_pwr_busy_t *b)
{
    return b && (b->pairing || b->forgetting || b->action_pending || b->transfer);
}

mao_pwr_do_t mao_power_idle(mao_power_t *p, uint32_t idle_ms, const mao_pwr_busy_t *busy)
{
    if (mao_power_busy(busy)) {
        return MAO_PWR_DO_NOTHING;            /* something needs MAO awake: rest later */
    }
    switch (p->st) {
    case MAO_PWR_ACTIVE:
        if (idle_ms >= MAO_PWR_SLEEP_MS) {
            p->st = MAO_PWR_SLEEP_DISPLAY;
            return MAO_PWR_DO_SLEEP_DISPLAY;
        }
        break;
    case MAO_PWR_SLEEP_DISPLAY:
        if (idle_ms >= MAO_PWR_NIGHT_MS) {
            p->st = MAO_PWR_NIGHT;
            return MAO_PWR_DO_NIGHT;
        }
        break;
    case MAO_PWR_NIGHT:
    default:
        break;
    }
    return MAO_PWR_DO_NOTHING;
}

mao_pwr_do_t mao_power_input(mao_power_t *p)
{
    if (p->st == MAO_PWR_ACTIVE) {
        return MAO_PWR_DO_NOTHING;
    }
    p->st = MAO_PWR_ACTIVE;
    return MAO_PWR_DO_WAKE;
}

bool mao_power_deep_allowed(const mao_pwr_busy_t *busy)
{
    return !mao_power_busy(busy);
}

void mao_wake_eat_reset(mao_wake_eat_t *w)
{
    memset(w, 0, sizeof(*w));
}

void mao_wake_eat_knob(mao_wake_eat_t *w, bool switch_down, uint32_t now_ms)
{
    w->eat = switch_down ? 1 : 0;             /* held: its release and click are the waking touch too */
    w->quiet_until = now_ms + MAO_PWR_WAKE_QUIET_MS;
    w->armed_quiet = true;
}

bool mao_wake_eat(mao_wake_eat_t *w, mao_in_t ev, bool dimmed, uint32_t now_ms)
{
    /* a press already being eaten: the rest of it */
    if (w->eat == 1) {
        if (ev == MAO_IN_RELEASE) {
            w->eat = 2;
            w->release_ms = now_ms;
        }
        return true;
    }
    if (w->eat == 2) {
        if ((ev == MAO_IN_CLICK || ev == MAO_IN_DOUBLE) && now_ms - w->release_ms < MAO_WAKE_CLICK_MS) {
            return true;                      /* posted with that release */
        }
        w->eat = 0;
    }
    /* just after a knob wake: whatever woke it */
    if (w->armed_quiet) {
        if ((int32_t)(now_ms - w->quiet_until) < 0) {
            if (ev == MAO_IN_PRESS) {
                w->eat = 1;
            } else if (ev == MAO_IN_RELEASE) {
                w->eat = 2;
                w->release_ms = now_ms;
            }
            return true;
        }
        w->armed_quiet = false;
    }
    /* a dimmed MAO: the first touch only wakes it */
    if (dimmed) {
        if (ev == MAO_IN_PRESS) {
            w->eat = 1;
        }
        return true;
    }
    return false;
}

mao_boot_kind_t mao_power_boot_kind(bool woke_from_sleep, uint32_t marker)
{
    return woke_from_sleep && marker == MAO_PWR_RTC_MAGIC ? MAO_BOOT_FROM_DEEP : MAO_BOOT_COLD;
}
