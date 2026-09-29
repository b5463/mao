/*
 * MAO's light field: the lattice of marks that grows out of the centre,
 * whose reach is a light's brightness (and, at boot, the field MAO arrives
 * through). All of it is MAO's own (M4.1):
 *
 *   the marks    cut from the ODD JOBS symbol - the whole mark, its block,
 *                its tail, its slit (one or two), its hexagon form solid and
 *                hollow, dots along the tail's slant, a dash, a dot - crisp
 *                pixel art (mao_field_marks.c, tools/odd_field_marks.py);
 *   the lattice  offset rows, 16 px along, 14 px down;
 *   the life     each cell comes up as the reach passes it (a little ragged,
 *                later towards the edge); whether it is lit is decided again
 *                every OCC_MS on its own clock, so the gaps wander through
 *                the light; its mark changes every CELL_MS; the heart is
 *                dense and full-weight (the hexagon carries it, the whole
 *                mark is kept rare), the edge thin and light;
 *   the colour   one hue - MAO's - in four inks from a pale lead down to a
 *                faint one; a light's brightness sets how strong they are.
 *
 * On the LAMP page it moves the way the camera's iris does: each ring of
 * cells follows the brightness a little after the one inside it, so a change
 * ripples outward, and the start of a turn sends a soft wave of the pale lead
 * through the light (outward turning up, inward turning down).
 *
 * The owner sets the reach (and the clock) every tick; only the cells whose
 * mark, ink or opacity changed are invalidated.
 */
#include <math.h>
#include <string.h>
#include "mao_ui_priv.h"

#define N         8                     /* lattice half-extent: 17 x 17 cells */
#define SIDE      (2 * N + 1)
#define PITCH     16                    /* px between marks along a row (odd rows half a pitch across) */
#define ROW       14                    /* px between rows */
#define STEP_MS   65                    /* the field's clock advances in steps, ~15 a second */
#define CELL_MS   260                   /* how long a mark holds before it changes */
#define OCC_MS    1400                  /* how long a cell keeps its lit / dark before it decides again */
#define OCC_CORE  0.95f                 /* the chance a cell is lit at the heart ... */
#define OCC_EDGE  0.35f                 /* ... and at the edge */
#define ARRIVE_JIT 20.0f                /* px: how ragged the growing edge is (at the rim; tight at the heart) */
#define FAR       124.0f
#define FADE      18.0f                 /* px of travel over which a cell comes up */
#define HEART_U   0.5f                  /* inside this share of the reach: the full-weight marks */
#define LIGHT_U   0.25f                 /* the outermost share: only light marks */

#define RIPPLE_MS_PER_PX  1.3f          /* each ring lags the centre by its distance: ~150 ms to the rim */
#define HIST_N            32            /* the reach's recent past (a tick each, ~1 s) */
#define PULSE_N           4
#define PULSE_PX_PER_MS   0.30f         /* a wave crosses the light in ~350 ms */
#define PULSE_WIDTH       13.0f         /* px: the wave's half-width */

/* The body of the light: the hexagon - the symbol's outer form - carries it;
 * the whole mark is one mark among nine, so it stays special. */
static const uint8_t kBody[9] = { MAO_FM_HEX, MAO_FM_TAIL, MAO_FM_RING, MAO_FM_BLOCK, MAO_FM_MARK,
                                  MAO_FM_SLIT2, MAO_FM_SLIT, MAO_FM_DOTS3, MAO_FM_DOT };
/* The heart: full weight, the mark one in eight. */
static const uint8_t kHeart[8] = { MAO_FM_HEX, MAO_FM_RING, MAO_FM_MARK, MAO_FM_HEX, MAO_FM_BLOCK, MAO_FM_RING,
                                   MAO_FM_SLIT2, MAO_FM_HEX };
/* The edge: only the light marks. */
static const uint8_t kEdge[3] = { MAO_FM_SLIT, MAO_FM_DOT, MAO_FM_DASH };

static struct {
    lv_obj_t *obj;
    uint8_t glyph[SIDE][SIDE];          /* the mark shown (MAO_FM_*) */
    uint8_t ink[SIDE][SIDE];
    uint8_t opa[SIDE][SIDE];            /* 0 = dark */
    float edge[SIDE][SIDE];             /* the reach at which the cell comes up (fixed per cell) */
    lv_color_t ink_col[4];
    float reach;
    int16_t oy;                         /* the field's centre, px below the screen centre */
    bool any;
    bool motion;                        /* this frame's field is the LAMP page's */
    struct { uint32_t t; float reach; } hist[HIST_N];
    uint8_t hist_head;
    uint32_t moved_at;                  /* the reach last changed (the ripple still travelling) */
    struct { uint32_t t0; int8_t dir; float from; } pulse[PULSE_N];
} s_f;

static float s_level = 1.0f;            /* a light's brightness: the marks' strength */

