/*
 * DEVICE page relationship layer (M3.0). Decides which page a device gets and
 * owns the pages that are about the relationship rather than the device:
 *
 *   NEW      heard nearby, not part of MAO's setup: name, NEW, PAIR.
 *            No remote control is offered until PAIR (MAO product policy,
 *            not network security: the transport is not authenticated).
 *   OFFLINE  KNOWN but not reachable: name, OFFLINE, CONNECT. Relationship
 *            memory is not connectivity: no stale CAPTURE / LEVEL controls.
 *            CONNECT stays a deliberate attempt (it may fail honestly).
 *   CONTROL  KNOWN and online: the normal generic device page (mao_app.c).
 *
 * PAIR is one deliberate press. The device becomes KNOWN only after the
 * record is committed; the page then turns into the normal device page in
 * place. It never connects, identifies or controls anything by itself.
 *
 * FORGET is deliberate and confirmed: a quiet word one step below CONNECT,
 * then "FORGET / <NAME>?" with NO focused - YES needs a turn and a press.
 * Forgetting an online device turns its page back into NEW (MAO still hears
 * it); an offline one leaves DEVICES. Runs in the dispatcher task.
 */
#include "mao_app_priv.h"

#include <stdio.h>
#include "sdkconfig.h"

#include <inttypes.h>
#include "esp_log.h"
#include "mao_audio.h"
#include "mao_led.h"
#include "mao_rel.h"
#include "mao_system.h"
#include "mao_ui.h"

static const char *TAG = "MAO_APP";

static const char *s_note;      /* PAIR outcome on the NEW page ("FULL" / "NOT SAVED") */
static uint64_t s_note_dev;

typedef enum { SHEET_NONE = 0, SHEET_DETAILS, SHEET_CONFIRM } sheet_t;
static sheet_t s_sheet;
static int8_t s_sheet_focus;
static sheet_t s_confirm_back;      /* where NO returns to */
static int8_t s_off_focus;          /* OFFLINE page: 0 CONNECT, 1 relationship word */
static volatile uint8_t s_layout = 1;   /* 0 = A (FORGET word), 1 = B (INFO -> details) */

mao_devpage_t mao_devpage(uint64_t id, mao_world_entry_t *w)
{
    if (!id || !mao_world_get(id, w)) {
        return MAO_DEVPAGE_NONE;
    }
    if (!w->known) {
        return MAO_DEVPAGE_NEW;
    }
    return w->online ? MAO_DEVPAGE_CONTROL : MAO_DEVPAGE_OFFLINE;
}

void mao_rel_page_reset(void)
{
    s_note = NULL;
    s_sheet = SHEET_NONE;
    s_off_focus = 0;
}

const char *mao_rel_word(void)
{
    return s_layout == 0 ? "FORGET" : "INFO";
}

bool mao_rel_sheet_open(void)
{
    return s_sheet != SHEET_NONE;
}

static void open_confirm(sheet_t back)
{
    s_sheet = SHEET_CONFIRM;
    s_sheet_focus = 0;                   /* NO: a destructive answer needs a deliberate turn */
    s_confirm_back = back;
    ESP_LOGI(TAG, "relationship: forget? (NO focused)");
}

void mao_rel_word_activate(const mao_world_entry_t *w)
{
    (void)w;
    mao_audio_touch();
    if (s_layout == 0) {
        open_confirm(SHEET_NONE);
    } else {
        s_sheet = SHEET_DETAILS;
        s_sheet_focus = 0;
        ESP_LOGI(TAG, "relationship: details");
    }
    mao_app_dev_refresh();
}

void mao_rel_sheet_draw(const mao_world_entry_t *w)
{
    static char question[ODD_NAME_MAX + 2];
    mao_ui_sheet_t sh = { .on = s_sheet != SHEET_NONE };
    if (s_sheet == SHEET_DETAILS) {
        sh.line1 = "PAIRED";
        sh.line2 = odd_device_type_name(w->device_type);
        sh.words[0] = "FORGET";
        sh.word_count = 1;
        sh.focus = 0;
    } else if (s_sheet == SHEET_CONFIRM) {
        snprintf(question, sizeof(question), "%s?", w->name);
        sh.hide_title = true;
        sh.line1 = "FORGET";
        sh.line2 = question;
        sh.words[0] = "NO";
        sh.words[1] = "YES";
        sh.word_count = 2;
        sh.focus = s_sheet_focus;
    }
    mao_ui_device_sheet(&sh);
}

static void forget(const mao_world_entry_t *w)
{
    const esp_err_t err = mao_rel_forget(w->id);
    s_sheet = SHEET_NONE;
    mao_audio_back();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "'%s' stays part of MAO's setup (forget not saved)", w->name);
        mao_app_dev_refresh();
        return;
    }
    ESP_LOGI(TAG, "'%s' is no longer part of MAO's setup%s", w->name,
             w->online ? " (still nearby: NEW)" : "");
    if (w->online) {
        mao_app_dev_refresh();           /* the page becomes NEW in place */
    } else {
        mao_app_go_devices();            /* neither known nor heard: it leaves DEVICES */
    }
}

