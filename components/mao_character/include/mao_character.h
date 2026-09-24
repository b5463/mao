/*
 * MAO character / presence layer.
 *
 * A procedural character built from two eyes (and, for an instant, a tiny
 * mouth), drawn with plain LVGL objects. Behaviour is physical: every pose
 * channel is a spring (mao_spring.h) and all proportions/gains live in
 * mao_character_tune.h. No needs, no emotions: reactions to input and events.
 *
 * Threading: mao_character_create() needs the display lock (it builds LVGL
 * objects). Every other function is a non-blocking command safe from any
 * task; commands are applied on the character's 30 Hz tick in the LVGL task.
 * The character never reads hardware.
 *
 * Priority (higher wins, idle always yields):
 *   navigation > press > dial > system reaction > idle
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Presentation states (for logs / tooling; not emotions). */
typedef enum {
    MAO_CHAR_IDLE = 0,
    MAO_CHAR_NOTICE,
    MAO_CHAR_FOLLOW,
    MAO_CHAR_DIZZY,
    MAO_CHAR_SLEEPY,
    MAO_CHAR_SURPRISED,
    MAO_CHAR_WARM,
    MAO_CHAR_AWAY,
    MAO_CHAR_STATE_COUNT,
} mao_character_state_t;

typedef enum {
    MAO_CHAR_REACT_NOTICE = 0,   /* system event: eyes widen briefly */
    MAO_CHAR_REACT_ATTEND,       /* something appeared: glance to the rim */
    MAO_CHAR_REACT_WARM,         /* long press: soft narrowing, lift, brief yellow */
    MAO_CHAR_REACT_WAKE,         /* restore full presence immediately */
} mao_character_reaction_t;

/* Dev-only previews ("mao anim <name>"). */
typedef enum {
    MAO_CHAR_PREVIEW_IDLE = 0,
    MAO_CHAR_PREVIEW_BLINK,
    MAO_CHAR_PREVIEW_FOLLOW,
    MAO_CHAR_PREVIEW_FAST,
    MAO_CHAR_PREVIEW_VERY_FAST,
    MAO_CHAR_PREVIEW_DIZZY,
    MAO_CHAR_PREVIEW_PRESS,
    MAO_CHAR_PREVIEW_WARM,
    MAO_CHAR_PREVIEW_SLEEPY,
    MAO_CHAR_PREVIEW_LEAVE,      /* leave, come back after a second */
    MAO_CHAR_PREVIEW_HIDE,       /* close and hide (to replay the boot sequence) */
    MAO_CHAR_PREVIEW_COUNT,
} mao_character_preview_t;

/* Build the character inside parent (display lock held). Starts invisible. */
esp_err_t mao_character_create(lv_obj_t *parent);

/* Knob geometry, so the orbit follows the knob's real angle. */
void mao_character_set_detents_per_rev(uint8_t detents_per_rev);

/* Dial detents (+ = clockwise). Speed, acceleration and reversals are
 * estimated continuously inside the character. */
void mao_character_dial(int32_t detents);

/* Knob held (compress) / released (spring back). */
void mao_character_press(bool down);

void mao_character_react(mao_character_reaction_t reaction);

/* First appearance: eyes open from closed. direction -1/0/+1: appear
 * displaced towards a dial turn and settle to the centre. */
void mao_character_appear(int direction);

/* Visual idle state only. Any dial/press/wake command restores presence. */
void mao_character_set_sleepy(bool sleepy);

/* Double click: widen, then drop and fold out through the bottom of the
 * circle, making room for the interface. return() brings it back. */
void mao_character_leave(void);
void mao_character_return(void);

mao_character_state_t mao_character_get_state(void);
const char *mao_character_state_name(mao_character_state_t state);

/* Development. */
void mao_character_debug_preview(mao_character_preview_t preview);
const char *mao_character_preview_name(mao_character_preview_t preview);
void mao_character_debug_dial(float detents_per_s, uint32_t duration_ms);
bool mao_character_debug_look(int index);
int mao_character_look_count(void);
/* Lark-style expression states (mao_lark_states.c). */
bool mao_character_debug_expression(int index);
int mao_character_expression_count(void);
const char *mao_character_expression_name(int index);

#ifdef __cplusplus
}
#endif