static uint32_t hash2(int a, int b)
{
    uint32_t h = ((uint32_t)a * 73856093u) ^ ((uint32_t)b * 19349663u);   /* unsigned: wraps, never overflows */
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

void mao_field_motion(bool on)
{
    s_f.motion = on;
}

/* Faint and quiet near the bottom, the full colour and its pale lead near
 * the top. */
void mao_field_level(float level01)
{
    s_level = level01 < 0.0f ? 0.0f : level01 > 1.0f ? 1.0f : level01;
}

void mao_field_pulse(int dir)
{
    const uint32_t now = lv_tick_get();
    int slot = 0;
    for (int k = 0; k < PULSE_N; k++) {
        if (!s_f.pulse[k].dir) {
            slot = k;
            break;
        }
        if ((int32_t)(s_f.pulse[k].t0 - s_f.pulse[slot].t0) < 0) {
            slot = k;                   /* all busy: the oldest makes way */
        }
    }
    s_f.pulse[slot].t0 = now;
    s_f.pulse[slot].dir = (int8_t)(dir >= 0 ? 1 : -1);
    s_f.pulse[slot].from = s_f.reach;   /* an inward wave starts at the light's edge */
}

/* The reach as it was `ago` ms before `now` (the rings further out see it later). */
static float reach_ago(uint32_t now, float ago, float cur)
{
    const uint32_t want = now - (uint32_t)ago;
    for (int k = 0; k < HIST_N; k++) {
        const int i = (s_f.hist_head + HIST_N - k) % HIST_N;
        if (!s_f.hist[i].t) {
            break;
        }
        if ((int32_t)(s_f.hist[i].t - want) <= 0) {
            return s_f.hist[i].reach;
        }
    }
    return cur;
}

/* How much of a wave passes a cell at radius d (0..1). */
static float pulse_at(uint32_t now, float d)
{
    float w = 0.0f;
    for (int k = 0; k < PULSE_N; k++) {
        if (!s_f.pulse[k].dir) {
            continue;
        }
        const float run = (float)(int32_t)(now - s_f.pulse[k].t0) * PULSE_PX_PER_MS;
        const float front = s_f.pulse[k].dir > 0 ? run : s_f.pulse[k].from - run;
        if (front > s_f.reach + PULSE_WIDTH || front < -PULSE_WIDTH) {
            s_f.pulse[k].dir = 0;       /* through the light: gone */
            continue;
        }
        const float q = 1.0f - fabsf(d - front) / PULSE_WIDTH;
        w = q > w ? q : w;
    }
    return w;
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, float t)
{
    const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return ((uint32_t)(ar + (br - ar) * t) << 16) | ((uint32_t)(ag + (bg - ag) * t) << 8) |
           (uint32_t)(ab + (bb - ab) * t);
}

static uint32_t s_accent = MAO_COL_DOT;

static void build_inks(void)
{
    /* One hue, four strengths: pale lead, the colour, dim, faint. */
    s_f.ink_col[0] = lv_color_hex(mix_rgb(s_accent, 0xFFFFFF, 0.45f));
    s_f.ink_col[1] = lv_color_hex(s_accent);
    s_f.ink_col[2] = lv_color_hex(mix_rgb(s_accent, MAO_COL_BG, 0.45f));
    s_f.ink_col[3] = lv_color_hex(mix_rgb(s_accent, MAO_COL_BG, 0.72f));
}

void mao_field_set_accent(uint32_t color)
{
    if ((color & 0xF8FCF8u) == (s_accent & 0xF8FCF8u)) {
        return;                             /* the same on the panel: no redraw */
    }
    s_accent = color;
    build_inks();
    if (s_f.obj && s_f.any) {
        lv_obj_invalidate(s_f.obj);         /* every lit cell changes ink */
    }
}

/* A cell's centre, px from the field's centre (odd rows half a pitch across). */
static int cell_x(int gx, int gy)
{
    return gx * PITCH + ((gy & 1) ? PITCH / 2 : 0);
}

static void cell_area(const lv_area_t *o, int gx, int gy, lv_area_t *a)
{
    const int32_t cx = (o->x1 + o->x2 + 1) / 2, cy = (o->y1 + o->y2 + 1) / 2;
    const int w = (int)mao_field_mark[0].header.w, h = (int)mao_field_mark[0].header.h;
    a->x1 = cx + cell_x(gx, gy) - w / 2;
    a->y1 = cy + s_f.oy + gy * ROW - h / 2;
    a->x2 = a->x1 + w - 1;
    a->y2 = a->y1 + h - 1;
}

static void draw_cb(lv_event_t *e)
{
    if (!s_f.any) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t o;
    lv_obj_get_coords(s_f.obj, &o);
    const lv_area_t *clip = &layer->_clip_area;
    const int32_t cx = (o.x1 + o.x2 + 1) / 2, cy = (o.y1 + o.y2 + 1) / 2 + s_f.oy;
    const int w = (int)mao_field_mark[0].header.w, h = (int)mao_field_mark[0].header.h;
    /* Only the cells this area touches (an offset row: half a pitch more). */
    int j0 = (int)floorf((float)(clip->y1 - cy - h) / (float)ROW), j1 = (int)ceilf((float)(clip->y2 - cy + h) / (float)ROW);
    int i0 = (int)floorf((float)(clip->x1 - cx - w - PITCH) / (float)PITCH);
    int i1 = (int)ceilf((float)(clip->x2 - cx + w) / (float)PITCH);
    j0 = j0 < -N ? -N : j0;
    j1 = j1 > N ? N : j1;
    i0 = i0 < -N ? -N : i0;
    i1 = i1 > N ? N : i1;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.recolor_opa = LV_OPA_COVER;
    for (int gy = j0; gy <= j1; gy++) {
        for (int gx = i0; gx <= i1; gx++) {
            const uint8_t a = s_f.opa[gy + N][gx + N];
            if (!a) {
                continue;
            }
            lv_area_t cell;
            cell_area(&o, gx, gy, &cell);
            d.src = &mao_field_mark[s_f.glyph[gy + N][gx + N]];
            d.recolor = s_f.ink_col[s_f.ink[gy + N][gx + N]];
            d.opa = a;
            lv_draw_image(layer, &d, &cell);
        }
    }
}

void mao_field_create(lv_obj_t *scr)
{
    build_inks();
    /* When each cell's turn comes: ragged at the edge, tight at the heart. */
    for (int gy = -N; gy <= N; gy++) {
        for (int gx = -N; gx <= N; gx++) {
            const float px = (float)cell_x(gx, gy), py = (float)(gy * ROW);
            const float dist = sqrtf(px * px + py * py);
            const float jit = (float)(hash2(gx + 31, gy - 17) % 1024u) / 1024.0f;
            s_f.edge[gy + N][gx + N] = dist + jit * ARRIVE_JIT * (0.18f + 0.82f * dist / FAR);
        }
    }
    s_f.obj = lv_obj_create(scr);
    lv_obj_remove_style_all(s_f.obj);
    lv_obj_remove_flag(s_f.obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_f.obj, 240, 240);
    lv_obj_set_align(s_f.obj, LV_ALIGN_CENTER);
    lv_obj_add_event_cb(s_f.obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

void mao_field_set(float reach, float strength, float oy, uint32_t now_ms)
{
    if (reach <= 0.0f && !s_f.any) {
        return;                        /* dark and staying dark: nothing to do */
    }
    /* The cells change only on the field's stepped clock or when its inputs
     * move: with neither, the last result stands (no full recompute). */
    {
        static float l_reach = -1.0f, l_strength = -1.0f, l_oy = 1e9f, l_level = -1.0f;
        static uint32_t l_step = UINT32_MAX;
        const uint32_t step = now_ms / STEP_MS;
        bool waves = false;
        for (int k = 0; k < PULSE_N; k++) {
            if (s_f.pulse[k].dir && (int32_t)(now_ms - s_f.pulse[k].t0) > 1500) {
                s_f.pulse[k].dir = 0;       /* never seen through (another scene had the field) */
            }
            waves |= s_f.pulse[k].dir != 0;
        }
        const bool rippling = s_f.motion && (waves || (int32_t)(now_ms - s_f.moved_at) < 400);
        if (!rippling && step == l_step && fabsf(reach - l_reach) < 0.25f && fabsf(strength - l_strength) < 0.004f &&
            fabsf(oy - l_oy) < 0.25f && fabsf(s_level - l_level) < 0.004f) {
            return;
        }
        l_step = step;
        l_reach = reach;
        l_strength = strength;
        l_oy = oy;
        l_level = s_level;
    }
    const int16_t noy = (int16_t)lrintf(oy);
    if (noy != s_f.oy) {
        /* The field moved: everything it covered and will cover is dirty. */
        lv_obj_invalidate(s_f.obj);
        s_f.oy = noy;
        memset(s_f.opa, 0, sizeof(s_f.opa));
    }
    if (fabsf(reach - s_f.reach) >= 0.25f) {
        s_f.moved_at = now_ms;
    }
    s_f.reach = reach;
    s_f.hist_head = (uint8_t)((s_f.hist_head + 1) % HIST_N);
    s_f.hist[s_f.hist_head].t = now_ms | 1u;
    s_f.hist[s_f.hist_head].reach = reach;
    /* a light's brightness sets the marks' strength (a dim lamp, a faint field) */
    const float str = (strength > 1.0f ? 1.0f : strength) * (0.28f + 0.72f * s_level);
    const int ink_shift = s_level < 0.34f ? 2 : s_level < 0.67f ? 1 : 0;   /* the pale lead only when it is bright */
    const uint32_t stepped = now_ms - now_ms % STEP_MS;
    lv_area_t o;
    lv_obj_get_coords(s_f.obj, &o);
    bool any = false;
    int16_t band_x1[SIDE], band_x2[SIDE];   /* per row: the span of changed cells */
    for (int k = 0; k < SIDE; k++) {
        band_x1[k] = INT16_MAX;
        band_x2[k] = INT16_MIN;
    }
    for (int gy = -N; gy <= N; gy++) {
        for (int gx = -N; gx <= N; gx++) {
            uint8_t g = 0, k = 0, a = 0;
            const float px = (float)cell_x(gx, gy), py = (float)(gy * ROW) + (float)s_f.oy;
            const float edge = s_f.edge[gy + N][gx + N];
            if (px * px + py * py <= 116.0f * 116.0f) {   /* inside the circle */
                const float d = sqrtf(px * px + py * py);
                float cell_reach = reach, wave = 0.0f;
                if (s_f.motion) {
                    cell_reach = reach_ago(now_ms, d * RIPPLE_MS_PER_PX, reach);   /* the rings follow in turn */
                    wave = pulse_at(now_ms, d);
                }
                if (cell_reach > edge) {
                    const float u = 1.0f - d / cell_reach;    /* 1 at the heart, 0 at the reach */
                    const float uu = u > 0.0f ? u : 0.0f;
                    /* lit or dark, decided again on the cell's own clock: the gaps wander */
                    const uint32_t oph = ((uint32_t)gx * 7919u + (uint32_t)gy * 104729u + 1000003u) % OCC_MS;
                    const float pon = OCC_EDGE + (OCC_CORE - OCC_EDGE) * sqrtf(uu);
                    if (mao_dots_noise(gx + 31, gy + 17, (stepped + oph) / OCC_MS) < pon) {
                        const uint32_t gph = ((uint32_t)gx * 613u + (uint32_t)gy * 977u + 4099u) % CELL_MS;
                        const unsigned r9 = (unsigned)(mao_dots_noise(gx - 5, gy + 3, (stepped + gph) / CELL_MS) * 8.999f);
                        g = u > HEART_U ? kHeart[(r9 * 5u + (unsigned)(u * 16.0f)) % 8u]
                            : u > LIGHT_U ? kBody[r9]
                            : kEdge[r9 % 3u];
                        const int ink = u > 0.8f ? 0 : u > 0.5f ? 1 : u > 0.22f ? 2 : 3;
                        k = (uint8_t)(ink + ink_shift > 3 ? 3 : ink + ink_shift);
                        const float grow = fminf(1.0f, (cell_reach - edge) / FADE);
                        float op = 255.0f * grow * str;
                        if (wave > 0.0f) {
                            op += (255.0f * str - op) * wave * 0.85f;
                            if (wave > 0.45f) {
                                k = 0;              /* the wave is the pale lead */
                            }
                        }
                        a = (uint8_t)lrintf(op);
                    }
                }
            }
            uint8_t *so = &s_f.opa[gy + N][gx + N];
            uint8_t *sg = &s_f.glyph[gy + N][gx + N];
            uint8_t *sk = &s_f.ink[gy + N][gx + N];
            if (a != *so || (a && (g != *sg || k != *sk))) {
                *so = a;
                *sg = g;
                *sk = k;
                lv_area_t ar;
                cell_area(&o, gx, gy, &ar);
                const int r = gy + N;
                band_x1[r] = (int16_t)(ar.x1 < band_x1[r] ? ar.x1 : band_x1[r]);
                band_x2[r] = (int16_t)(ar.x2 > band_x2[r] ? ar.x2 : band_x2[r]);
            }
            any |= a != 0;
        }
    }
    /* One area per group of neighbouring changed rows, not one per cell. */
    const int32_t ocy = (o.y1 + o.y2 + 1) / 2 + s_f.oy;
    const int hh = (int)mao_field_mark[0].header.h / 2 + 1;
    for (int k = 0; k < SIDE;) {
        if (band_x1[k] > band_x2[k]) {
            k++;
            continue;
        }
        int e = k;
        int16_t x1 = band_x1[k], x2 = band_x2[k];
        while (e + 1 < SIDE && band_x1[e + 1] <= band_x2[e + 1] && e + 1 - k < 3) {
            e++;
            x1 = band_x1[e] < x1 ? band_x1[e] : x1;
            x2 = band_x2[e] > x2 ? band_x2[e] : x2;
        }
        const lv_area_t ar = { x1, ocy + (k - N) * ROW - hh, x2, ocy + (e - N) * ROW + hh };
        lv_obj_invalidate_area(s_f.obj, &ar);
        k = e + 1;
    }
    s_f.any = any;
}
