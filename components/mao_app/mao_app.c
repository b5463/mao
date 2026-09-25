#include "mao_app.h"
#include "mao_app_priv.h"

#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mao_audio.h"
#include "mao_character.h"
#include "mao_devices.h"
#include "odd_bus.h"
#include "mao_display.h"
#include "mao_events.h"
#include "mao_input.h"
#include "mao_led.h"
#include "mao_settings.h"
#include "mao_system.h"
#include "mao_ui.h"

static const char *TAG = "MAO_APP";

#define SLEEPY_TIMEOUT_MS     (CONFIG_MAO_SLEEPY_TIMEOUT_S * 1000)
#define SLEEPY_BRIGHTNESS_PCT 35          /* of the preferred brightness */
#define DIAL_SETTLE_US        (700 * 1000)
#define MENU_BUMP_GAP_US      (250 * 1000)
#define SLEEPY_FADE_MS        2500        /* the light goes down slowly ... */
#define WAKE_FADE_MS          150         /* ... and comes back at once */
#define TICK_FULL_DPS         60.0f       /* dial speed at which ticks are softest */

static mao_dial_speed_t s_logged_speed = MAO_DIAL_STILL;
static bool s_logged_reversing;
static int64_t s_last_dial_us;
static int64_t s_last_bump_us;

/* ---------------------------------------------------------------------- */
/* Device views: models are built here from the registry + capabilities.  */
/* ---------------------------------------------------------------------- */

static void refresh_devices_list(void)
{
    static mao_device_t devs[MAO_DEVICES_MAX];   /* dispatcher task only */
    mao_ui_devices_t model = { 0 };
    for (int i = 0; i < MAO_DEVICES_MAX && model.count < MAO_UI_DEVICES_MAX; i++) {
        if (mao_devices_get(i, &devs[model.count])) {
            model.name[model.count] = devs[model.count].info.name;
            model.online[model.count] = devs[model.count].online;
            model.count++;
        }
    }
    int sel = mao_state()->devices_index;
    if (sel >= model.count) {
        sel = model.count > 0 ? model.count - 1 : 0;
        mao_state_set_devices_index(sel);
    }
    model.selected = sel;
    mao_ui_devices_update(&model);
}

/* List row -> registry slot (the list shows used slots in order). */
static int list_row_to_slot(int row)
{
    int n = 0;
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        mao_device_t d;
        if (mao_devices_get(i, &d)) {
            if (n == row) {
                return i;
            }
            n++;
        }
    }
    return -1;
}

static bool open_device(mao_device_t *dev, mao_device_controls_t *ctl)
{
    const int slot = mao_devices_find(mao_state()->device_id);
    if (slot < 0 || !mao_devices_get(slot, dev)) {
        return false;
    }
    mao_device_controls(dev, ctl);
    return true;
}

static void refresh_device_panel(void)
{
    mao_device_t dev;
    mao_device_controls_t ctl;
    if (!open_device(&dev, &ctl)) {
        return;
    }
    const mao_ui_device_t model = {
        .title = dev.info.name,
        .has_level = ctl.level_idx >= 0,
        .level = ctl.level_idx >= 0 ? dev.caps[ctl.level_idx].value : 0,
        .has_toggle = ctl.toggle_idx >= 0,
        .on = ctl.toggle_idx >= 0 ? dev.caps[ctl.toggle_idx].value != 0 : true,
        .online = dev.online,
        .problem = dev.link_problem,
        .described = dev.described,
    };
    mao_ui_device_update(&model);
}

static void refresh_device_views(void)
{
    const mao_view_t v = mao_state()->view;
    if (v == MAO_VIEW_DEVICES) {
        refresh_devices_list();
    } else if (v == MAO_VIEW_DEVICE) {
        refresh_device_panel();
    }
}

/* ---------------------------------------------------------------------- */

static void go_view(mao_view_t view)
{
    const mao_app_state_t *st = mao_state();
    if (st->view == view) {
        return;
    }
    ESP_LOGI(TAG, "view %s -> %s", mao_ui_view_name(st->view), mao_ui_view_name(view));
    /* Discover quickly only while the user is looking at devices. */
    mao_devices_set_active(view == MAO_VIEW_DEVICES || view == MAO_VIEW_DEVICE);
    mao_state_set_view(view);
    if (view == MAO_VIEW_DEVICES || view == MAO_VIEW_DEVICE) {
        refresh_device_views();
    }
    mao_ui_show(view, st->menu_index);
}

