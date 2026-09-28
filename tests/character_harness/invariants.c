/*
 * Character invariants (M4.1, s133): properties every frame must keep, in
 * every scenario and seed, whatever the exact animation. They make a new
 * harness baseline safe to approve: the hashes say "it changed", these say
 * "and it is still correct". Built on harness.c's LVGL stand-in.
 *   ./invariants.exe <scenario> <seed>   -> one line, exit 1 on a violation
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void inv_frame(void);
#define FRAME_HOOK inv_frame()
#define main harness_main
#include "harness.c"
#undef main

/* The renderer's creation order (mao_character_draw.c): per eye, 9 parts. */
enum { P_EYE, P_IRIS, P_PUPIL, P_SHINE, P_SHINE2, P_STAR, P_LID, P_LOWER, P_COVER, PARTS };
#define EYE_OBJ(i, part) (&s_objs[(i) * PARTS + (part)])
#define OFFSCREEN 185
#define MAO_ACCENT_SLEEP_Q (0xC7A6F7u & 0xF8FCF8u)   /* the lavender, as drawn */

static const char *s_scn;
static int s_bad;
static float s_prev_x[2], s_prev_y[2];
static int s_have_prev;
static float s_max_jump;
static uint32_t s_gather_at, s_return_at;

static void fail(const char *what, int i)
{
    if (s_bad < 6) {
        printf("  VIOLATION %s (eye %d) at t=%u\n", what, i, s_now);
    }
    s_bad++;
}

static bool visible(const lv_obj_t *o)
{
    return !(o->flags & LV_OBJ_FLAG_HIDDEN);
}

static void inv_frame(void)
{
    bool both = true;
    for (int i = 0; i < 2; i++) {
        /* I1: centred parts keep even sizes (they grow about their centre) */
        const int centred[] = { P_EYE, P_IRIS, P_PUPIL, P_SHINE, P_SHINE2, P_COVER };
        for (size_t k = 0; k < sizeof(centred) / sizeof(centred[0]); k++) {
            const lv_obj_t *o = EYE_OBJ(i, centred[k]);
            if (visible(o) && ((o->w & 1) || (o->h & 1))) {
                fail("odd size", i);
            }
        }
        const lv_obj_t *e = EYE_OBJ(i, P_EYE);
        if (!visible(e)) {
            both = false;
            continue;
        }
        /* I2: a visible eye is sane and near the screen */
        if (e->w < 1 || e->h < 1 || e->w > 240 || e->h > 240 || abs(e->x) > OFFSCREEN || abs(e->y) > OFFSCREEN) {
            fail("eye out of bounds", i);
        }
        /* I3: frame-to-frame motion (measured; asserted per scenario below) */
        if (s_have_prev & (1 << i)) {
            const float j = hypotf((float)e->x - s_prev_x[i], (float)e->y - s_prev_y[i]);
            s_max_jump = fmaxf(s_max_jump, j);
            if (getenv("INV_TRACE") && j >= 12.0f) {
                printf("  jump eye %d t=%u %.1f px  (%d,%d)->(%d,%d) w=%d h=%d\n", i, s_now, j, (int)s_prev_x[i],
                       (int)s_prev_y[i], e->x, e->y, e->w, e->h);
            }
        }
        s_prev_x[i] = (float)e->x;
        s_prev_y[i] = (float)e->y;
        s_have_prev |= 1 << i;
    }
    /* I4: the eyes keep their sides (left stays left) */
    if (visible(EYE_OBJ(0, P_EYE)) && visible(EYE_OBJ(1, P_EYE)) &&
        EYE_OBJ(0, P_EYE)->x > EYE_OBJ(1, P_EYE)->x + 2) {
        if (getenv("INV_TRACE")) {
            printf("  crossed t=%u: L x=%d w=%d  R x=%d w=%d\n", s_now, EYE_OBJ(0, P_EYE)->x, EYE_OBJ(0, P_EYE)->w,
                   EYE_OBJ(1, P_EYE)->x, EYE_OBJ(1, P_EYE)->w);
        }
        fail("eyes crossed", 0);
    }
    /* I5: in the ordinary scenarios MAO is always there (blinks close the
     * eyes with covers; the eyes themselves never vanish) */
    const bool ordinary = !strcmp(s_scn, "idle") || !strcmp(s_scn, "dial") || !strcmp(s_scn, "press") ||
                          !strcmp(s_scn, "react") || !strcmp(s_scn, "mind") || !strcmp(s_scn, "looks");
    if (ordinary && s_now > 2500 && !both) {
        fail("eyes missing", -1);
    }
    /* I6: the gather contract - gone shortly after a gather, back after a
     * return (the gather scenario: gather at 2500 + k * 3290 ms) */
    if (!strcmp(s_scn, "gather") && s_now >= 2500) {
        const uint32_t k = (s_now - 2500) / 3290, t = (s_now - 2500) % 3290;
        (void)k;
        const uint32_t gone_from = MAO_CHAR_GATHER_MS + 100, back_from = MAO_CHAR_GATHER_MS + 600 + 1000;
        if (t >= gone_from && t < MAO_CHAR_GATHER_MS + 600 && (visible(EYE_OBJ(0, P_EYE)) || visible(EYE_OBJ(1, P_EYE)))) {
            fail("still there after a gather", -1);
        }
        if (t >= back_from && (!both || EYE_OBJ(0, P_EYE)->w < 40 || EYE_OBJ(1, P_EYE)->w < 40)) {
            fail("not back after a return", -1);
        }
    }
    /* I7: one MAO colour - both eyes always the same */
    if (both && EYE_OBJ(0, P_EYE)->bg != EYE_OBJ(1, P_EYE)->bg) {
        fail("eyes in different colours", -1);
    }
    /* I9: the accent (M4.1) - the pink egg's colour only ever leans within
     * MAO's mood palette (no black, no stray hue), and asleep it is the
     * lavender. Not in "looks", which switches to the other looks. */
    if (both && strcmp(s_scn, "looks") != 0) {
        const uint32_t c = EYE_OBJ(0, P_EYE)->bg;
        const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        if (r < 0xA0 || r > 0xF8 || g < 0x5C || g > 0xC8 || b < 0x66 || b > 0xF8) {
            fail("eye colour outside the accent palette", -1);
        }
        if (!strcmp(s_scn, "sleep") && s_now >= 4000 && s_now < 21000 && c != (MAO_ACCENT_SLEEP_Q)) {
            fail("asleep but not lavender", -1);
        }
    }
    /* I10: HOME's lamp scale (M4.1) - while the eyes look at it, no part of
     * either eye reaches the rim where it is drawn (inner edge 102 px). */
    if (!strcmp(s_scn, "homelook")) {
        for (int i = 0; i < 2; i++) {
            const lv_obj_t *e = EYE_OBJ(i, P_EYE);
            if (!visible(e)) {
                continue;
            }
            const float x1 = (float)e->x - (float)e->w * 0.5f, y1 = (float)e->y - (float)e->h * 0.5f;   /* centre-aligned */
            const float x2 = x1 + (float)e->w, y2 = y1 + (float)e->h;
            const float rr = fminf((float)e->w, (float)e->h) * 0.5f;
            for (int k = 0; k < 4; k++) {
                const float cx = (k & 1) ? x2 - rr : x1 + rr, cy = (k & 2) ? y2 - rr : y1 + rr;
                if (sqrtf(cx * cx + cy * cy) + rr > 99.0f) {
                    fail("an eye reaches the lamp scale", i);
                }
            }
        }
    }
    (void)both;
}

