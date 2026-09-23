/*
 * DEVICES list and the generic device view. Pure presentation of models the
 * app builds from the registry; no knowledge of ODD BUS or device kinds.
 *
 * DEVICES: small "DEVICES" title, device names as a typographic list (offline
 * rows dimmed), one status word below: LOOKING... / NONE / ONLINE / OFFLINE.
 * DEVICE: name above, one large value (the dial's LEVEL, or ON/OFF when the
 * device only toggles), a quiet status word: OFF / OFFLINE / NO REPLY.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "mao_display.h"
#include "mao_ui_priv.h"
#include "mao_ui.h"

#define TICK_MS          33
#define LOOKING_MS       6000     /* then "NONE" (discovery continues) */

typedef struct {
    mao_ui_list_t list;
    lv_obj_t *title;
    lv_obj_t *status;
    mao_ui_text_cache_t cache[2];
    float presence;
    lv_timer_t *timer;
    uint32_t shown_at_ms;
    mao_ui_devices_t model;
    char names[MAO_UI_DEVICES_MAX][20];
} devlist_t;

typedef struct {
    lv_obj_t *title;
    lv_obj_t *value;
    lv_obj_t *status;
    mao_ui_text_cache_t cache[3];
    float presence;
    lv_timer_t *timer;
    lv_opa_t value_opa;
} panel_t;

static devlist_t s_list;
static panel_t s_panel;

/* ---------------------------------------------------------------------- */
/* DEVICES list                                                           */
/* ---------------------------------------------------------------------- */

static const char *list_status(void)
{
    const mao_ui_devices_t *m = &s_list.model;
    if (m->count == 0) {
        return (lv_tick_get() - s_list.shown_at_ms) < LOOKING_MS ? "LOOKING..." : "NONE";
    }
    return m->online[m->selected] ? "ONLINE" : "OFFLINE";
}

static void list_tick(lv_timer_t *t)
{
    const float p = s_list.presence;
    const char *st = list_status();
    if (strcmp(lv_label_get_text(s_list.status), st) != 0) {
        lv_label_set_text(s_list.status, st);
    }
    mao_ui_text_state(s_list.title, (int16_t)lrintf(-86.0f - (1.0f - p) * 6.0f), (lv_opa_t)(120.0f * p),
                      &s_list.cache[0]);
    mao_ui_text_state(s_list.status, (int16_t)lrintf(84.0f + (1.0f - p) * 6.0f), (lv_opa_t)(140.0f * p),
                      &s_list.cache[1]);
    /* Keep ticking while empty so LOOKING... can turn into NONE. */
    if (!mao_ui_anim_running(&s_list.presence) && (p < 0.01f || s_list.model.count > 0)) {
        lv_timer_pause(t);
    }
}

void mao_devlist_show(bool show, uint32_t delay_ms)
{
    if (show) {
        s_list.shown_at_ms = lv_tick_get();
    }
    mao_ui_list_show(&s_list.list, show, s_list.model.selected, delay_ms);
    mao_ui_anim_float(&s_list.presence, show ? 1.0f : 0.0f,
                      show ? MAO_UI_T_ENTER : MAO_UI_T_LEAVE, delay_ms, show, s_list.timer);
}

void mao_ui_devices_update(const mao_ui_devices_t *m)
{
    if (!mao_display_lock(0)) {
        return;
    }
    s_list.model = *m;
    const char *texts[MAO_UI_DEVICES_MAX];
    float scale[MAO_UI_DEVICES_MAX];
    for (int i = 0; i < m->count && i < MAO_UI_DEVICES_MAX; i++) {
        snprintf(s_list.names[i], sizeof(s_list.names[i]), "%s", m->name[i] ? m->name[i] : "?");
        s_list.model.name[i] = s_list.names[i];
        texts[i] = s_list.names[i];
        scale[i] = m->online[i] ? 1.0f : 0.45f;
    }
    mao_ui_list_set_items(&s_list.list, texts, scale, m->count);
    mao_ui_list_select(&s_list.list, m->selected);
    lv_timer_resume(s_list.timer);
    mao_display_unlock();
}

void mao_ui_devices_bump(int direction)
{
    if (mao_display_lock(0)) {
        mao_ui_list_bump(&s_list.list, direction);
        mao_display_unlock();
    }
}

/* ---------------------------------------------------------------------- */
/* Device view                                                            */
/* ---------------------------------------------------------------------- */

static void panel_tick(lv_timer_t *t)
{
    const float p = s_panel.presence;
    mao_ui_text_state(s_panel.title, (int16_t)lrintf(-62.0f - (1.0f - p) * 8.0f), (lv_opa_t)(150.0f * p),
                      &s_panel.cache[0]);
    mao_ui_text_state(s_panel.value, (int16_t)lrintf(-4.0f + (1.0f - p) * 14.0f),
                      (lv_opa_t)((float)s_panel.value_opa * p), &s_panel.cache[1]);
    mao_ui_text_state(s_panel.status, (int16_t)lrintf(46.0f + (1.0f - p) * 8.0f), (lv_opa_t)(150.0f * p),
                      &s_panel.cache[2]);
    if (!mao_ui_anim_running(&s_panel.presence)) {
        lv_timer_pause(t);
    }
}

void mao_devpanel_show(bool show, uint32_t delay_ms)
{
    mao_ui_anim_float(&s_panel.presence, show ? 1.0f : 0.0f,
                      show ? MAO_UI_T_ENTER : MAO_UI_T_LEAVE, delay_ms, show, s_panel.timer);
}

static void set_text(lv_obj_t *o, const char *s)
{
    if (strcmp(lv_label_get_text(o), s) != 0) {
        lv_label_set_text(o, s);
    }
}

void mao_ui_device_update(const mao_ui_device_t *m)
{
    if (!mao_display_lock(0)) {
        return;
    }
    set_text(s_panel.title, m->title ? m->title : "");

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
    s_panel.value_opa = !m->online ? 70 : ((m->has_toggle && !m->on) ? 100 : 255);
    s_panel.cache[1].opa = 1;   /* force re-apply of the value opacity */
    lv_timer_resume(s_panel.timer);
    mao_display_unlock();
}

/* ---------------------------------------------------------------------- */

void mao_devices_ui_create(lv_obj_t *scr)
{
    mao_ui_list_create(&s_list.list, scr, &lv_font_montserrat_20, MAO_UI_DEVICES_MAX, 40.0f, 0);
    s_list.title = mao_ui_make_text(scr, &lv_font_montserrat_14, MAO_UI_FG, 4);
    lv_label_set_text(s_list.title, "DEVICES");
    s_list.status = mao_ui_make_text(scr, &lv_font_montserrat_14, MAO_UI_DIM, 4);
    lv_label_set_text(s_list.status, "LOOKING...");
    s_list.cache[0].y = s_list.cache[1].y = INT16_MIN;
    s_list.timer = lv_timer_create(list_tick, TICK_MS, NULL);
    lv_timer_pause(s_list.timer);

    s_panel.title = mao_ui_make_text(scr, &lv_font_montserrat_20, MAO_UI_FG, 3);
    s_panel.value = mao_ui_make_text(scr, &lv_font_montserrat_48, MAO_UI_FG, 2);
    s_panel.status = mao_ui_make_text(scr, &lv_font_montserrat_14, MAO_UI_DIM, 4);
    for (int i = 0; i < 3; i++) {
        s_panel.cache[i].y = INT16_MIN;
    }
    s_panel.value_opa = 255;
    s_panel.timer = lv_timer_create(panel_tick, TICK_MS, NULL);
    lv_timer_pause(s_panel.timer);
}