static void apply_brightness(bool sleepy)
{
    const uint8_t pref = mao_settings_get()->brightness;
    uint8_t pct = sleepy ? (uint8_t)(pref * SLEEPY_BRIGHTNESS_PCT / 100) : pref;
    if (pct < 5) {
        pct = 5;
    }
    mao_display_fade_brightness(pct, sleepy ? SLEEPY_FADE_MS : WAKE_FADE_MS);
}

static void wake(void)
{
    if (!mao_state()->awake) {
        mao_state_set_awake(true);
        mao_character_set_sleepy(false);
        apply_brightness(false);
        ESP_LOGI(TAG, "awake");
    }
}

/* Classify the dial movement, log class changes, and give rotary audio
 * feedback thinned by speed so fast spins never become a buzz. */
static mao_dial_motion_t dial_motion(int32_t detents, int64_t now)
{
    if (now - s_last_dial_us > DIAL_SETTLE_US) {
        s_logged_speed = MAO_DIAL_STILL;
        s_logged_reversing = false;
    }
    s_last_dial_us = now;
    const mao_dial_motion_t m = mao_state_dial(detents, now);
    if (m.speed != s_logged_speed || m.reversing != s_logged_reversing) {
        ESP_LOGI(TAG, "dial %s%s (%.0f detents/s)", mao_dial_speed_name(m.speed),
                 m.reversing ? " +REVERSING" : "", (double)m.detents_per_s);
        s_logged_speed = m.speed;
        s_logged_reversing = m.reversing;
    }
    return m;
}

/* Rotary sound follows speed continuously: softer and sparser as the dial
 * spins faster (mao_audio does the thinning). */
static void dial_tick(const mao_dial_motion_t *m)
{
    float i = m->detents_per_s / TICK_FULL_DPS;
    i = i > 1.0f ? 1.0f : i;
    mao_audio_tick((uint8_t)(i * 255.0f));
}

/* ---------------------------------------------------------------------- */
/* Per-view input handling                                                */
/* ---------------------------------------------------------------------- */

static void on_intro(const mao_event_t *ev)
{
    if (ev->type == MAO_EVENT_INPUT_CW || ev->type == MAO_EVENT_INPUT_CCW) {
        mao_settings_set_first_boot_done(true);
        ESP_LOGI(TAG, "first encounter complete (first-boot flag stored)");
        mao_audio_notice();
        mao_led_pulse(MAO_LED_PULSE_NOTICE);
        ESP_LOGI(TAG, "view INTRO -> HOME");
        mao_state_set_view(MAO_VIEW_HOME);
        mao_ui_intro_exit(ev->type == MAO_EVENT_INPUT_CW ? 1 : -1);
    }
}

static void on_home(const mao_event_t *ev, int64_t now)
{
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        const int32_t d = ev->type == MAO_EVENT_INPUT_CW ? ev->value : -ev->value;
        const mao_dial_motion_t m = dial_motion(d, now);
        mao_character_dial(d);
        dial_tick(&m);
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
    case MAO_EVENT_INPUT_DOUBLE_CLICK:
        /* MAO makes room for the system (the UI choreographs the drop). */
        mao_audio_confirm();
        go_view(MAO_VIEW_MENU);
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_character_react(MAO_CHAR_REACT_WARM);
        mao_audio_warm();
        mao_led_pulse(MAO_LED_PULSE_CONFIRM);
        break;
    default:
        break;
    }
}

static void on_menu(const mao_event_t *ev, int64_t now)
{
    const mao_app_state_t *st = mao_state();
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        const int32_t d = ev->type == MAO_EVENT_INPUT_CW ? ev->value : -ev->value;
        const mao_dial_motion_t m = dial_motion(d, now);
        const int last = mao_ui_menu_count() - 1;
        int idx = st->menu_index + (int)d;
        if (idx < 0) {
            idx = 0;
        } else if (idx > last) {
            idx = last;
        }
        if (idx != st->menu_index) {
            mao_state_set_menu_index(idx);
            mao_ui_menu_select(idx);
            dial_tick(&m);
        } else if (now - s_last_bump_us >= MENU_BUMP_GAP_US) {
            s_last_bump_us = now;
            mao_ui_menu_bump(d > 0 ? 1 : -1);
        }
        break;
    }
    case MAO_EVENT_INPUT_CLICK:
        ESP_LOGI(TAG, "open %s", mao_ui_menu_label(st->menu_index));
        mao_audio_confirm();
        mao_led_pulse(MAO_LED_PULSE_CONFIRM);
        go_view(st->menu_index == MAO_MENU_DEVICES ? MAO_VIEW_DEVICES : MAO_VIEW_PLACEHOLDER);
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();
        go_view(MAO_VIEW_HOME);
        mao_character_react(MAO_CHAR_REACT_BACK);
        break;
    default:
        break;
    }
}

