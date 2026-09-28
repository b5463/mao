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

/* The device page in dots (see dotpage_layout). */
static struct {
    mao_ui_dotpage_t m;
    char name[20];
    mao_spring_t reach;            /* the field follows the brightness */
    mao_spring_t hole;             /* (kept for the page's flinch) */
    mao_spring_t blade[6];         /* the camera's blades, each on its own spring: the iris closes as a ripple */
    mao_spring_t leave;            /* under FORGET?: 1 staying .. 0 gone (it recedes as FORGET is chosen) */
    uint8_t left_kind;             /* what was just forgotten: 0 a mark, 1 a light's field, 2 an iris */
    mao_spring_t twist;            /* the knob turned on the camera: the iris answers with a small turn */
    uint8_t nudges;                /* presses in a row that chose nothing (then: TURN) */
    int32_t left_pct;
    uint8_t shots;                 /* frames taken on this visit (the filmstrip) */
    mao_spring_t optsel;           /* 0 the instrument .. 1 the hold-and-turn options are up */
    uint32_t level_until;          /* the brightness number shows until then */
    uint32_t shot_at;              /* the last capture landed (the flash and the wave) */
    uint32_t word_at;              /* a short word about the last action ... */
    const char *word;              /* ... NOT NOW / NOT TAKEN */
    struct {                       /* the sheet, in dots: copies of its words */
        char l1[24], l2[24], w[2][12];
        bool big;
        uint8_t style;
        int8_t n, focus;
        uint32_t since;            /* when this sheet (or its kind) appeared */
    } sh;
    uint32_t nudge_at;             /* a press chose nothing */
    uint32_t gone_at;              /* a device was just forgotten */
} s_dp;

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

#define PI_F            3.14159265f

static mao_glyph_t core_glyph(uint16_t type)
{
    switch (type) {
    case 6: return MAO_GLYPH_PLUS;       /* ODD_DEVICE_CAMERA: an aim */
    case 2: return MAO_GLYPH_STAR;       /* ODD_DEVICE_LIGHT: a light */
    default: return MAO_GLYPH_CROSS;
    }
}

/* The camera (M4.1): its own instrument - an iris, drawn in dots, animated
 * by the lamp's rules rather than its material:
 *   - things arrive staggered: each blade answers on its own spring, and the
 *     iris grows in through a ragged, soft front, as the field does;
 *   - weight falls with distance: blade marks are heavy at the aperture and
 *     thin out towards the rim;
 *   - depth is a few ink tiers, not outlines;
 *   - at rest it shimmers quietly on a stepped clock - a few marks at a time,
 *     so only those few are redrawn;
 *   - soft springs, no cartoon bounce.
 * Taken frames collect as a filmstrip on the lower rim. */
#define CAM_TYPE        6        /* ODD_DEVICE_CAMERA */
#define CAM_R           80.0f    /* the iris ring on the page */
#define CAM_AP          30.0f    /* the aperture at rest */
#define CAM_AP_PRESS    21.0f    /* the finger is on it */
#define CAM_AP_SHUT     0.0f     /* the frame is being taken */
#define CAM_PREVIEW_R   40.0f    /* the carousel's small iris: the size of a light's preview at full */
#define CAM_PREVIEW_AP  15.0f
#define CAM_STRIP_R     103.0f   /* the filmstrip's arc */
#define CAM_STRIP_GAP   0.115f   /* rad between frames */
#define CAM_STRIP_REACH 2.42f    /* rad from the bottom up each side: to just below the name */
#define CAM_STEP_MS     65       /* the field's stepped clock */
#define CAM_ROLL_MS     1300     /* a mark's hold: few change per step */

