/*
 * DEVICE page relationship + security layer (M3.0, M3.1). Decides which
 * page a device gets and owns the pages that are about the relationship
 * rather than the device (docs/device_relationships.md, docs/link_security.md):
 *
 *   NEW      heard nearby, not part of MAO's setup: name, NEW, PAIR.
 *   VERIFY   remembered (e.g. an M3.0 relationship) but never secured: VERIFY.
 *   REPAIR   paired, heard, but it could not prove the stored identity
 *            (wrong key, lost credential, another controller): REPAIR.
 *   OFFLINE  paired / remembered, not reachable: OFFLINE, CONNECT, INFO.
 *   CONTROL  paired and proven this session: the normal device page.
 *
 * PAIR / VERIFY / REPAIR run the same ceremony (mao_link): PAIRING, then the
 * six-digit code with CANCEL / MATCH (CANCEL focused), then CONFIRM ON
 * DEVICE, then the normal page in place. Nothing is remembered or trusted
 * before the ceremony's commit point. No character performance: the words
 * carry it.
 *
 * FORGET is deliberate and confirmed; for a proven device it first revokes
 * MAO's authorization on the device (bounded), then erases everything here.
 * Runs in the dispatcher task.
 */
#include "mao_app_priv.h"

#include <inttypes.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "mao_audio.h"
#include "mao_devices.h"
#include "mao_led.h"
#include "mao_link.h"
#include "mao_rel.h"
#include "mao_system.h"
#include "mao_ui.h"

static const char *TAG = "MAO_APP";

static const char *s_note;      /* outcome of the last ceremony on this page */
static uint64_t s_note_dev;

typedef enum { SHEET_NONE = 0, SHEET_DETAILS, SHEET_CONFIRM, SHEET_FORGETTING } sheet_t;
static sheet_t s_sheet;
static int8_t s_sheet_focus;       /* CONFIRM: the knob's place, -1 FORGET / 0 middle / +1 KEEP */
static sheet_t s_confirm_back;      /* where NO returns to */
static int8_t s_off_focus;          /* OFFLINE page: 0 CONNECT, 1 relationship word */
/* 0 = A (FORGET -> confirm), 1 = B (INFO -> details -> FORGET). M4.1: A - the
 * details sheet said nothing the page does not (PAIRED, the type). */
static volatile uint8_t s_layout = 0;
static uint64_t s_forget_dev;       /* a revocation in flight for this device */
static bool s_forget_online;
static int8_t s_cer_focus;          /* SAS screen: the knob's place, -1 CANCEL / 0 middle / +1 MATCH */

/* A two-answer question starts with the knob in the middle: neither answer
 * is a press away (M4.1). Before, CANCEL / NO were focused, and the natural
 * reaction - press to agree - cancelled the pairing. The destructive or
 * trusting answer still needs a deliberate turn and a press. */
static int8_t knob_step(int8_t pos, const mao_event_t *ev)
{
    const int8_t n = (int8_t)(pos + (ev->type == MAO_EVENT_INPUT_CW ? 1 : -1));
    return n > 1 ? 1 : (n < -1 ? -1 : n);
}

static int8_t answer_of(int8_t pos)
{
    return pos < 0 ? 0 : (pos > 0 ? 1 : -1);    /* words[0] left, words[1] right */
}

static bool ceremony_for(uint64_t id, mao_link_pair_status_t *ps)
{
    mao_link_pair_status(ps);
    return ps->dev_id == id && ps->st != ODL_C_IDLE;
}

mao_devpage_t mao_devpage(uint64_t id, mao_world_entry_t *w)
{
    if (!id || !mao_world_get(id, w)) {
        return MAO_DEVPAGE_NONE;
    }
    if (!w->known) {
        return MAO_DEVPAGE_NEW;
    }
    if (!w->has_cred) {
        return w->online ? MAO_DEVPAGE_VERIFY : MAO_DEVPAGE_OFFLINE;
    }
    if (w->auth_failed) {
        return MAO_DEVPAGE_REPAIR;
    }
    if (w->online && (w->compat == ODD_COMPAT_INCOMPATIBLE || w->compat == ODD_COMPAT_INVALID)) {
        return MAO_DEVPAGE_INCOMPATIBLE;   /* trusted, just not operable: never REPAIR */
    }
    return w->online ? MAO_DEVPAGE_CONTROL : MAO_DEVPAGE_OFFLINE;
}

