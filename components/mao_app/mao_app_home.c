/*
 * HOME's input (M4.1): the settings tune (hold and turn: SOUND / SCREEN),
 * the light used last (a plain turn), fiddling with it (mao_fiddle.h), and
 * a press that opens DEVICES. Split from mao_app.c; the view machine and
 * the shared helpers stay there (mao_app_priv.h).
 */
#include "mao_app_priv.h"

#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mao_audio.h"
#include "mao_character.h"
#include "mao_devices.h"
#include "mao_display.h"
#include "mao_fiddle.h"
#include "mao_led.h"
#include "mao_settings.h"
#include "mao_ui.h"
#include "mao_world.h"

static const char *TAG = "MAO_APP";
bool mao_app_home_hint_allowed(void)
{
    return mao_settings_get()->devices_opened < MAO_SETTINGS_HINT_UNTIL;
}

/* HOME's own settings (M4.1): hold and turn - SOUND to the left, SCREEN to
 * the right (one detent each), the middle cancels; letting go on one opens
 * it: turn to set it, live; a press, or a few seconds without input,
 * finishes and saves once. A long press without a turn is still MAO's warm
 * reaction. */
#define TUNE_STEP        5
#define TUNE_SCREEN_MIN  10               /* never a black screen */
#define TUNE_CLOSE_US    (5 * 1000 * 1000)

static struct {
    bool down, menu;
    int8_t acc;                           /* -1 SOUND, 0 middle, +1 SCREEN */
    int8_t adjust;                        /* -1 none, 0 SOUND, 1 SCREEN */
    int value;
    int64_t last_us;
} s_tune = { .adjust = -1 };

bool mao_app_tune_active(void)
{
    return s_tune.adjust >= 0 || s_tune.menu;
}

void mao_app_tune_close(void)
{
    if (s_tune.adjust == 0) {
        mao_settings_set_volume((uint8_t)s_tune.value);
        ESP_LOGI(TAG, "sound set to %d%%", s_tune.value);
    } else if (s_tune.adjust == 1) {
        mao_settings_set_brightness((uint8_t)s_tune.value);
        ESP_LOGI(TAG, "screen set to %d%%", s_tune.value);
    }
    s_tune = (typeof(s_tune)) { .adjust = -1 };
    mao_app_apply_brightness(false);
    mao_ui_home_tune(MAO_TUNE_OFF, -1, 0);
}

static void tune_open(int8_t which)
{
    s_tune.adjust = which;
    s_tune.value = which == 0 ? mao_settings_get()->volume : mao_settings_get()->brightness;
    s_tune.last_us = esp_timer_get_time();
    ESP_LOGI(TAG, "tune %s (%d%%)", which == 0 ? "sound" : "screen", s_tune.value);
    mao_ui_home_tune(MAO_TUNE_ADJUST, which, s_tune.value);
}

