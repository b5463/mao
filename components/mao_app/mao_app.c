#include "mao_app.h"
#include "mao_app_priv.h"

#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
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
#include "mao_link.h"
#include "mao_rel.h"
#include "mao_world.h"
#include "mao_quirks.h"
#include "mao_power.h"

static const char *TAG = "MAO_APP";

#define SLEEPY_TIMEOUT_MS     (CONFIG_MAO_SLEEPY_TIMEOUT_S * 1000)
#define SLEEPY_BRIGHTNESS_PCT 35          /* of the preferred brightness */
#define DIAL_SETTLE_US        (700 * 1000)
#define MENU_BUMP_GAP_US      (250 * 1000)
#define SLEEPY_FADE_MS        2500        /* the light goes down slowly ... */
#define WAKE_FADE_MS          150         /* ... and comes back at once */
/* Longer rest - the sleeping screen, light sleep, the night - is
 * mao_app_power.c (the ladder: mao_power.h). */
#define TICK_FULL_DPS         60.0f       /* dial speed at which ticks are softest */

/* DEVICE page control focus: 0 = the value (LEVEL), 1 = POWER, 2 = ACTION.
 * Editing means the dial changes the LEVEL; otherwise it moves the focus. */
static int8_t s_dev_focus;
static bool s_dev_edit;
static esp_timer_handle_t s_settle_timer;   /* retires action feedback presentation */

/* Camera-use context (UI-local, never protocol): how the page is laid out,
 * what the current action is, and whether the user is in a shooting rhythm.
 * Only feedback INTENSITY depends on it - never safety or identity. */
static uint8_t s_cam_layout = 1;     /* 0 A: READY + STORAGE; 1 B: asymmetric (default); 2 C: n% */
/* Per device (registry slot, never reused within a boot): each device can have
 * its own action in flight (M3.2). */
static int32_t s_act_sem[MAO_DEVICES_MAX];     /* semantic of its action in flight / last resolved */
static bool s_act_via_centre[MAO_DEVICES_MAX]; /* invoked from the centre word (tool feedback owner) */
static int s_fb_slot = -1;           /* the device whose result the character is showing */
static int64_t s_dev_opened_us;      /* entry guard against a double tap's second click */
static int64_t s_list_opened_us;     /* the same guard for DEVICES, opened by a press on HOME */
static int64_t s_cap_last_done_us;
static uint8_t s_cap_streak;         /* routine captures in a row (habituation) */
static bool s_feedback_up;           /* the character currently borrows the page */
#define ENTRY_GUARD_US      (350 * 1000)
#define CAPTURE_RHYTHM_US   (6LL * 1000 * 1000)
#define MAGNET_QUIET_US     (1500 * 1000)
static void on_action_update(int32_t value);
static void on_ui_settle(void);

static mao_dial_speed_t s_logged_speed = MAO_DIAL_STILL;
static bool s_logged_reversing;
static int64_t s_last_dial_us;
static int64_t s_last_bump_us;

/* ---------------------------------------------------------------------- */
/* Device views: models are built here from the registry + capabilities.  */
/* ---------------------------------------------------------------------- */

/* The focusable controls of a device, in display order: the centre first
 * (level value, or the primary action), then the words row (POWER, then the
 * remaining actions). Focus is an index into this list. */
typedef struct {
    uint8_t kind;   /* 0 centre-level, 1 centre-action, 2 toggle word, 3 action word, 4 CONNECT,
                     * 6 relationship word (INFO / FORGET) */
    int idx;        /* caps[] index (levels/toggles) or action list index (actions) */
} devctl_t;

#define DEVCTL_MAX (4 + MAO_CONTROLS_MAX_ACTIONS)

static int build_controls(const mao_device_controls_t *ctl, devctl_t out[DEVCTL_MAX])
{
    int n = 0;
    if (ctl->level_idx >= 0) {
        out[n++] = (devctl_t) { .kind = 0, .idx = ctl->level_idx };
    } else if (ctl->primary_action >= 0) {
        out[n++] = (devctl_t) { .kind = 1, .idx = ctl->primary_action };
    }
    if (ctl->toggle_idx >= 0) {
        out[n++] = (devctl_t) { .kind = 2, .idx = ctl->toggle_idx };
    }
    for (int i = 0; i < ctl->action_count; i++) {
        if (i != ctl->primary_action || ctl->level_idx >= 0) {
            out[n++] = (devctl_t) { .kind = 3, .idx = i };
        }
    }
    /* CONNECT is MAO's own relationship action: always last, selectable like
     * any word (single press), so no gesture ever overloads the shutter. */
    out[n++] = (devctl_t) { .kind = 4, .idx = -1 };
    /* Relationship management: one step deeper, never next to the shutter. */
    out[n++] = (devctl_t) { .kind = 6, .idx = -1 };
    return n;
}

/* Control-list index -> the UI's focus numbering (0 centre, 1.. words,
 * words + 1 CONNECT, words + 2 relationship word). */
static int8_t ui_focus(const devctl_t *list, int n, int focus)
{
    int words = 0;
    for (int i = 0; i < n; i++) {
        words += list[i].kind == 2 || list[i].kind == 3;
    }
    words = words > MAO_UI_DEVICE_WORDS ? MAO_UI_DEVICE_WORDS : words;
    if (focus < 0 || focus >= n) {
        return 0;
    }
    switch (list[focus].kind) {
    case 0:
    case 1:
        return 0;
    case 4:
        return (int8_t)(words + 1);
    case 6:
        return (int8_t)(words + 2);
    default: {
        int w = 0;
        for (int i = 0; i < focus; i++) {
            w += list[i].kind == 2 || list[i].kind == 3;
        }
        return (int8_t)(w + 1);
    }
    }
}

/* The device says whether its primary operation is possible right now. */
static bool device_ready(const mao_device_t *dev, const mao_device_controls_t *ctl)
{
    return ctl->ready_idx < 0 || dev->caps[ctl->ready_idx].value != 0;
}