static void on_placeholder(const mao_event_t *ev)
{
    switch (ev->type) {
    case MAO_EVENT_INPUT_CLICK:
        mao_audio_back();
        go_view(MAO_VIEW_MENU);
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();
        go_view(MAO_VIEW_HOME);
        mao_character_react(MAO_CHAR_REACT_BACK);
        break;
    default:
        break;
    }
}

static void on_devices(const mao_event_t *ev, int64_t now)
{
    const mao_app_state_t *st = mao_state();
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        const int32_t d = ev->type == MAO_EVENT_INPUT_CW ? ev->value : -ev->value;
        const mao_dial_motion_t m = dial_motion(d, now);
        const int count = mao_devices_count();
        int idx = st->devices_index + (int)d;
        idx = idx < 0 ? 0 : (idx > count - 1 ? count - 1 : idx);
        if (count > 0 && idx != st->devices_index) {
            mao_state_set_devices_index(idx);
            refresh_devices_list();
            dial_tick(&m);
        } else if (now - s_last_bump_us >= MENU_BUMP_GAP_US) {
            s_last_bump_us = now;
            mao_ui_devices_bump(d > 0 ? 1 : -1);
        }
        break;
    }
    case MAO_EVENT_INPUT_CLICK: {
        const int slot = list_row_to_slot(st->devices_index);
        mao_device_t dev;
        if (slot >= 0 && mao_devices_get(slot, &dev)) {
            ESP_LOGI(TAG, "open device '%s'", dev.info.name);
            mao_state_set_device(dev.info.id);
            mao_audio_confirm();
            mao_led_pulse(MAO_LED_PULSE_CONFIRM);
            go_view(MAO_VIEW_DEVICE);
        }
        break;
    }
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();
        go_view(MAO_VIEW_HOME);
        break;
    default:
        break;
    }
}

/* The generic device view: the dial drives the device's LEVEL-like
 * capability, a click toggles its POWER-like capability. Which capabilities
 * those are is decided by mao_device_controls() from what the device reports. */
static void on_device(const mao_event_t *ev, int64_t now)
{
    mao_device_t dev;
    mao_device_controls_t ctl;
    const bool ok = open_device(&dev, &ctl);
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        const int32_t d = ev->type == MAO_EVENT_INPUT_CW ? ev->value : -ev->value;
        const mao_dial_motion_t m = dial_motion(d, now);
        if (!ok || !dev.online || !dev.described || ctl.level_idx < 0) {
            break;
        }
        const mao_device_cap_t *c = &dev.caps[ctl.level_idx];
        int32_t v = c->value + d * c->cap.step;
        v = v < c->cap.min ? c->cap.min : (v > c->cap.max ? c->cap.max : v);
        if (v != c->value) {
            mao_devices_set_value(dev.info.id, c->cap.id, v);
            refresh_device_panel();
            dial_tick(&m);
        }
        break;
    }
    case MAO_EVENT_INPUT_CLICK:
        if (ok && dev.online && dev.described && ctl.toggle_idx >= 0) {
            const mao_device_cap_t *c = &dev.caps[ctl.toggle_idx];
            const int32_t v = c->value ? c->cap.min : c->cap.max;
            mao_devices_set_value(dev.info.id, c->cap.id, v);
            refresh_device_panel();
            if (v) {
                mao_audio_confirm();
            } else {
                mao_audio_back();
            }
            mao_led_pulse(MAO_LED_PULSE_CONFIRM);
        }
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();
        go_view(MAO_VIEW_DEVICES);
        break;
    default:
        break;
    }
}

static void on_device_event(const mao_event_t *ev)
{
    if (ev->type == MAO_EVENT_DEVICE_FOUND) {
        /* Newly available (first sighting, or back from offline): one brief
         * acknowledgement. Beacons from devices already online never get here. */
        mao_device_t dev;
        if (mao_devices_get((int)ev->value, &dev)) {
            ESP_LOGI(TAG, "device available: '%s'", dev.info.name);
        }
        mao_audio_notice();
        mao_led_pulse(MAO_LED_PULSE_NOTICE);
        if (mao_state()->view == MAO_VIEW_HOME && mao_state()->awake) {
            mao_character_react(MAO_CHAR_REACT_ATTEND);
        }
    }
    refresh_device_views();
}

/* ---------------------------------------------------------------------- */
/* System / development events                                            */
/* ---------------------------------------------------------------------- */

