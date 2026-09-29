/*
 * The dot field (M4.1, after the user's references): MAO's system language is
 * glowing glyphs on a black circle - a diagonal lattice of dots, squares,
 * pluses, crosses, dashes and asterisks whose kind and size follow the
 * distance from the centre; fields that bloom from one centre dot, breathe
 * and turn, and collapse back; a solid core disc with rings of glyphs around
 * it. The few unavoidable words are drawn with the same dots (5x7 matrix).
 *
 * One LVGL object with a custom draw callback renders a per-frame display
 * list; nothing is an LVGL object per glyph. Scenes build the list each UI
 * tick (begin / add... / end); end() invalidates only when it changed. The
 * draw callback skips glyphs outside the strip LVGL is rendering.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mao_ui_priv.h"

#define DOTS_MAX      1200
#define LATTICE       9.0f       /* px between lattice cells (diagonal grid) */
#define FIELD_R       118.0f
#define PI_F          3.14159265f

typedef struct {
    int16_t x, y;
    uint8_t d;                 /* size, px */
    uint8_t opa;
    uint8_t kind;              /* mao_glyph_t */
    uint8_t col;               /* 0 MAO, 1 ground (cut-out) */
} dot_t;

static struct {
    lv_obj_t *obj;
    dot_t dot[DOTS_MAX];
    int n;
    uint32_t hash, last_hash;
    dot_t prev[DOTS_MAX];      /* the last frame drawn: only what changed is redrawn */
    int prev_n;
} s_d;

/* An internal kind: a pre-rendered image (the name on the rim), x / y its
 * centre, d its cache slot, col its generation (so a new name redraws). */
#define DOT_IMAGE 200

#define ARC_DOTS 260
#define ARC_TRACK 1.5f      /* extra columns between letters on the curve: the inner rows crowd otherwise */

float mao_dots_text_arc_width(const char *s, float pitch)
{
    const int n = (int)strlen(s);
    return n > 0 ? ((float)n * (6.0f + ARC_TRACK) - (1.0f + ARC_TRACK)) * pitch : 0.0f;
}
typedef struct {
    char s[24];
    float r, pitch;
    bool bottom;
    int n;
    float x[ARC_DOTS], y[ARC_DOTS], th[ARC_DOTS];
    uint32_t used;
    float d;                   /* the dot size the mask was rendered at */
    uint8_t *px;               /* the name, rendered once: an anti-aliased A8 mask */
    lv_image_dsc_t img;
    int16_t cx, cy;            /* the mask's centre, px from the screen centre */
    uint8_t gen;
} arc_text_t;
static arc_text_t s_arc[2];
static uint32_t s_arc_clock;

#define DIRTY_MAX 16           /* more changed dots than this: one full redraw (LVGL keeps 32 areas, shared) */

