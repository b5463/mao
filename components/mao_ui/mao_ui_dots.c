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
} s_d;

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
    uint32_t h = (uint32_t)(i * 73856093) ^ (uint32_t)(j * 19349663) ^ (t * 83492791u);
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
    const lv_color_t cols[2] = { lv_color_hex(MAO_COL_DOT), lv_color_hex(MAO_COL_BG) };
    for (int i = 0; i < s_d.n; i++) {
        const dot_t *d = &s_d.dot[i];
        const int32_t s = d->d, h = s / 2;
        const int32_t x0 = cx + d->x - h, y0 = cy + d->y - h;
        if (y0 + s < clip->y1 || y0 > clip->y2) {
            continue;                  /* not in the strip being rendered */
        }
        r.bg_color = cols[d->col & 1];
        r.bg_opa = d->opa;
        const int32_t t = s >= 7 ? 2 : 1;   /* stroke of the line glyphs */
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
    if (x * x + y * y > 119.0f * 119.0f) {
        return;                        /* the circle is the frame */
    }
    dot_t *o = &s_d.dot[s_d.n++];
    o->x = (int16_t)lrintf(x);
    o->y = (int16_t)lrintf(y);
    o->d = (uint8_t)(size > 120.0f ? 120 : lrintf(size));
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

bool mao_dots_end(void)
{
    const bool changed = s_d.hash != s_d.last_hash;
    if (changed) {
        s_d.last_hash = s_d.hash;
        lv_obj_invalidate(s_d.obj);
    }
    return changed;
}
