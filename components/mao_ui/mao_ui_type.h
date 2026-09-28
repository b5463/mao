/*
 * MAO typography and colour tokens. The only place fonts and colours are
 * chosen.
 *
 * One family (BIZ UDPGothic, SIL OFL 1.1: fonts/OFL.txt), five roles. No bold
 * hierarchy: emphasis comes from size, position, opacity, the focus line and
 * motion (docs/m4_1_ui_motion_concept.md section 6).
 */
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(mao_font_fact);      /* 13 px, printable ASCII + middle dot */
LV_FONT_DECLARE(mao_font_name);      /* 18 px, printable ASCII + middle dot */
LV_FONT_DECLARE(mao_font_word);      /* 30 px, printable ASCII + middle dot */
LV_FONT_DECLARE(mao_font_code);      /* 44 px digits (pairing code) */
LV_FONT_DECLARE(mao_font_numeral);   /* 64 px digits, % - . O N F (LEVEL, ON/OFF, ...) */

/* Type roles. */
#define MAO_FONT_SMALL       (&mao_font_fact)      /* FACT: facts, conditions, state words, INFO */
#define MAO_FONT_NORMAL      (&mao_font_name)      /* NAME: list neighbours, the page's anchor title */
#define MAO_FONT_LARGE       (&mao_font_word)      /* WORD: the one important word */
#define MAO_FONT_CODE        (&mao_font_code)
#define MAO_FONT_NUMERAL     (&mao_font_numeral)

#define MAO_TRACK_SMALL      4
#define MAO_TRACK_NORMAL     3
#define MAO_TRACK_LARGE      4
#define MAO_TRACK_NUMERAL    (-1)

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