static void on_idle_timeout(void)
{
    const mao_app_state_t *st = mao_state();
    if (st->view == MAO_VIEW_INTRO) {
        return;   /* the first encounter waits indefinitely */
    }
    mao_state_note_idle();
    if (st->view != MAO_VIEW_HOME) {
        go_view(MAO_VIEW_HOME);
    }
    if (st->awake) {
        mao_state_set_awake(false);
        mao_character_set_sleepy(true);
        apply_brightness(true);
        mao_led_set_state(MAO_LED_STATE_OFF);
        ESP_LOGI(TAG, "sleepy after %d s without input", CONFIG_MAO_SLEEPY_TIMEOUT_S);
    }
}

static void on_dev_command(int32_t value, int64_t now)
{
    if (value == MAO_DEVCMD_STATUS) {
        const mao_app_state_t *st = mao_state();
        mao_input_stats_t in;
        mao_input_get_stats(&in);
        ESP_LOGI(TAG, "status: view=%s menu=%d dial=%" PRId32 " awake=%d idle=%" PRIu32 "ms character=%s",
                 mao_ui_view_name(st->view), st->menu_index, st->dial_position, st->awake,
                 mao_state_idle_ms(now), mao_character_state_name(mao_character_get_state()));
        ESP_LOGI(TAG, "status: encoder detents=%" PRIu32 " invalid=%" PRIu32 " recovered=%" PRIu32
                 " rest_bounces=%" PRIu32 " events_dropped=%" PRIu32,
                 in.detents, in.invalid_transitions, in.recovered_detents, in.rest_bounces,
                 mao_events_dropped());
        mao_system_log_heap(TAG, "status");
        mao_devices_log_status();
    } else if (value == MAO_DEVCMD_ODD_SELFTEST) {
        odd_bus_selftest();
    } else if (value == MAO_DEVCMD_ODD_RESET) {
        mao_devices_reset_latency();
        ESP_LOGI(TAG, "latency statistics reset");
    } else if (value >= MAO_DEVCMD_DIAL_BASE) {
        /* dial: value = base + dps * 1000 + seconds (synthetic detents to the character only) */
        const int32_t v = value - MAO_DEVCMD_DIAL_BASE;
        const float dps = (float)(v / 1000) - 500.0f;
        const uint32_t seconds = (uint32_t)(v % 1000);
        ESP_LOGI(TAG, "dev: dial %.0f detents/s for %" PRIu32 " s", (double)dps, seconds);
        wake();
        go_view(MAO_VIEW_HOME);
        mao_character_debug_dial(dps, seconds * 1000);
        mao_system_idle_kick(seconds * 1000 + SLEEPY_TIMEOUT_MS);
    } else if (value >= MAO_DEVCMD_REACT_BASE) {
        const int r = value - MAO_DEVCMD_REACT_BASE;
        if (r < MAO_CHAR_REACT_COUNT) {
            wake();
            go_view(MAO_VIEW_HOME);
            ESP_LOGI(TAG, "dev: react %s", mao_character_reaction_name((mao_character_reaction_t)r));
            mao_character_react((mao_character_reaction_t)r);
        } else {
            for (int i = 0; i < MAO_CHAR_REACT_COUNT; i++) {
                ESP_LOGI(TAG, "dev: react %d = %s", i, mao_character_reaction_name((mao_character_reaction_t)i));
            }
        }
    } else if (value >= MAO_DEVCMD_EXPR_BASE) {
        const int idx = value - MAO_DEVCMD_EXPR_BASE;
        wake();
        mao_system_idle_kick(SLEEPY_TIMEOUT_MS);
        if (!mao_character_debug_expression(idx)) {
            for (int i = 0; i < mao_character_expression_count(); i++) {
                ESP_LOGI(TAG, "dev: state %d = %s", i, mao_character_expression_name(i));
            }
        } else {
            ESP_LOGI(TAG, "dev: state %s", mao_character_expression_name(idx));
        }
    } else if (value >= MAO_DEVCMD_LOOK_BASE) {
        if (!mao_character_debug_look(value - MAO_DEVCMD_LOOK_BASE)) {
            ESP_LOGW(TAG, "dev: look 0..%d", mao_character_look_count() - 1);
        }
    } else if (value >= MAO_DEVCMD_VIEW_BASE) {
        const mao_view_t v = (mao_view_t)(value - MAO_DEVCMD_VIEW_BASE);
        if (v == MAO_VIEW_PLACEHOLDER && mao_state()->view == MAO_VIEW_HOME) {
            go_view(MAO_VIEW_MENU);
        }
        go_view(v);
    } else if (value >= MAO_DEVCMD_ANIM_BASE) {
        const mao_character_preview_t p = (mao_character_preview_t)(value - MAO_DEVCMD_ANIM_BASE);
        wake();   /* previews show the awake character */
        go_view(MAO_VIEW_HOME);
        mao_character_debug_preview(p);
        mao_system_idle_kick(SLEEPY_TIMEOUT_MS);
    } else if (value == MAO_DEVCMD_REPLAY_BOOT) {
        go_view(MAO_VIEW_HOME);
        mao_ui_debug_replay_boot();
    } else if (value == MAO_DEVCMD_SNAP) {
        mao_display_snapshot_dump();
    } else if (value > MAO_DEVCMD_FLOOD_BASE) {
        const uint32_t seconds = (uint32_t)(value - MAO_DEVCMD_FLOOD_BASE);
        ESP_LOGI(TAG, "flood: 50 Hz ODD broadcasts for %" PRIu32 " s", seconds);
        mao_devices_debug_flood(seconds * 1000);
    } else if (value > MAO_DEVCMD_STRESS_BASE) {
        const uint32_t seconds = (uint32_t)(value - MAO_DEVCMD_STRESS_BASE);
        ESP_LOGI(TAG, "stress: continuous fast orbit for %" PRIu32 " s", seconds);
        wake();
        go_view(MAO_VIEW_HOME);
        mao_character_debug_dial(70.0f, seconds * 1000);
        mao_system_idle_kick(seconds * 1000 + SLEEPY_TIMEOUT_MS);
    }
}

