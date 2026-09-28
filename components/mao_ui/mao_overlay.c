/*
 * Text layers above HOME: menu, placeholder page and first encounter.
 * Typography, spacing and motion only: no boxes, bullets, icons, chevrons,
 * cards or scrollbars. The selected word IS the interface.
 *
 * Menu: the words sit on a gentle arc that follows the round edge. Each word
 * exists in two sizes (NORMAL, LARGE) and cross-fades between them by its
 * distance from the selection, so the selected word appears to grow as it
 * arrives, without any bitmap scaling. Scroll position is a spring: every
 * detent retargets it (deterministic index, continuous motion). Words fade
 * out as they approach the circular boundary.
 */
#include <math.h>
#include "mao_ui_priv.h"
#include "mao_ui.h"

/* Layout tuning. */
#define MENU_SPACING      46.0f    /* px between words */
#define MENU_ARC          -14.0f   /* px sideways at MENU_ARC_REF from centre */
#define MENU_ARC_REF      92.0f
#define MENU_ENTER_DY     10.0f    /* words open outward from the centre, rising slightly */
#define MENU_ENTER_SPREAD 0.40f    /* row spacing at the start of the entrance */
#define MENU_EDGE_R       104.0f   /* words fade out towards the circle */
#define MENU_EDGE_FADE    34.0f
#define MENU_BUMP_V       2.6f     /* elastic resistance at the ends */
#define PAGE_TITLE_Y      -22.0f
#define PAGE_NOTE_Y       24.0f
#define INTRO_WORD_Y      -14.0f
#define INTRO_PROMPT_Y    26.0f

#define MENU_MAX          8

/* Menu movement sits between SNAP and HEAVY: 120-250 ms per step. */
#define MENU_POS_PROFILE  ((mao_spring_profile_t){ .k = 260.0f, .zeta = 0.78f })

typedef struct {
    lv_obj_t *small[MENU_MAX];
    lv_obj_t *large[MENU_MAX];
    mao_text_cache_t cs[MENU_MAX], cl[MENU_MAX];
    int count;
    mao_spring_t pos;
    mao_spring_t presence;
    uint32_t show_at;
    float show_target;
} menu_t;

typedef struct {
    lv_obj_t *title;
    lv_obj_t *note;
    mao_text_cache_t ct, cn;
    mao_spring_t presence;
} page_t;

typedef struct {
    lv_obj_t *word;
    lv_obj_t *prompt;
    mao_text_cache_t cw, cp;
    mao_spring_t presence;
    mao_spring_t shift;
} intro_t;