/* Returns true when the tune owned the event. */
static bool on_home_tune(const mao_event_t *ev, int64_t now)
{
    if (s_tune.adjust >= 0) {
        s_tune.last_us = now;
        switch (ev->type) {
        case MAO_EVENT_INPUT_CW:
        case MAO_EVENT_INPUT_CCW: {
            const int d = ev->type == MAO_EVENT_INPUT_CW ? (int)ev->value : -(int)ev->value;
            const int lo = s_tune.adjust == 1 ? TUNE_SCREEN_MIN : 0;
            int v = s_tune.value + d * TUNE_STEP;
            v = v < lo ? lo : (v > 100 ? 100 : v);
            if (v != s_tune.value) {
                s_tune.value = v;
                if (s_tune.adjust == 0) {
                    mao_audio_set_volume((uint8_t)v);
                    mao_audio_tick(160);             /* hear the new level */
                } else {
                    mao_display_set_brightness((uint8_t)v);   /* see it */
                }
                mao_ui_home_tune(MAO_TUNE_ADJUST, s_tune.adjust, v);
            }
            return true;
        }
        case MAO_EVENT_INPUT_CLICK:
        case MAO_EVENT_INPUT_LONG_PRESS:
            mao_audio_confirm();
            mao_app_tune_close();
            return true;
        default:
            return true;                          /* press / release / double: nothing more */
        }
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_PRESS:
        s_tune.down = true;
        s_tune.menu = false;
        s_tune.acc = 0;
        return false;                             /* the eyes still answer the finger */
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        if (!s_tune.down) {
            return false;                         /* a plain turn: the eyes follow */
        }
        if (!s_tune.menu) {
            s_tune.menu = true;
            mao_character_press(false);
        }
        const int8_t a = (int8_t)(s_tune.acc + (ev->type == MAO_EVENT_INPUT_CW ? 1 : -1));
        const int8_t acc = a > 1 ? 1 : (a < -1 ? -1 : a);
        if (acc != s_tune.acc || !s_tune.menu) {
            mao_audio_tick(120);
        }
        s_tune.acc = acc;
        mao_ui_home_tune(MAO_TUNE_CHOOSE, acc < 0 ? 0 : acc > 0 ? 1 : -1, 0);
        return true;
    }
    case MAO_EVENT_INPUT_RELEASE: {
        const bool menu = s_tune.menu;
        const int8_t acc = s_tune.acc;
        s_tune.down = false;
        s_tune.menu = false;
        if (!menu) {
            return false;
        }
        mao_character_press(false);
        if (acc == 0) {
            mao_ui_home_tune(MAO_TUNE_OFF, -1, 0);   /* the middle: nothing */
        } else {
            mao_audio_touch();
            tune_open(acc < 0 ? 0 : 1);
        }
        return true;
    }
    default:
        return false;
    }
}

/* One turn on a light - its page, or HOME's plain turn. Turning up a light
 * that is off switches it on. Slow turns are fine, fast ones travel: 1 % a
 * detent when setting it carefully, up to 8 % when sweeping across. */
void mao_app_lamp_turn(const mao_device_t *dev, const mao_device_controls_t *ctl, int32_t d,
                      const mao_dial_motion_t *m)
{
    const mao_device_cap_t *c = &dev->caps[ctl->level_idx];
    const bool off = ctl->toggle_idx >= 0 && dev->caps[ctl->toggle_idx].value == 0;
    if (d > 0 && off) {
        const mao_device_cap_t *tc = &dev->caps[ctl->toggle_idx];
        mao_devices_set_value(dev->info.id, tc->cap.id, tc->cap.max);
        return;
    }
    if (off) {
        return;
    }
    const int32_t span = c->cap.max - c->cap.min;
    /* the speed window is 250 ms: one detent in it reads 4 /s */
    const int32_t pct = m->detents_per_s <= 4.5f ? 1 : m->detents_per_s <= 8.5f ? 2
                        : m->detents_per_s < 20.0f ? 4 : 8;
    const int32_t want = span * pct / 100;
    const int32_t step = want > c->cap.step ? want : (c->cap.step > 0 ? c->cap.step : 1);
    int32_t v = c->value + d * step;
    v = v < c->cap.min ? c->cap.min : (v > c->cap.max ? c->cap.max : v);
    if (v != c->value) {
        mao_devices_set_value(dev->info.id, c->cap.id, v);
    }
}

/* HOME's light (M4.1): the light used last - or, before any was, the first
 * one in the setup - when it is here and answering. */
static bool home_lamp(mao_device_t *dev, mao_device_controls_t *ctl)
{
    mao_world_entry_t devs[MAO_UI_DEVICES_MAX];
    uint64_t want = mao_settings_get()->last_lamp;
    for (int pass = 0; pass < 2; pass++) {
        const int n = mao_world_list(devs);
        for (int i = 0; i < n; i++) {
            if ((want && devs[i].id != want) || !devs[i].known) {
                continue;
            }
            mao_world_entry_t w;
            const int slot = mao_devices_find(devs[i].id);
            if (mao_devpage(devs[i].id, &w) != MAO_DEVPAGE_CONTROL || slot < 0 || !mao_devices_get(slot, dev)) {
                continue;
            }
            mao_device_controls(dev, ctl);
            if (dev->described && dev->online && ctl->level_idx >= 0) {
                return true;
            }
        }
        if (!want) {
            break;
        }
        want = 0;                                 /* the last one is away: any light in the setup */
    }
    return false;
}