static esp_err_t invoke_action_checked(const mao_device_t *dev, const mao_device_controls_t *ctl, int list_idx)
{
    if (mao_link_state(dev->info.id) != MAO_LINK_SECURE) {
        /* Belt and braces: only a paired device proven this session is ever
         * controlled (the pages already offer nothing else). */
        ESP_LOGW(TAG, "refused: '%s' is not a proven paired device", dev->info.name);
        return ESP_ERR_INVALID_STATE;
    }
    /* Known-not-ready: answer locally, send nothing (§43); the remote stays
     * authoritative for races - a BUSY ACK resolves those. */
    if (ctl->action_sem[list_idx] == ODD_ACTION_CAPTURE && !device_ready(dev, ctl)) {
        ESP_LOGI(TAG, "not now: '%s' reports not ready", dev->info.name);
        mao_audio_back();
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = mao_devices_invoke_action(dev->info.id, dev->caps[ctl->action_idx[list_idx]].cap.id);
    const int slot = mao_devices_find(dev->info.id);
    if (err == ESP_OK && slot >= 0) {
        s_act_sem[slot] = ctl->action_sem[list_idx];
        s_act_via_centre[slot] = false;   /* the page sets it when the centre word invoked */
        mao_audio_touch();   /* the controller's own tiny cue - never a shutter sound */
        mao_led_pulse(MAO_LED_PULSE_CONFIRM);
    }
    return err;
}

static uint64_t s_sel_dev;   /* the selected device's identity (not its row) */
static mao_devpage_t s_page_kind;   /* the open page's kind, to settle focus when it changes */

static void refresh_devices_list(void)
{
    static mao_world_entry_t devs[MAO_WORLD_MAX];   /* dispatcher task only */
    uint64_t ids[MAO_UI_DEVICES_MAX] = { 0 };
    mao_ui_devices_t model = { 0 };
    /* One list: KNOWN devices (stable pair order, offline ones stay), then
     * nearby DISCOVERED ones (first-seen order). */
    const int n = mao_world_list(devs);
    for (int i = 0; i < n && model.count < MAO_UI_DEVICES_MAX; i++) {
        model.name[model.count] = devs[i].name;
        model.online[model.count] = devs[i].online;
        model.type[model.count] = devs[i].device_type;
        model.known[model.count] = devs[i].known;
        model.level[model.count] = -1;
        {
            mao_device_t d;
            mao_device_controls_t c;
            const int slot = mao_devices_find(devs[i].id);
            if (slot >= 0 && mao_devices_get(slot, &d)) {
                mao_device_controls(&d, &c);
                if (c.level_idx >= 0) {
                    const mao_device_cap_t *lc = &d.caps[c.level_idx];
                    const int32_t span = lc->cap.max - lc->cap.min;
                    model.level[model.count] = (int8_t)(span > 0 ? (lc->value - lc->cap.min) * 100 / span : 0);
                    model.power[model.count] = c.toggle_idx < 0 || d.caps[c.toggle_idx].value != 0;
                }
            }
        }
        const bool cannot_operate = devs[i].has_cred && devs[i].online &&
                                    (devs[i].compat == ODD_COMPAT_INCOMPATIBLE || devs[i].compat == ODD_COMPAT_INVALID);
        model.note[model.count] = devs[i].auth_failed ? 2
                                  : cannot_operate    ? (devs[i].compat == ODD_COMPAT_INCOMPATIBLE ? 3 : 4)
                                                      : (devs[i].known && !devs[i].has_cred && devs[i].online);
        ids[model.count] = devs[i].id;
        model.count++;
    }
    /* Selection sticks to the device, whatever announcements or state
     * refreshes do to the list around it. */
    int sel = -1;
    for (int i = 0; i < model.count; i++) {
        if (ids[i] == s_sel_dev) {
            sel = i;
        }
    }
    if (sel < 0) {
        sel = mao_state()->devices_index;
        sel = sel >= model.count ? (model.count > 0 ? model.count - 1 : 0) : (sel < 0 ? 0 : sel);
        s_sel_dev = model.count > 0 ? ids[sel] : 0;
    }
    mao_state_set_devices_index(sel);
    model.selected = sel;
    mao_ui_devices_update(&model);
}

/* List row -> the device it shows (0 = none). */
static uint64_t list_row_id(int row, int *count)
{
    mao_world_entry_t devs[MAO_WORLD_MAX];
    const int n = mao_world_list(devs);
    if (count) {
        *count = n;
    }
    return row >= 0 && row < n ? devs[row].id : 0;
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

static mao_hold_state_t s_hold = { .sel = -1 };

void mao_app_hold_reset(void)
{
    mao_hold_reset(&s_hold);
}

bool mao_app_hold_menu(int8_t *sel)
{
    if (sel) {
        *sel = s_hold.sel;
    }
    return s_hold.menu;
}

mao_hold_t mao_app_hold(const mao_event_t *ev, bool has_right, bool has_left)
{
    bool moved = false;
    const mao_hold_t r = mao_hold_step(&s_hold, ev->type, has_right, has_left, &moved);
    if (moved) {
        mao_audio_tick(120);
    }
    return r;
}

static void refresh_device_panel(void)
{
    mao_world_entry_t w;
    const mao_devpage_t kind = mao_devpage(mao_state()->device_id, &w);
    if (kind == MAO_DEVPAGE_NONE && mao_state()->view == MAO_VIEW_DEVICE) {
        /* The device is gone (a nearby, unpaired one left): nothing to show
         * - back to the list, rather than an empty or stale page. */
        ESP_LOGI(TAG, "device page: its device is gone, back to the list");
        mao_app_go_devices();
        return;
    }
    if (kind != s_page_kind) {
        /* PAIR / FORGET / reachability changed the page under the user:
         * focus settles on its first control (CAPTURE, LEVEL or PAIR). */
        s_page_kind = kind;
        s_dev_focus = 0;
        s_dev_edit = false;
        mao_rel_page_reset();
    }
    mao_rel_sheet_draw(&w);
    if (kind != MAO_DEVPAGE_CONTROL) {
        const mao_ui_dotpage_t off = { .on = false };
        mao_ui_device_dots(&off);
        mao_rel_page_draw(kind, &w);
        return;
    }
    mao_device_t dev;
    mao_device_controls_t ctl;
    if (!open_device(&dev, &ctl)) {
        return;
    }
    {
        /* M4.1: the page in dots. The field is the brightness, or the puck's
         * core is the primary action; the device's own business (identify,
         * sync test, ready, storage) is not shown. */
        const bool level = ctl.level_idx >= 0;
        int32_t pct = 0;
        if (level) {
            const mao_device_cap_t *c = &dev.caps[ctl.level_idx];
            const int32_t span = c->cap.max - c->cap.min;
            pct = span > 0 ? (c->value - c->cap.min) * 100 / span : 0;
        }
        const mao_ui_dotpage_t dm = {
            .on = true,
            .level = level,
            .level_pct = pct,
            .power = ctl.toggle_idx < 0 || dev.caps[ctl.toggle_idx].value != 0,
            .online = dev.online,
            .described = dev.described,
            .type = dev.info.device_type,
            .menu = s_hold.menu,
            .menu_sel = s_hold.sel,
            .opt_right = "CONNECT",
            .opt_left = mao_rel_word(),
            /* Storage stays the device's business - until it stops you. */
            .fact = ctl.storage_idx >= 0 && dev.caps[ctl.storage_idx].value <= 0 ? "FULL" : NULL,
            .name = dev.info.name,
        };
        mao_ui_device_dots(&dm);
    }
    devctl_t list[DEVCTL_MAX];
    const int n = build_controls(&ctl, list);
    if (s_dev_focus >= n) {
        s_dev_focus = 0;   /* the focused control vanished (capability change) */
        s_dev_edit = false;
    }
    mao_ui_device_t model = {
        .title = dev.info.name,
        .has_level = ctl.level_idx >= 0,
        .level = ctl.level_idx >= 0 ? dev.caps[ctl.level_idx].value : 0,
        .has_toggle = ctl.toggle_idx >= 0,
        .on = ctl.toggle_idx >= 0 ? dev.caps[ctl.toggle_idx].value != 0 : true,
        .online = dev.online,
        .problem = dev.link_problem,
        .described = dev.described,
        .focus = ui_focus(list, n, s_dev_focus),
        .editing = s_dev_edit,
        .rel_word = mao_rel_word(),
    };
    /* Everything below is derived from capabilities and generic semantic
     * names - no product knowledge anywhere. */
    static char storage_txt[16];
    for (int i = 0; i < n; i++) {
        if (list[i].kind == 4 || list[i].kind == 6) {
            continue;   /* CONNECT and the relationship word have their own places */
        }
        const char *label =
            list[i].kind == 2 ? "POWER" : odd_action_semantic_name(ctl.action_sem[list[i].idx]);
        if (i == 0 && list[i].kind == 1) {
            model.primary = label;
        } else if (i > 0 && model.word_count < MAO_UI_DEVICE_WORDS) {
            model.words[model.word_count++] = label;
        }
    }
    /* Facts: normal conditions are quiet, exceptions create information. */
    const mao_action_state_t ast = mao_devices_action_state(dev.info.id);
    const bool pending = ast == MAO_ACTION_SENDING || ast == MAO_ACTION_ACCEPTED;
    if (!dev.online) {
        if (ctl.ready_idx >= 0 || ctl.storage_idx >= 0) {
            model.status_l = "OFFLINE";
        }
    } else {
        if (ctl.ready_idx >= 0) {
            const bool ready = dev.caps[ctl.ready_idx].value != 0;
            model.centre_dim = !ready && !pending && ctl.primary_action >= 0;
            if (s_cam_layout == 0) {
                model.status_l = ready || pending ? "READY" : "NOT READY";
            } else if (!ready && !pending) {
                model.status_l = "NOT READY";   /* only the exception is shown */
            }
        }
        if (ctl.storage_idx >= 0) {
            const long v = (long)dev.caps[ctl.storage_idx].value;
            if (s_cam_layout == 2) {
                snprintf(storage_txt, sizeof(storage_txt), "%ld%%", v);
            } else {
                snprintf(storage_txt, sizeof(storage_txt), "STORAGE %ld", v);
            }
            model.status_r = storage_txt;
            model.status_r_emph = v <= 10;       /* running low: a little more present */
        }
    }
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

static int64_t s_view_since_us;   /* when the current view was entered (page idle) */
static void on_page_idle(int64_t now);
static void start_page_idle(void);

void mao_app_go_view(mao_view_t view)
{
    const mao_app_state_t *st = mao_state();
    if (st->view == view) {
        return;
    }
    ESP_LOGI(TAG, "view %s -> %s", mao_ui_view_name(st->view), mao_ui_view_name(view));
    if (st->view == MAO_VIEW_DEVICE) {
        mao_character_peek(false);
        s_feedback_up = false;
        s_dev_edit = false;
    }
    /* Discover quickly only while the user is looking at devices. */
    mao_devices_set_active(view == MAO_VIEW_DEVICES || view == MAO_VIEW_DEVICE);
    mao_state_set_view(view);
    s_view_since_us = esp_timer_get_time();
    if (view != MAO_VIEW_HOME && mao_app_tune_active()) {
        mao_app_tune_close();                     /* never left open behind another view */
    }
    mao_ui_home_hint(view == MAO_VIEW_HOME && mao_app_home_hint_allowed());
    if (view == MAO_VIEW_DEVICES || view == MAO_VIEW_DEVICE) {
        refresh_device_views();
    }
    mao_ui_show(view, st->menu_index);
}

void mao_app_apply_brightness(bool sleepy)
{
    const uint8_t pref = mao_settings_get()->brightness;
    uint8_t pct = sleepy ? (uint8_t)(pref * SLEEPY_BRIGHTNESS_PCT / 100) : pref;
    if (pct < 5) {
        pct = 5;
    }
    mao_display_fade_brightness(pct, sleepy ? SLEEPY_FADE_MS : WAKE_FADE_MS);
}

/* Quirks (M4.1): the moments for MAO's small hidden behaviours are found
 * here and in mao_app_home.c; mao_quirks decides whether one happens. */
static mao_quirks_t s_quirks;
static int64_t s_woke_us;
static uint8_t s_visit_caps;          /* frames taken on this device-page visit */

mao_quirks_t *mao_app_quirks(void)
{
    return &s_quirks;
}

int64_t mao_app_woke_us(void)
{
    return s_woke_us;
}

bool mao_app_quirk_roll(mao_qk_t k)
{
    return mao_quirks_roll(&s_quirks, k, (uint32_t)(esp_timer_get_time() / 1000), esp_random() % 1000u);
}

static void wake(void)
{
    if (!mao_state()->awake) {
        s_woke_us = esp_timer_get_time();
        mao_state_set_awake(true);
        mao_character_set_sleepy(false);
        mao_app_apply_brightness(false);
        ESP_LOGI(TAG, "awake");
    }
}

/* Classify the dial movement, log class changes, and give rotary audio
 * feedback thinned by speed so fast spins never become a buzz. */
mao_dial_motion_t mao_app_dial_motion(int32_t detents, int64_t now)
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
void mao_app_dial_tick(const mao_dial_motion_t *m)
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


static void on_menu(const mao_event_t *ev, int64_t now)
{
    const mao_app_state_t *st = mao_state();
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        const int32_t d = ev->type == MAO_EVENT_INPUT_CW ? ev->value : -ev->value;
        const mao_dial_motion_t m = mao_app_dial_motion(d, now);
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
            mao_app_dial_tick(&m);
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
        mao_app_go_view(mao_ui_menu_id(st->menu_index) == MAO_MENU_ID_DEVICES ? MAO_VIEW_DEVICES : MAO_VIEW_PLACEHOLDER);
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();
        mao_app_go_view(MAO_VIEW_HOME);
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
        mao_app_go_view(MAO_VIEW_MENU);
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();
        mao_app_go_view(MAO_VIEW_HOME);
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
        const mao_dial_motion_t m = mao_app_dial_motion(d, now);
        int count = 0;
        list_row_id(0, &count);
        int idx = st->devices_index + (int)d;
        idx = idx < 0 ? 0 : (idx > count - 1 ? count - 1 : idx);
        if (count > 0 && idx != st->devices_index) {
            mao_state_set_devices_index(idx);
            s_sel_dev = list_row_id(idx, NULL);
            refresh_devices_list();
            mao_app_dial_tick(&m);
        } else if (now - s_last_bump_us >= MENU_BUMP_GAP_US) {
            s_last_bump_us = now;
            mao_ui_devices_bump(d > 0 ? 1 : -1);
        }
        break;
    }
    case MAO_EVENT_INPUT_CLICK: {
        if (now - s_list_opened_us < ENTRY_GUARD_US) {
            ESP_LOGI(TAG, "click ignored: list just opened");
            break;
        }
        mao_world_entry_t dev;
        if (mao_world_get(list_row_id(st->devices_index, NULL), &dev)) {
            ESP_LOGI(TAG, "open device '%s' (%s)", dev.name, dev.known ? "known" : "new");
            mao_state_set_device(dev.id);
            if (dev.known) {
                mao_settings_note_last_device(dev.id);   /* the list starts here after a reboot */
            }
            mao_app_hold_reset();
            s_visit_caps = 0;
            if (mao_quirks_opened(&s_quirks, dev.id, (uint32_t)(now / 1000)) && mao_app_quirk_roll(MAO_QK_REVISIT)) {
                mao_character_quirk(MAO_QUIRK_CURIOUS);   /* this one again? */
            }
            s_dev_focus = 0;   /* the centre: the value, or the primary action */
            s_dev_edit = false;
            s_page_kind = mao_devpage(dev.id, &dev);
            mao_rel_page_reset();
            s_dev_opened_us = now;
            s_cap_streak = 0;
            mao_audio_confirm();
            mao_led_pulse(MAO_LED_PULSE_CONFIRM);
            mao_app_go_view(MAO_VIEW_DEVICE);
        }
        break;
    }
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();
        mao_app_go_view(MAO_VIEW_HOME);
        break;
    default:
        break;
    }
}

/* The generic device view: the dial drives the device's LEVEL-like
 * capability, a click toggles its POWER-like capability. Which capabilities
 * those are is decided by mao_device_controls() from what the device reports. */
/* The user takes the controls back: any decorative feedback yields NOW. */
static void interrupt_feedback(void)
{
    if (s_feedback_up) {
        if (s_settle_timer) {
            esp_timer_stop(s_settle_timer);
        }
        mao_character_peek(false);
        s_feedback_up = false;
    }
}

static bool centre_is_action(const mao_device_controls_t *ctl)
{
    return ctl->level_idx < 0 && ctl->primary_action >= 0 && s_dev_focus == 0;
}

/* The dot page (M4.1, reworked from use): the page is the device, nothing
 * else.
 *   LIGHT: turn = brightness (turning up a light that is off switches it
 *          on); press = on / off.
 *   action device: press = its primary action (the capture).
 *   hold: BACK / CONNECT (right) / FORGET (left) - mao_app_hold().
 * Returns true when it handled the event. */
static bool on_device_dots(const mao_event_t *ev, int64_t now, const mao_device_t *dev,
                           const mao_device_controls_t *ctl, const mao_world_entry_t *w)
{
    const bool level = ctl->level_idx >= 0;
    const bool action = !level && ctl->primary_action >= 0;
    s_dev_focus = 0;
    const bool was_menu = mao_app_hold_menu(NULL);
    switch (mao_app_hold(ev, true, true)) {
    case MAO_HOLD_EATEN:
        if (!was_menu && action) {
            mao_ui_device_feedback(MAO_UI_FB_REST);   /* not a capture after all */
        }
        refresh_device_panel();
        return true;
    case MAO_HOLD_BACK:
        mao_app_go_devices();
        return true;
    case MAO_HOLD_RIGHT:
        ESP_LOGI(TAG, "connect requested: '%s'", dev->info.name);
        mao_ui_device_connect_hot(1.0f);
        mao_audio_confirm();
        mao_transfer_connect();
        refresh_device_panel();
        return true;
    case MAO_HOLD_LEFT:
        mao_rel_word_activate(w);
        return true;
    default:
        break;
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        const int32_t d = ev->type == MAO_EVENT_INPUT_CW ? ev->value : -ev->value;
        const mao_dial_motion_t m = mao_app_dial_motion(d, now);
        if (action) {
            mao_ui_device_turn(d);                   /* turning does nothing here: the iris still answers */
        }
        if (level && dev->described && dev->online) {
            mao_app_lamp_turn(dev, ctl, d, &m);
            mao_settings_note_last_lamp(dev->info.id);   /* HOME's turn sets this one */
            refresh_device_panel();
            mao_app_dial_tick(&m);
        }
        return true;
    }
    case MAO_EVENT_INPUT_PRESS:
        if (action && dev->online) {
            mao_ui_device_feedback(MAO_UI_FB_PRESS);
        }
        return true;
    case MAO_EVENT_INPUT_RELEASE:
        if (action) {
            const mao_action_state_t st = mao_devices_action_state(dev->info.id);
            mao_ui_device_feedback(st == MAO_ACTION_SENDING || st == MAO_ACTION_ACCEPTED ? MAO_UI_FB_PENDING
                                                                                          : MAO_UI_FB_REST);
        }
        return true;
    case MAO_EVENT_INPUT_CLICK:
        if (!dev->described) {
            return true;
        }
        if (now - s_dev_opened_us < ENTRY_GUARD_US) {
            ESP_LOGI(TAG, "click ignored: page just opened");
            return true;
        }
        if (level) {
            if (ctl->toggle_idx >= 0 && dev->online) {
                const mao_device_cap_t *tc = &dev->caps[ctl->toggle_idx];
                const int32_t v = tc->value ? tc->cap.min : tc->cap.max;
                mao_devices_set_value(dev->info.id, tc->cap.id, v);
                if (v) {
                    mao_audio_confirm();
                } else {
                    mao_audio_back();
                }
                mao_led_pulse(MAO_LED_PULSE_CONFIRM);
            }
        } else if (action) {
            interrupt_feedback();
            const esp_err_t err = invoke_action_checked(dev, ctl, ctl->primary_action);
            if (err == ESP_OK && mao_devices_find(dev->info.id) >= 0) {
                s_act_via_centre[mao_devices_find(dev->info.id)] = true;
                mao_ui_device_feedback(MAO_UI_FB_PENDING);
            } else {
                mao_ui_device_feedback(MAO_UI_FB_BUSY);   /* not now: the core could not move */
            }
        }
        refresh_device_panel();
        return true;
    default:
        return true;                         /* double press: nothing */
    }
}