/* I8: motion limits per scenario - the measured maxima plus a margin. They
 * catch a teleport (a part jumping across the screen in one frame), not
 * fast motion itself. */
static float jump_limit(const char *s)
{
    static const struct { const char *s; float px; } k[] = {
        { "idle", 14 }, { "press", 12 }, { "dial", 42 }, { "react", 22 }, { "sleep", 16 }, { "mind", 22 },
        { "looks", 26 }, { "states", 44 }, { "previews", 64 }, { "menu", 70 }, { "peek", 60 },
        { "transfer", 115 }, { "gather", 40 }, { "homelook", 40 },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (!strcmp(k[i].s, s)) {
            return k[i].px;
        }
    }
    return 1e9f;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "invariants <scenario> <seed>\n");
        return 2;
    }
    s_scn = argv[1];
    char *hv[] = { argv[0], argv[1], argv[2] };
    (void)hv;
    s_rng = (uint32_t)strtoul(argv[2], NULL, 10) | 1u;
    s_now = 1000;
    s_next_refresh = s_now;
    if (mao_character_create(&s_screen) != 0) {
        printf("%s seed=%s: create failed\n", argv[1], argv[2]);
        return 1;
    }
    mao_character_set_detents_per_rev(30);
    scenario(argv[1]);
    if (s_max_jump > jump_limit(s_scn)) {
        printf("  VIOLATION a jump of %.1f px in one frame (limit %.0f)\n", s_max_jump, jump_limit(s_scn));
        s_bad++;
    }
    printf("%s seed=%s frames=%u max_jump=%.1f %s\n", argv[1], argv[2], s_frames, s_max_jump,
           s_bad ? "FAIL" : "ok");
    return s_bad ? 1 : 0;
}