void mao_rel_note_unreached(uint64_t id)
{
    s_note = "NOT REACHED";              /* CONNECT found nothing: say so, then TRY AGAIN */
    s_note_dev = id;
    mao_app_dev_refresh();
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
    s_sheet_focus = 0;                   /* the middle: a destructive answer needs a deliberate turn */
    s_confirm_back = back;
    ESP_LOGI(TAG, "relationship: forget? (knob in the middle)");
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

/* ---------------------------------------------------------------------- */
/* Drawing                                                                */
/* ---------------------------------------------------------------------- */

static void draw_ceremony_sheet(const mao_world_entry_t *w, const mao_link_pair_status_t *ps, mao_ui_sheet_t *sh)
{
    static char code[8];
    static char q[ODD_NAME_MAX + 12];
    odl_sas_text(ps->sas, code);
    sh->on = true;
    sh->style = MAO_SHEET_CODE;
    sh->line2 = code;
    sh->line2_big = true;
    if (ps->st == ODL_C_SAS_READY) {
        snprintf(q, sizeof(q), "SAME ON %s?", w->name);   /* compare with the device, then decide */
        sh->line1 = q;
        sh->words[0] = "CANCEL";
        sh->words[1] = "MATCH";
        sh->word_count = 2;
        sh->focus = answer_of(s_cer_focus);
    } else {
        snprintf(q, sizeof(q), "CONFIRM ON %s", w->name);  /* the device's own yes; the code stays */
        sh->line1 = q;
        sh->word_count = 0;
    }
}

void mao_rel_sheet_draw(const mao_world_entry_t *w)
{
    static char question[ODD_NAME_MAX + 2];
    mao_ui_sheet_t sh = { .on = s_sheet != SHEET_NONE };
    mao_link_pair_status_t ps;
    if (ceremony_for(w->id, &ps) && (ps.st == ODL_C_SAS_READY || ps.st == ODL_C_WAIT_ACCEPT)) {
        draw_ceremony_sheet(w, &ps, &sh);
    } else if (s_sheet == SHEET_DETAILS) {
        sh.line1 = w->has_cred ? "PAIRED" : "REMEMBERED";
        sh.line2 = odd_device_type_name(w->device_type);
        sh.words[0] = "FORGET";
        sh.word_count = 1;
        sh.focus = 0;
    } else if (s_sheet == SHEET_CONFIRM) {
        snprintf(question, sizeof(question), "%s?", w->name);
        sh.style = MAO_SHEET_FORGET;
        sh.hide_title = true;
        sh.line1 = "FORGET";
        sh.line2 = question;
        sh.words[0] = "FORGET";           /* left: the way the menu went */
        sh.words[1] = "KEEP";
        sh.word_count = 2;
        sh.focus = answer_of(s_sheet_focus);
    } else if (s_sheet == SHEET_FORGETTING) {
        sh.style = MAO_SHEET_FORGET;
        sh.hide_title = true;
        sh.line1 = "FORGETTING";
        sh.line2 = w->name;
    }
    mao_ui_device_sheet(&sh);
}

/* The relationship pages in dots (M4.1): the device's mark, its state and
 * the one thing a press does; holding brings BACK and FORGET up. */
void mao_rel_page_draw(mao_devpage_t kind, const mao_world_entry_t *w)
{
    if (kind != MAO_DEVPAGE_NEW && kind != MAO_DEVPAGE_VERIFY && kind != MAO_DEVPAGE_REPAIR &&
        kind != MAO_DEVPAGE_OFFLINE && kind != MAO_DEVPAGE_INCOMPATIBLE) {
        const mao_ui_dotpage_t off = { .on = false };
        mao_ui_device_dots(&off);
        return;
    }
    mao_link_pair_status_t ps;
    const bool cer = ceremony_for(w->id, &ps);
    const bool pairing = cer && (ps.st == ODL_C_WAIT_COMMIT || ps.st == ODL_C_WAIT_REVEAL);
    const bool noted = s_note && s_note_dev == w->id;
    const char *fact = kind == MAO_DEVPAGE_NEW      ? "NEW"
                       : kind == MAO_DEVPAGE_REPAIR  ? "NOT VERIFIED"
                       : kind == MAO_DEVPAGE_OFFLINE ? "OFFLINE"
                       : kind == MAO_DEVPAGE_INCOMPATIBLE
                           ? (w->compat == ODD_COMPAT_INCOMPATIBLE ? "INCOMPATIBLE" : "UNREADABLE")   /* M4.1 D5 */
                           : NULL;
    if (pairing) {
        fact = "PAIRING";                /* a short cryptographic beat: the word holds */
    } else if (noted) {
        fact = s_note;
    }
    int8_t sel;
    const bool menu = mao_app_hold_menu(&sel);
    const mao_ui_dotpage_t dm = {
        .on = true,
        .kind = MAO_DOTPAGE_WORD,
        .online = kind != MAO_DEVPAGE_OFFLINE,
        .described = true,
        .type = w->device_type,
        .known = kind != MAO_DEVPAGE_NEW,
        .busy = pairing,
        /* after a refusal or a miss, the press word says what pressing does now */
        .word = kind == MAO_DEVPAGE_INCOMPATIBLE ? NULL
                : noted                          ? "TRY AGAIN"
                : kind == MAO_DEVPAGE_NEW        ? "PAIR"
                : kind == MAO_DEVPAGE_VERIFY     ? "VERIFY"
                : kind == MAO_DEVPAGE_REPAIR     ? "REPAIR"
                                                 : "CONNECT",
        .fact = fact,
        .menu = menu,
        .menu_sel = sel,
        .opt_left = kind == MAO_DEVPAGE_NEW ? NULL : mao_rel_word(),
        .name = w->name,
    };
    mao_ui_device_dots(&dm);
}

/* ---------------------------------------------------------------------- */
/* Ceremony                                                               */
/* ---------------------------------------------------------------------- */

static const char *fail_note(odl_fail_t f)
{
    switch (f) {
    case ODL_F_NOT_PAIRING: return "NOT IN PAIRING MODE";   /* the device's own step comes first */
    case ODL_F_REJECTED:    return "REFUSED ON THE DEVICE";
    case ODL_F_MISMATCH:    return "NO MATCH";
    case ODL_F_TIMEOUT:     return "TRY AGAIN";
    case ODL_F_STORAGE:     return "NOT SAVED";
    default:                return "PAIR FAILED";
    }
}

static void start_ceremony(mao_devpage_t kind, const mao_world_entry_t *w)
{
    uint8_t mac[6];
    mao_device_t d;
    bool have_mac = false;
    if (w->slot >= 0 && mao_devices_get(w->slot, &d)) {
        memcpy(mac, d.mac, 6);          /* the radio we hear it on */
        have_mac = true;
    } else if (kind == MAO_DEVPAGE_REPAIR) {
        have_mac = mao_rel_peer_mac(w->id, mac);
    }
    if (!have_mac) {
        s_note = "TRY AGAIN";
        s_note_dev = w->id;
        mao_app_dev_refresh();
        return;
    }
    s_note = NULL;
    s_cer_focus = 0;                     /* the middle: the code must be compared first */
    mao_ui_device_feedback(MAO_UI_FB_PENDING);
    mao_audio_touch();
    ESP_LOGI(TAG, "%s '%s': secure pairing", kind == MAO_DEVPAGE_NEW ? "pair" : kind == MAO_DEVPAGE_VERIFY ?
             "verify" : "re-pair", w->name);
    if (mao_link_pair_start(w->id, mac, w->name, w->device_type) != ESP_OK) {
        s_note = "TRY AGAIN";
        s_note_dev = w->id;
    }
    mao_app_dev_refresh();
}

/* The ceremony moved (MAO_EVENT_LINK_CHANGED): outcomes become page notes. */
void mao_rel_on_link_event(int32_t what)
{
    if (what == MAO_LINK_EV_REVOKED && s_forget_dev) {
        uint64_t id;
        const bool confirmed = mao_link_revoke_confirmed(&id);
        const uint64_t dev = s_forget_dev;
        s_forget_dev = 0;
        s_sheet = SHEET_NONE;
        ESP_LOGI(TAG, "remote revocation %s", confirmed ? "confirmed" : "NOT confirmed (device may keep a stale "
                 "controller credential until its next pairing)");
        const esp_err_t err = mao_rel_forget(dev);
        if (err == ESP_OK && !s_forget_online) {
            mao_app_go_devices();
            return;
        }
        if (err == ESP_OK) {
            mao_ui_device_feedback(MAO_UI_FB_GONE);   /* the page becomes NEW: the old mark comes apart */
        }
    }
    mao_link_pair_status_t ps;
    mao_link_pair_status(&ps);
    if (ps.st == ODL_C_FAILED || ps.st == ODL_C_CANCELLED) {
        s_note = ps.st == ODL_C_CANCELLED ? NULL : fail_note(ps.fail);
        s_note_dev = ps.dev_id;
        mao_ui_device_feedback(ps.st == ODL_C_CANCELLED ? MAO_UI_FB_REST : MAO_UI_FB_FAILED);
        if (ps.st == ODL_C_FAILED) {
            mao_audio_back();
        }
        mao_link_pair_reset();
    } else if (ps.st == ODL_C_PAIRED) {
        s_note = NULL;
        mao_ui_device_feedback(MAO_UI_FB_DONE);   /* it lands: the wave; the page becomes the device */
        mao_audio_confirm();
        mao_led_pulse(MAO_LED_PULSE_CONFIRM);
        mao_link_pair_reset();
    }
    mao_app_dev_refresh();
}

static bool ceremony_input(const mao_world_entry_t *w, const mao_event_t *ev)
{
    mao_link_pair_status_t ps;
    if (!ceremony_for(w->id, &ps) || ps.st > ODL_C_WAIT_ACCEPT) {
        return false;
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW:
        if (ps.st == ODL_C_SAS_READY) {
            const int8_t f = knob_step(s_cer_focus, ev);
            if (f != s_cer_focus) {
                s_cer_focus = f;
                mao_audio_tick(160);
                ESP_LOGI(TAG, "pairing: %s", f > 0 ? "MATCH chosen" : f < 0 ? "CANCEL chosen" : "knob in the middle");
            }
        }
        break;
    case MAO_EVENT_INPUT_CLICK:
        if (ps.st == ODL_C_SAS_READY && s_cer_focus > 0) {
            ESP_LOGI(TAG, "pairing: codes match (user)");
            mao_audio_confirm();
            mao_link_pair_confirm();
        } else if (ps.st == ODL_C_SAS_READY && s_cer_focus < 0) {
            ESP_LOGI(TAG, "pairing: cancelled by the user");
            mao_audio_back();
            mao_link_pair_cancel();
        } else if (ps.st == ODL_C_SAS_READY) {
            mao_ui_device_feedback(MAO_UI_FB_NUDGE);   /* nothing chosen: turn */
        }
        break;
    case MAO_EVENT_INPUT_LONG_PRESS:
        ESP_LOGI(TAG, "pairing: cancelled (BACK)");
        mao_audio_back();
        mao_link_pair_cancel();          /* BACK aborts safely before the commit point */
        break;
    default:
        break;
    }
    mao_app_dev_refresh();
    return true;
}

/* ---------------------------------------------------------------------- */
/* FORGET                                                                 */
/* ---------------------------------------------------------------------- */

static void forget(const mao_world_entry_t *w)
{
    mao_audio_back();
    if (w->has_cred && mao_link_revoke(w->id) == ESP_OK) {
        /* Proven device: ask it to drop our authorization first (bounded). */
        s_forget_dev = w->id;
        s_forget_online = w->online;
        s_sheet = SHEET_FORGETTING;
        mao_app_dev_refresh();
        return;
    }
    const esp_err_t err = mao_rel_forget(w->id);
    s_sheet = SHEET_NONE;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "'%s' stays part of MAO's setup (forget not saved)", w->name);
        mao_app_dev_refresh();
        return;
    }
    ESP_LOGI(TAG, "'%s' is no longer part of MAO's setup%s", w->name, w->online ? " (still nearby: NEW)" :
             w->has_cred ? " (offline: the device may keep a stale controller credential)" : "");
    if (w->online) {
        mao_ui_device_feedback(MAO_UI_FB_GONE);
        mao_app_dev_refresh();           /* the page becomes NEW in place */
    } else {
        mao_app_go_devices();            /* neither known nor heard: it leaves DEVICES */
    }
}