/* Fiddling with HOME's light (mao_fiddle.h): the light has already
 * followed the knob; MAO only lets it show. */
static mao_fiddle_t s_fiddle;
static uint32_t s_fiddle_told_ms;
static bool fiddle_says(const char *name, mao_fiddle_level_t l, uint32_t ms);

/* A moment after the last turn of HOME's light: the stop is read
 * (mao_fiddle_quiet) - MAO waiting for the tsk that did not come, or a clean
 * setting it forgives. A one-shot timer, re-armed by every turn. */
#define HOME_QUIET_US (1200 * 1000)
static esp_timer_handle_t s_quiet_timer;

static void quiet_cb(void *arg)
{
    (void)arg;
    mao_event_post(MAO_EVENT_HOME_QUIET, 0);
}

static void home_quiet_arm(void)
{
    if (!s_quiet_timer) {
        const esp_timer_create_args_t args = { .callback = quiet_cb, .name = "mao_homequiet" };
        if (esp_timer_create(&args, &s_quiet_timer) != ESP_OK) {
            return;
        }
    }
    esp_timer_stop(s_quiet_timer);
    esp_timer_start_once(s_quiet_timer, HOME_QUIET_US);
}

void mao_app_home_quiet(int64_t now)
{
    const uint32_t ms = (uint32_t)(now / 1000);
    switch (mao_fiddle_quiet(&s_fiddle, ms)) {
    case MAO_FIDDLE_QUIET_HELD:
        if (mao_app_quirk_roll(MAO_QK_HOLD)) {
            mao_character_quirk(MAO_QUIRK_HOLD);   /* it was waiting to see what you would do */
        }
        break;
    case MAO_FIDDLE_QUIET_CLEAN:
        if (mao_app_quirk_roll(MAO_QK_FORGIVE)) {
            mao_character_quirk(MAO_QUIRK_FORGIVE);   /* that was a proper setting: fine */
        }
        break;
    default:
        break;
    }
}

static void home_fiddle(uint64_t id, int32_t d, int64_t now)
{
    mao_device_t dev;
    mao_device_controls_t ctl;
    const int slot = mao_devices_find(id);
    if (slot < 0 || !mao_devices_get(slot, &dev)) {
        return;
    }
    mao_device_controls(&dev, &ctl);
    if (ctl.level_idx < 0) {
        return;
    }
    const mao_device_cap_t *c = &dev.caps[ctl.level_idx];
    const bool off = ctl.toggle_idx >= 0 && dev.caps[ctl.toggle_idx].value == 0;
    const int8_t edge = off || c->value <= c->cap.min ? -1 : (c->value >= c->cap.max ? 1 : 0);
    const uint32_t ms = (uint32_t)(now / 1000);
    fiddle_says(dev.info.name, mao_fiddle_turn(&s_fiddle, d, edge, ms), ms);
}

/* What the fiddling detector found becomes MAO's mood: a new level is its
 * reaction (and the grudge after it); still at it keeps the mood going.
 * Returns true when MAO reacted. */