static uint32_t cam_hash(uint32_t a, uint32_t b)
{
    uint32_t h = (a * 374761393u) ^ (b * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* The four ink tiers as opacity: lead, colour, dim, faint. */
static const float kCamInk[4] = { 255.0f, 200.0f, 130.0f, 70.0f };

/* The iris at (x, y): ring radius r, per-blade apertures ap[6] (0 = shut:
 * the blades meet in a star), `front` the growing front's radius (the iris
 * arrives through it), opacity opa. */
static float s_iris_twist;        /* rad: the camera page's answer to the knob (iris_draw adds it) */

static void iris_draw(float x, float y, float r, const float ap[6], float front, float opa, uint32_t now)
{
    if (opa < 4.0f || r < 2.0f || front <= 0.0f) {
        return;
    }
    now -= now % CAM_STEP_MS;
    const bool big = r > 40.0f;
    const float step = big ? 5.0f : 3.4f;
    for (int k = 0; k < 6; k++) {
        const float apk = clampf(ap[k], 0.0f, r * 0.8f);
        const float a = 0.5236f - 0.9f * (apk / r) + (float)k * 1.0472f + s_iris_twist;   /* they turn as it opens */
        const float vx = apk * cosf(a), vy = apk * sinf(a);                   /* the aperture's corner */
        const float dx = -sinf(a), dy = cosf(a);                               /* the blade's edge */
        const float len = sqrtf(fmaxf(r * r - apk * apk, 0.0f));
        int i = 0;
        for (float t = 0.0f; t < len - 2.0f; t += step, i++) {
            const float px = vx + dx * t, py = vy + dy * t;
            const float w = t / len;                                          /* 0 at the aperture .. 1 at the rim */
            const uint32_t here = cam_hash((uint32_t)(k * 131 + i), 17u);
            const float jit = (float)(here % 1024u) / 1024.0f;
            const float kf = (front - (sqrtf(px * px + py * py) + jit * 18.0f)) / 14.0f;
            if (kf <= 0.0f) {
                continue;                                                     /* not yet reached by the front */
            }
            if ((here >> 10) % 100u < (uint32_t)(3.0f + 30.0f * w * w)) {
                continue;                                                     /* sparser towards the rim */
            }
            const uint32_t phase = (here >> 3) % CAM_ROLL_MS;
            const uint32_t gen = (now + phase) / CAM_ROLL_MS;
            const uint32_t h = cam_hash(here, gen);
            const uint32_t ci = h % 100u;
            const int ink = w < 0.3f ? (ci < 60 ? 0 : ci < 85 ? 1 : 2)
                          : w < 0.65f ? (ci < 35 ? 0 : ci < 65 ? 1 : ci < 90 ? 2 : 3)
                                      : (ci < 32 ? 1 : ci < 72 ? 2 : 3);
            const float size = (big ? 3.4f : 2.4f) - (big ? 1.2f : 0.6f) * w;
            const mao_glyph_t gl = w < 0.22f ? MAO_GLYPH_SQUARE : MAO_GLYPH_DOT;
            mao_dots_glyph(x + px, y + py, size, opa * kCamInk[ink] / 255.0f * fminf(kf, 1.0f), gl, 0);
        }
    }
    /* the ring: faint, a little broken, arriving last */
    const int ring = big ? 56 : 22;
    for (int i = 0; i < ring; i++) {
        const uint32_t here = cam_hash((uint32_t)i, 911u);
        const float kf = (front - (r + (float)(here % 1024u) / 1024.0f * 18.0f)) / 14.0f;
        if (kf <= 0.0f || (here >> 10) % 100u < 14u) {
            continue;
        }
        const uint32_t gen = (now + (here >> 3) % CAM_ROLL_MS) / CAM_ROLL_MS;
        const int ink = cam_hash(here, gen) % 100u < 55 ? 2 : 3;
        const float an = (float)i * 6.2832f / (float)ring;
        mao_dots_glyph(x + r * cosf(an), y + r * sinf(an), big ? 2.6f : 2.0f, opa * kCamInk[ink] / 255.0f * fminf(kf, 1.0f),
                       MAO_GLYPH_DOT, 0);
    }
}

/* The field is shared by the carousel's lamp preview and the lamp page: each
 * scene asks for it, the tick grants the one with more presence. */
static struct {
    float reach, strength, oy, hole, weight;
} s_fq;

static void field_request(float reach, float strength, float oy, float hole, float weight)
{
    if (weight > s_fq.weight) {
        s_fq = (typeof(s_fq)) { reach, strength, oy, hole, weight };
    }
}

#define CAR_R        100.0f    /* the ring the devices sit on */
#define CAR_STEP     0.62f     /* rad between neighbours */
#define PREVIEW_Y    -20.0f    /* the preview is the hero ... */
#define CAR_NAME_Y   52.0f     /* ... the name secondary, under it */
#define CAR_STATE_Y  74.0f
#define NAME_PITCH   2.6f
#define SHEET_PREVIEW_Y -24.0f /* a device's preview on its own relationship page, or under a question */

/* A light's preview: its field at its brightness; at full, the size of a
 * camera's preview iris. */
static float lamp_preview_reach(float pct01)
{
    return 28.0f + 26.0f * pct01;
}

/* A device as an object on the ring (or anywhere): its disc, its type cut
 * out of it; hollow when away; bigger when chosen. */
static void device_mark(float x, float y, float r, uint16_t type, bool online, bool known, float opa, float t)
{
    if (known && !online) {
        /* away: its outline only */
        const int n = r > 12.0f ? 20 : 10;
        const float sz = r > 12.0f ? 3.5f : 2.5f;
        for (int k = 0; k < n; k++) {
            const float an = (float)k * 2.0f * PI_F / (float)n;
            mao_dots_glyph(x + r * sinf(an), y - r * cosf(an), sz, opa * 0.7f, MAO_GLYPH_SQUARE, 0);
        }
        return;
    }
    const float pulse = known ? 1.0f : 1.0f + 0.12f * sinf(t * 2.0f * PI_F / 1.1f);   /* new: it calls */
    mao_dots_glyph(x, y, 2.0f * r * pulse, opa, MAO_GLYPH_DOT, 0);
    mao_dots_glyph(x, y, r * 1.0f, opa, core_glyph(type), 1);
}

/* A device as itself wherever it appears (M4.1): a light as its small
 * field, a camera as its small iris, anything else as its mark. Its state
 * changes how the same thing looks, never what it is:
 *   PREV_LIVE   as it is;
 *   PREV_FAINT  away, or not yet trusted: faint and small, the iris closed;
 *   PREV_CALL   new, nearby: it breathes - it asks to be paired.
 * scale 0..1 grows it in; pct is a light's brightness (-1 unknown); it sits
 * on the vertical axis (the field is centred). */
enum { PREV_LIVE = 0, PREV_FAINT, PREV_CALL };
#define LIGHT_TYPE 2             /* ODD_DEVICE_LIGHT */

static void device_preview(uint16_t type, int pct, int state, float y, float scale, float opa, float weight,
                           uint32_t now)
{
    if (scale <= 0.0f || opa < 1.0f) {
        return;
    }
    const float t = (float)now / 1000.0f;
    const float breathe = state == PREV_CALL ? 0.5f + 0.5f * sinf(t * 2.0f * PI_F / 1.6f) : 1.0f;
    if (type == LIGHT_TYPE) {
        const float reach = lamp_preview_reach(pct >= 0 ? (float)pct / 100.0f : 0.6f) * scale *
                            (state == PREV_FAINT ? 0.7f : 1.0f) * (state == PREV_CALL ? 0.86f + 0.14f * breathe : 1.0f);
        field_request(reach, (state == PREV_FAINT ? 0.35f : 1.0f) * opa / 255.0f, y, 0.0f, weight);
    } else if (type == CAM_TYPE) {
        const float a = state == PREV_FAINT ? 0.0f : CAM_PREVIEW_AP * (state == PREV_CALL ? 0.55f + 0.45f * breathe : 1.0f);
        const float ap[6] = { a, a, a, a, a, a };
        iris_draw(0.0f, y, CAM_PREVIEW_R * (0.7f + 0.3f * scale), ap, (CAM_PREVIEW_R + 22.0f) * scale,
                  opa * (state == PREV_FAINT ? 0.45f : 1.0f), now);
    } else {
        device_mark(0.0f, y, 20.0f * scale, type, state != PREV_FAINT, state != PREV_CALL, opa, t);
    }
}

/* DEVICES as a carousel (M4.1 rework, user review): every device is an
 * object on the rim; the knob turns the ring, the chosen one comes to the
 * top and grows. The centre says which device, large, and shows it live: a
 * light as its own little field at its brightness, anything else as its
 * core. A state word appears only when something is not normal. */
static void list_layout(uint32_t now)
{
    for (int i = 0; i < MAO_UI_DEVICES_MAX; i++) {
        mao_ui_text_place(s_list.large[i], 0.0f, 0.0f, 0.0f, &s_list.cl[i]);   /* labels unused: dots */
        mao_ui_text_place(s_list.small[i], 0.0f, 0.0f, 0.0f, &s_list.cs[i]);
    }
    mao_ui_text_place(s_list.state, 0.0f, 0.0f, 0.0f, &s_list.cst);
    const float p = clampf(s_list.presence.x, 0.0f, 1.0f);
    if (p < 0.004f) {
        return;
    }
    const float t = (float)now / 1000.0f;
    const float grow = smooth01(p / 0.85f);
    const float late = smooth01((p - 0.45f) / 0.55f);
    const int count = s_list.model.count;
    const bool empty = count == 0;
    const float pos = s_list.pos.x;
    const int shown = empty ? -1 : (int)clampf(lrintf(pos), 0.0f, (float)(count - 1));
    const float change = empty ? 1.0f : 1.0f - smooth01(fabsf(pos - (float)shown) * 2.2f);

    /* The ring: objects fly out from the gathered dot to their places. */
    if (empty) {
        const float an = t * 1.4f;                   /* looking: one mark goes round */
        mao_dots_glyph(CAR_R * grow * sinf(an), -CAR_R * grow * cosf(an), 4.0f, 200.0f * grow, MAO_GLYPH_DOT, 0);
        mao_dots_glyph(0.0f, 0.0f, 6.0f + 2.0f * sinf(t * 3.0f), 200.0f * grow, MAO_GLYPH_DOT, 0);
    }
    for (int i = 0; i < count; i++) {
        const float an = ((float)i - pos) * CAR_STEP;
        if (fabsf(an) > 2.6f) {
            continue;
        }
        const float sel = 1.0f - smooth01(fabsf((float)i - pos) / 0.8f);
        const float r = CAR_R * grow;
        const float x = r * sinf(an), y = -r * cosf(an);
        const bool on = s_list.model.online[i], known = s_list.model.known[i];
        const float o = (90.0f + 165.0f * sel) * grow * clampf((2.6f - fabsf(an)) / 0.6f, 0.0f, 1.0f);
        device_mark(x, y, 5.0f + 1.5f * sel, s_list.model.type[i], on, known, o, t);   /* where you are, not what */
    }

    if (!empty) {
        /* The centre: which device, and how it is. */
        const bool on = s_list.model.online[shown], known = s_list.model.known[shown];
        const int8_t lv = s_list.model.level[shown];
        /* the device as itself; a light that is off is its faint self */
        const bool dark = lv >= 0 && on && !s_list.model.power[shown];
        const int st = !known ? PREV_CALL : (!on || dark) ? PREV_FAINT : PREV_LIVE;
        device_preview(s_list.model.type[shown], lv, st, PREVIEW_Y, late * change, 255.0f * late * change, p, now);
        const char *nm = s_list.names[shown];
        const int cols = mao_dots_text_cols(nm);
        const float pitch = cols > 1 ? fminf(NAME_PITCH, 170.0f / (float)(cols - 1)) : NAME_PITCH;
        mao_dots_text(nm, 0.0f, CAR_NAME_Y, pitch, pitch * 0.85f, (on || !known ? 255.0f : 130.0f) * late * change, -1.0f);
        if (lv >= 0 && on && !s_list.model.power[shown]) {
            mao_dots_text("OFF", 0.0f, CAR_STATE_Y, 2.2f, 1.8f, 150.0f * late * change, -1.0f);
        }
        s_list.sel_x = 0.0f;
        s_list.sel_y = PREVIEW_Y;    /* the page grows out of the preview */
    }
    const char *st = list_state(now);
    if (st && empty) {
        mao_dots_text("SWITCH ONE ON", 0.0f, 52.0f, 1.9f, 1.5f, 130.0f * late, -1.0f);   /* what to do about it */
    }
    if (st) {
        mao_dots_text(st, 0.0f, empty ? 30.0f : CAR_STATE_Y, 2.2f, 1.8f, 170.0f * late * change, -1.0f);
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
    const float p = p0 * (1.0f - s) * (s_dp.m.on ? 0.0f : 1.0f);   /* recedes under a sheet; hidden by dots */
    const float hot = clampf(s_panel.hot.x, 0.0f, 1.0f);
    const float lift = s * 6.0f;              /* ... and tilts away a little */
    /* The name IS the selected list row, moved deeper: it travels from its
     * list position up to the heading, and back again on the way out. */
    const float title_opa = (s_panel.online ? 170.0f : 110.0f) * p0 * (s_panel.sheet.hide_title ? 1.0f - s : 1.0f);
    mao_ui_text_place(s_panel.title, 0.0f, s_panel.ty.x - lift, s_dp.m.on ? 0.0f : title_opa, &s_panel.ct);
    sheet_layout(s_dp.m.on ? 0.0f : p0, s);

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
        snprintf(s_dp.sh.l1, sizeof(s_dp.sh.l1), "%s", sheet->line1 ? sheet->line1 : "");
        snprintf(s_dp.sh.l2, sizeof(s_dp.sh.l2), "%s", sheet->line2 ? sheet->line2 : "");
        for (int i = 0; i < 2; i++) {
            snprintf(s_dp.sh.w[i], sizeof(s_dp.sh.w[i]), "%s",
                     i < sheet->word_count && sheet->words[i] ? sheet->words[i] : "");
        }
        s_dp.sh.big = sheet->line2_big;
        if (!s_dp.sh.since || sheet->style != s_dp.sh.style || sheet->word_count != s_dp.sh.n) {
            s_dp.sh.since = lv_tick_get();
        }
        s_dp.sh.style = sheet->style;
        s_dp.sh.n = sheet->word_count;
        s_dp.sh.focus = sheet->focus;
        /* the text lives in the labels; keep no pointers to the caller's strings */
        s_panel.sheet.line1 = s_panel.sheet.line2 = NULL;
        s_panel.sheet.words[0] = s_panel.sheet.words[1] = NULL;
    }
    if (!sheet->on) {
        s_dp.sh.since = 0;
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
        for (int k = 0; k < 6; k++) {
            s_dp.blade[k].v += 150.0f + 12.0f * (float)k;   /* the iris opens a little past its rest ... */
        }
        s_dp.shot_at = lv_tick_get();           /* ... after the exposure flash */
        if (s_dp.m.on && s_dp.m.kind == MAO_DOTPAGE_CONTROL && !s_dp.m.level && s_dp.shots < 99) {
            s_dp.shots++;                       /* a frame joins the filmstrip */
        }
        s_dp.word = NULL;
        break;
    case MAO_UI_FB_BUSY:
        s_panel.cdy.target = 0.0f;
        s_panel.cdy.v += 45.0f;                 /* it could not move: barely yields */
        mao_focus_mode(MAO_FOCUS_NORMAL);
        s_dp.word = "NOT NOW";
        s_dp.word_at = lv_tick_get();
        break;
    case MAO_UI_FB_FAILED:
        s_panel.cdy.target = 0.0f;
        s_panel.cdx.v += 90.0f;                 /* a small misalignment that settles */
        s_dp.word = "NOT TAKEN";
        s_dp.word_at = lv_tick_get();
        mao_focus_mode(MAO_FOCUS_NORMAL);
        mao_focus_event(MAO_COL_RED, 240);
        break;
    case MAO_UI_FB_NUDGE:
        s_dp.nudges = s_dp.nudge_at && lv_tick_get() - s_dp.nudge_at < 3000 ? (uint8_t)(s_dp.nudges + 1) : 1;
        s_dp.nudge_at = lv_tick_get();
        break;
    case MAO_UI_FB_UNSURE:
        s_panel.cdy.target = 0.0f;
        mao_focus_mode(MAO_FOCUS_NORMAL);
        s_dp.word = "NO ANSWER";                /* say so: never let silence look like success or failure */
        s_dp.word_at = lv_tick_get();
        break;
    case MAO_UI_FB_GONE:
        s_dp.gone_at = lv_tick_get();
        s_dp.left_kind = s_dp.m.kind != MAO_DOTPAGE_CONTROL ? 0 : s_dp.m.level ? (s_dp.m.power ? 1 : 0) : 2;
        s_dp.left_pct = s_dp.m.level_pct;
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
/* The device page in dots (M4.1)                                         */
/* ---------------------------------------------------------------------- */

#define DP_SEED          14.0f     /* the field at the lowest brightness: a small cluster */
#define DP_REACH_MAX     150.0f    /* past the rim: the field fills the circle */


/* A short state line, wrapped once at a space when it does not fit. */
static void fact_text(const char *f, float y, float opa)
{
    const int cols = mao_dots_text_cols(f);
    if ((float)cols * 2.2f <= 176.0f) {
        mao_dots_text(f, 0.0f, y, 2.2f, 1.8f, opa, -1.0f);
        return;
    }
    char a[24], b[24];
    const int n = (int)strlen(f);
    int cut = -1;
    for (int i = 0; i < n; i++) {                /* the space nearest the middle */
        if (f[i] == ' ' && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) {
            cut = i;
        }
    }
    if (cut < 0 || cut >= (int)sizeof(a) || n - cut - 1 >= (int)sizeof(b)) {
        mao_dots_text(f, 0.0f, y, 176.0f / (float)cols, 1.6f, opa, -1.0f);
        return;
    }
    memcpy(a, f, (size_t)cut);
    a[cut] = '\0';
    snprintf(b, sizeof(b), "%s", f + cut + 1);
    mao_dots_text(a, 0.0f, y - 10.0f, 2.2f, 1.8f, opa, -1.0f);
    mao_dots_text(b, 0.0f, y + 10.0f, 2.2f, 1.8f, opa, -1.0f);
}

/* Underway: a short row of dots lighting in sequence, in free space. */
static void scan_row(float y, float opa, float t)
{
    for (int i = 0; i < 7; i++) {
        const float ph = sinf(t * 5.0f - (float)i * 0.75f);
        const float lit = ph > 0.0f ? ph * ph * ph : 0.0f;
        mao_dots_glyph((float)(i - 3) * 8.0f, y, 3.0f, opa * (50.0f + 205.0f * lit), MAO_GLYPH_SQUARE, 0);
    }
}

/* A relationship page: the device's mark, what state it is in, and the one
 * thing a press does. Just forgotten: what it was (its field, its iris)
 * recedes to nothing through its own ragged front, then the new mark
 * (calling: NEW) grows in its place. */
static void word_page(float p, float menu, float t, uint32_t now)
{
    const float keep = p * (1.0f - menu);
    const float press = clampf(s_panel.cdy.x / 3.0f, 0.0f, 1.0f);
    const float since_gone = s_dp.gone_at ? (float)(now - s_dp.gone_at) : 1e9f;
    float grow = 1.0f;
    if (since_gone < 900.0f) {
        const float k = smooth01(since_gone / 520.0f);
        if (s_dp.left_kind == 1) {
            field_request(lamp_preview_reach((float)s_dp.left_pct / 100.0f) * 0.55f * (1.0f - k), 0.6f * (1.0f - k),
                          SHEET_PREVIEW_Y, 0.0f, 1.0f);
        } else if (s_dp.left_kind == 2) {
            const float pap[6] = { CAM_PREVIEW_AP, CAM_PREVIEW_AP, CAM_PREVIEW_AP, CAM_PREVIEW_AP, CAM_PREVIEW_AP,
                                   CAM_PREVIEW_AP };
            iris_draw(0.0f, SHEET_PREVIEW_Y, CAM_PREVIEW_R, pap, (CAM_PREVIEW_R + 22.0f) * 0.55f * (1.0f - k),
                      140.0f * (1.0f - k) * keep, now);
        }
        grow = smooth01((since_gone - 450.0f) / 450.0f);
    }
    /* the device as itself: new (it calls), or away / not yet trusted (faint) */
    const int st = !s_dp.m.known ? PREV_CALL : PREV_FAINT;
    device_preview(s_dp.m.type, -1, st, SHEET_PREVIEW_Y, grow * (1.0f - 0.08f * press), 255.0f * keep * grow, 1.0f, now);
    if (s_dp.m.busy) {
        scan_row(28.0f, keep, t);                 /* underway: between the device and its word */
    }
    if (s_dp.m.fact) {
        fact_text(s_dp.m.fact, s_dp.m.busy ? 46.0f : 36.0f, 160.0f * keep * grow);
    }
    if (s_dp.m.word && !s_dp.m.busy) {
        /* nothing to press while something is underway */
        mao_dots_text_halo(s_dp.m.word, s_panel.cdx.x * 0.8f, 68.0f + s_panel.cdy.x, 3.0f, 2.6f, 255.0f * keep * grow);
    }
}

/* Two answers on the lower band: neither chosen at first (the knob starts in
 * the middle); the chosen one comes forward, the other steps back. A press
 * that chose nothing makes them lean in: turn. */
static void sheet_answers(float a, uint32_t now, float y)
{
    const float nudge = s_dp.nudge_at && now - s_dp.nudge_at < 420 ? (float)(now - s_dp.nudge_at) / 420.0f : 1.0f;
    const float lean = nudge < 1.0f ? 9.0f * sinf(nudge * 3.0f * PI_F) * (1.0f - nudge) : 0.0f;
    for (int i = 0; i < s_dp.sh.n && i < 2; i++) {
        const bool chosen = s_dp.sh.focus == i;
        const bool other = s_dp.sh.focus >= 0 && !chosen;
        const float side = s_dp.sh.n == 2 ? (i == 0 ? -1.0f : 1.0f) : 0.0f;
        const float x = side * (chosen ? 44.0f : 56.0f) - side * lean;
        mao_dots_text_halo(s_dp.sh.w[i], x, y, chosen ? 2.8f : 2.0f, chosen ? 2.4f : 1.6f,
                           (chosen ? 255.0f : other ? 70.0f : 160.0f) * a);
    }
    if (s_dp.sh.n == 2 && s_dp.sh.focus < 0) {
        mao_dots_glyph(0.0f, y, 4.0f, 200.0f * a, MAO_GLYPH_DOT, 0);   /* the knob, in the middle */
        /* pressed again without turning: say it */
        if (s_dp.nudges >= 2 && now - s_dp.nudge_at < 1800) {
            const float f = clampf((float)(1800 - (now - s_dp.nudge_at)) / 300.0f, 0.0f, 1.0f);
            mao_dots_text("TURN", 0.0f, y + 22.0f, 2.2f, 1.8f, 230.0f * a * f, -1.0f);
        }
    }
}

/* A sheet over the page, in dots. */
static void dot_sheet(float a, float t, uint32_t now)
{
    if (a < 0.004f) {
        return;
    }
    const float since = s_dp.sh.since ? (float)(now - s_dp.sh.since) : 0.0f;
    if (s_dp.sh.style == MAO_SHEET_FORGET) {
        /* The question is the device itself: a control page condenses into
         * its preview (dotpage_layout), a relationship page's mark stands
         * here. The page's name stays above; the two answers below. The
         * preview recedes and dims as FORGET is chosen. */
        if (s_dp.m.kind == MAO_DOTPAGE_WORD) {
            const float k = s_dp.sh.n == 0 ? smooth01(since / 600.0f) : s_dp.sh.focus == 0 ? 0.45f : 0.0f;
            device_preview(s_dp.m.type, -1, !s_dp.m.known ? PREV_CALL : PREV_FAINT, SHEET_PREVIEW_Y,
                           1.0f - 0.45f * k, 255.0f * a * (1.0f - 0.7f * k), 2.0f, now);
        }
        if (s_dp.sh.n == 0) {
            fact_text("FORGETTING", 60.0f, 170.0f * a);
        } else {
            sheet_answers(a, now, 62.0f);
        }
        return;
    }
    if (s_dp.sh.style == MAO_SHEET_CODE) {
        if (s_dp.sh.l1[0]) {
            const int cols = mao_dots_text_cols(s_dp.sh.l1);
            const float pitch = cols > 1 ? fminf(2.2f, 176.0f / (float)(cols - 1)) : 2.2f;
            mao_dots_text(s_dp.sh.l1, 0.0f, -48.0f, pitch, pitch * 0.8f, 170.0f * a, -1.0f);
        }
        if (s_dp.sh.l2[0]) {
            const int cols = mao_dots_text_cols(s_dp.sh.l2);
            const float pitch = cols > 1 ? fminf(4.6f, 184.0f / (float)(cols - 1)) : 4.6f;
            mao_dots_text(s_dp.sh.l2, 0.0f, -6.0f, pitch, pitch * 0.85f, 255.0f * a, -1.0f);
        }
        if (s_dp.sh.n == 0) {
            scan_row(44.0f, a, t);                /* waiting on the device: under the code */
        }
        sheet_answers(a, now, 50.0f);
        return;
    }
    if (s_dp.sh.l1[0]) {
        mao_dots_text(s_dp.sh.l1, 0.0f, -44.0f, 2.0f, 1.6f, 160.0f * a, -1.0f);
    }
    if (s_dp.sh.l2[0]) {
        const int cols = mao_dots_text_cols(s_dp.sh.l2);
        const float pitch = cols > 1 ? fminf(3.0f, 196.0f / (float)(cols - 1)) : 3.0f;
        mao_dots_text(s_dp.sh.l2, 0.0f, -8.0f, pitch, pitch * 0.85f, 255.0f * a, -1.0f);
    }
    sheet_answers(a, now, 46.0f);
}

static void dotpage_layout(uint32_t now)
{
    const float p0 = clampf(s_panel.presence.x, 0.0f, 1.0f);
    const float sh = clampf(s_panel.sh.x, 0.0f, 1.0f);
    const float p = s_dp.m.on ? p0 * (1.0f - sh) : 0.0f;
    const float t = (float)now / 1000.0f;
    const float menu = clampf(s_dp.optsel.x, 0.0f, 1.0f);   /* how far the options are up */
    /* The reach follows the model every frame (brightness, power, presence). */
    const float pct = clampf((float)s_dp.m.level_pct, 0.0f, 100.0f) / 100.0f;
    s_dp.reach.target = s_panel.presence.target > 0.5f && s_dp.m.on && s_dp.m.level && s_dp.m.power && s_dp.m.online
                            ? DP_SEED + (DP_REACH_MAX - DP_SEED) * pct
                            : 0.0f;
    if (!s_dp.m.on || p0 < 0.004f) {
        return;
    }
    /* The page grows out of the carousel's preview: it starts where the
     * preview was and moves to the centre as it arrives. */
    const float oy = PREVIEW_Y * (1.0f - smooth01(p0));
    /* FORGET?: the page condenses into its own preview (the thing you would
     * forget), which recedes as FORGET is chosen and goes as it happens. */
    const bool forget_q = s_dp.sh.style == MAO_SHEET_FORGET && sh > 0.004f;
    const bool fsheet = forget_q && s_dp.m.kind == MAO_DOTPAGE_CONTROL;
    const float fk = fsheet ? smooth01(sh) : 0.0f;
    s_dp.leave.target = !fsheet ? 1.0f : s_dp.sh.n == 0 ? 0.0f : s_dp.sh.focus == 0 ? 0.55f : 1.0f;
    const float leave = fsheet ? clampf(s_dp.leave.x, 0.0f, 1.0f) : 1.0f;
    const float qy = oy + (SHEET_PREVIEW_Y - oy) * fk;
    if (s_dp.m.level) {
        float reach = s_dp.reach.x * smooth01(p0 / 0.9f);
        float strength = (1.0f - sh) * (1.0f - 0.9f * menu);
        if (fsheet) {
            const float small = s_dp.m.power && s_dp.m.online ? lamp_preview_reach(pct) : 0.0f;
            reach += (small * leave - reach) * fk;
            strength = (1.0f - 0.9f * menu) * (0.45f + 0.55f * leave);
        }
        field_request(reach, strength, qy, 0.0f, p0 + 0.01f);
    }

    /* The name, small, where the circle is still wide enough for it. */
    const int cols = mao_dots_text_cols(s_dp.name);
    const float npitch = cols > 1 ? fminf(2.4f, 124.0f / (float)(cols - 1)) : 2.4f;
    const float nopa = (s_dp.m.online ? 170.0f : 100.0f) * (forget_q ? p0 : p) * (1.0f - menu);   /* the question keeps it */
    if (s_dp.m.level && s_dp.reach.x > 60.0f) {
        mao_dots_text_halo(s_dp.name, 0.0f, -92.0f, npitch, 1.9f, nopa);   /* over the field */
    } else {
        mao_dots_text(s_dp.name, 0.0f, -92.0f, npitch, 1.9f, nopa, -1.0f);
    }

    if (s_dp.m.kind == MAO_DOTPAGE_WORD) {
        word_page(p, menu, t, now);
    } else if (s_dp.m.level) {
        if (!s_dp.m.online) {
            mao_dots_text_halo("OFFLINE", 0.0f, 0.0f, 3.0f, 2.4f, 170.0f * p);
        } else if (!s_dp.m.power) {
            mao_dots_add(0.0f, oy, 5.0f, 110.0f * p);
            mao_dots_text_halo("OFF", 0.0f, 34.0f, 3.0f, 2.4f, 150.0f * p * (1.0f - menu));
        } else if (now < s_dp.level_until) {
            /* While the knob moves: how much, large, over the field. */
            char num[8];
            snprintf(num, sizeof(num), "%ld", (long)s_dp.m.level_pct);
            const float left = (float)(s_dp.level_until - now);
            const float fade = clampf(left / 300.0f, 0.0f, 1.0f);
            mao_dots_text_halo(num, 0.0f, 0.0f, 6.0f, 5.0f, 255.0f * p * fade * (1.0f - menu));
        }
    } else {
        /* An action device (the camera): the iris. The finger narrows it;
         * the frame being taken shuts it (the blades meet in a star); a
         * frame taken flashes the opening and the blades open a little past
         * rest, one after another; failure makes it flinch. Its number, large
         * in the opening, says which frame that was. */
        const bool pending = s_panel.cdy.target > 1.0f && s_panel.cdy.target < 2.0f;
        const bool pressed = s_panel.cdy.target > 2.5f;
        const float apt = !s_dp.m.online ? 0.0f : pending ? CAM_AP_SHUT : pressed ? CAM_AP_PRESS : CAM_AP;
        for (int k = 0; k < 6; k++) {
            s_dp.blade[k].target = apt;
        }
        const float arrive = smooth01(p0);
        const float under = fsheet ? leave : smooth01(p / fmaxf(p0, 0.001f));
        const float cy = qy;
        const float jit = s_panel.cdx.x * 0.5f;                    /* failure: a flinch */
        const float rpage = CAM_PREVIEW_R + (CAM_R - CAM_PREVIEW_R) * arrive;
        const float r = rpage + (CAM_PREVIEW_R - rpage) * fk;       /* FORGET?: back to its preview's size */
        float ap[6];
        for (int k = 0; k < 6; k++) {
            ap[k] = fmaxf(0.0f, (CAM_PREVIEW_AP + (s_dp.blade[k].x - CAM_PREVIEW_AP) * arrive) * (r / rpage) +
                                    fabsf(jit) * 0.2f);
        }
        const bool blocked = s_dp.m.fact != NULL;                  /* e.g. FULL: it cannot take a frame */
        s_iris_twist = s_dp.twist.x;
        iris_draw(jit, cy, r, ap, (r + 22.0f) * smooth01(p0 / 0.85f) * under,
                  (s_dp.m.online ? (blocked ? 120.0f : 255.0f) : 110.0f) * p0 * (1.0f - 0.8f * menu) *
                      (fsheet ? 0.45f + 0.55f * leave : 1.0f),
                  now);
        const float since = s_dp.shot_at ? (float)(now - s_dp.shot_at) : 1e9f;
        if (since < 200.0f) {
            /* the exposure: the opening is light for an instant */
            const float k = since / 200.0f;
            mao_dots_glyph(jit, cy, 2.0f * fmaxf(ap[0], 10.0f) + 6.0f, 255.0f * p * (1.0f - k * k), MAO_GLYPH_DOT, 0);
        } else if (since < 1500.0f && s_dp.shots > 0 && s_dp.m.online) {
            char num[4];
            snprintf(num, sizeof(num), "%u", (unsigned)s_dp.shots);
            const float f = clampf((1500.0f - since) / 300.0f, 0.0f, 1.0f);
            const float pitch = s_dp.shots > 9 ? 3.2f : 4.2f;
            mao_dots_text(num, jit, cy, pitch, pitch * 0.9f, 255.0f * p * f * (1.0f - menu), -1.0f);
        }
        s_iris_twist = 0.0f;
        /* The filmstrip: this visit's frames around the rim, the newest
         * dropping from the aperture into its place. Fixed slots - the first
         * at the bottom, then right, left, right... up both sides to the name -
         * so a frame never moves once it has landed; only when the strip is
         * full do all of them close up, and none is ever dropped. */
        const int n = s_dp.shots;
        const float land = s_dp.shot_at ? clampf((float)(now - s_dp.shot_at - 120) / 380.0f, 0.0f, 1.0f) : 1.0f;
        const int ranks = n / 2;                                     /* slots per side beyond the bottom one */
        const float gap = ranks > 0 ? fminf(CAM_STRIP_GAP, CAM_STRIP_REACH / (float)ranks) : CAM_STRIP_GAP;
        for (int i = 0; i < n; i++) {
            const int rank = (i + 1) / 2;
            const float side = i == 0 ? 0.0f : (i & 1) ? -1.0f : 1.0f;   /* right first (a falls towards the right) */
            const float a = PI_F * 0.5f + side * (float)rank * gap;
            float x = CAM_STRIP_R * cosf(a), y = CAM_STRIP_R * sinf(a);
            float sz = 6.0f;
            if (i == n - 1 && land < 1.0f) {
                const float e = smooth01(land);
                x = jit + (x - jit) * e;
                y = cy + (y - cy) * e;
                sz = 9.0f - 4.0f * e;
            }
            mao_dots_glyph(x, y, sz, (i == n - 1 ? 255.0f : 170.0f) * p * (1.0f - menu), MAO_GLYPH_SQUARE, 0);
        }
        if (!s_dp.m.online) {
            mao_dots_text_halo("OFFLINE", 0.0f, 0.0f, 2.6f, 2.1f, 170.0f * p);
        } else if (blocked) {
            mao_dots_text_halo(s_dp.m.fact, 0.0f, cy, 2.6f, 2.2f, 235.0f * p * (1.0f - menu));
            mao_dots_text_halo("FREE UP SPACE", 0.0f, 88.0f, 1.8f, 1.5f, 170.0f * p * (1.0f - menu));
        } else if (s_dp.word && now - s_dp.word_at < 1400) {
            const float f = clampf((float)(1400 - (now - s_dp.word_at)) / 300.0f, 0.0f, 1.0f);
            mao_dots_text_halo(s_dp.word, 0.0f, cy, 2.2f, 1.9f, 230.0f * p * f * (1.0f - menu));
        }
    }

    /* Holding: MAO's options, either side of the centre - CONNECT to the
     * right (towards the world), FORGET to the left; BACK in the middle. */
    if (menu > 0.01f) {
        /* Pointing at one: it takes the centre, large; the other goes. In
         * the middle: both, small, each on its own side. */
        const int8_t ms = s_dp.m.menu_sel;
        for (int i = 0; i < 2; i++) {
            const char *wd = i == 0 ? s_dp.m.opt_right : s_dp.m.opt_left;
            if (!wd) {
                continue;
            }
            if (ms == i) {
                mao_dots_text_halo(wd, 0.0f, 0.0f, 3.0f, 2.6f, 255.0f * p * menu);
            } else if (ms < 0) {
                const float x = (i == 0 ? 70.0f : -70.0f);
                /* FORGET, rarely needed, steps back */
                mao_dots_text_halo(wd, x, 0.0f, 1.7f, 1.4f, (i == 1 ? 90.0f : 150.0f) * p * menu);
            }
        }
        if (ms < 0) {
            /* Letting go here goes back: a long press still means back. */
            mao_dots_text_halo("BACK", 0.0f, 0.0f, 2.2f, 1.8f, 255.0f * p * menu);
        }
    }
    dot_sheet(p0 * sh, t, now);
}

void mao_ui_device_turn(int32_t detents)
{
    if (!mao_display_lock(0)) {
        return;
    }
    s_dp.twist.v += 1.4f * (float)(detents > 3 ? 3 : detents < -3 ? -3 : detents);   /* a small turn, back */
    mao_ui_wake();
    mao_display_unlock();
}

static struct {
    bool allowed;
    uint32_t since;
} s_hint;

void mao_ui_home_hint(bool allowed)
{
    if (!mao_display_lock(0)) {
        return;
    }
    s_hint.allowed = allowed;
    s_hint.since = lv_tick_get();
    mao_ui_wake();
    mao_display_unlock();
}

/* The first encounter, in MAO's own dots: the name arrives through a ragged
 * front, TURN breathes under it; turning pulls the name aside as it goes and
 * the eyes appear (mao_ui_intro_exit). */
static uint32_t s_intro_t0;

static bool intro_layout(uint32_t now)
{
    float p, shift;
    mao_overlay_intro_get(&p, &shift);
    if (p < 0.004f) {
        return false;
    }
    if (!s_intro_t0) {
        s_intro_t0 = now;
    }
    const float t = (float)(now - s_intro_t0) / 1000.0f;
    const float front = 20.0f + 150.0f * smooth01(t / 1.6f);
    mao_dots_text_front("MAO", shift, -12.0f, 6.0f, 5.0f, 255.0f * p, front);
    const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf(t * 3.2f));
    const float late = smooth01((t - 1.2f) / 0.8f);          /* after the name has arrived */
    mao_dots_text("TURN", 0.0f, 34.0f, 2.2f, 1.8f, 190.0f * p * p * p * pulse * late, -1.0f);
    return true;
}

/* HOME's settings (mao_ui_home_tune): chosen, then set, in dots. */
static struct {
    mao_tune_mode_t mode;
    int8_t sel;                  /* CHOOSE: -1 middle, 0 SOUND, 1 SCREEN; ADJUST: which */
    int value;
    uint32_t since;              /* when it came up (the eyes gather first) */
    mao_spring_t shown;          /* the drawn value follows the set one softly */
} s_tune;

void mao_ui_home_tune(mao_tune_mode_t mode, int8_t sel, int value)
{
    if (!mao_display_lock(0)) {
        return;
    }
    if (mode != MAO_TUNE_OFF && s_tune.mode == MAO_TUNE_OFF) {
        mao_character_gather();                  /* MAO makes room */
        s_tune.since = lv_tick_get();
        s_tune.shown.x = (float)value;
    } else if (mode == MAO_TUNE_OFF && s_tune.mode != MAO_TUNE_OFF) {
        mao_character_return();                  /* and comes back */
    }
    if (mode == MAO_TUNE_ADJUST && s_tune.mode != MAO_TUNE_ADJUST) {
        s_tune.shown.x = (float)value;
    }
    s_tune.mode = mode;
    s_tune.sel = sel;
    s_tune.value = value;
    s_tune.shown.target = (float)value;
    mao_ui_wake();
    mao_display_unlock();
}

static bool home_tune_layout(uint32_t now, float dt)
{
    if (s_tune.mode == MAO_TUNE_OFF) {
        return false;
    }
    mao_spring_step(&s_tune.shown, dt);
    const float in = smooth01((float)(now - s_tune.since - MAO_CHAR_GATHER_MS) / 220.0f);
    if (s_tune.mode == MAO_TUNE_CHOOSE) {
        /* the same three-position switch as a device page's hold */
        if (s_tune.sel < 0) {
            mao_dots_glyph(0.0f, 0.0f, 5.0f, 220.0f * in, MAO_GLYPH_DOT, 0);
            mao_dots_text("SOUND", -60.0f, 0.0f, 1.9f, 1.6f, 160.0f * in, -1.0f);
            mao_dots_text("SCREEN", 60.0f, 0.0f, 1.9f, 1.6f, 160.0f * in, -1.0f);
        } else {
            mao_dots_text(s_tune.sel == 0 ? "SOUND" : "SCREEN", 0.0f, 0.0f, 3.0f, 2.6f, 255.0f * in, -1.0f);
        }
        return true;
    }
    /* ADJUST: a ring filled to the level, the level large, its name above */
    const float v = clampf(s_tune.shown.x, 0.0f, 100.0f);
    const int n = 40;
    for (int i = 0; i < n; i++) {
        const float f = (float)i / (float)(n - 1);
        const float an = -2.6f + 5.2f * f;             /* a 300 deg arc, open at the bottom */
        const bool lit = f * 100.0f <= v + 0.01f;
        mao_dots_glyph(94.0f * sinf(an), -94.0f * cosf(an), lit ? 5.0f : 3.0f, (lit ? 255.0f : 60.0f) * in,
                       MAO_GLYPH_SQUARE, 0);
    }
    mao_dots_text(s_tune.sel == 0 ? "SOUND" : "SCREEN", 0.0f, -44.0f, 2.2f, 1.8f, 170.0f * in, -1.0f);
    char num[4];
    snprintf(num, sizeof(num), "%d", (int)lrintf(v));
    mao_dots_text(num, 0.0f, 10.0f, 6.0f, 5.0f, 255.0f * in, -1.0f);
    return true;
}

/* HOME, left alone for a moment by someone new to it: a quiet PRESS. */
static void home_hint_layout(uint32_t now)
{
    if (!s_hint.allowed || now - s_hint.since < 6000 || s_tune.mode != MAO_TUNE_OFF) {
        return;
    }
    const float t = (float)(now - s_hint.since - 6000) / 1000.0f;
    const float in = clampf(t / 0.8f, 0.0f, 1.0f);
    const float breathe = 0.75f + 0.25f * sinf(t * 2.0f * PI_F / 2.4f);
    mao_dots_text("PRESS", 0.0f, 98.0f, 2.0f, 1.7f, 190.0f * in * breathe, -1.0f);
}

void mao_ui_device_dots(const mao_ui_dotpage_t *m)
{
    if (!mao_display_lock(0)) {
        return;
    }
    if (m->on && m->level && s_dp.m.on && m->level_pct != s_dp.m.level_pct && m->power) {
        s_dp.level_until = lv_tick_get() + 1200;   /* the number shows while the knob moves */
    }
    if (m->on && (!s_dp.m.on || strcmp(s_dp.name, m->name ? m->name : "") != 0)) {
        s_dp.shots = 0;                           /* a new visit: an empty filmstrip */
    }
    s_dp.m = *m;
    snprintf(s_dp.name, sizeof(s_dp.name), "%s", m->name ? m->name : "");
    s_dp.m.name = s_dp.name;
    s_dp.optsel.target = m->menu ? 1.0f : 0.0f;
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
    mao_spring_step(&s_dp.reach, dt);
    mao_spring_step(&s_dp.hole, dt);
    for (int k = 0; k < 6; k++) {
        mao_spring_step(&s_dp.blade[k], dt);
    }
    mao_spring_step(&s_dp.leave, dt);
    mao_spring_step(&s_dp.twist, dt);
    mao_spring_step(&s_dp.optsel, dt);
    s_fq.weight = 0.0f;
    s_fq.reach = 0.0f;
    mao_dots_begin();
    list_layout(now);
    dotpage_layout(now);
    home_hint_layout(now);
    const bool intro_up = intro_layout(now);
    const bool tune_up = home_tune_layout(now, dt);
    mao_dots_end();
    mao_field_set(s_fq.weight > 0.0f ? s_fq.reach : 0.0f, s_fq.strength, s_fq.oy, now);
    panel_layout();

    /* One focus line: the surface with the most presence owns it. */
    const float lp = clampf(s_list.presence.x, 0.0f, 1.0f), pp = clampf(s_panel.presence.x, 0.0f, 1.0f);
    float fx = 0.0f, fy = 0.0f, fw = 0.0f, fp = 0.0f;
    if (pp >= lp && pp > 0.02f) {
        panel_focus(&fx, &fy, &fw);
        const float s = clampf(s_panel.sh.x, 0.0f, 1.0f);
        const bool sheet_words = s > 0.5f && s_panel.sheet.word_count > 0;
        fp = pp * (sheet_words ? s : (1.0f - s)) * (s_dp.m.on ? 0.0f : 1.0f) *
             (s_panel.no_centre && s_panel.focus == 0 ? 0.0f : 1.0f);
    }
    if (fp > 0.02f && fw > 0.0f) {
        mao_focus_target(fx, fy, fw);
    }
    mao_focus_presence(fp);

    /* Keep ticking while the list is visible and empty so NOTHING NEARBY can appear. */
    const bool waiting = s_list.presence.target > 0.5f && s_list.model.count == 0 &&
                         (now - s_list.shown_at_ms) < LOOKING_MS + 100;
    const bool field_alive = s_list.presence.x > 0.004f ||   /* the dots shimmer while they are there */
                             (s_dp.m.on && s_panel.presence.x > 0.004f) || s_dp.reach.x > 0.5f;
    return field_alive || waiting || s_hint.allowed || intro_up || tune_up || s_list.show_at != 0 || s_panel.show_at != 0 ||
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
    mao_spring_init(&s_dp.reach, 0.0f, ((mao_spring_profile_t){ .k = 90.0f, .zeta = 0.92f }));
    mao_spring_init(&s_dp.hole, CAM_AP, ((mao_spring_profile_t){ .k = 240.0f, .zeta = 0.45f }));
    mao_spring_init(&s_dp.leave, 1.0f, ((mao_spring_profile_t){ .k = 120.0f, .zeta = 0.9f }));
    mao_spring_init(&s_dp.twist, 0.0f, ((mao_spring_profile_t){ .k = 70.0f, .zeta = 0.75f }));
    mao_spring_init(&s_tune.shown, 0.0f, ((mao_spring_profile_t){ .k = 180.0f, .zeta = 0.9f }));
    for (int k = 0; k < 6; k++) {
        /* soft, each a little different: they arrive one after another */
        mao_spring_init(&s_dp.blade[k], CAM_AP,
                        ((mao_spring_profile_t){ .k = 150.0f - 11.0f * (float)k, .zeta = 0.82f }));
    }
    mao_spring_init(&s_dp.optsel, 0.0f, MAO_UI_SELECT);
}