static void on_device(const mao_event_t *ev, int64_t now)
{
    mao_world_entry_t w;
    const mao_devpage_t kind = mao_devpage(mao_state()->device_id, &w);
    if (mao_rel_sheet_open() && kind != MAO_DEVPAGE_NONE) {
        mao_rel_sheet_input(&w, ev);     /* a question is open: it owns the knob */
        return;
    }
    if (kind != MAO_DEVPAGE_CONTROL) {
        /* Not part of the setup (or gone): only relationship words exist. */
        mao_rel_page_input(kind, &w, ev,
                           ev->type == MAO_EVENT_INPUT_CLICK && now - s_dev_opened_us < ENTRY_GUARD_US);
        return;
    }
    mao_device_t dev;
    mao_device_controls_t ctl;
    const bool ok = open_device(&dev, &ctl);
    if (ok && on_device_dots(ev, now, &dev, &ctl, &w)) {
        return;
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_PRESS:
        if (ok && centre_is_action(&ctl)) {
            mao_ui_device_feedback(MAO_UI_FB_PRESS);   /* the word answers the finger */
        }
        break;
    case MAO_EVENT_INPUT_RELEASE:
        if (ok && centre_is_action(&ctl)) {
            const mao_action_state_t st = mao_devices_action_state(dev.info.id);
            mao_ui_device_feedback(st == MAO_ACTION_SENDING || st == MAO_ACTION_ACCEPTED ? MAO_UI_FB_PENDING
                                                                                         : MAO_UI_FB_REST);
        }
        break;
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW: {
        const int32_t d = ev->type == MAO_EVENT_INPUT_CW ? ev->value : -ev->value;
        const mao_dial_motion_t m = mao_app_dial_motion(d, now);
        if (!ok || !dev.described) {
            break;
        }
        if (s_dev_edit && ctl.level_idx >= 0) {
            /* Editing: the physical encoder IS the control. */
            if (!dev.online) {
                break;
            }
            const mao_device_cap_t *c = &dev.caps[ctl.level_idx];
            int32_t v = c->value + d * c->cap.step;
            v = v < c->cap.min ? c->cap.min : (v > c->cap.max ? c->cap.max : v);
            if (v != c->value) {
                mao_devices_set_value(dev.info.id, c->cap.id, v);
                refresh_device_panel();
                mao_app_dial_tick(&m);
            }
            break;
        }
        /* Focus moves between the controls this device advertises. */
        devctl_t list[DEVCTL_MAX];
        const int n = build_controls(&ctl, list);
        int next = s_dev_focus + (int)d;
        next = next < 0 ? 0 : (next > n - 1 ? n - 1 : next);
        if (n > 0 && next != s_dev_focus) {
            s_dev_focus = (int8_t)next;
            refresh_device_panel();
            mao_app_dial_tick(&m);
        }
        break;
    }
    case MAO_EVENT_INPUT_CLICK:
        if (!ok || !dev.described) {
            break;
        }
        if (now - s_dev_opened_us < ENTRY_GUARD_US) {
            /* The second tap of a double tap that opened this page: it was
             * never meant for the shutter. */
            ESP_LOGI(TAG, "click ignored: page just opened");
            break;
        }
        devctl_t list[DEVCTL_MAX];
        const int n = build_controls(&ctl, list);
        if (s_dev_focus >= n) {
            break;
        }
        const devctl_t *c = &list[s_dev_focus];
        if (c->kind == 0) {
            /* The value: enter / leave editing. */
            s_dev_edit = !s_dev_edit;
            mao_audio_touch();
            refresh_device_panel();
        } else if (c->kind == 2 && dev.online) {
            const mao_device_cap_t *tc = &dev.caps[c->idx];
            const int32_t v = tc->value ? tc->cap.min : tc->cap.max;
            mao_devices_set_value(dev.info.id, tc->cap.id, v);
            refresh_device_panel();
            if (v) {
                mao_audio_confirm();
            } else {
                mao_audio_back();
            }
            mao_led_pulse(MAO_LED_PULSE_CONFIRM);
        } else if (c->kind == 1 || c->kind == 3) {
            /* One press = one action identity, sent at once: no character
             * "thinking" before a command (that beat belongs to CONNECT). */
            interrupt_feedback();
            const uint32_t t_release = ev->time_ms;
            const int64_t t_commit = esp_timer_get_time();
            const esp_err_t err = invoke_action_checked(&dev, &ctl, c->idx);
            if (err == ESP_OK && mao_devices_find(dev.info.id) >= 0) {
                s_act_via_centre[mao_devices_find(dev.info.id)] = c->kind == 1;
            }
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "%s: release -> committed %lld ms",
                         odd_action_semantic_name(ctl.action_sem[c->idx]),
                         (long long)(t_commit / 1000 - (int64_t)t_release));
                if (c->kind == 1) {
                    mao_ui_device_feedback(MAO_UI_FB_PENDING);
                }
            } else {
                ESP_LOGI(TAG, "%s refused locally (%s)", odd_action_semantic_name(ctl.action_sem[c->idx]),
                         err == ESP_ERR_INVALID_STATE && mao_devices_action_state(dev.info.id) != MAO_ACTION_SENDING &&
                         mao_devices_action_state(dev.info.id) != MAO_ACTION_ACCEPTED ? "not ready" : "still working");
                if (c->kind == 1) {
                    /* Still working / not ready: the word could not move. */
                    mao_ui_device_feedback(MAO_UI_FB_BUSY);
                }
            }
        } else if (c->kind == 6) {
            mao_rel_word_activate(&w);
        } else if (c->kind == 4) {
            /* CONNECT: MAO visits the device. Works offline too - the attempt
             * fails honestly (the failed escape), which is the answer. */
            ESP_LOGI(TAG, "connect requested: '%s'", dev.info.name);
            mao_ui_device_connect_hot(1.0f);
            mao_audio_confirm();
            mao_transfer_connect();
        }
        break;
    case MAO_EVENT_INPUT_DOUBLE_CLICK:
        /* Deliberately nothing on a device page: every CLICK has already
         * acted, so a double gesture here could only mean "two clicks". */
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();
        mao_app_go_view(MAO_VIEW_DEVICES);
        break;
    default:
        break;
    }
}

