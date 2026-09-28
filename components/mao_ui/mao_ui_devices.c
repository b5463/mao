/*
 * DEVICES list and the generic device view. Pure presentation of models the
 * app builds from the registry; no knowledge of ODD BUS or device kinds.
 * Words, one focus line and springs, on the shared UI tick.
 * M4.1 composition: docs/m4_1_ui_motion_concept.md section 3.
 *
 * DEVICES: names on a gentle vertical arc in stable relationship order; the
 * selected one large with the focus line under it; the eyes watch from below
 * the lower rim (mao_character_peek). No title, and no status for a device
 * in its normal state: a word appears under the selected name only when
 * something is not normal (NEW, OFFLINE, VERIFY, REPAIR, INCOMPATIBLE,
 * UNREADABLE). Unavailable names recede (dimmer, wider tracking).
 * DEVICE: the name travels from its row to the top and stays (the anchor);
 * one primary (the action word or the LEVEL numeral) at the optical centre;
 * below it a condition only when it matters (NOT READY, OFF...), one quiet
 * fact (STORAGE), the secondary words, and CONNECT / INFO on one line.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mao_character.h"
#include "mao_display.h"
#include "mao_ui_priv.h"
#include "mao_ui.h"

#define LOOKING_MS        6000     /* then "NOTHING NEARBY" (discovery continues) */
#define LIST_SPACING      46.0f
#define LIST_ARC          -14.0f
#define LIST_ARC_REF      92.0f
#define LIST_EDGE_TOP     100.0f   /* names fade towards the upper rim ... */
#define LIST_EDGE_BOTTOM  64.0f    /* ... and earlier towards the lower one, where the eyes are */
#define LIST_EDGE_FADE    28.0f
#define LIST_BUMP_V       2.6f
#define LIST_STATE_DY     30.0f    /* the state word sits under the selected name */
#define LIST_ENTER_X      72.0f    /* the world is to the right: names arrive from there */
#define OFFLINE_SCALE     0.45f
#define NEW_SCALE         0.70f    /* nearby but not yet known: present, not yet at home */
#define RECEDE_TRACK      2        /* extra tracking for an unavailable name (distance) */

#define PANEL_TITLE_Y     -82.0f
#define PANEL_PRIMARY_Y   -30.0f
#define PANEL_COND_Y      8.0f     /* NOT READY / OFF / NO REPLY: only when it matters */
#define PANEL_FACT_Y      32.0f    /* quiet facts (STORAGE) */
#define PANEL_WORDS_Y     58.0f
#define PANEL_BOTTOM_Y    84.0f    /* CONNECT and the relationship word, one line */
#define PANEL_WORD_GAP    20.0f
#define PANEL_BOTTOM_GAP  24.0f
#define SHEET_L1_Y        -36.0f
#define SHEET_L2_Y        -8.0f
#define SHEET_WORDS_Y     44.0f
#define SHEET_WORD_DX     62.0f    /* fits CANCEL | MATCH on the round screen */
#define WORD_TRACK        2
#define BOTTOM_TRACK      3

#define LIST_POS_PROFILE  MAO_UI_SELECT

typedef struct {
    lv_obj_t *small[MAO_UI_DEVICES_MAX];
    lv_obj_t *large[MAO_UI_DEVICES_MAX];
    mao_text_cache_t cs[MAO_UI_DEVICES_MAX], cl[MAO_UI_DEVICES_MAX];
    int32_t track_l[MAO_UI_DEVICES_MAX], track_s[MAO_UI_DEVICES_MAX];
    int width_l[MAO_UI_DEVICES_MAX];
    lv_obj_t *state;            /* the one state word (or the empty-list word) */
    mao_text_cache_t cst;
    mao_spring_t pos;
    mao_spring_t presence;
    mao_spring_t side;          /* x offset: 0 in place, LIST_ENTER_X off to the right */
    uint32_t show_at;
    float show_target;
    uint32_t shown_at_ms;
    float sel_x, sel_y;         /* selected row's current place (page hand-off, gaze) */
    int16_t attend_x, attend_y; /* last attention sent to the character */
    mao_ui_devices_t model;
    char names[MAO_UI_DEVICES_MAX][20];
} devlist_t;

