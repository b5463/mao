/*
 * The ODD JOBS field on MAO's round screen (odd_field.h: KINO D4's boot field,
 * scaled). One LVGL object; each lit lattice cell is drawn as the few
 * filled rectangles that cover its 5x5 glyph (precomputed, ~3 on average):
 * the cheapest primitive the software renderer has, no image decoding. Only
 * cells inside the area being redrawn are issued, and only changed cells are
 * invalidated, so a steady field costs a handful of cells per frame. The four
 * inks are one hue - MAO's colour - from a pale lead down to a faint one.
 *
 * The owner sets the reach (and the clock) every tick; only the cells whose
 * mark, ink or opacity changed are invalidated.
 */
#include <math.h>
#include <string.h>
#include "odd_field.h"
#include "mao_ui_priv.h"

#define N        10                     /* lattice half-extent: 21 x 21 cells */
#define SIDE     (2 * N + 1)
#define GLYPH_PX 10                     /* 5 bits x 2 px */
#define STEP_MS  65                     /* the field's clock advances in steps: a quarter of a mark's hold,
                                         * so re-rolls stay scattered but land ~15 times a second */

static const odd_field_geom_t kGeom = ODD_FIELD_GEOM_ROUND240;

static struct {
    lv_obj_t *obj;
    uint8_t glyph[SIDE][SIDE];
    uint8_t ink[SIDE][SIDE];
    uint8_t opa[SIDE][SIDE];            /* 0 = dark */
    lv_color_t ink_col[4];
    float reach;
    int16_t oy;                         /* the field's centre, px below the screen centre */
    bool any;
} s_f;

static uint32_t mix_rgb(uint32_t a, uint32_t b, float t)
{
    const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return ((uint32_t)(ar + (br - ar) * t) << 16) | ((uint32_t)(ag + (bg - ag) * t) << 8) |
           (uint32_t)(ab + (bb - ab) * t);
}

/* Each glyph as rectangles in bit units: x, y, w, h (0 = end of list). */
static const uint8_t kRects[ODD_FIELD_GLYPHS][8][4] = {
    [ODD_FIELD_HASH]   = { {1,0,1,5}, {3,0,1,5}, {0,1,5,1}, {0,3,5,1} },
    [ODD_FIELD_STAR]   = { {0,2,5,1}, {2,0,1,5}, {1,1,3,3}, {0,0,1,1}, {4,0,1,1}, {0,4,1,1}, {4,4,1,1} },
    [ODD_FIELD_NOTCH]  = { {1,0,4,1}, {0,1,1,4}, {4,0,1,5}, {0,4,5,1}, {2,2,1,1} },
    [ODD_FIELD_DISC]   = { {0,1,5,3}, {1,0,3,5} },
    [ODD_FIELD_SQUARE] = { {0,0,5,5} },
    [ODD_FIELD_BAR2]   = { {1,1,1,3}, {3,1,1,3} },
    [ODD_FIELD_BAR1]   = { {2,1,1,3} },
    [ODD_FIELD_RUN]    = { {0,2,1,1}, {2,2,1,1}, {4,2,1,1} },
    [ODD_FIELD_DOT]    = { {2,2,1,1} },
};

static void build_inks(void)
{
    /* One hue, four strengths: pale lead, the colour, dim, faint. */
    s_f.ink_col[0] = lv_color_hex(mix_rgb(MAO_COL_DOT, 0xFFFFFF, 0.45f));
    s_f.ink_col[1] = lv_color_hex(MAO_COL_DOT);
    s_f.ink_col[2] = lv_color_hex(mix_rgb(MAO_COL_DOT, MAO_COL_BG, 0.45f));
    s_f.ink_col[3] = lv_color_hex(mix_rgb(MAO_COL_DOT, MAO_COL_BG, 0.72f));
}