/* 5x7 matrix, columns left to right, bit 0 = top row. ' '..'Z'. */
static const uint8_t kFont[][5] = {
    [' ' - ' '] = { 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['%' - ' '] = { 0x23, 0x13, 0x08, 0x64, 0x62 },
    ['-' - ' '] = { 0x08, 0x08, 0x08, 0x08, 0x08 },
    ['.' - ' '] = { 0x00, 0x60, 0x60, 0x00, 0x00 },
    ['0' - ' '] = { 0x3E, 0x51, 0x49, 0x45, 0x3E },
    ['1' - ' '] = { 0x00, 0x42, 0x7F, 0x40, 0x00 },
    ['2' - ' '] = { 0x42, 0x61, 0x51, 0x49, 0x46 },
    ['3' - ' '] = { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    ['4' - ' '] = { 0x18, 0x14, 0x12, 0x7F, 0x10 },
    ['5' - ' '] = { 0x27, 0x45, 0x45, 0x45, 0x39 },
    ['6' - ' '] = { 0x3C, 0x4A, 0x49, 0x49, 0x30 },
    ['7' - ' '] = { 0x01, 0x71, 0x09, 0x05, 0x03 },
    ['8' - ' '] = { 0x36, 0x49, 0x49, 0x49, 0x36 },
    ['9' - ' '] = { 0x06, 0x49, 0x49, 0x29, 0x1E },
    ['?' - ' '] = { 0x02, 0x01, 0x51, 0x09, 0x06 },
    ['A' - ' '] = { 0x7E, 0x11, 0x11, 0x11, 0x7E },
    ['B' - ' '] = { 0x7F, 0x49, 0x49, 0x49, 0x36 },
    ['C' - ' '] = { 0x3E, 0x41, 0x41, 0x41, 0x22 },
    ['D' - ' '] = { 0x7F, 0x41, 0x41, 0x22, 0x1C },
    ['E' - ' '] = { 0x7F, 0x49, 0x49, 0x49, 0x41 },
    ['F' - ' '] = { 0x7F, 0x09, 0x09, 0x09, 0x01 },
    ['G' - ' '] = { 0x3E, 0x41, 0x49, 0x49, 0x7A },
    ['H' - ' '] = { 0x7F, 0x08, 0x08, 0x08, 0x7F },
    ['I' - ' '] = { 0x00, 0x41, 0x7F, 0x41, 0x00 },
    ['J' - ' '] = { 0x20, 0x40, 0x41, 0x3F, 0x01 },
    ['K' - ' '] = { 0x7F, 0x08, 0x14, 0x22, 0x41 },
    ['L' - ' '] = { 0x7F, 0x40, 0x40, 0x40, 0x40 },
    ['M' - ' '] = { 0x7F, 0x02, 0x0C, 0x02, 0x7F },
    ['N' - ' '] = { 0x7F, 0x04, 0x08, 0x10, 0x7F },
    ['O' - ' '] = { 0x3E, 0x41, 0x41, 0x41, 0x3E },
    ['P' - ' '] = { 0x7F, 0x09, 0x09, 0x09, 0x06 },
    ['Q' - ' '] = { 0x3E, 0x41, 0x51, 0x21, 0x5E },
    ['R' - ' '] = { 0x7F, 0x09, 0x19, 0x29, 0x46 },
    ['S' - ' '] = { 0x46, 0x49, 0x49, 0x49, 0x31 },
    ['T' - ' '] = { 0x01, 0x01, 0x7F, 0x01, 0x01 },
    ['U' - ' '] = { 0x3F, 0x40, 0x40, 0x40, 0x3F },
    ['V' - ' '] = { 0x1F, 0x20, 0x40, 0x20, 0x1F },
    ['W' - ' '] = { 0x3F, 0x40, 0x38, 0x40, 0x3F },
    ['X' - ' '] = { 0x63, 0x14, 0x08, 0x14, 0x63 },
    ['Y' - ' '] = { 0x07, 0x08, 0x70, 0x08, 0x07 },
    ['Z' - ' '] = { 0x61, 0x51, 0x49, 0x45, 0x43 },
};

static const uint8_t *glyph_bits(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c < ' ' || c > 'Z') {
        c = '?';
    }
    const uint8_t *g = kFont[c - ' '];
    if (c != ' ' && !(g[0] | g[1] | g[2] | g[3] | g[4])) {
        g = kFont['?' - ' '];          /* a character this matrix has no shape for */
    }
    return g;
}

static uint32_t hash_step(uint32_t h, uint32_t v)
{
    return (h ^ v) * 16777619u;
}

float mao_dots_noise(int i, int j, uint32_t t)
{
    uint32_t h = ((uint32_t)i * 73856093u) ^ ((uint32_t)j * 19349663u) ^ (t * 83492791u);   /* unsigned: wraps, never overflows */
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (float)(h & 0xFFFF) / 65535.0f;
}

/* ---------------------------------------------------------------------- */

static void rect(lv_layer_t *layer, lv_draw_rect_dsc_t *r, int32_t x1, int32_t y1, int32_t w, int32_t h)
{
    const lv_area_t a = { x1, y1, x1 + w - 1, y1 + h - 1 };
    lv_draw_rect(layer, r, &a);
}

static uint32_t s_accent = MAO_COL_DOT;

void mao_dots_set_accent(uint32_t color)
{
    if ((color & 0xF8FCF8u) == (s_accent & 0xF8FCF8u)) {
        return;                             /* the same on the panel: no redraw */
    }
    s_accent = color;
    if (s_d.obj && s_d.n > 0) {
        lv_obj_invalidate(s_d.obj);
    }
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int32_t cx = (a.x1 + a.x2 + 1) / 2, cy = (a.y1 + a.y2 + 1) / 2;
    const lv_area_t *clip = &layer->_clip_area;
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    lv_draw_line_dsc_t ln;
    lv_draw_line_dsc_init(&ln);
    const lv_color_t cols[2] = { lv_color_hex(s_accent), lv_color_hex(MAO_COL_BG) };
    for (int i = 0; i < s_d.n; i++) {
        const dot_t *d = &s_d.dot[i];
        const int32_t s = d->d, h = s / 2;
        const int32_t x0 = cx + d->x - h, y0 = cy + d->y - h;
        if (d->kind != DOT_IMAGE && (y0 + s < clip->y1 || y0 > clip->y2)) {
            continue;                  /* not in the strip being rendered */
        }
        r.bg_color = cols[d->col & 1];
        r.bg_opa = d->opa;
        const int32_t t = s >= 7 ? 2 : 1;   /* stroke of the line glyphs */
        if (d->kind == DOT_IMAGE) {
            const arc_text_t *at = &s_arc[d->d & 1];
            if (!at->px) {
                continue;
            }
            lv_draw_image_dsc_t id;
            lv_draw_image_dsc_init(&id);
            id.src = &at->img;
            id.recolor = cols[0];
            id.recolor_opa = LV_OPA_COVER;
            id.opa = d->opa;
            const lv_area_t ia = { cx + d->x - (int32_t)at->img.header.w / 2, cy + d->y - (int32_t)at->img.header.h / 2,
                                   cx + d->x - (int32_t)at->img.header.w / 2 + (int32_t)at->img.header.w - 1,
                                   cy + d->y - (int32_t)at->img.header.h / 2 + (int32_t)at->img.header.h - 1 };
            lv_draw_image(layer, &id, &ia);
            continue;
        }
        switch (d->kind) {
        case MAO_GLYPH_SQUARE:
            r.radius = s >= 5 ? 1 : 0;
            rect(layer, &r, x0, y0, s, s);
            break;
        case MAO_GLYPH_PLUS:
        case MAO_GLYPH_STAR:
            r.radius = 0;
            rect(layer, &r, x0, cy + d->y - t / 2, s, t);
            rect(layer, &r, cx + d->x - t / 2, y0, t, s);
            if (d->kind == MAO_GLYPH_PLUS) {
                break;
            }
            __attribute__((fallthrough));   /* the asterisk adds its diagonals */
        case MAO_GLYPH_CROSS: {
            ln.color = cols[d->col & 1];
            ln.opa = d->opa;
            ln.width = t;
            const float q = (float)h * (d->kind == MAO_GLYPH_STAR ? 0.72f : 1.0f);
            ln.p1 = (lv_point_precise_t) { cx + d->x - q, cy + d->y - q };
            ln.p2 = (lv_point_precise_t) { cx + d->x + q, cy + d->y + q };
            lv_draw_line(layer, &ln);
            ln.p1 = (lv_point_precise_t) { cx + d->x - q, cy + d->y + q };
            ln.p2 = (lv_point_precise_t) { cx + d->x + q, cy + d->y - q };
            lv_draw_line(layer, &ln);
            break;
        }
        case MAO_GLYPH_DASH:
            r.radius = 0;
            rect(layer, &r, x0, cy + d->y - t / 2, s, t);
            break;
        case MAO_GLYPH_DOT:
        default:
            /* A circle is the renderer's dearest shape; at a few px it reads
             * the same as a square with its corners eased. */
            r.radius = s <= 3 ? 0 : (s <= 6 ? 1 : LV_RADIUS_CIRCLE);
            rect(layer, &r, x0, y0, s, s);
            break;
        }
    }
}

void mao_dots_create(lv_obj_t *scr)
{
    s_d.obj = lv_obj_create(scr);
    lv_obj_remove_style_all(s_d.obj);
    lv_obj_remove_flag(s_d.obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_d.obj, 240, 240);
    lv_obj_set_align(s_d.obj, LV_ALIGN_CENTER);
    lv_obj_add_event_cb(s_d.obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

void mao_dots_begin(void)
{
    s_d.n = 0;
    s_d.hash = 2166136261u;
}

void mao_dots_glyph(float x, float y, float size, float opa, mao_glyph_t kind, uint8_t col)
{
    if (s_d.n >= DOTS_MAX || opa < 6.0f || size < 1.0f) {
        return;
    }
    /* Integer rounding and an integer circle test: floats are software on
     * the C3 and this runs for every dot of every frame. */
    const int32_t ix = (int32_t)(x < 0.0f ? x - 0.5f : x + 0.5f), iy = (int32_t)(y < 0.0f ? y - 0.5f : y + 0.5f);
    if (ix * ix + iy * iy > 119 * 119) {
        return;                        /* the circle is the frame */
    }
    dot_t *o = &s_d.dot[s_d.n++];
    o->x = (int16_t)ix;
    o->y = (int16_t)iy;
    o->d = (uint8_t)(size > 120.0f ? 120 : (int32_t)(size + 0.5f));
    o->opa = (uint8_t)(opa > 255.0f ? 255 : opa);
    o->kind = (uint8_t)kind;
    o->col = col;
    s_d.hash = hash_step(s_d.hash, ((uint32_t)(uint16_t)o->x << 16) | (uint16_t)o->y);
    s_d.hash = hash_step(s_d.hash, ((uint32_t)o->d << 24) | ((uint32_t)o->opa << 16) | ((uint32_t)o->kind << 8) | col);
}

void mao_dots_add(float x, float y, float d, float opa)
{
    mao_dots_glyph(x, y, d, opa, MAO_GLYPH_DOT, 0);
}

int mao_dots_text_cols(const char *s)
{
    const int n = (int)strlen(s);
    return n > 0 ? n * 6 - 1 : 0;
}

void mao_dots_text(const char *s, float cx, float cy, float pitch, float d, float opa, float reveal_r)
{
    const int cols = mao_dots_text_cols(s);
    const float x0 = cx - (float)(cols - 1) * pitch * 0.5f;
    const float y0 = cy - 3.0f * pitch;
    for (int k = 0; s[k]; k++) {
        const uint8_t *g = glyph_bits(s[k]);
        for (int c = 0; c < 5; c++) {
            for (int row = 0; row < 7; row++) {
                if (!(g[c] & (1u << row))) {
                    continue;
                }
                const float x = x0 + (float)(k * 6 + c) * pitch, y = y0 + (float)row * pitch;
                if (reveal_r >= 0.0f && x * x + y * y > reveal_r * reveal_r) {
                    continue;          /* not yet reached by the bloom */
                }
                mao_dots_glyph(x, y, d, opa, MAO_GLYPH_SQUARE, 0);
            }
        }
    }
}

void mao_dots_text_front(const char *s, float cx, float cy, float pitch, float d, float opa, float front)
{
    const int cols = mao_dots_text_cols(s);
    const float x0 = cx - (float)(cols - 1) * pitch * 0.5f;
    const float y0 = cy - 3.0f * pitch;
    for (int k = 0; s[k]; k++) {
        const uint8_t *g = glyph_bits(s[k]);
        for (int c = 0; c < 5; c++) {
            for (int row = 0; row < 7; row++) {
                if (!(g[c] & (1u << row))) {
                    continue;
                }
                const float x = x0 + (float)(k * 6 + c) * pitch, y = y0 + (float)row * pitch;
                /* each dot's own moment: a ragged, soft front, as the field's */
                uint32_t h = (uint32_t)(k * 131 + c * 17 + row) * 2654435761u;
                h ^= h >> 15;
                const float jit = (float)(h % 1024u) / 1024.0f;
                const float kf = (front - (sqrtf(x * x + y * y) + jit * 22.0f)) / 12.0f;
                if (kf <= 0.0f) {
                    continue;
                }
                mao_dots_glyph(x, y, d, opa * (kf < 1.0f ? kf : 1.0f), MAO_GLYPH_SQUARE, 0);
            }
        }
    }
}

/* Text set along the rim (M4.1): centred at the top, reading clockwise with
 * the letters upright (their tops outward), or at the bottom, reading
 * counter-clockwise with their tops towards the centre - so it reads left
 * to right either way. r is the radius of the middle row. The positions are
 * cached per text (a sine and a cosine per column otherwise, every frame). */

static const arc_text_t *arc_layout(const char *s, float r, bool bottom, float pitch)
{
    s_arc_clock++;
    for (int i = 0; i < 2; i++) {
        arc_text_t *a = &s_arc[i];
        if (a->n >= 0 && a->r == r && a->pitch == pitch && a->bottom == bottom && !strncmp(a->s, s, sizeof(a->s))) {
            a->used = s_arc_clock;
            return a;
        }
    }
    arc_text_t *a = s_arc[0].used <= s_arc[1].used ? &s_arc[0] : &s_arc[1];
    snprintf(a->s, sizeof(a->s), "%s", s);
    a->r = r;
    a->pitch = pitch;
    a->bottom = bottom;
    a->used = s_arc_clock;
    a->n = 0;
    a->d = -1.0f;                                    /* the mask is rendered on first use */
    const float width = mao_dots_text_arc_width(s, pitch) / pitch;   /* in columns */
    for (int k = 0; s[k]; k++) {
        const uint8_t *g = glyph_bits(s[k]);
        for (int c = 0; c < 5; c++) {
            if (!g[c]) {
                continue;
            }
            const float along = ((float)k * (6.0f + ARC_TRACK) + (float)c - (width - 1.0f) * 0.5f) * pitch;
            const float th = along / r, sn = sinf(th), cs = cosf(th);
            for (int row = 0; row < 7 && a->n < ARC_DOTS; row++) {
                if (!(g[c] & (1u << row))) {
                    continue;
                }
                const float up = (3.0f - (float)row) * pitch;          /* the letter's top outward (top) */
                const float rr = bottom ? r - up : r + up;
                a->x[a->n] = rr * sn;
                a->y[a->n] = bottom ? rr * cs : -rr * cs;
                a->th[a->n] = bottom ? -th : th;
                a->n++;
            }
        }
    }
    return a;
}

/* The name on the rim as one image: each of its dots a square turned with
 * the curve, at its exact place, anti-aliased (4 x 4 samples a pixel). Drawn
 * dot by dot, every dot was rounded to a pixel on its own - neighbours in
 * one stroke went different ways, and the letters looked hand-scratched. */
static void arc_render(arc_text_t *a, float d)
{
    free(a->px);
    a->px = NULL;
    a->d = d;
    if (a->n == 0) {
        return;
    }
    float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
    for (int i = 0; i < a->n; i++) {
        x1 = fminf(x1, a->x[i]);
        y1 = fminf(y1, a->y[i]);
        x2 = fmaxf(x2, a->x[i]);
        y2 = fmaxf(y2, a->y[i]);
    }
    const int pad = (int)ceilf(d) + 1;
    const int ox = (int)floorf(x1) - pad, oy = (int)floorf(y1) - pad;
    const int w = (int)ceilf(x2) + pad - ox + 1, h = (int)ceilf(y2) + pad - oy + 1;
    a->px = calloc((size_t)w * (size_t)h, 1);
    if (!a->px) {
        return;
    }
    const float half = d * 0.5f;
    for (int i = 0; i < a->n; i++) {
        const float cs = cosf(a->th[i]), sn = sinf(a->th[i]);
        const int bx0 = (int)floorf(a->x[i] - d) - ox, bx1 = (int)ceilf(a->x[i] + d) - ox;
        const int by0 = (int)floorf(a->y[i] - d) - oy, by1 = (int)ceilf(a->y[i] + d) - oy;
        for (int py = by0; py <= by1; py++) {
            for (int px = bx0; px <= bx1; px++) {
                if (px < 0 || py < 0 || px >= w || py >= h) {
                    continue;
                }
                int hit = 0;
                for (int sy = 0; sy < 4; sy++) {
                    for (int sx = 0; sx < 4; sx++) {
                        const float qx = (float)(px + ox) + ((float)sx + 0.5f) * 0.25f - 0.5f - a->x[i];
                        const float qy = (float)(py + oy) + ((float)sy + 0.5f) * 0.25f - 0.5f - a->y[i];
                        const float u = qx * cs + qy * sn, v = -qx * sn + qy * cs;   /* into the dot's own frame */
                        hit += fabsf(u) <= half && fabsf(v) <= half;
                    }
                }
                const int val = a->px[py * w + px] + hit * 21;   /* a little over full: the small dots keep their weight */
                a->px[py * w + px] = (uint8_t)(val > 255 ? 255 : val);
            }
        }
    }
    a->img = (lv_image_dsc_t){ .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
                                           .w = (uint32_t)w, .h = (uint32_t)h, .stride = (uint32_t)w },
                               .data_size = (uint32_t)(w * h), .data = a->px };
    a->cx = (int16_t)(ox + w / 2);
    a->cy = (int16_t)(oy + h / 2);
    a->gen++;
}

static void push_image(int slot, const arc_text_t *a, float opa)
{
    if (s_d.n >= DOTS_MAX || opa < 6.0f || !a->px) {
        return;
    }
    dot_t *o = &s_d.dot[s_d.n++];
    o->x = a->cx;
    o->y = a->cy;
    o->d = (uint8_t)slot;
    o->opa = (uint8_t)(opa > 255.0f ? 255 : opa);
    o->kind = DOT_IMAGE;
    o->col = a->gen;
    s_d.hash = hash_step(s_d.hash, ((uint32_t)(uint16_t)o->x << 16) | (uint16_t)o->y);
    s_d.hash = hash_step(s_d.hash, ((uint32_t)o->d << 24) | ((uint32_t)o->opa << 16) | ((uint32_t)o->kind << 8) | o->col);
}

void mao_dots_text_arc(const char *s, float r, bool bottom, float pitch, float d, float opa, bool halo)
{
    if (opa < 6.0f || !s[0]) {
        return;
    }
    arc_text_t *a = (arc_text_t *)arc_layout(s, r, bottom, pitch);
    if (halo) {
        for (int i = 0; i < a->n; i++) {
            mao_dots_glyph(a->x[i], a->y[i], pitch * 2.6f, 235.0f * (opa / 255.0f), MAO_GLYPH_SQUARE, 1);
        }
    }
    if (a->d != d) {
        arc_render(a, d);
    }
    push_image((int)(a - s_arc), a, opa);
}

void mao_dots_text_halo(const char *s, float cx, float cy, float pitch, float d, float opa)
{
    const int cols = mao_dots_text_cols(s);
    /* A black block behind the whole word first, then the word. */
    const float w = (float)(cols + 2) * pitch, h = 9.0f * pitch;
    for (float y = cy - h * 0.5f + pitch * 0.5f; y < cy + h * 0.5f; y += pitch * 2.0f) {
        for (float x = cx - w * 0.5f + pitch * 0.5f; x < cx + w * 0.5f; x += pitch * 2.0f) {
            mao_dots_glyph(x + pitch * 0.5f, y + pitch * 0.5f, pitch * 2.0f + 1.0f, 235.0f * (opa / 255.0f),
                           MAO_GLYPH_SQUARE, 1);
        }
    }
    mao_dots_text(s, cx, cy, pitch, d, opa, -1.0f);
}

/* The lattice field: a diagonal grid that turns slowly; glyphs are busy
 * near the core, plain in the middle, fine dashes towards the edge. */
void mao_dots_field(float radius, float strength, float angle, uint32_t now_ms)
{
    if (radius < 1.0f || strength < 0.01f) {
        return;
    }
    const uint32_t t = now_ms / 160;   /* the shimmer steps, it does not flicker */
    const float ca = cosf(angle + PI_F / 4.0f), sa = sinf(angle + PI_F / 4.0f);
    const int n = (int)(FIELD_R / LATTICE) + 1;
    for (int j = -n; j <= n; j++) {
        for (int i = -n; i <= n; i++) {
            const float gx = (float)i * LATTICE, gy = (float)j * LATTICE;
            const float x = gx * ca - gy * sa, y = gx * sa + gy * ca;
            const float r = sqrtf(x * x + y * y);
            if (r > radius) {
                continue;
            }
            const float u = 1.0f - r / radius;          /* 1 at the core */
            const float nz = mao_dots_noise(i, j, t);
            if (nz < 0.22f + 0.30f * (1.0f - u)) {
                continue;                               /* sparser outwards */
            }
            mao_glyph_t k;
            float size;
            if (u > 0.72f) {
                k = nz > 0.8f ? MAO_GLYPH_STAR : (nz > 0.6f ? MAO_GLYPH_PLUS : MAO_GLYPH_SQUARE);
                size = 5.0f + 2.0f * u;
            } else if (u > 0.38f) {
                k = nz > 0.75f ? MAO_GLYPH_PLUS : (nz > 0.5f ? MAO_GLYPH_SQUARE : MAO_GLYPH_DOT);
                size = 3.5f + 2.5f * u;
            } else {
                k = nz > 0.7f ? MAO_GLYPH_DASH : MAO_GLYPH_DOT;
                size = 2.0f + 3.0f * u;
            }
            mao_dots_glyph(x, y, size, (60.0f + 195.0f * u) * strength * (0.6f + 0.4f * nz), k, 0);
        }
    }
}

/* One ring of glyphs at radius r: count glyphs from angle phase, over the
 * given fraction of the circle (1 = full). */
void mao_dots_ring(float r, int count, float size, float opa, mao_glyph_t kind, float phase, float fraction)
{
    if (r < 1.0f || count <= 0 || opa < 6.0f) {
        return;
    }
    const int shown = (int)((float)count * fraction + 0.5f);
    for (int k = 0; k < shown; k++) {
        const float a = phase + (float)k * 2.0f * PI_F / (float)count;
        mao_dots_glyph(r * sinf(a), -r * cosf(a), size, opa, kind, 0);
    }
}

static bool same_dot(const dot_t *a, const dot_t *b)
{
    return a->x == b->x && a->y == b->y && a->d == b->d && a->opa == b->opa && a->kind == b->kind &&
           a->col == b->col;
}

static void dot_area(const lv_area_t *o, const dot_t *d, lv_area_t *a)
{
    const int32_t cx = (o->x1 + o->x2 + 1) / 2, cy = (o->y1 + o->y2 + 1) / 2;
    if (d->kind == DOT_IMAGE) {
        const arc_text_t *at = &s_arc[d->d & 1];
        const int32_t hw = (int32_t)at->img.header.w / 2 + 1, hh = (int32_t)at->img.header.h / 2 + 1;
        a->x1 = cx + d->x - hw;
        a->y1 = cy + d->y - hh;
        a->x2 = cx + d->x + hw;
        a->y2 = cy + d->y + hh;
        return;
    }
    const int32_t h = d->d / 2 + 2;            /* the glyph and its strokes */
    a->x1 = cx + d->x - h;
    a->y1 = cy + d->y - h;
    a->x2 = cx + d->x + h;
    a->y2 = cy + d->y + h;
}

/* Only the dots that changed since the last frame are redrawn (a quiet
 * shimmer costs a few small areas, not the whole screen); many changes at
 * once (a page moving) are one full redraw. */
bool mao_dots_end(void)
{
    const bool changed = s_d.hash != s_d.last_hash;
    if (!changed) {
        return false;
    }
    s_d.last_hash = s_d.hash;
    lv_area_t o;
    lv_obj_get_coords(s_d.obj, &o);
    const int n = s_d.n > s_d.prev_n ? s_d.n : s_d.prev_n;
    lv_area_t dirty[DIRTY_MAX];
    int nd = 0;
    bool full = false;
    /* Everything that changed, as one box: when too many dots moved to list
     * them, only this box is redrawn - usually the preview or the iris, not
     * the whole screen. */
    lv_area_t all = { INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN };
    for (int i = 0; i < n; i++) {
        const bool now_in = i < s_d.n, was_in = i < s_d.prev_n;
        if (now_in && was_in && same_dot(&s_d.dot[i], &s_d.prev[i])) {
            continue;
        }
        lv_area_t a, b;
        dot_area(&o, was_in ? &s_d.prev[i] : &s_d.dot[i], &a);
        dot_area(&o, now_in ? &s_d.dot[i] : &s_d.prev[i], &b);
        const lv_area_t u = { LV_MIN(a.x1, b.x1), LV_MIN(a.y1, b.y1), LV_MAX(a.x2, b.x2), LV_MAX(a.y2, b.y2) };
        all = (lv_area_t) { LV_MIN(all.x1, u.x1), LV_MIN(all.y1, u.y1), LV_MAX(all.x2, u.x2), LV_MAX(all.y2, u.y2) };
        if (nd < DIRTY_MAX) {
            dirty[nd++] = u;
        } else {
            full = true;
        }
    }
    if (full) {
        lv_obj_invalidate_area(s_d.obj, &all);
    } else {
        for (int i = 0; i < nd; i++) {
            lv_obj_invalidate_area(s_d.obj, &dirty[i]);
        }
    }
    memcpy(s_d.prev, s_d.dot, sizeof(dot_t) * (size_t)s_d.n);
    s_d.prev_n = s_d.n;
    return true;
}