typedef struct {
    lv_obj_t *title;
    lv_obj_t *value;
    lv_obj_t *status;           /* condition row, or the centre word of a page without controls */
    lv_obj_t *connect;          /* "CONNECT": typography, never a button */
    lv_obj_t *primary;          /* the centre word (a device's primary action) */
    lv_obj_t *word[MAO_UI_DEVICE_WORDS];      /* the other control words */
    lv_obj_t *fact_l, *fact_r;  /* fact_l: a model condition (NOT READY); fact_r: a quiet fact */
    lv_obj_t *rel;              /* relationship word (INFO / FORGET) */
    lv_obj_t *sh_l1, *sh_l2, *sh_big, *sh_w[2];
    mao_text_cache_t crel, csl1, csl2, csbig, csw[2];
    mao_ui_sheet_t sheet;
    mao_spring_t sh;            /* sheet presence 0..1 */
    mao_text_cache_t ct, cv, cst, cc, cpr, cw[MAO_UI_DEVICE_WORDS], cfl, cfr;
    int w_value, w_primary, w_word[MAO_UI_DEVICE_WORDS], w_connect, w_rel, w_shw[2];
    int32_t tr_primary, tr_title;
    float x_word[MAO_UI_DEVICE_WORDS];
    int8_t focus;
    int8_t word_count;
    bool editing;
    bool has_value, has_facts;
    bool fact_l_on, fact_r_on, fact_r_emph;
    bool connect_hidden;
    bool no_centre;
    bool centre_dim;
    bool online;
    bool has_rel;
    mao_spring_t cdy, cdx;      /* centre word tool-feedback offsets, px */
    mao_spring_t fdy;           /* storage number change: a small settle, px */
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

static bool set_text(lv_obj_t *o, const char *s)
{
    if (strcmp(lv_label_get_text(o), s) != 0) {
        lv_label_set_text(o, s);
        return true;
    }
    return false;
}

/* Where the focus line sits under a label of this font centred at y: a
 * little under its baseline. */
static float under(const lv_font_t *f, float y)
{
    return y + (float)(f->line_height - f->base_line) - (float)f->line_height * 0.5f + 3.0f;
}

static int measure(lv_obj_t *o, const lv_font_t *f, int32_t track)
{
    return mao_ui_text_width(f, track, lv_label_get_text(o));
}

/* Delayed show/hide of a presence spring (show enters PAGE, hide leaves TAP-fast). */
static void presence_request(mao_spring_t *p, uint32_t *show_at, float *show_target, bool show, uint32_t delay_ms)
{
    *show_target = show ? 1.0f : 0.0f;
    p->p = show ? MAO_UI_PAGE : MAO_SPRING_SNAP;
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

/* The selected device's state word; NULL when it is simply there. */
static const char *list_state(uint32_t now)
{
    const mao_ui_devices_t *m = &s_list.model;
    if (m->count == 0) {
        return (now - s_list.shown_at_ms) < LOOKING_MS ? NULL : "NOTHING NEARBY";
    }
    const int i = m->selected;
    if (!m->known[i]) {
        return "NEW";
    }
    switch (m->note[i]) {
    case 1: return "VERIFY";
    case 2: return "REPAIR";
    case 3: return "INCOMPATIBLE";
    case 4: return "UNREADABLE";
    default: break;
    }
    return m->online[i] ? NULL : "OFFLINE";
}

static bool row_recedes(int i)
{
    return s_list.model.known[i] && !s_list.model.online[i];
}

static void list_layout(uint32_t now)
{
    const float p = clampf(s_list.presence.x, 0.0f, 1.0f);
    const float spread = 0.40f + 0.60f * p;
    const float enter = (1.0f - p) * 10.0f;
    const float side = s_list.side.x;
    const int count = s_list.model.count;
    for (int i = 0; i < MAO_UI_DEVICES_MAX; i++) {
        float large = 0.0f, small = 0.0f, x = 0.0f, y = 0.0f;
        if (i < count) {
            const float d = (float)i - s_list.pos.x;
            const float ad = fabsf(d);
            const float sel = 1.0f - smooth01(ad / 0.8f);
            y = d * LIST_SPACING * spread + enter;
            /* The arc, plus the lateral entrance: rows further from the
             * selection trail a little (they are further away). */
            x = LIST_ARC * (y / LIST_ARC_REF) * (y / LIST_ARC_REF) + side * (1.0f + 0.25f * ad);
            const float lim = y < 0.0f ? LIST_EDGE_TOP : LIST_EDGE_BOTTOM;
            const float edge = clampf((lim - fabsf(y)) / LIST_EDGE_FADE, 0.0f, 1.0f);
            const float row = s_list.model.note[i] ? NEW_SCALE
                              : !s_list.model.online[i] ? OFFLINE_SCALE
                              : (s_list.model.known[i] ? 1.0f : NEW_SCALE);
            large = 255.0f * sel * edge * p * row;
            small = MAO_OPA_CONTEXT * (1.0f - sel) * edge * p * row;
        }
        mao_ui_text_place(s_list.large[i], x, y, large, &s_list.cl[i]);
        mao_ui_text_place(s_list.small[i], x, y, small, &s_list.cs[i]);
        if (i == s_list.model.selected) {
            s_list.sel_x = x;
            s_list.sel_y = y;   /* where the name lives; the page picks it up here */
        }
    }
    const char *st = list_state(now);
    if (st) {
        set_text(s_list.state, st);
    }
    const bool empty = count == 0;
    const float sy = empty ? 0.0f : s_list.sel_y + LIST_STATE_DY;
    mao_ui_text_place(s_list.state, empty ? side : s_list.sel_x, sy,
                      st ? MAO_OPA_SECONDARY * p : 0.0f, &s_list.cst);

    /* MAO watches the selection from the rim (composition, not a pointer). */
    if (p > 0.3f && s_list.presence.target > 0.5f) {
        const int16_t ax = (int16_t)lrintf(empty ? 0.0f : s_list.sel_x);
        const int16_t ay = (int16_t)lrintf(empty ? -20.0f : s_list.sel_y);
        if (abs(ax - s_list.attend_x) >= 3 || abs(ay - s_list.attend_y) >= 3) {
            s_list.attend_x = ax;
            s_list.attend_y = ay;
            mao_character_attend(ax, ay);
        }
    }
}

void mao_devlist_show(bool show, uint32_t delay_ms, bool lateral)
{
    if (show) {
        s_list.shown_at_ms = lv_tick_get();
        s_list.pos.target = (float)s_list.model.selected;
        if (s_list.presence.x < 0.01f) {
            s_list.pos.x = s_list.pos.target;
            s_list.pos.v = 0.0f;
            s_list.side.x = lateral ? LIST_ENTER_X : 0.0f;
            s_list.side.v = 0.0f;
        }
        s_list.side.target = 0.0f;
        s_list.attend_x = s_list.attend_y = INT16_MIN;   /* tell the character again */
    } else {
        s_list.side.target = lateral ? LIST_ENTER_X : 0.0f;
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
        const int32_t extra = row_recedes(i) ? RECEDE_TRACK : 0;
        s_list.width_l[i] = mao_ui_fit(s_list.large[i], MAO_FONT_LARGE, MAO_TRACK_LARGE + extra, 0.0f,
                                       &s_list.track_l[i]);
        mao_ui_fit(s_list.small[i], MAO_FONT_NORMAL, MAO_TRACK_NORMAL + extra, LIST_SPACING, &s_list.track_s[i]);
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

static void sheet_layout(float p0, float s)
{
    const float a = p0 * s;
    const float rise = (1.0f - s) * 8.0f;
    mao_ui_text_place(s_panel.sh_l1, 0.0f, SHEET_L1_Y + rise, MAO_OPA_SECONDARY * a, &s_panel.csl1);
    const bool big = s_panel.sheet.line2_big;
    mao_ui_text_place(s_panel.sh_l2, 0.0f, SHEET_L2_Y + rise, big ? 0.0f : 235.0f * a, &s_panel.csl2);
    mao_ui_text_place(s_panel.sh_big, 0.0f, SHEET_L2_Y + 2.0f + rise, big ? 255.0f * a : 0.0f, &s_panel.csbig);
    const int n = s_panel.sheet.word_count;
    for (int i = 0; i < 2; i++) {
        const float x = n == 2 ? (i == 0 ? -SHEET_WORD_DX : SHEET_WORD_DX) : 0.0f;
        const float o = i < n ? (s_panel.sheet.focus == i ? 255.0f : (float)MAO_OPA_CONTEXT) : 0.0f;
        mao_ui_text_place(s_panel.sh_w[i], x, SHEET_WORDS_Y + rise, o * a, &s_panel.csw[i]);
    }
}

/* Lay out the secondary words as a centred row that fits its chord. */
static void words_row_layout(void)
{
    const int n = s_panel.word_count;
    if (n <= 0) {
        return;
    }
    float total = 0.0f;
    for (int i = 0; i < n; i++) {
        total += (float)s_panel.w_word[i];
    }
    const float room = 2.0f * mao_ui_chord_half(PANEL_WORDS_Y);
    float gap = PANEL_WORD_GAP;
    if (n > 1 && total + gap * (float)(n - 1) > room) {
        gap = fmaxf(10.0f, (room - total) / (float)(n - 1));
    }
    float x = -(total + gap * (float)(n - 1)) * 0.5f;
    for (int i = 0; i < n; i++) {
        s_panel.x_word[i] = x + (float)s_panel.w_word[i] * 0.5f;
        x += (float)s_panel.w_word[i] + gap;
    }
}

/* CONNECT and the relationship word share the bottom line. */
static void bottom_row(float *x_connect, float *x_rel)
{
    const bool c = !s_panel.connect_hidden, r = s_panel.has_rel;
    if (c && r) {
        const float total = (float)(s_panel.w_connect + s_panel.w_rel) + PANEL_BOTTOM_GAP;
        *x_connect = -total * 0.5f + (float)s_panel.w_connect * 0.5f;
        *x_rel = total * 0.5f - (float)s_panel.w_rel * 0.5f;
    } else {
        *x_connect = 0.0f;
        *x_rel = 0.0f;
    }
}

static void panel_layout(void)
{
    const float p0 = clampf(s_panel.presence.x, 0.0f, 1.0f);
    const float s = clampf(s_panel.sh.x, 0.0f, 1.0f);
    const float p = p0 * (1.0f - s);          /* the page recedes under a sheet */
    const float hot = clampf(s_panel.hot.x, 0.0f, 1.0f);
    const float lift = s * 6.0f;              /* ... and tilts away a little */
    /* The name IS the selected list row, moved deeper: it travels from its
     * list position up to the heading, and back again on the way out. */
    const float title_opa = (s_panel.online ? 170.0f : 110.0f) * p0 * (s_panel.sheet.hide_title ? 1.0f - s : 1.0f);
    mao_ui_text_place(s_panel.title, 0.0f, s_panel.ty.x - lift, title_opa, &s_panel.ct);
    sheet_layout(p0, s);

    /* Focus is the line; neighbours recede a step. */
    const float cf = s_panel.focus == 0 ? 1.0f : 0.62f;
    const float cy = PANEL_PRIMARY_Y + (1.0f - p) * 14.0f - lift + s_panel.cdy.x;
    const float cdim = s_panel.centre_dim ? 0.5f : 1.0f;
    if (s_panel.no_centre) {
        /* Nothing live to operate: the quiet state word sits where the
         * control would be. */
        mao_ui_text_place(s_panel.status, 0.0f, PANEL_PRIMARY_Y + 4.0f + (1.0f - p) * 8.0f - lift,
                          MAO_OPA_SECONDARY * p, &s_panel.cst);
    } else if (s_panel.has_value) {
        mao_ui_text_place(s_panel.value, s_panel.cdx.x, cy, s_panel.value_opa * cf * p, &s_panel.cv);
    } else {
        mao_ui_text_place(s_panel.primary, s_panel.cdx.x, cy, 255.0f * cf * cdim * p, &s_panel.cpr);
    }
    const float row = smooth01((p - 0.4f) / 0.6f);
    const float yield = s_panel.editing ? 0.40f : 1.0f;   /* LEVEL editing: the page yields to the value */
    const float wy = PANEL_WORDS_Y + (1.0f - p) * 6.0f - lift;
    for (int i = 0; i < s_panel.word_count; i++) {
        mao_ui_text_place(s_panel.word[i], s_panel.x_word[i], wy,
                          (s_panel.focus == i + 1 ? 255.0f : (float)MAO_OPA_CONTEXT) * row * yield, &s_panel.cw[i]);
    }
    /* Condition: a model condition (NOT READY) or the page's own (OFF...). */
    const float condy = PANEL_COND_Y + (1.0f - p) * 6.0f - lift;
    if (s_panel.fact_l_on) {
        mao_ui_text_place(s_panel.fact_l, 0.0f, condy, MAO_OPA_SECONDARY * row, &s_panel.cfl);
    } else {
        mao_ui_text_place(s_panel.fact_l, 0.0f, condy, 0.0f, &s_panel.cfl);
    }
    if (!s_panel.no_centre) {
        const bool has_cond = lv_label_get_text(s_panel.status)[0] != '\0' && !s_panel.fact_l_on;
        mao_ui_text_place(s_panel.status, 0.0f, condy, has_cond ? MAO_OPA_SECONDARY * row : 0.0f, &s_panel.cst);
    }
    /* The quiet fact; an exception (storage running low) gains presence. */
    if (s_panel.fact_r_on) {
        const float fo = s_panel.fact_r_emph ? 235.0f : (float)MAO_OPA_SECONDARY;
        mao_ui_text_place(s_panel.fact_r, 0.0f, PANEL_FACT_Y + (1.0f - p) * 6.0f - lift + s_panel.fdy.x,
                          fo * row * yield, &s_panel.cfr);
    } else {
        mao_ui_text_place(s_panel.fact_r, 0.0f, PANEL_FACT_Y, 0.0f, &s_panel.cfr);
    }
    /* CONNECT and the relationship word: quietest, one line. */
    float xc, xr;
    bottom_row(&xc, &xr);
    const bool cfocus = s_panel.focus == s_panel.word_count + 1;
    const float ch = cfocus ? 1.0f : hot;
    const float by = PANEL_BOTTOM_Y + (1.0f - p) * 6.0f - lift;
    const float bottom = smooth01((p - 0.5f) / 0.5f) * yield;
    mao_ui_text_place(s_panel.connect, xc, by - hot * 2.0f,
                      s_panel.connect_hidden ? 0.0f : (100.0f + 155.0f * ch) * bottom, &s_panel.cc);
    const bool rfocus = s_panel.focus == s_panel.word_count + 2;
    mao_ui_text_place(s_panel.rel, xr, by, s_panel.has_rel ? (rfocus ? 255.0f : 80.0f) * bottom : 0.0f,
                      &s_panel.crel);
}

/* Where the focus line goes on the page (or on its sheet). */
static void panel_focus(float *x, float *y, float *w)
{
    if (s_panel.sh.x > 0.5f && s_panel.sheet.word_count > 0) {
        const int f = s_panel.sheet.focus;
        const int n = s_panel.sheet.word_count;
        *x = n == 2 ? (f == 0 ? -SHEET_WORD_DX : SHEET_WORD_DX) : 0.0f;
        *y = under(MAO_FONT_NORMAL, SHEET_WORDS_Y);
        *w = (float)s_panel.w_shw[f < 0 || f > 1 ? 0 : f];
        return;
    }
    const int8_t f = s_panel.focus;
    if (f == 0) {
        *x = 0.0f;
        if (s_panel.has_value) {
            *y = under(MAO_FONT_NUMERAL, PANEL_PRIMARY_Y) + 3.0f;   /* clear of the digits */
            *w = (float)s_panel.w_value;
        } else {
            *y = under(MAO_FONT_LARGE, PANEL_PRIMARY_Y);
            *w = (float)s_panel.w_primary;
        }
    } else if (f <= s_panel.word_count) {
        *x = s_panel.x_word[f - 1];
        *y = under(MAO_FONT_SMALL, PANEL_WORDS_Y);
        *w = (float)s_panel.w_word[f - 1];
    } else {
        float xc, xr;
        bottom_row(&xc, &xr);
        const bool c = f == s_panel.word_count + 1;
        *x = c ? xc : xr;
        *y = under(MAO_FONT_SMALL, PANEL_BOTTOM_Y);
        *w = (float)(c ? s_panel.w_connect : s_panel.w_rel);
    }
}

void mao_ui_device_sheet(const mao_ui_sheet_t *sheet)
{
    if (!mao_display_lock(0)) {
        return;
    }
    if (sheet->on) {
        s_panel.sheet = *sheet;
        set_text(s_panel.sh_l1, sheet->line1 ? sheet->line1 : "");
        set_text(s_panel.sh_l2, sheet->line2 && !sheet->line2_big ? sheet->line2 : "");
        set_text(s_panel.sh_big, sheet->line2 && sheet->line2_big ? sheet->line2 : "");
        for (int i = 0; i < 2; i++) {
            set_text(s_panel.sh_w[i], i < sheet->word_count && sheet->words[i] ? sheet->words[i] : "");
            s_panel.w_shw[i] = measure(s_panel.sh_w[i], MAO_FONT_NORMAL, MAO_TRACK_NORMAL);
        }
        /* the text lives in the labels; keep no pointers to the caller's strings */
        s_panel.sheet.line1 = s_panel.sheet.line2 = NULL;
        s_panel.sheet.words[0] = s_panel.sheet.words[1] = NULL;
    }
    s_panel.sh.target = sheet->on ? 1.0f : 0.0f;
    mao_ui_wake();
    mao_display_unlock();
}

void mao_ui_device_feedback(mao_ui_fb_t fb)
{
    if (!mao_display_lock(0)) {
        return;
    }
    switch (fb) {
    case MAO_UI_FB_PRESS:
        s_panel.cdy.target = 3.0f;              /* connected to the button */
        mao_focus_mode(MAO_FOCUS_PRESS);
        break;
    case MAO_UI_FB_PENDING:
        s_panel.cdy.target = 1.5f;              /* held, weighted - and still */
        mao_focus_mode(MAO_FOCUS_DASH);         /* the aperture: underway */
        break;
    case MAO_UI_FB_DONE:
        s_panel.cdy.target = 0.0f;
        s_panel.cdy.v -= 70.0f;                 /* released tension: a tiny overshoot */
        mao_focus_mode(MAO_FOCUS_NORMAL);       /* the line lands back ... */
        mao_focus_event(MAO_COL_YELLOW, 300);   /* ... in yellow, once */
        break;
    case MAO_UI_FB_BUSY:
        s_panel.cdy.target = 0.0f;
        s_panel.cdy.v += 45.0f;                 /* it could not move: barely yields */
        mao_focus_mode(MAO_FOCUS_NORMAL);
        break;
    case MAO_UI_FB_FAILED:
        s_panel.cdy.target = 0.0f;
        s_panel.cdx.v += 90.0f;                 /* a small misalignment that settles */
        mao_focus_mode(MAO_FOCUS_NORMAL);
        mao_focus_event(MAO_COL_RED, 240);
        break;
    case MAO_UI_FB_REST:
    default:
        s_panel.cdy.target = 0.0f;
        mao_focus_mode(MAO_FOCUS_NORMAL);
        break;
    }
    mao_ui_wake();
    mao_display_unlock();
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
        mao_focus_mode(MAO_FOCUS_NORMAL);
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
    if (set_text(s_panel.title, m->title ? m->title : "")) {
        s_panel.tr_title = -1;
    }
    mao_ui_fit(s_panel.title, MAO_FONT_NORMAL, MAO_TRACK_NORMAL + (m->online ? 0 : RECEDE_TRACK), PANEL_TITLE_Y,
               &s_panel.tr_title);
    s_panel.focus = m->focus;
    s_panel.editing = m->editing;
    s_panel.connect_hidden = m->connect_hidden;
    s_panel.no_centre = m->no_centre;
    s_panel.centre_dim = m->centre_dim;
    s_panel.online = m->online;
    s_panel.has_rel = m->rel_word != NULL;
    if (m->rel_word) {
        set_text(s_panel.rel, m->rel_word);
    }
    s_panel.w_rel = measure(s_panel.rel, MAO_FONT_SMALL, BOTTOM_TRACK);
    s_panel.w_connect = measure(s_panel.connect, MAO_FONT_SMALL, BOTTOM_TRACK);
    s_panel.has_value = m->primary == NULL;
    if (m->no_centre) {
        lv_obj_add_flag(s_panel.value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_panel.primary, LV_OBJ_FLAG_HIDDEN);
        s_panel.cv.opa = 0;
        s_panel.cpr.opa = 0;
    } else if (m->primary) {
        if (set_text(s_panel.primary, m->primary)) {
            s_panel.tr_primary = -1;
        }
        s_panel.w_primary = mao_ui_fit(s_panel.primary, MAO_FONT_LARGE, MAO_TRACK_LARGE, PANEL_PRIMARY_Y,
                                       &s_panel.tr_primary);
        lv_obj_add_flag(s_panel.value, LV_OBJ_FLAG_HIDDEN);
        s_panel.cv.opa = 0;
    } else {
        lv_obj_add_flag(s_panel.primary, LV_OBJ_FLAG_HIDDEN);
        s_panel.cpr.opa = 0;
    }
    s_panel.word_count = m->word_count > MAO_UI_DEVICE_WORDS ? MAO_UI_DEVICE_WORDS : m->word_count;
    for (int i = 0; i < MAO_UI_DEVICE_WORDS; i++) {
        if (i < s_panel.word_count && m->words[i]) {
            set_text(s_panel.word[i], m->words[i]);
            s_panel.w_word[i] = measure(s_panel.word[i], MAO_FONT_SMALL, WORD_TRACK);
        } else {
            lv_obj_add_flag(s_panel.word[i], LV_OBJ_FLAG_HIDDEN);
            s_panel.cw[i].opa = 0;
            s_panel.w_word[i] = 0;
        }
    }
    words_row_layout();
    s_panel.has_facts = m->status_l != NULL || m->status_r != NULL;
    s_panel.fact_l_on = m->status_l != NULL;
    s_panel.fact_r_on = m->status_r != NULL;
    s_panel.fact_r_emph = m->status_r_emph;
    if (m->status_r && strcmp(lv_label_get_text(s_panel.fact_r), m->status_r) != 0 &&
        lv_label_get_text(s_panel.fact_r)[0] != '\0') {
        s_panel.fdy.x = -3.0f;                  /* the old value steps aside; the new settles */
        s_panel.fdy.v = 0.0f;
    }
    set_text(s_panel.fact_l, m->status_l ? m->status_l : "");
    set_text(s_panel.fact_r, m->status_r ? m->status_r : "");

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
    s_panel.w_value = measure(s_panel.value, MAO_FONT_NUMERAL, MAO_TRACK_NUMERAL);

    const char *status = "";
    if (!m->online) {
        status = "OFFLINE";
    } else if (m->problem) {
        status = "NO REPLY";
    } else if (m->has_toggle && m->has_level && !m->on) {
        status = "OFF";
    }
    if (m->no_centre && m->status_c) {
        status = m->status_c;
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
    mao_spring_step(&s_list.side, dt);
    mao_spring_step(&s_panel.presence, dt);
    mao_spring_step(&s_panel.ty, dt);
    mao_spring_step(&s_panel.hot, dt);
    mao_spring_step(&s_panel.cdy, dt);
    mao_spring_step(&s_panel.cdx, dt);
    mao_spring_step(&s_panel.fdy, dt);
    mao_spring_step(&s_panel.sh, dt);
    list_layout(now);
    panel_layout();

    /* One focus line: the surface with the most presence owns it. */
    const float lp = clampf(s_list.presence.x, 0.0f, 1.0f), pp = clampf(s_panel.presence.x, 0.0f, 1.0f);
    float fx = 0.0f, fy = 0.0f, fw = 0.0f, fp = 0.0f;
    if (pp >= lp && pp > 0.02f) {
        panel_focus(&fx, &fy, &fw);
        const float s = clampf(s_panel.sh.x, 0.0f, 1.0f);
        const bool sheet_words = s > 0.5f && s_panel.sheet.word_count > 0;
        fp = pp * (sheet_words ? s : (1.0f - s)) * (s_panel.no_centre && s_panel.focus == 0 ? 0.0f : 1.0f);
    } else if (lp > 0.02f && s_list.model.count > 0) {
        const int i = s_list.model.selected;
        fx = s_list.sel_x;
        fy = under(MAO_FONT_LARGE, s_list.sel_y);
        fw = (float)s_list.width_l[i];
        fp = lp * (row_recedes(i) ? 0.5f : 1.0f);
    }
    if (fp > 0.02f && fw > 0.0f) {
        mao_focus_target(fx, fy, fw);
    }
    mao_focus_presence(fp);

    /* Keep ticking while the list is visible and empty so NOTHING NEARBY can appear. */
    const bool waiting = s_list.presence.target > 0.5f && s_list.model.count == 0 &&
                         (now - s_list.shown_at_ms) < LOOKING_MS + 100;
    return waiting || s_list.show_at != 0 || s_panel.show_at != 0 ||
           !mao_spring_settled(&s_list.pos, 0.002f) || !mao_spring_settled(&s_list.presence, 0.002f) ||
           !mao_spring_settled(&s_list.side, 0.05f) ||
           !mao_spring_settled(&s_panel.presence, 0.002f) || !mao_spring_settled(&s_panel.ty, 0.05f) ||
           !mao_spring_settled(&s_panel.hot, 0.005f) || !mao_spring_settled(&s_panel.cdy, 0.05f) ||
           !mao_spring_settled(&s_panel.cdx, 0.05f) || !mao_spring_settled(&s_panel.fdy, 0.05f) ||
           !mao_spring_settled(&s_panel.sh, 0.002f);
}

void mao_devices_ui_create(lv_obj_t *scr)
{
    for (int i = 0; i < MAO_UI_DEVICES_MAX; i++) {
        s_list.small[i] = mao_ui_make_text(scr, MAO_FONT_NORMAL, MAO_COL_FG, MAO_TRACK_NORMAL, "");
        s_list.large[i] = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, MAO_TRACK_LARGE, "");
        mao_ui_text_cache_reset(&s_list.cs[i]);
        mao_ui_text_cache_reset(&s_list.cl[i]);
        s_list.track_l[i] = s_list.track_s[i] = -1;
    }
    s_list.state = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_DIM, MAO_TRACK_SMALL, "");
    mao_ui_text_cache_reset(&s_list.cst);
    mao_spring_init(&s_list.pos, 0.0f, LIST_POS_PROFILE);
    mao_spring_init(&s_list.presence, 0.0f, MAO_UI_PAGE);
    mao_spring_init(&s_list.side, 0.0f, MAO_UI_PAGE);

    s_panel.title = mao_ui_make_text(scr, MAO_FONT_NORMAL, MAO_COL_FG, MAO_TRACK_NORMAL, "");
    s_panel.value = mao_ui_make_text(scr, MAO_FONT_NUMERAL, MAO_COL_FG, MAO_TRACK_NUMERAL, "");
    s_panel.status = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_DIM, MAO_TRACK_SMALL, "");
    s_panel.connect = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, BOTTOM_TRACK, "CONNECT");
    s_panel.primary = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, MAO_TRACK_LARGE, "");
    for (int i = 0; i < MAO_UI_DEVICE_WORDS; i++) {
        s_panel.word[i] = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, WORD_TRACK, "");
        mao_ui_text_cache_reset(&s_panel.cw[i]);
    }
    s_panel.fact_l = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, MAO_TRACK_SMALL, "");
    s_panel.fact_r = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_DIM, WORD_TRACK, "");
    s_panel.rel = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, BOTTOM_TRACK, "");
    s_panel.sh_l1 = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, MAO_TRACK_SMALL, "");
    s_panel.sh_l2 = mao_ui_make_text(scr, MAO_FONT_NORMAL, MAO_COL_FG, MAO_TRACK_NORMAL, "");
    s_panel.sh_big = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, MAO_TRACK_LARGE, "");
    mao_ui_text_cache_reset(&s_panel.csbig);
    for (int i = 0; i < 2; i++) {
        s_panel.sh_w[i] = mao_ui_make_text(scr, MAO_FONT_NORMAL, MAO_COL_FG, MAO_TRACK_NORMAL, "");
        mao_ui_text_cache_reset(&s_panel.csw[i]);
    }
    s_panel.tr_primary = s_panel.tr_title = -1;
    mao_ui_text_cache_reset(&s_panel.crel);
    mao_ui_text_cache_reset(&s_panel.csl1);
    mao_ui_text_cache_reset(&s_panel.csl2);
    mao_spring_init(&s_panel.sh, 0.0f, MAO_UI_SHEET);
    mao_ui_text_cache_reset(&s_panel.cpr);
    mao_ui_text_cache_reset(&s_panel.cfl);
    mao_ui_text_cache_reset(&s_panel.cfr);
    mao_ui_text_cache_reset(&s_panel.ct);
    mao_ui_text_cache_reset(&s_panel.cv);
    mao_ui_text_cache_reset(&s_panel.cst);
    mao_ui_text_cache_reset(&s_panel.cc);
    mao_spring_init(&s_panel.presence, 0.0f, MAO_UI_PAGE);
    mao_spring_init(&s_panel.ty, PANEL_TITLE_Y, MAO_UI_PAGE);
    mao_spring_init(&s_panel.hot, 0.0f, MAO_SPRING_SNAP);
    /* Tool feedback: quick and physical, ~120-200 ms to settle. */
    mao_spring_init(&s_panel.cdy, 0.0f, MAO_UI_TAP);
    mao_spring_init(&s_panel.cdx, 0.0f, (mao_spring_profile_t){ .k = 600.0f, .zeta = 0.45f });
    mao_spring_init(&s_panel.fdy, 0.0f, (mao_spring_profile_t){ .k = 420.0f, .zeta = 0.9f });
    s_panel.value_opa = 255.0f;
    s_panel.online = true;
}