void mao_rel_sheet_input(const mao_world_entry_t *w, const mao_event_t *ev)
{
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW:
        if (s_sheet == SHEET_CONFIRM) {
            const int8_t f = ev->type == MAO_EVENT_INPUT_CW ? 1 : 0;
            if (f != s_sheet_focus) {
                s_sheet_focus = f;
                mao_audio_tick(160);
                ESP_LOGI(TAG, "relationship: forget? %s focused", f ? "YES" : "NO");
            }
        }
        break;
    case MAO_EVENT_INPUT_CLICK:
        if (s_sheet == SHEET_DETAILS) {
            mao_audio_touch();
            open_confirm(SHEET_DETAILS);
        } else if (s_sheet_focus == 1) {
            forget(w);
            return;
        } else {
            mao_audio_back();
            s_sheet = s_confirm_back;    /* NO: nothing happened */
            ESP_LOGI(TAG, "relationship: forget cancelled");
        }
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        mao_audio_back();                /* BACK: one level up, never a decision */
        s_sheet = s_sheet == SHEET_CONFIRM ? s_confirm_back : SHEET_NONE;
        ESP_LOGI(TAG, "relationship: back (%s)", s_sheet == SHEET_DETAILS ? "details" : "device page");
        break;
    default:
        break;                           /* double press: nothing */
    }
    mao_app_dev_refresh();
}

static void draw_offline(const mao_world_entry_t *w)
{
    (void)w;
    mao_ui_device_t m = {
        .title = w->name,
        .online = false,              /* the status word reads OFFLINE */
        .described = true,
        .no_centre = true,
        .focus = (int8_t)(1 + s_off_focus),   /* CONNECT or the relationship word */
        .rel_word = mao_rel_word(),
    };
    mao_ui_device_update(&m);
}

void mao_rel_page_draw(mao_devpage_t kind, const mao_world_entry_t *w)
{
    if (kind == MAO_DEVPAGE_OFFLINE) {
        draw_offline(w);
        return;
    }
    if (kind != MAO_DEVPAGE_NEW) {
        return;
    }
    const bool noted = s_note && s_note_dev == w->id;
    mao_ui_device_t m = {
        .title = w->name,
        .primary = "PAIR",            /* the one thing this page offers */
        .online = w->online,
        .described = true,
        .focus = 0,
        .connect_hidden = true,
        .status_l = noted ? s_note : "NEW",
        .status_r = w->online ? NULL : "OFFLINE",
    };
    mao_ui_device_update(&m);
}

static void pair(const mao_world_entry_t *w)
{
    mao_ui_device_feedback(MAO_UI_FB_PENDING);    /* a short persistence beat */
    const esp_err_t err = mao_rel_pair(w->id, w->name, w->device_type);
    if (err == ESP_OK) {
        s_note = NULL;
        ESP_LOGI(TAG, "'%s' is now part of MAO's setup", w->name);
        mao_ui_device_feedback(MAO_UI_FB_DONE);  /* the word resolves into the device's controls */
        mao_audio_confirm();
        mao_led_pulse(MAO_LED_PULSE_CONFIRM);
    } else {
        s_note = err == ESP_ERR_NO_MEM ? "FULL" : "NOT SAVED";
        s_note_dev = w->id;
        mao_ui_device_feedback(MAO_UI_FB_FAILED);
        mao_audio_back();
    }
    mao_app_dev_refresh();
}

bool mao_rel_page_input(mao_devpage_t kind, const mao_world_entry_t *w, const mao_event_t *ev, bool guarded)
{
    if (ev->type == MAO_EVENT_INPUT_LONG_PRESS) {
        mao_app_go_devices();                    /* BACK, as on every device page */
        return true;
    }
    if (kind == MAO_DEVPAGE_OFFLINE) {
        if (ev->type == MAO_EVENT_INPUT_CW || ev->type == MAO_EVENT_INPUT_CCW) {
            const int8_t f = ev->type == MAO_EVENT_INPUT_CW ? 1 : 0;
            if (f != s_off_focus) {
                s_off_focus = f;
                mao_audio_tick(160);
                ESP_LOGI(TAG, "offline page: %s focused", f ? mao_rel_word() : "CONNECT");
                mao_app_dev_refresh();
            }
        } else if (ev->type == MAO_EVENT_INPUT_CLICK && !guarded && s_off_focus == 1) {
            mao_rel_word_activate(w);
        } else if (ev->type == MAO_EVENT_INPUT_CLICK && !guarded) {
            /* The user's own intent: try to reach it (MAO's visit). */
            ESP_LOGI(TAG, "connect requested: '%s' (known, offline)", w->name);
            mao_ui_device_connect_hot(1.0f);
            mao_audio_confirm();
            mao_transfer_connect();
        }
        return true;
    }
    if (kind != MAO_DEVPAGE_NEW) {
        return kind == MAO_DEVPAGE_NONE;         /* nothing to operate on a vanished device */
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_PRESS:
        mao_ui_device_feedback(MAO_UI_FB_PRESS);
        break;
    case MAO_EVENT_INPUT_RELEASE:
        mao_ui_device_feedback(MAO_UI_FB_REST);
        break;
    case MAO_EVENT_INPUT_CLICK:
        if (guarded) {
            ESP_LOGI(TAG, "click ignored: page just opened");
        } else {
            pair(w);
        }
        break;
    default:
        break;                                   /* one word: the dial has nothing to move */
    }
    return true;
}

#if CONFIG_MAO_DEV_CONSOLE
static void dev_layout(char *arg)
{
    s_layout = arg && (arg[0] == 'a' || arg[0] == 'A') ? 0 : 1;
    ESP_LOGI(TAG, "relationship word layout %s", s_layout == 0 ? "A (FORGET after CONNECT)"
                                                             : "B (INFO -> details -> FORGET)");
    mao_event_post(MAO_EVENT_REL_CHANGED, 0);   /* redraw */
}
#endif

void mao_rel_init_dev(void)
{
#if CONFIG_MAO_DEV_CONSOLE
    mao_devcmd_register("rellayout", dev_layout);
#endif
}