static bool fiddle_says(const char *name, mao_fiddle_level_t l, uint32_t ms)
{
    static const mao_character_reaction_t kReact[] = {
        [MAO_FIDDLE_NOTICE] = MAO_CHAR_REACT_FIDDLE_NOTICE,
        [MAO_FIDDLE_ANNOYED] = MAO_CHAR_REACT_FIDDLE_ANNOYED,
        [MAO_FIDDLE_FED_UP] = MAO_CHAR_REACT_FIDDLE_FED_UP,
    };
    if (l != MAO_FIDDLE_NONE) {
        ESP_LOGI(TAG, "fiddling with '%s': %s (score %d)", name,
                 l == MAO_FIDDLE_NOTICE ? "noticed" : l == MAO_FIDDLE_ANNOYED ? "annoyed" : "fed up",
                 mao_fiddle_score(&s_fiddle, ms));
        mao_character_react(kReact[l]);
        s_fiddle_told_ms = ms;
        if (l == MAO_FIDDLE_NOTICE && mao_app_woke_us() &&
            (uint32_t)(ms - (uint32_t)(mao_app_woke_us() / 1000)) < MAO_QK_GRUMPY_MS &&
            mao_app_quirk_roll(MAO_QK_GRUMPY)) {
            mao_character_quirk(MAO_QUIRK_GRUMPY);   /* barely awake, and already this */
        }
        return true;
    }
    if (s_fiddle.level > 0 && s_fiddle.last_event_ms == ms && ms - s_fiddle_told_ms >= 500u) {
        /* still at it: the mood it is in lasts (at most twice a second) */
        mao_character_react(MAO_CHAR_REACT_FIDDLE_ONGOING);
        s_fiddle_told_ms = ms;
        return true;
    }
    return s_fiddle.level > 0;                /* in a mood: no cheerful nod on top */
}

/* While the light's arc is up (it stays ~1.6 s after the last turn - the
 * UI's HOME_LAMP_HOLD_MS), a press is the light's switch. */
#define HOME_ARC_UP_US (1600 * 1000)
static int64_t s_arc_until_us;

static bool light_off(const mao_device_t *dev, const mao_device_controls_t *ctl)
{
    return ctl->toggle_idx >= 0 && dev->caps[ctl->toggle_idx].value == 0;
}

#define HOME_OFF_REST_US   (300 * 1000)   /* stopped at the bottom this long, then one more turn down: off */
#define HOME_ON_MIN_PCT    10             /* switched on from the bottom: this much, not a dark "on" */
static bool s_at_bottom;                  /* the level is at the bottom (and the light on) */
static int64_t s_last_turn_us;            /* the previous turn: a sweep never pauses */

/* Switch the light: off, or on at the level it had - at least a little. */
static void home_lamp_switch(const mao_device_t *dev, const mao_device_controls_t *ctl, bool on)
{
    const mao_device_cap_t *tc = &dev->caps[ctl->toggle_idx];
    mao_devices_set_value(dev->info.id, tc->cap.id, on ? tc->cap.max : tc->cap.min);
    const mao_device_cap_t *lc = &dev->caps[ctl->level_idx];
    const int32_t floor_v = lc->cap.min + (lc->cap.max - lc->cap.min) * HOME_ON_MIN_PCT / 100;
    if (on && lc->value < floor_v) {
        mao_devices_set_value(dev->info.id, lc->cap.id, floor_v);
    }
    ESP_LOGI(TAG, "'%s' switched %s from HOME", dev->info.name, on ? "on" : "off");
    if (on) {
        mao_audio_confirm();
    } else {
        mao_audio_back();
    }
    mao_led_pulse(MAO_LED_PULSE_CONFIRM);
    /* The mood behaviour: flicking the light is fiddling like any other;
     * a single switch just gets MAO's quiet "done" (no waiting for a result). */
    const uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (!fiddle_says(dev->info.name, mao_fiddle_switch(&s_fiddle, ms), ms)) {
        mao_character_react(MAO_CHAR_REACT_DONE);
    }
}

static void home_lamp_show(uint64_t id)
{
    s_arc_until_us = esp_timer_get_time() + HOME_ARC_UP_US;
    mao_device_t dev;
    mao_device_controls_t ctl;
    const int slot = mao_devices_find(id);
    if (slot < 0 || !mao_devices_get(slot, &dev)) {
        return;
    }
    mao_device_controls(&dev, &ctl);
    if (ctl.level_idx < 0) {
        return;
    }
    const mao_device_cap_t *c = &dev.caps[ctl.level_idx];
    const int32_t span = c->cap.max - c->cap.min;
    const int pct = span > 0 ? (int)((c->value - c->cap.min) * 100 / span) : 0;
    mao_ui_home_lamp(dev.info.name, pct, ctl.toggle_idx < 0 || dev.caps[ctl.toggle_idx].value != 0);
}