static void cell_area(const lv_area_t *o, int gx, int gy, lv_area_t *a)
{
    const int32_t cx = (o->x1 + o->x2 + 1) / 2, cy = (o->y1 + o->y2 + 1) / 2;
    a->x1 = cx + gx * kGeom.pitch - GLYPH_PX / 2;
    a->y1 = cy + s_f.oy + gy * kGeom.pitch - GLYPH_PX / 2;
    a->x2 = a->x1 + GLYPH_PX - 1;
    a->y2 = a->y1 + GLYPH_PX - 1;
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
    const float pit = (float)kGeom.pitch;
    /* Only the cells this area touches. */
    int j0 = (int)floorf((float)(clip->y1 - cy - GLYPH_PX) / pit), j1 = (int)ceilf((float)(clip->y2 - cy + GLYPH_PX) / pit);
    int i0 = (int)floorf((float)(clip->x1 - cx - GLYPH_PX) / pit), i1 = (int)ceilf((float)(clip->x2 - cx + GLYPH_PX) / pit);
    j0 = j0 < -N ? -N : j0;
    j1 = j1 > N ? N : j1;
    i0 = i0 < -N ? -N : i0;
    i1 = i1 > N ? N : i1;
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = 0;
    const int b = kGeom.bit;
    for (int gy = j0; gy <= j1; gy++) {
        for (int gx = i0; gx <= i1; gx++) {
            const uint8_t a = s_f.opa[gy + N][gx + N];
            if (!a) {
                continue;
            }
            lv_area_t cell;
            cell_area(&o, gx, gy, &cell);
            r.bg_color = s_f.ink_col[s_f.ink[gy + N][gx + N]];
            r.bg_opa = a;
            const uint8_t (*rc)[4] = kRects[s_f.glyph[gy + N][gx + N]];
            for (int k = 0; k < 8 && rc[k][2]; k++) {
                const lv_area_t ar = { cell.x1 + rc[k][0] * b, cell.y1 + rc[k][1] * b,
                                       cell.x1 + (rc[k][0] + rc[k][2]) * b - 1, cell.y1 + (rc[k][1] + rc[k][3]) * b - 1 };
                lv_draw_rect(layer, &r, &ar);
            }
        }
    }
}

void mao_field_create(lv_obj_t *scr)
{
    build_inks();
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
     * move: with neither, the last result stands (no 441-cell recompute). */
    {
        static float l_reach = -1.0f, l_strength = -1.0f, l_oy = 1e9f;
        static uint32_t l_step = UINT32_MAX;
        const uint32_t step = now_ms / STEP_MS;
        if (step == l_step && fabsf(reach - l_reach) < 0.25f && fabsf(strength - l_strength) < 0.004f &&
            fabsf(oy - l_oy) < 0.25f) {
            return;
        }
        l_step = step;
        l_reach = reach;
        l_strength = strength;
        l_oy = oy;
    }
    const int16_t noy = (int16_t)lrintf(oy);
    if (noy != s_f.oy) {
        /* The field moved: everything it covered and will cover is dirty. */
        lv_obj_invalidate(s_f.obj);
        s_f.oy = noy;
        memset(s_f.opa, 0, sizeof(s_f.opa));
    }
    s_f.reach = reach;
    lv_area_t o;
    lv_obj_get_coords(s_f.obj, &o);
    bool any = false;
    int changed = 0;
    now_ms -= now_ms % STEP_MS;
    int16_t band_x1[SIDE], band_x2[SIDE];   /* per row: the span of changed cells */
    for (int k = 0; k < SIDE; k++) {
        band_x1[k] = INT16_MAX;
        band_x2[k] = INT16_MIN;
    }
    for (int gy = -N; gy <= N; gy++) {
        for (int gx = -N; gx <= N; gx++) {
            uint8_t g = 0, k = 0, a = 0;
            const float px = (float)(gx * kGeom.pitch), py = (float)(gy * kGeom.pitch) + (float)s_f.oy;
            if (px * px + py * py <= 116.0f * 116.0f) {   /* inside the circle */
                odd_field_cell_t c;
                if (odd_field_cell(&kGeom, gx, gy, reach, (int32_t)now_ms, &c)) {
                    g = c.glyph;
                    k = c.ink;
                    a = (uint8_t)lrintf(255.0f * c.grow * (strength > 1.0f ? 1.0f : strength));
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
                const int row = gy + N;
                band_x1[row] = (int16_t)(ar.x1 < band_x1[row] ? ar.x1 : band_x1[row]);
                band_x2[row] = (int16_t)(ar.x2 > band_x2[row] ? ar.x2 : band_x2[row]);
                changed++;
            }
            any |= a != 0;
        }
    }
    /* One area per group of neighbouring changed rows, not one per cell. */
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
        lv_area_t top, bot;
        cell_area(&o, 0, k - N, &top);
        cell_area(&o, 0, e - N, &bot);
        const lv_area_t ar = { x1, top.y1, x2, bot.y2 };
        lv_obj_invalidate_area(s_f.obj, &ar);
        k = e + 1;
    }
    s_f.any = any;
    (void)changed;
}