void mao_rel_sheet_input(const mao_world_entry_t *w, const mao_event_t *ev)
{
    if (s_sheet == SHEET_FORGETTING) {
        return;                          /* a bounded revocation is running */
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW:
        if (s_sheet == SHEET_CONFIRM) {
            const int8_t f = knob_step(s_sheet_focus, ev);
            if (f != s_sheet_focus) {
                s_sheet_focus = f;
                mao_audio_tick(160);
                ESP_LOGI(TAG, "relationship: forget? %s", f < 0 ? "FORGET chosen" : f > 0 ? "KEEP chosen"
                                                                                           : "knob in the middle");
            }
        }
        break;
    case MAO_EVENT_INPUT_CLICK:
        if (s_sheet == SHEET_DETAILS) {
            mao_audio_touch();
            open_confirm(SHEET_DETAILS);
        } else if (s_sheet_focus < 0) {
            forget(w);
            return;
        } else if (s_sheet_focus > 0) {
            mao_audio_back();
            s_sheet = s_confirm_back;    /* KEEP: nothing happened */
            ESP_LOGI(TAG, "relationship: forget cancelled");
        } else {
            mao_ui_device_feedback(MAO_UI_FB_NUDGE);   /* nothing chosen: turn */
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

/* ---------------------------------------------------------------------- */
/* Input on relationship pages                                            */
/* ---------------------------------------------------------------------- */

bool mao_rel_page_input(mao_devpage_t kind, const mao_world_entry_t *w, const mao_event_t *ev, bool guarded)
{
    if (kind != MAO_DEVPAGE_NONE && ceremony_input(w, ev)) {
        return true;                     /* a ceremony for this device owns the knob */
    }
    if (kind != MAO_DEVPAGE_NEW && kind != MAO_DEVPAGE_VERIFY && kind != MAO_DEVPAGE_REPAIR &&
        kind != MAO_DEVPAGE_OFFLINE && kind != MAO_DEVPAGE_INCOMPATIBLE) {
        if (ev->type == MAO_EVENT_INPUT_LONG_PRESS) {
            mao_app_go_devices();        /* BACK from a vanished device */
        }
        return kind == MAO_DEVPAGE_NONE;
    }
    /* Holding: BACK, or FORGET for anything that is part of MAO's setup. */
    switch (mao_app_hold(ev, false, kind != MAO_DEVPAGE_NEW)) {
    case MAO_HOLD_EATEN:
        mao_ui_device_feedback(MAO_UI_FB_REST);
        mao_app_dev_refresh();
        return true;
    case MAO_HOLD_BACK:
        mao_app_go_devices();
        return true;
    case MAO_HOLD_LEFT:
        mao_rel_word_activate(w);
        return true;
    case MAO_HOLD_RIGHT:
        return true;
    default:
        break;
    }
    /* Press: the page's one word. */
    const bool has_word = kind != MAO_DEVPAGE_INCOMPATIBLE;
    switch (ev->type) {
    case MAO_EVENT_INPUT_PRESS:
        if (has_word) {
            mao_ui_device_feedback(MAO_UI_FB_PRESS);
        }
        break;
    case MAO_EVENT_INPUT_RELEASE:
        mao_ui_device_feedback(MAO_UI_FB_REST);
        break;
    case MAO_EVENT_INPUT_CLICK:
        if (guarded) {
            ESP_LOGI(TAG, "click ignored: page just opened");
        } else if (kind == MAO_DEVPAGE_OFFLINE) {
            /* The user's own intent: try to reach it (MAO's visit). */
            ESP_LOGI(TAG, "connect requested: '%s' (known, offline)", w->name);
            mao_ui_device_connect_hot(1.0f);
            mao_audio_confirm();
            mao_transfer_connect();
        } else if (has_word) {
            start_ceremony(kind, w);
        }
        break;
    default:
        break;                           /* the knob alone does nothing here */
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
