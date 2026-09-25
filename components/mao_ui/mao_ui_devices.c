/*
 * DEVICES list and the generic device view. Pure presentation of models the
 * app builds from the registry; no knowledge of ODD BUS or device kinds.
 * Same language as the menu: words only, springs only, on the shared UI tick.
 *
 * DEVICES: small "DEVICES" title, device names on the menu's gentle arc (the
 * selected name large, neighbours small, offline names dimmed), one status
 * word below: LOOKING / NONE / ONLINE / OFFLINE.
 * DEVICE: name above, one large value (the dial's LEVEL, or ON/OFF when the
 * device only toggles), a quiet status word: OFF / OFFLINE / NO REPLY.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "mao_display.h"
#include "mao_ui_priv.h"
#include "mao_ui.h"

#define LOOKING_MS        6000     /* then "NONE" (discovery continues) */
#define LIST_SPACING      46.0f
#define LIST_ARC          -14.0f
#define LIST_ARC_REF      92.0f
#define LIST_EDGE_R       78.0f    /* names fade before the title / status rows */
#define LIST_EDGE_FADE    26.0f
#define LIST_BUMP_V       2.6f
#define LIST_TITLE_Y      -88.0f
#define LIST_STATUS_Y     86.0f
#define PANEL_TITLE_Y     -70.0f
#define PANEL_VALUE_Y     -16.0f
#define PANEL_CTRL_Y      34.0f    /* the control words row (POWER, actions) */
#define PANEL_STATUS_Y    62.0f
#define PANEL_CONNECT_Y   86.0f
#define OFFLINE_SCALE     0.45f

#define LIST_POS_PROFILE  ((mao_spring_profile_t){ .k = 260.0f, .zeta = 0.78f })

typedef struct {
    lv_obj_t *small[MAO_UI_DEVICES_MAX];
    lv_obj_t *large[MAO_UI_DEVICES_MAX];
    mao_text_cache_t cs[MAO_UI_DEVICES_MAX], cl[MAO_UI_DEVICES_MAX];
    lv_obj_t *title;
    lv_obj_t *status;
    mao_text_cache_t ct, cst;
    mao_spring_t pos;
    mao_spring_t presence;
    uint32_t show_at;
    float show_target;
    uint32_t shown_at_ms;
    float sel_y;                /* selected row's current y (page hand-off) */
    mao_ui_devices_t model;
    char names[MAO_UI_DEVICES_MAX][20];
} devlist_t;

typedef struct {
    lv_obj_t *title;
    lv_obj_t *value;
    lv_obj_t *status;
    lv_obj_t *connect;          /* "CONNECT": typography, never a button */
    lv_obj_t *power;            /* the POWER control word */
    lv_obj_t *action;           /* the ACTION control word (e.g. IDENTIFY) */
    mao_text_cache_t ct, cv, cst, cc, cpw, cac;
    int8_t focus;
    bool editing;
    bool has_power, has_action;
    mao_spring_t presence;
    mao_spring_t ty;            /* title travels from its list row position */
    mao_spring_t hot;           /* CONNECT emphasis while arming / starting */
    uint32_t show_at;
    float show_target;
    float value_opa;
} panel_t;

