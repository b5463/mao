/*
 * The ODD JOBS field: the lattice of small glyphs that grows out of a seed at
 * the centre, fills the screen and draws back in. Shared visual language of
 * ODD JOBS devices - it is KINO D4's boot field (firmware/p4/main/ui.c,
 * boot_field()) with its geometry made a parameter, so a 800 x 480 camera and
 * a 240 px round controller draw the same thing at their own scale.
 *
 * Pure C, no graphics dependency: it decides, per lattice cell, whether the
 * cell is lit, which of nine marks it shows and in which of four inks, and
 * how far up out of the ground it is. The caller draws. Every decision is a
 * hash of the cell's own coordinates (and of time for the mark), so a cell
 * looks the same frame to frame and the field grows rather than churns.
 *
 * What it means is the caller's: on KINO the reach is the boot's clock; on a
 * light controller the reach is how bright the light is.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The alphabet, 5x5, one byte per row, bit 4 = left column (KINO's nine). */
enum {
    ODD_FIELD_HASH = 0,   /* #  the commonest mark, the one that carries the field */
    ODD_FIELD_STAR,       /* *  solid centre, four detached diagonal points */
    ODD_FIELD_NOTCH,      /* a square ring broken at one corner, a dot inside */
    ODD_FIELD_DISC,       /* a stepped circle */
    ODD_FIELD_SQUARE,
    ODD_FIELD_BAR2,       /* two short uprights - most of the rim */
    ODD_FIELD_BAR1,
    ODD_FIELD_RUN,        /* a short run of dots */
    ODD_FIELD_DOT,
    ODD_FIELD_GLYPHS,
};
extern const uint8_t ODD_FIELD_GLYPH[ODD_FIELD_GLYPHS][5];

typedef struct {
    int pitch;            /* px between cell centres */
    int bit;              /* px per glyph bit (a glyph is 5 bits square) */
    float far;            /* distance at which the field has thinned to its rim */
    float jitter;         /* how late a cell may arrive, at `far` */
    float fade;           /* px of travel over which a cell comes up */
    int cell_ms;          /* how long a cell holds its mark before re-rolling */
} odd_field_geom_t;

/* KINO D4: 800 x 480 panel. */
#define ODD_FIELD_GEOM_KINO { .pitch = 20, .bit = 3, .far = 470.0f, .jitter = 74.0f, .fade = 66.0f, .cell_ms = 260 }
/* KINO's geometry scaled to a 240 px round screen (MAO). */
#define ODD_FIELD_GEOM_ROUND240 { .pitch = 12, .bit = 2, .far = 124.0f, .jitter = 20.0f, .fade = 18.0f, .cell_ms = 260 }

typedef struct {
    uint8_t glyph;        /* ODD_FIELD_* */
    uint8_t ink;          /* 0 lead .. 3 faintest (the caller's palette) */
    float grow;           /* 0 not yet up .. 1 fully up */
} odd_field_cell_t;

/* Cell (gx, gy), lattice coordinates from the centre, with the field out to
 * `reach` px at time ms. False if the cell is dark (unlit, or not reached). */
bool odd_field_cell(const odd_field_geom_t *g, int gx, int gy, float reach, int32_t ms, odd_field_cell_t *out);

/* The same, split for speed: what depends only on a cell's position (its
 * distance, jitter, whether it is ever lit, its clock phase) computed once,
 * then the per-frame part. odd_field_cell() is exactly the two in turn. */
typedef struct {
    float edge;        /* distance + jitter: the reach at which it appears */
    float d;           /* distance / far */
    bool lit;          /* ever lit (fixed by position) */
    uint32_t phase;    /* its own clock's offset */
} odd_field_pre_t;
void odd_field_precompute(const odd_field_geom_t *g, int gx, int gy, odd_field_pre_t *pre);
bool odd_field_cell_pre(const odd_field_geom_t *g, int gx, int gy, const odd_field_pre_t *pre, float reach, int32_t ms,
                        odd_field_cell_t *out);

#ifdef __cplusplus
}
#endif
