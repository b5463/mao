/*
 * MAO typography and colour tokens. The only place fonts and colours are
 * chosen: replacing the placeholder typeface later means editing this file.
 *
 * One family, three sizes, generous tracking. No bold hierarchy: emphasis
 * comes from size, position, opacity and motion.
 */
#pragma once

#include "lvgl.h"

/* Type scale (Montserrat is a development placeholder). */
#define MAO_FONT_SMALL       (&lv_font_montserrat_14)   /* status, secondary */
#define MAO_FONT_NORMAL      (&lv_font_montserrat_20)   /* menu, ordinary values */
#define MAO_FONT_LARGE       (&lv_font_montserrat_28)   /* the one important word/value */

#define MAO_TRACK_SMALL      5
#define MAO_TRACK_NORMAL     3
#define MAO_TRACK_LARGE      5

/* Palette: monochrome at rest. Colour is an event, never decoration. */
#define MAO_COL_BG           0x08080A   /* near-black */
#define MAO_COL_FG           0xF1ECE2   /* warm off-white */
#define MAO_COL_DIM          0x6E6C68   /* secondary text */
#define MAO_COL_COBALT       0x3D63FF   /* active / movement / connection */
#define MAO_COL_YELLOW       0xF2C94C   /* completion / positive */
#define MAO_COL_RED          0xE5484D   /* actual failure */

/* Opacity levels for text. */
#define MAO_OPA_PRIMARY      255
#define MAO_OPA_CONTEXT      110        /* neighbouring menu words */
#define MAO_OPA_SECONDARY    130        /* status / notes */