void mao_app_on_home(const mao_event_t *ev, int64_t now)
{
    mao_ui_home_hint(mao_app_home_hint_allowed());   /* any input restarts the hint's wait */
    if (on_home_tune(ev, now)) {
        return;
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        const int32_t d = ev->type == MAO_EVENT_INPUT_CW ? ev->value : -ev->value;
        const mao_dial_motion_t m = mao_app_dial_motion(d, now);
        mao_device_t dev;
        mao_device_controls_t ctl;
        if (home_lamp(&dev, &ctl)) {
            /* a plain turn is the light: the eyes follow the scale's end.
             * Like a dimmer: turning down at the bottom switches it off,
             * turning up switches it on again at its level. */
            const mao_device_cap_t *lc = &dev.caps[ctl.level_idx];
            const bool was_off = light_off(&dev, &ctl);
            const bool paused = now - s_last_turn_us >= HOME_OFF_REST_US;
            s_last_turn_us = now;
            if (d < 0 && ctl.toggle_idx >= 0 && !was_off && lc->value <= lc->cap.min && s_at_bottom && paused) {
                home_lamp_switch(&dev, &ctl, false);      /* stopped at the bottom, turned once more: off */
                s_at_bottom = false;
            } else if (d > 0 && was_off && ctl.toggle_idx >= 0) {
                home_lamp_switch(&dev, &ctl, true);       /* on again, never a dark "on" */
            } else {
                mao_app_lamp_turn(&dev, &ctl, d, &m);
            }
            {
                mao_device_t after;                       /* where the level is now */
                const int slot = mao_devices_find(dev.info.id);
                if (slot >= 0 && mao_devices_get(slot, &after)) {
                    const mao_device_cap_t *ac = &after.caps[ctl.level_idx];
                    /* a sweep that reaches the bottom stops there; only a turn after a pause goes past it */
                    s_at_bottom = ac->value <= ac->cap.min && !light_off(&after, &ctl);
                }
            }
            mao_settings_note_last_lamp(dev.info.id);
            home_lamp_show(dev.info.id);
            home_fiddle(dev.info.id, d, now);
            home_quiet_arm();
        } else {
            mao_character_dial(d);                /* no light here: the eyes follow */
        }
        mao_app_dial_tick(&m);
        break;
    }
    case MAO_EVENT_INPUT_PRESS:
        /* Touching MAO: compress + a soft low contact sound. */
        mao_character_press(true);
        mao_audio_touch();
        break;
    case MAO_EVENT_INPUT_RELEASE:
        mao_character_press(false);
        mao_audio_release();
        break;
    case MAO_EVENT_INPUT_CLICK: {
        /* The light's arc is up: this press is its switch. */
        mao_device_t dev;
        mao_device_controls_t ctl;
        if (now < s_arc_until_us && home_lamp(&dev, &ctl) && ctl.toggle_idx >= 0) {
            home_lamp_switch(&dev, &ctl, light_off(&dev, &ctl));
            home_lamp_show(dev.info.id);          /* the arc answers: lit, or dark */
            break;
        }
        /* M4.1 (D1): one press - MAO makes room and the world arrives. */
        mao_settings_note_devices_opened();
        mao_audio_confirm();
        mao_app_note_list_opened(now);
        mao_app_go_view(MAO_VIEW_DEVICES);
        break;
    }
    case MAO_EVENT_INPUT_DOUBLE_CLICK:
        break;   /* no meaning anywhere: every CLICK has already acted */
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_character_react(MAO_CHAR_REACT_WARM);
        mao_audio_warm();
        mao_led_pulse(MAO_LED_PULSE_CONFIRM);
        break;
    default:
        break;
    }
}

/* A tune set and left alone closes itself (on_page_idle). Returns true when it did. */
bool mao_app_tune_idle(int64_t now)
{
    if (s_tune.adjust >= 0 && now - s_tune.last_us >= TUNE_CLOSE_US) {
        mao_app_tune_close();                     /* set and left alone: done */
        return true;
    }
    return false;
}
