/*
 * The ODD JOBS field (see odd_field.h). A port of KINO D4's boot_field():
 * the same hashes, glyphs, distributions and frontier, with the geometry as a
 * parameter and the drawing left to the caller. KINO's per-quadrant camera
 * colouring is the camera's own use of the field and is not part of it.
 */
#include "odd_field.h"

#include <math.h>

const uint8_t ODD_FIELD_GLYPH[ODD_FIELD_GLYPHS][5] = {
    { 0x0A, 0x1F, 0x0A, 0x1F, 0x0A },   /* # */
    { 0x15, 0x0E, 0x1F, 0x0E, 0x15 },   /* * */
    { 0x0F, 0x11, 0x15, 0x11, 0x1F },   /* notch */
    { 0x0E, 0x1F, 0x1F, 0x1F, 0x0E },   /* disc */
    { 0x1F, 0x1F, 0x1F, 0x1F, 0x1F },   /* square */
    { 0x00, 0x0A, 0x0A, 0x0A, 0x00 },   /* two uprights */
    { 0x00, 0x04, 0x04, 0x04, 0x00 },   /* one upright */
    { 0x00, 0x00, 0x15, 0x00, 0x00 },   /* run */
    { 0x00, 0x00, 0x04, 0x00, 0x00 },   /* dot */
};

static uint32_t hash2(int a, int b)
{
    uint32_t h = ((uint32_t)a * 374761393u) ^ ((uint32_t)b * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

bool odd_field_cell(const odd_field_geom_t *g, int gx, int gy, float reach, int32_t ms, odd_field_cell_t *out)
{
    if (reach <= 0.0f) {
        return false;
    }
    const float dx = (float)(gx * g->pitch), dy = (float)(gy * g->pitch);
    const float dist = sqrtf(dx * dx + dy * dy);

    /* When this cell's turn comes: ragged at the edge, tight at the origin. */
    const float jit = (float)(hash2(gx + 31, gy - 17) % 1024u) / 1024.0f;
    const float spread = g->jitter * (0.18f + 0.82f * (dist / g->far));
    const float k = (reach - (dist + jit * spread)) / g->fade;
    if (k <= 0.0f) {
        return false;
    }

    /* Whether a cell is lit is fixed by position; the rim is mostly empty. */
    const float d = dist / g->far;
    const uint32_t here = hash2(gx, gy);
    const uint32_t drop = (uint32_t)(6.0f + 74.0f * d * d);
    if ((here >> 3) % 100u < drop) {
        return false;
    }

    /* The mark re-rolls on the cell's own clock (the changes scatter). */
    const uint32_t phase = hash2(gx + 977, gy - 613) % (uint32_t)g->cell_ms;
    const uint32_t gen = (uint32_t)(ms + (int32_t)phase) / (uint32_t)g->cell_ms;
    const uint32_t h = hash2(gx, (int)((uint32_t)gy + gen * 7919u));

    /* Weight falls with distance; the alphabet sorts itself by weight. */
    int gl;
    const uint32_t pick = (h >> 11) % 100u;
    if (d < 0.30f) {
        gl = pick < 34 ? ODD_FIELD_HASH : pick < 52 ? ODD_FIELD_STAR : pick < 66 ? ODD_FIELD_DISC
             : pick < 78 ? ODD_FIELD_NOTCH : pick < 88 ? ODD_FIELD_SQUARE : pick < 95 ? ODD_FIELD_BAR2
             : ODD_FIELD_DOT;
    } else if (d < 0.62f) {
        gl = pick < 28 ? ODD_FIELD_HASH : pick < 42 ? ODD_FIELD_STAR : pick < 54 ? ODD_FIELD_NOTCH
             : pick < 63 ? ODD_FIELD_DISC : pick < 70 ? ODD_FIELD_SQUARE : pick < 86 ? ODD_FIELD_BAR2
             : pick < 94 ? ODD_FIELD_BAR1 : ODD_FIELD_DOT;
    } else {
        gl = pick < 38 ? ODD_FIELD_BAR2 : pick < 56 ? ODD_FIELD_DOT : pick < 72 ? ODD_FIELD_RUN
             : pick < 86 ? ODD_FIELD_BAR1 : pick < 94 ? ODD_FIELD_HASH : ODD_FIELD_STAR;
    }

    /* Ink per cell; the lead ink leads in every band. */
    const uint32_t ci = (h >> 19) % 100u;
    int ink;
    if (d < 0.34f) {
        ink = ci < 54 ? 0 : ci < 76 ? 1 : ci < 94 ? 2 : 3;
    } else if (d < 0.66f) {
        ink = ci < 48 ? 0 : ci < 66 ? 1 : ci < 88 ? 2 : 3;
    } else {
        ink = ci < 36 ? 0 : ci < 50 ? 1 : ci < 76 ? 2 : 3;
    }

    out->glyph = (uint8_t)gl;
    out->ink = (uint8_t)ink;
    out->grow = k < 1.0f ? k : 1.0f;
    return true;
}