static menu_t s_menu;
static page_t s_page;
static intro_t s_intro;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smooth01(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* ---------------------------------------------------------------------- */
/* Menu                                                                   */
/* ---------------------------------------------------------------------- */

static void menu_layout(void)
{
    const float p = clampf(s_menu.presence.x, 0.0f, 1.0f);
    const float q = clampf(s_page.presence.x, 0.0f, 1.0f);
    const float context = p * (1.0f - q);           /* neighbours leave when a page opens */
    const bool handed_over = q > 0.01f;             /* the page title is the selected word */
    const float enter = (1.0f - p) * MENU_ENTER_DY;
    const float spread = MENU_ENTER_SPREAD + (1.0f - MENU_ENTER_SPREAD) * p;

    for (int i = 0; i < s_menu.count; i++) {
        const float d = (float)i - s_menu.pos.x;
        const float ad = fabsf(d);
        const float sel = 1.0f - smooth01(ad / 0.8f);
        const float y = d * MENU_SPACING * spread + enter;
        const float x = MENU_ARC * (y / MENU_ARC_REF) * (y / MENU_ARC_REF);
        const float edge = clampf((MENU_EDGE_R - fabsf(y)) / MENU_EDGE_FADE, 0.0f, 1.0f);
        const float falloff = 1.0f - 0.35f * clampf(ad - 1.0f, 0.0f, 2.0f);

        const bool is_selected = lrintf(s_menu.pos.target) == i;
        const float large = 255.0f * sel * edge * (is_selected ? p * (handed_over ? 0.0f : 1.0f) : context);
        const float small = MAO_OPA_CONTEXT * (1.0f - sel) * edge * falloff * context;
        mao_ui_text_place(s_menu.large[i], x, y, large, &s_menu.cl[i]);
        mao_ui_text_place(s_menu.small[i], x, y, small, &s_menu.cs[i]);
    }
}

void mao_overlay_menu_show(bool show, int index, uint32_t delay_ms)
{
    if (show) {
        s_menu.pos.target = (float)index;
        if (s_menu.presence.x < 0.01f) {
            s_menu.pos.x = (float)index;   /* appear already on the selection */
            s_menu.pos.v = 0.0f;
        }
    }
    s_menu.show_target = show ? 1.0f : 0.0f;
    /* Arrive with weight; leave quickly so MAO can return into a clear space. */
    s_menu.presence.p = show ? MAO_SPRING_HEAVY : MAO_SPRING_SNAP;
    if (delay_ms) {
        s_menu.show_at = lv_tick_get() + delay_ms;
    } else {
        s_menu.show_at = 0;
        s_menu.presence.target = s_menu.show_target;
    }
}

void mao_overlay_menu_select(int index)
{
    s_menu.pos.target = (float)index;
}

void mao_overlay_menu_bump(int direction)
{
    s_menu.pos.v += direction > 0 ? MENU_BUMP_V : -MENU_BUMP_V;
}

/* ---------------------------------------------------------------------- */
/* Placeholder page: the selected word relocates to become its title.     */
/* ---------------------------------------------------------------------- */

static void page_layout(void)
{
    const float q = clampf(s_page.presence.x, 0.0f, 1.0f);
    const bool visible = q > 0.01f || s_page.presence.target > 0.5f;
    mao_ui_text_place(s_page.title, 0.0f, PAGE_TITLE_Y * q, visible ? 255.0f : 0.0f, &s_page.ct);
    mao_ui_text_place(s_page.note, 0.0f, PAGE_NOTE_Y + 10.0f * (1.0f - q),
                      MAO_OPA_SECONDARY * smooth01((q - 0.6f) / 0.4f), &s_page.cn);   /* leaves first */
}

void mao_overlay_page_show(bool show, const char *title)
{
    if (show && title) {
        lv_label_set_text(s_page.title, title);
    }
    s_page.presence.target = show ? 1.0f : 0.0f;
}

/* ---------------------------------------------------------------------- */
/* First encounter                                                        */
/* ---------------------------------------------------------------------- */

static void intro_layout(uint32_t now)
{
    /* M4.1: the first encounter is drawn in dots (mao_ui_devices.c,
     * intro_layout); the labels stay, invisible, so layout and caches are
     * unchanged. */
    (void)now;
    mao_ui_text_place(s_intro.word, s_intro.shift.x, INTRO_WORD_Y, 0.0f, &s_intro.cw);
    mao_ui_text_place(s_intro.prompt, 0.0f, INTRO_PROMPT_Y, 0.0f, &s_intro.cp);
}

void mao_overlay_intro_get(float *presence, float *shift)
{
    *presence = clampf(s_intro.presence.x, 0.0f, 1.0f);
    *shift = s_intro.shift.x;
}

void mao_overlay_intro_exit(int direction)
{
    /* The word is pulled towards the turn as it dissolves; MAO appears there. */
    s_intro.presence.p = MAO_SPRING_SNAP;      /* clear the stage quickly */
    s_intro.presence.target = 0.0f;
    s_intro.shift.target = (float)direction * 22.0f;
}

/* ---------------------------------------------------------------------- */

bool mao_overlay_tick(float dt, uint32_t now)
{
    if (s_menu.show_at && (int32_t)(now - s_menu.show_at) >= 0) {
        s_menu.show_at = 0;
        s_menu.presence.target = s_menu.show_target;
    }
    mao_spring_step(&s_menu.pos, dt);
    mao_spring_step(&s_menu.presence, dt);
    mao_spring_step(&s_page.presence, dt);
    mao_spring_step(&s_intro.presence, dt);
    mao_spring_step(&s_intro.shift, dt);

    menu_layout();
    page_layout();
    intro_layout(now);

    const bool intro_waiting = s_intro.presence.target > 0.5f;
    return s_menu.show_at != 0 || intro_waiting ||
           !mao_spring_settled(&s_menu.pos, 0.002f) || !mao_spring_settled(&s_menu.presence, 0.002f) ||
           !mao_spring_settled(&s_page.presence, 0.002f) || !mao_spring_settled(&s_intro.presence, 0.002f) ||
           !mao_spring_settled(&s_intro.shift, 0.05f);
}

void mao_overlay_create(lv_obj_t *scr, bool intro_visible)
{
    s_menu.count = mao_ui_menu_count() < MENU_MAX ? mao_ui_menu_count() : MENU_MAX;
    for (int i = 0; i < s_menu.count; i++) {
        s_menu.small[i] = mao_ui_make_text(scr, MAO_FONT_NORMAL, MAO_COL_FG, MAO_TRACK_NORMAL, mao_ui_menu_label(i));
        s_menu.large[i] = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, MAO_TRACK_LARGE, mao_ui_menu_label(i));
        mao_ui_text_cache_reset(&s_menu.cs[i]);
        mao_ui_text_cache_reset(&s_menu.cl[i]);
    }
    mao_spring_init(&s_menu.pos, 0.0f, MENU_POS_PROFILE);
    mao_spring_init(&s_menu.presence, 0.0f, MAO_SPRING_HEAVY);

    s_page.title = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, MAO_TRACK_LARGE, "");
    s_page.note = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_DIM, MAO_TRACK_SMALL, "NOT YET");
    mao_ui_text_cache_reset(&s_page.ct);
    mao_ui_text_cache_reset(&s_page.cn);
    mao_spring_init(&s_page.presence, 0.0f, MAO_SPRING_HEAVY);

    s_intro.word = mao_ui_make_text(scr, MAO_FONT_LARGE, MAO_COL_FG, 8, "MAO");
    s_intro.prompt = mao_ui_make_text(scr, MAO_FONT_SMALL, MAO_COL_FG, MAO_TRACK_SMALL, "TURN");
    mao_ui_text_cache_reset(&s_intro.cw);
    mao_ui_text_cache_reset(&s_intro.cp);
    mao_spring_init(&s_intro.presence, intro_visible ? 1.0f : 0.0f, MAO_SPRING_HEAVY);
    mao_spring_init(&s_intro.shift, 0.0f, MAO_SPRING_SOFT);
}