/* ---------------------------------------------------------------------- */

static bool is_input(mao_event_type_t t)
{
    return t >= MAO_EVENT_INPUT_CW && t <= MAO_EVENT_INPUT_DOUBLE_CLICK;
}

static void on_event(const mao_event_t *ev, void *ctx)
{
    (void)ctx;
    const int64_t now = esp_timer_get_time();

    if (is_input(ev->type)) {
        mao_state_note_input(now);
        mao_system_idle_kick(SLEEPY_TIMEOUT_MS);
        wake();
        switch (mao_state()->view) {
        case MAO_VIEW_INTRO:       on_intro(ev); break;
        case MAO_VIEW_HOME:        on_home(ev, now); break;
        case MAO_VIEW_MENU:        on_menu(ev, now); break;
        case MAO_VIEW_PLACEHOLDER: on_placeholder(ev); break;
        case MAO_VIEW_DEVICES:     on_devices(ev, now); break;
        case MAO_VIEW_DEVICE:      on_device(ev, now); break;
        default: break;
        }
        return;
    }

    switch (ev->type) {
    case MAO_EVENT_SYSTEM_READY:
        mao_led_set_state(MAO_LED_STATE_OFF);   /* the LED is off at rest */
        mao_state_note_input(now);
        mao_system_idle_kick(SLEEPY_TIMEOUT_MS);
        if (mao_state()->view == MAO_VIEW_HOME) {
            mao_ui_boot();
        }
        break;
    case MAO_EVENT_IDLE_TIMEOUT:
        on_idle_timeout();
        break;
    case MAO_EVENT_DEV_COMMAND:
        on_dev_command(ev->value, now);
        break;
    case MAO_EVENT_DEVICE_FOUND:
    case MAO_EVENT_DEVICE_LOST:
    case MAO_EVENT_DEVICE_CHANGED:
        on_device_event(ev);
        break;
    default:
        break;
    }
}

esp_err_t mao_app_init(void)
{
    const mao_settings_t *cfg = mao_settings_get();
    const mao_view_t first_view = cfg->first_boot_done ? MAO_VIEW_HOME : MAO_VIEW_INTRO;
    mao_state_init(first_view);
    if (first_view == MAO_VIEW_INTRO) {
        ESP_LOGI(TAG, "first boot: showing the first encounter");
    }

    mao_audio_set_volume(cfg->volume);
    mao_character_set_detents_per_rev(mao_input_detents_per_rev());
    ESP_RETURN_ON_ERROR(mao_ui_init(first_view), TAG, "ui");
    ESP_RETURN_ON_ERROR(mao_event_subscribe(on_event, NULL), TAG, "subscribe");
    ESP_RETURN_ON_ERROR(mao_display_start(cfg->brightness), TAG, "display start");
    ESP_LOGI(TAG, "app ready: view %s, sleepy after %d s", mao_ui_view_name(first_view),
             CONFIG_MAO_SLEEPY_TIMEOUT_S);
    return ESP_OK;
}