/* Which devices MAO has met this session, and when they were last lost.
 * A genuinely new device is a strong stimulus (analytical curiosity); a
 * device coming back is worth a look proportional to how long it was gone;
 * repeated announcements are almost invisible (habituation in the mind). */
typedef struct {
    uint64_t id;
    uint32_t lost_ms;   /* 0 = currently around */
} met_device_t;
static met_device_t s_met[MAO_DEVICES_MAX];

static met_device_t *met(uint64_t id)
{
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        if (s_met[i].id == id) {
            return &s_met[i];
        }
    }
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        if (!s_met[i].id) {
            s_met[i].id = id;
            s_met[i].lost_ms = 0;
            return &s_met[i];
        }
    }
    s_met[0] = (met_device_t) { .id = id };
    return &s_met[0];
}

#define BACK_IS_NEWS_MS (60 * 1000)   /* gone longer than this = worth a look */

static void on_device_event(const mao_event_t *ev, int64_t now_us)
{
    const uint32_t now = (uint32_t)(now_us / 1000);
    mao_device_t dev;
    const bool have = mao_devices_get((int)ev->value, &dev);
    if (have && ev->type != MAO_EVENT_DEVICE_LOST) {
        /* Live ANNOUNCE is authoritative: a KNOWN device that was renamed or
         * reflashed as another profile keeps its relationship (device_id),
         * and its fallback name/type follow (written only when they change). */
        mao_rel_note_live(dev.info.id, dev.info.name, dev.info.device_type);
    }
    if (have && ev->type != MAO_EVENT_DEVICE_CHANGED && mao_rel_is_known(dev.info.id)) {
        ESP_LOGI(TAG, "known device %s: '%s'", ev->type == MAO_EVENT_DEVICE_FOUND ? "online" : "offline",
                 dev.info.name);
    }
    if (ev->type == MAO_EVENT_DEVICE_FOUND && have) {
        bool known = false;
        for (int i = 0; i < MAO_DEVICES_MAX; i++) {
            known = known || s_met[i].id == dev.info.id;
        }
        met_device_t *m = met(dev.info.id);
        const uint32_t gone = m->lost_ms ? now - m->lost_ms : 0;
        m->lost_ms = 0;
        ESP_LOGI(TAG, "device available: '%s'%s", dev.info.name,
                 !known ? " (new)" : (gone > BACK_IS_NEWS_MS ? " (long absence)" : ""));
        mao_audio_notice();
        mao_led_pulse(MAO_LED_PULSE_NOTICE);
        /* The character only stirs while someone is around to see it. */
        if (mao_state()->awake) {
            if (!known) {
                mao_character_react(MAO_CHAR_REACT_DEVICE_ON);    /* analytical curiosity */
            } else if (mao_state()->view == MAO_VIEW_HOME && mao_state_idle_ms(now_us) > 15000 &&
                       mao_app_quirk_roll(MAO_QK_OFFSCREEN)) {
                /* idle at HOME, a known device returns: MAO looks past the rim, towards it */
                mao_character_quirk((esp_random() & 1) ? MAO_QUIRK_OFFSCREEN_LEFT : MAO_QUIRK_OFFSCREEN_RIGHT);
            } else if (gone > BACK_IS_NEWS_MS) {
                mao_character_react(MAO_CHAR_REACT_ATTEND);       /* a glance: "you again" */
            } else {
                mao_character_react(MAO_CHAR_REACT_NOTICE);       /* barely registered */
            }
        }
    } else if (ev->type == MAO_EVENT_DEVICE_LOST && have) {
        met(dev.info.id)->lost_ms = now;
        ESP_LOGI(TAG, "device gone: '%s'", dev.info.name);
        mao_transfer_device_lost(dev.info.id);
        if (mao_state()->awake && !mao_transfer_active()) {
            mao_character_react(MAO_CHAR_REACT_DEVICE_OFF);       /* confirm, move on */
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
    if (mao_transfer_active()) {
        mao_system_idle_kick(SLEEPY_TIMEOUT_MS);   /* no dozing off mid-transfer */
        return;
    }
    mao_state_note_idle();
    if (st->awake) {
        ESP_LOGI(TAG, "sleepy after %d s without input", CONFIG_MAO_SLEEPY_TIMEOUT_S);
    }
    mao_app_doze();
}

void mao_app_doze(void)
{
    const mao_app_state_t *st = mao_state();
    if (st->view != MAO_VIEW_HOME) {
        mao_app_go_view(MAO_VIEW_HOME);
    }
    if (st->awake) {
        mao_state_set_awake(false);
        mao_character_set_sleepy(true);
        mao_app_apply_brightness(true);
        mao_led_set_state(MAO_LED_STATE_OFF);
    }
}

void mao_app_wake_now(int64_t now)
{
    mao_state_note_input(now);
    mao_system_idle_kick(SLEEPY_TIMEOUT_MS);
    wake();
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
    } else if (value == MAO_DEVCMD_PERF_BURST) {
        mao_display_perf_burst(3000);
    } else if (value == MAO_DEVCMD_ODD_INCARNATION) {
        ESP_LOGI(TAG, "MAO ODD incarnation = %016llx", (unsigned long long)mao_devices_incarnation());
    } else if (value == MAO_DEVCMD_STALE_SET) {
        mao_devices_debug_stale_set();
    } else if (value == MAO_DEVCMD_ACTION_DUMP) {
        mao_devices_action_dump();
    } else if (value == MAO_DEVCMD_ACTION_INVOKE) {
        for (int i = 0; i < MAO_DEVICES_MAX; i++) {
            mao_device_t dv;
            mao_device_controls_t ct;
            if (mao_devices_get(i, &dv)) {
                mao_device_controls(&dv, &ct);
                if (ct.action_count > 0 && mao_link_state(dv.info.id) != MAO_LINK_SECURE) {
                    ESP_LOGW(TAG, "dev: odd-action refused for '%s' (not a proven paired device)", dv.info.name);
                    continue;
                }
                if (ct.action_count > 0) {
                    const int pick = ct.primary_action >= 0 ? ct.primary_action : 0;
                    ESP_LOGI(TAG, "dev: invoking %s on '%s'",
                             odd_action_semantic_name(ct.action_sem[pick]), dv.info.name);
                    mao_devices_invoke_action(dv.info.id, dv.caps[ct.action_idx[pick]].cap.id);
                    break;
                }
            }
        }
    } else if (value == MAO_DEVCMD_ODD_SELFTEST) {
        odd_bus_selftest();
    } else if (value == MAO_DEVCMD_ODD_RESET) {
        mao_devices_reset_latency();
        ESP_LOGI(TAG, "latency statistics reset");
    } else if (value >= MAO_DEVCMD_SEQSEED_BASE) {
        odd_bus_debug_set_seq((uint16_t)(value - MAO_DEVCMD_SEQSEED_BASE));
    } else if (value >= MAO_DEVCMD_DIAL_BASE) {
        /* dial: value = base + dps * 1000 + seconds (synthetic detents to the character only) */
        const int32_t v = value - MAO_DEVCMD_DIAL_BASE;
        const float dps = (float)(v / 1000) - 500.0f;
        const uint32_t seconds = (uint32_t)(v % 1000);
        ESP_LOGI(TAG, "dev: dial %.0f detents/s for %" PRIu32 " s", (double)dps, seconds);
        wake();
        mao_app_go_view(MAO_VIEW_HOME);
        mao_character_debug_dial(dps, seconds * 1000);
        mao_system_idle_kick(seconds * 1000 + SLEEPY_TIMEOUT_MS);
    } else if (value >= MAO_DEVCMD_CAMLAYOUT_BASE) {
        s_cam_layout = (uint8_t)(value - MAO_DEVCMD_CAMLAYOUT_BASE);
        ESP_LOGI(TAG, "dev: device-page facts layout %c", 'A' + s_cam_layout);
        refresh_device_views();
    } else if (value >= MAO_DEVCMD_NOVELTY_BASE) {
        mao_character_debug_novelty((uint8_t)(value - MAO_DEVCMD_NOVELTY_BASE));
    } else if (value >= MAO_DEVCMD_INTEREST_BASE) {
        wake();
        mao_character_debug_interest((uint8_t)(value - MAO_DEVCMD_INTEREST_BASE));
    } else if (value >= MAO_DEVCMD_TRANSFER_BASE) {
        mao_transfer_devcmd(value - MAO_DEVCMD_TRANSFER_BASE);
    } else if (value >= MAO_DEVCMD_REACT_BASE) {
        const int r = value - MAO_DEVCMD_REACT_BASE;
        if (r < MAO_CHAR_REACT_COUNT) {
            wake();
            mao_app_go_view(MAO_VIEW_HOME);
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
            mao_app_go_view(MAO_VIEW_MENU);
        }
        mao_app_go_view(v);
    } else if (value >= MAO_DEVCMD_ANIM_BASE) {
        const mao_character_preview_t p = (mao_character_preview_t)(value - MAO_DEVCMD_ANIM_BASE);
        wake();   /* previews show the awake character */
        mao_app_go_view(MAO_VIEW_HOME);
        mao_character_debug_preview(p);
        mao_system_idle_kick(SLEEPY_TIMEOUT_MS);
    } else if (value == MAO_DEVCMD_REPLAY_BOOT) {
        mao_app_go_view(MAO_VIEW_HOME);
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
        mao_app_go_view(MAO_VIEW_HOME);
        mao_character_debug_dial(70.0f, seconds * 1000);
        mao_system_idle_kick(seconds * 1000 + SLEEPY_TIMEOUT_MS);
    }
}

/* ---------------------------------------------------------------------- */

static bool is_input(mao_event_type_t t)
{
    return t >= MAO_EVENT_INPUT_CW && t <= MAO_EVENT_INPUT_DOUBLE_CLICK;
}

/* A dimmed or resting MAO (M4.1): the first touch only wakes it - a press
 * is eaten whole (held, turned while held, its release and click), the
 * waking turn is eaten - so it can never open a list or change a light in
 * the dark by accident (mao_power.h: mao_wake_eat). */

static void on_event(const mao_event_t *ev, void *ctx)
{
    (void)ctx;
    const int64_t now = esp_timer_get_time();

    if (is_input(ev->type)) {
        const bool dimmed = !mao_state()->awake;
        mao_state_note_input(now);
        mao_system_idle_kick(SLEEPY_TIMEOUT_MS);
        mao_app_power_input();
        wake();
        if (mao_app_power_eat(ev->type, dimmed)) {
            return;   /* the touch that woke MAO only woke it */
        }
        if (mao_transfer_input(ev)) {
            return;   /* a transfer is running; it decides what input means */
        }
        switch (mao_state()->view) {
        case MAO_VIEW_INTRO:       on_intro(ev); break;
        case MAO_VIEW_HOME:        mao_app_on_home(ev, now); break;
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
        start_page_idle();
        mao_led_set_state(MAO_LED_STATE_OFF);   /* the LED is off at rest */
        mao_state_note_input(now);
        mao_system_idle_kick(SLEEPY_TIMEOUT_MS);
        if (mao_state()->view == MAO_VIEW_HOME && mao_app_power_from_deep()) {
            mao_ui_wake_boot();                   /* MAO's own deep sleep ended: the short wake */
            mao_app_apply_brightness(false);
            mao_ui_home_hint(mao_app_home_hint_allowed());
        } else if (mao_state()->view == MAO_VIEW_HOME) {
            mao_ui_boot();
            mao_ui_home_hint(mao_app_home_hint_allowed());
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
        on_device_event(ev, now);
        break;
    case MAO_EVENT_TRANSFER_STEP:
        mao_transfer_step(ev->value);
        break;
    case MAO_EVENT_DEVICE_PROBED:
        mao_transfer_probed(ev->value);
        break;
    case MAO_EVENT_ACTION_UPDATE:
        on_action_update(ev->value);
        break;
    case MAO_EVENT_UI_SETTLE:
        on_ui_settle();
        break;
    case MAO_EVENT_REL_CHANGED:
        refresh_device_views();
        break;
    case MAO_EVENT_LINK_CHANGED:
        mao_rel_on_link_event(ev->value);
        break;
    case MAO_EVENT_PAGE_IDLE:
        on_page_idle(now);
        break;
    case MAO_EVENT_HOME_QUIET:
        if (mao_state()->view == MAO_VIEW_HOME && mao_state()->awake) {
            mao_app_home_quiet(now);
        }
        break;
    case MAO_EVENT_POWER_RESTED:
        mao_app_power_rested();
        break;
    case MAO_EVENT_POWER_WAKE:
        mao_app_power_woken(ev->value, now);
        break;
    case MAO_EVENT_POWER_DEEP:
        mao_app_power_deep((uint32_t)ev->value);
        break;
    default:
        break;
    }
}

/* A page left alone (M4.1): a device page goes back to the list after a
 * minute without input - a brushed knob must not change a light nobody is
 * looking at - and the list goes back HOME. Never mid-question, mid-pairing
 * or mid-transfer. */
#define PAGE_IDLE_MS 60000

static void on_page_idle(int64_t now)
{
    const mao_app_state_t *st = mao_state();
    mao_app_power_tick(now);                      /* the longer rest */
    if (!st->awake) {
        return;                                   /* asleep: nothing else to tidy */
    }
    if (mao_app_tune_idle(now)) {
        return;
    }
    if (mao_state_idle_ms(now) < PAGE_IDLE_MS || now - s_view_since_us < (int64_t)PAGE_IDLE_MS * 1000 ||
        mao_transfer_active() || mao_rel_sheet_open()) {
        return;
    }
    mao_link_pair_status_t ps;
    mao_link_pair_status(&ps);
    if (ps.st != ODL_C_IDLE) {
        return;
    }
    if (st->view == MAO_VIEW_DEVICE) {
        ESP_LOGI(TAG, "device page left alone: back to the list");
        mao_app_hold_reset();
        mao_app_go_view(MAO_VIEW_DEVICES);
    } else if (st->view == MAO_VIEW_DEVICES) {
        ESP_LOGI(TAG, "list left alone: home");
        mao_app_go_view(MAO_VIEW_HOME);
    }
}

static void page_idle_cb(void *arg)
{
    (void)arg;
    mao_event_post(MAO_EVENT_PAGE_IDLE, 0);
}

static void start_page_idle(void)
{
    static esp_timer_handle_t t;
    if (!t) {
        const esp_timer_create_args_t args = { .callback = page_idle_cb, .name = "mao_pageidle" };
        if (esp_timer_create(&args, &t) != ESP_OK) {
            return;
        }
        esp_timer_start_periodic(t, 5000 * 1000);
    }
}

static void settle_cb(void *arg)
{
    (void)arg;
    mao_event_post(MAO_EVENT_UI_SETTLE, 0);
}

static void schedule_settle(uint32_t ms)
{
    if (!s_settle_timer) {
        const esp_timer_create_args_t args = { .callback = settle_cb, .name = "mao_settle" };
        if (esp_timer_create(&args, &s_settle_timer) != ESP_OK) {
            return;
        }
    }
    esp_timer_stop(s_settle_timer);
    esp_timer_start_once(s_settle_timer, (uint64_t)ms * 1000);
}

/* Action transaction progress -> restrained punctuation. Two layers:
 * the TOOL answers first (the centre word's microstates), the CHARACTER is
 * interruptible and secondary. Routine captures habituate: the first gets a
 * brief look, the next a shorter one, then mostly the tool alone. Exceptions
 * (BUSY, FAILED, UNKNOWN) still earn the eyes. Nothing here can block input:
 * any new press retires the feedback at once (interrupt_feedback). */
static void show_feedback(mao_character_reaction_t r, uint32_t hold_ms)
{
    const mao_view_t v = mao_state()->view;
    if (v == MAO_VIEW_DEVICE) {
        mao_character_peek(true);
        s_feedback_up = true;
        mao_character_react(r);
        schedule_settle(hold_ms);
    } else if (v == MAO_VIEW_HOME) {
        mao_character_react(r);   /* the user left: a small note where they are */
    }
    /* Elsewhere (menus, lists): silent state resolution - never hijack. */
}

static void on_action_update(int32_t value)
{
    const mao_action_state_t st = (mao_action_state_t)(value & 0x0F);
    const int slot = value >> 4;
    if (slot < 0 || slot >= MAO_DEVICES_MAX) {
        return;
    }
    /* A result belongs to its device. While another device's page is open it
     * only updates the world (the page redraws from it); it never borrows this
     * page's word or the character (M3.2). */
    const bool on_page = mao_state()->view == MAO_VIEW_DEVICE;
    const bool visible = !on_page || mao_devices_find(mao_state()->device_id) == slot;
    if (!visible) {
        refresh_device_views();
        return;
    }
    const bool capture = s_act_sem[slot] == ODD_ACTION_CAPTURE;
    const bool tool = capture && s_act_via_centre[slot] && on_page;
    s_fb_slot = slot;
    switch (st) {
    case MAO_ACTION_ACCEPTED:
        if (!capture) {
            show_feedback(MAO_CHAR_REACT_ACK, 2000);   /* resolved by the result */
        }
        /* CAPTURE: the held word already says "underway". */
        break;
    case MAO_ACTION_DONE:
        if (tool) {
            mao_ui_device_feedback(MAO_UI_FB_DONE);
        }
        if (capture) {
            mao_audio_shutter();                 /* heard as well as seen: it was taken */
        }
        if (capture) {
            const int64_t now = esp_timer_get_time();
            s_cap_streak = (s_cap_last_done_us && now - s_cap_last_done_us < CAPTURE_RHYTHM_US)
                               ? (uint8_t)(s_cap_streak < 9 ? s_cap_streak + 1 : 9) : 0;
            s_cap_last_done_us = now;
            s_visit_caps = s_visit_caps < 255 ? (uint8_t)(s_visit_caps + 1) : 255;
            if (s_visit_caps == MAO_QK_SERIES_N && mao_app_quirk_roll(MAO_QK_SERIES)) {
                /* a whole strip of them: it looks at the frames, pleased */
                show_feedback(MAO_CHAR_REACT_DONE, 1400);
                mao_character_attend(0, 118);
                mao_character_quirk(MAO_QUIRK_DELIGHT);
            } else if (mao_app_quirk_roll(MAO_QK_DELIGHT)) {
                show_feedback(MAO_CHAR_REACT_DONE, 1000);   /* rare: a golden glint instead of the nod */
                mao_character_quirk(MAO_QUIRK_DELIGHT);
                mao_audio_notice();                         /* and, for once, a note */
            } else if (s_cap_streak == 0) {
                show_feedback(MAO_CHAR_REACT_DONE, 800);     /* "Yes. Done." */
            } else if (s_cap_streak == 1) {
                show_feedback(MAO_CHAR_REACT_DONE, 480);     /* shorter */
            } else if ((esp_random() % 100) < 12) {
                show_feedback(MAO_CHAR_REACT_DONE, 420);     /* the occasional glance */
            }
            /* Otherwise the word's settle is the whole answer: working with
             * you, not commenting on every photograph. */
        } else {
            show_feedback(MAO_CHAR_REACT_DONE, 1200);
        }
        break;
    case MAO_ACTION_BUSY:
        if (tool) {
            mao_ui_device_feedback(MAO_UI_FB_BUSY);
        }
        show_feedback(MAO_CHAR_REACT_BUSY, capture ? 900 : 1800);
        break;
    case MAO_ACTION_FAILED:
        if (tool) {
            mao_ui_device_feedback(MAO_UI_FB_FAILED);
        }
        show_feedback(MAO_CHAR_REACT_FAIL, capture ? 1300 : 2200);
        break;
    case MAO_ACTION_UNKNOWN:
        if (tool) {
            mao_ui_device_feedback(MAO_UI_FB_UNSURE);
        }
        show_feedback(MAO_CHAR_REACT_UNSURE, 1500);   /* hold, second look, settle */
        break;
    default:
        break;
    }
    /* Magnetism: after a secondary action resolves, the primary action takes
     * focus back - but only if the user is not steering right now. */
    if ((st == MAO_ACTION_DONE || st == MAO_ACTION_FAILED || st == MAO_ACTION_BUSY) && !capture &&
        mao_state()->view == MAO_VIEW_DEVICE && esp_timer_get_time() - s_last_dial_us > MAGNET_QUIET_US) {
        s_dev_focus = 0;
    }
    refresh_device_views();
}

static void on_ui_settle(void)
{
    mao_device_t fb;
    const mao_action_state_t st = s_fb_slot >= 0 && mao_devices_get(s_fb_slot, &fb)
                                      ? mao_devices_action_state(fb.info.id) : MAO_ACTION_IDLE;
    if (st == MAO_ACTION_BUSY) {
        mao_character_react(MAO_CHAR_REACT_IDLE);   /* end the held busy loop */
    }
    if (st != MAO_ACTION_SENDING && st != MAO_ACTION_ACCEPTED) {
        mao_character_peek(false);                  /* the typography returns */
        s_feedback_up = false;
    }
}

void mao_app_note_list_opened(int64_t now)
{
    s_list_opened_us = now;                       /* the entry guard for the press that opened it */
}

void mao_app_dev_refresh(void)
{
    refresh_device_views();
}

void mao_app_go_devices(void)
{
    mao_audio_back();
    mao_app_go_view(MAO_VIEW_DEVICES);
}

void mao_app_back_to_device(uint64_t id)
{
    mao_world_entry_t w;
    if (!mao_world_get(id, &w)) {
        return;                              /* gone meanwhile: stay home */
    }
    wake();
    mao_state_set_device(id);
    mao_app_hold_reset();
    s_dev_focus = 0;
    s_page_kind = mao_devpage(id, &w);
    s_dev_opened_us = esp_timer_get_time();
    mao_app_go_view(MAO_VIEW_DEVICE);
    refresh_device_panel();
}

void mao_app_go_home(void)
{
    wake();
    if (mao_state()->view != MAO_VIEW_HOME) {
        mao_app_go_view(MAO_VIEW_HOME);
    }
}

#if CONFIG_MAO_DEV_CONSOLE
/* Development: "quirk <name>" plays a quirk now (its rarity and cooldown are
 * the app's; this only asks the face) - for review and tests. */
static void dev_quirk(char *arg)
{
    for (int q = 0; q < MAO_QUIRK_COUNT; q++) {
        if (arg && strcmp(arg, mao_character_quirk_name((mao_character_quirk_t)q)) == 0) {
            ESP_LOGI(TAG, "dev: quirk %s", arg);
            mao_character_quirk((mao_character_quirk_t)q);
            return;
        }
    }
    ESP_LOGW(TAG, "quirk <hold|forgive|curious|delight|offscreen-left|offscreen-right|grumpy>");
}
#endif

esp_err_t mao_app_init(void)
{
    const mao_settings_t *cfg = mao_settings_get();
    const mao_view_t first_view = cfg->first_boot_done ? MAO_VIEW_HOME : MAO_VIEW_INTRO;
    mao_state_init(first_view);
    if (first_view == MAO_VIEW_INTRO) {
        ESP_LOGI(TAG, "first boot: showing the first encounter");
    }

    mao_audio_set_volume(cfg->volume);
    s_sel_dev = cfg->last_device;              /* the list opens on the device used last */
    mao_character_set_detents_per_rev(mao_input_detents_per_rev());
    ESP_RETURN_ON_ERROR(mao_ui_init(first_view), TAG, "ui");
    mao_rel_init_dev();
    mao_quirks_reset(&s_quirks);
    const bool from_deep = mao_app_power_init();
    mao_app_power_register();
#if CONFIG_MAO_DEV_CONSOLE
    mao_devcmd_register("quirk", dev_quirk);   /* test hook: a quirk now, past its rarity */
#endif
    ESP_RETURN_ON_ERROR(mao_event_subscribe(on_event, NULL), TAG, "subscribe");
    /* after MAO's own deep sleep the light comes up from the sleeping level */
    ESP_RETURN_ON_ERROR(mao_display_start(from_deep ? MAO_PWR_SLEEP_PCT : cfg->brightness), TAG, "display start");
    ESP_LOGI(TAG, "app ready: view %s, sleepy after %d s", mao_ui_view_name(first_view),
             CONFIG_MAO_SLEEPY_TIMEOUT_S);
    return ESP_OK;
}