static devlist_t s_list;
static panel_t s_panel;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smooth01(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void set_text(lv_obj_t *o, const char *s)
{
    if (strcmp(lv_label_get_text(o), s) != 0) {
        lv_label_set_text(o, s);
    }
}

/* Delayed show/hide of a presence spring (show enters HEAVY, hide leaves SNAP). */
static void presence_request(mao_spring_t *p, uint32_t *show_at, float *show_target, bool show, uint32_t delay_ms)
{
    *show_target = show ? 1.0f : 0.0f;
    p->p = show ? MAO_SPRING_HEAVY : MAO_SPRING_SNAP;
    if (delay_ms) {
        *show_at = lv_tick_get() + delay_ms;
    } else {
        *show_at = 0;
        p->target = *show_target;
    }
}

static void presence_due(mao_spring_t *p, uint32_t *show_at, float show_target, uint32_t now)
{
    if (*show_at && (int32_t)(now - *show_at) >= 0) {
        *show_at = 0;
        p->target = show_target;
    }
}

/* ---------------------------------------------------------------------- */
/* DEVICES list                                                           */
/* ---------------------------------------------------------------------- */

static const char *list_status(uint32_t now)
{
    const mao_ui_devices_t *m = &s_list.model;
    if (m->count == 0) {
        return (now - s_list.shown_at_ms) < LOOKING_MS ? "LOOKING" : "NONE";
    }
    return m->online[m->selected] ? "ONLINE" : "OFFLINE";
}

static void list_layout(uint32_t now)
{
    const float p = clampf(s_list.presence.x, 0.0f, 1.0f);
    const float spread = 0.40f + 0.60f * p;
    const float enter = (1.0f - p) * 10.0f;
    const int count = s_list.model.count;
    for (int i = 0; i < MAO_UI_DEVICES_MAX; i++) {
        float large = 0.0f, small = 0.0f, x = 0.0f, y = 0.0f;
        if (i < count) {
            const float d = (float)i - s_list.pos.x;
            const float ad = fabsf(d);
            const float sel = 1.0f - smooth01(ad / 0.8f);
            y = d * LIST_SPACING * spread + enter;
            x = LIST_ARC * (y / LIST_ARC_REF) * (y / LIST_ARC_REF);
            const float edge = clampf((LIST_EDGE_R - fabsf(y)) / LIST_EDGE_FADE, 0.0f, 1.0f);
            const float row = s_list.model.online[i] ? 1.0f : OFFLINE_SCALE;
            large = 255.0f * sel * edge * p * row;
            small = MAO_OPA_CONTEXT * (1.0f - sel) * edge * p * row;
        }
        mao_ui_text_place(s_list.large[i], x, y, large, &s_list.cl[i]);
        mao_ui_text_place(s_list.small[i], x, y, small, &s_list.cs[i]);
        if (i == s_list.model.selected) {
            s_list.sel_y = y;   /* where the name lives; the page picks it up here */
        }
    }
    set_text(s_list.status, list_status(now));
    mao_ui_text_place(s_list.title, 0.0f, LIST_TITLE_Y - (1.0f - p) * 6.0f, 120.0f * p, &s_list.ct);
    mao_ui_text_place(s_list.status, 0.0f, LIST_STATUS_Y + (1.0f - p) * 6.0f, MAO_OPA_SECONDARY * p, &s_list.cst);
}

void mao_devlist_show(bool show, uint32_t delay_ms)
{
    if (show) {
        s_list.shown_at_ms = lv_tick_get();
        s_list.pos.target = (float)s_list.model.selected;
        if (s_list.presence.x < 0.01f) {
            s_list.pos.x = s_list.pos.target;
            s_list.pos.v = 0.0f;
        }
    }
    presence_request(&s_list.presence, &s_list.show_at, &s_list.show_target, show, delay_ms);
}

void mao_ui_devices_update(const mao_ui_devices_t *m)
{
    if (!mao_display_lock(0)) {
        return;
    }
    s_list.model = *m;
    if (s_list.model.count > MAO_UI_DEVICES_MAX) {
        s_list.model.count = MAO_UI_DEVICES_MAX;
    }
    for (int i = 0; i < s_list.model.count; i++) {
        snprintf(s_list.names[i], sizeof(s_list.names[i]), "%s", m->name[i] ? m->name[i] : "?");
        s_list.model.name[i] = s_list.names[i];
        set_text(s_list.small[i], s_list.names[i]);
        set_text(s_list.large[i], s_list.names[i]);
    }
    s_list.pos.target = (float)s_list.model.selected;
    mao_ui_wake();
    mao_display_unlock();
}

void mao_ui_devices_bump(int direction)
{
    if (mao_display_lock(0)) {
        s_list.pos.v += direction > 0 ? LIST_BUMP_V : -LIST_BUMP_V;
        mao_ui_wake();
        mao_display_unlock();
    }
}

/* ---------------------------------------------------------------------- */
/* Device view                                                            */
/* ---------------------------------------------------------------------- */

static void panel_layout(void)
{
    const float p = clampf(s_panel.presence.x, 0.0f, 1.0f);
    const float hot = clampf(s_panel.hot.x, 0.0f, 1.0f);
    /* The name IS the selected list row, moved deeper: it travels from its
     * list position up to the heading, and back again on the way out. */
    mao_ui_text_place(s_panel.title, 0.0f, s_panel.ty.x, 150.0f * p, &s_panel.ct);
    /* Focus is typography: the focused control is bright, the others recede.
     * Editing lifts the value a pixel and gives it full presence. */
    const float vf = s_panel.focus == 0 ? (s_panel.editing ? 1.0f : 0.9f) : 0.62f;
    mao_ui_text_place(s_panel.value, 0.0f,
                      PANEL_VALUE_Y + (1.0f - p) * 14.0f - (s_panel.editing ? 2.0f : 0.0f),
                      s_panel.value_opa * vf * p, &s_panel.cv);
    const float row = smooth01((p - 0.4f) / 0.6f);
    if (s_panel.has_power) {
        const float x = s_panel.has_action ? -62.0f : 0.0f;
        mao_ui_text_place(s_panel.power, x, PANEL_CTRL_Y + (1.0f - p) * 6.0f,
                          (s_panel.focus == 1 ? 255.0f : (float)MAO_OPA_CONTEXT) * row, &s_panel.cpw);
    }
    if (s_panel.has_action) {
        const float x = s_panel.has_power ? 54.0f : 0.0f;
        mao_ui_text_place(s_panel.action, x, PANEL_CTRL_Y + (1.0f - p) * 6.0f,
                          (s_panel.focus == 2 ? 255.0f : (float)MAO_OPA_CONTEXT) * row, &s_panel.cac);
    }
    mao_ui_text_place(s_panel.status, 0.0f, PANEL_STATUS_Y + (1.0f - p) * 8.0f,
                      MAO_OPA_SECONDARY * smooth01((p - 0.4f) / 0.6f), &s_panel.cst);
    /* CONNECT is a quiet word until the user reaches for it. */
    mao_ui_text_place(s_panel.connect, 0.0f, PANEL_CONNECT_Y + (1.0f - p) * 6.0f - hot * 2.0f,
                      (100.0f + 155.0f * hot) * smooth01((p - 0.5f) / 0.5f), &s_panel.cc);
}

void mao_devpanel_show(bool show, uint32_t delay_ms)
{
    if (show) {
        /* Arrive from the selected row's place in the list. */
        s_panel.ty.x = s_list.sel_y;
        s_panel.ty.v = 0.0f;
        s_panel.ty.target = PANEL_TITLE_Y;
        s_panel.hot.x = 0.0f;
        s_panel.hot.target = 0.0f;
    } else {
        s_panel.ty.target = 0.0f;   /* back towards the row's resting spot */
    }
    presence_request(&s_panel.presence, &s_panel.show_at, &s_panel.show_target, show, delay_ms);
}

void mao_ui_device_connect_hot(float v)
{
    if (mao_display_lock(0)) {
        s_panel.hot.target = clampf(v, 0.0f, 1.0f);
        mao_ui_wake();
        mao_display_unlock();
    }
}

void mao_ui_device_update(const mao_ui_device_t *m)
{
    if (!mao_display_lock(0)) {
        return;
    }
    set_text(s_panel.title, m->title ? m->title : "");
    s_panel.focus = m->focus;
    s_panel.editing = m->editing;
    s_panel.has_power = m->has_toggle;
    s_panel.has_action = m->action != NULL;
    if (m->action) {
        set_text(s_panel.action, m->action);
    }
    if (!s_panel.has_power) {
        lv_obj_add_flag(s_panel.power, LV_OBJ_FLAG_HIDDEN);
        s_panel.cpw.opa = 0;
    }
    if (!s_panel.has_action) {
        lv_obj_add_flag(s_panel.action, LV_OBJ_FLAG_HIDDEN);
        s_panel.cac.opa = 0;
    }

    char big[12];
    if (!m->described) {
        snprintf(big, sizeof(big), "...");
    } else if (m->has_level) {
        snprintf(big, sizeof(big), "%ld", (long)m->level);
    } else if (m->has_toggle) {
        snprintf(big, sizeof(big), "%s", m->on ? "ON" : "OFF");
    } else {
        snprintf(big, sizeof(big), "-");
    }
    set_text(s_panel.value, big);

    const char *status = "";
    if (!m->online) {
        status = "OFFLINE";
    } else if (m->problem) {
        status = "NO REPLY";
    } else if (m->has_toggle && m->has_level && !m->on) {
        status = "OFF";
    }
    set_text(s_panel.status, status);

    /* The number dims when the device is off or unreachable. */
    s_panel.value_opa = !m->online ? 70.0f : ((m->has_toggle && !m->on) ? 100.0f : 255.0f);
    mao_ui_wake();
    mao_display_unlock();
}

/* ---------------------------------------------------------------------- */

bool mao_devices_ui_tick(float dt, uint32_t now)
{
    presence_due(&s_list.presence, &s_list.show_at, s_list.show_target, now);
    presence_due(&s_panel.presence, &s_panel.show_at, s_panel.show_target, now);
    mao_spring_step(&s_list.pos, dt);
    mao_spring_step(&s_list.presence, dt);
    mao_spring_step(&s_panel.presence, dt);
    mao_spring_step(&s_panel.ty, dt);
    mao_spring_step(&s_panel.hot, dt);
    list_layout(now);
    panel_layout();

    /* Keep ticking while the list is visible and empty so LOOKING can become NONE. */
    const bool waiting = s_list.presence.target > 0.5f && s_list.model.count == 0 &&
                         (now - s_list.shown_at_ms) < LOOKING_MS + 100;
    return waiting || s_list.show_at != 0 || s_panel.show_at != 0 ||
           !mao_spring_settled(&s_list.pos, 0.002f) || !mao_spring_settled(&s_list.presence, 0.002f) ||
           !mao_spring_settled(&s_panel.presence, 0.002f) || !mao_spring_settled(&s_panel.ty, 0.05f) ||
           !mao_spring_settled(&s_panel.hot, 0.005f);
}

void mao_devices_ui_create(lv_obj_t *scr)
{
    for (int i = 0; i < MAO_UI_DEVICES_MAX; i++) {
        s_list.small[i] = mao_ui_make_text(scr, MAO_FONT_NORMAL, MAO_COL_FG, MAO_TRACK_NORMAL, "");
        s_list.large[i] = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, MAO_TRACK_LARGE, "");
        mao_ui_text_cache_reset(&s_list.cs[i]);
        mao_ui_text_cache_reset(&s_list.cl[i]);
    }
    s_list.title = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, MAO_TRACK_SMALL, "DEVICES");
    s_list.status = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_DIM, MAO_TRACK_SMALL, "LOOKING");
    mao_ui_text_cache_reset(&s_list.ct);
    mao_ui_text_cache_reset(&s_list.cst);
    mao_spring_init(&s_list.pos, 0.0f, LIST_POS_PROFILE);
    mao_spring_init(&s_list.presence, 0.0f, MAO_SPRING_HEAVY);

    s_panel.title = mao_ui_make_text(scr, MAO_FONT_NORMAL, MAO_COL_FG, MAO_TRACK_NORMAL, "");
    s_panel.value = mao_ui_make_text(scr, &lv_font_montserrat_48, MAO_COL_FG, 2, "");
    s_panel.status = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_DIM, MAO_TRACK_SMALL, "");
    s_panel.connect = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, 6, "CONNECT");
    s_panel.power = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, MAO_TRACK_SMALL, "POWER");
    s_panel.action = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, MAO_TRACK_SMALL, "");
    mao_ui_text_cache_reset(&s_panel.cpw);
    mao_ui_text_cache_reset(&s_panel.cac);
    mao_ui_text_cache_reset(&s_panel.ct);
    mao_ui_text_cache_reset(&s_panel.cv);
    mao_ui_text_cache_reset(&s_panel.cst);
    mao_ui_text_cache_reset(&s_panel.cc);
    mao_spring_init(&s_panel.presence, 0.0f, MAO_SPRING_HEAVY);
    mao_spring_init(&s_panel.ty, PANEL_TITLE_Y, MAO_SPRING_HEAVY);
    mao_spring_init(&s_panel.hot, 0.0f, MAO_SPRING_SNAP);
    s_panel.value_opa = 255.0f;
}
