/*
 * MAO character / presence layer.
 *
 * A small procedural character (two eyes, an optional minimal mouth) drawn
 * with plain LVGL objects. It owns a reaction-state machine and its own idle
 * behaviour; it does not simulate needs or emotions.
 *
 * Threading: mao_character_create() must be called with the display lock
 * held (it builds LVGL objects). Every other function is a non-blocking
 * command that may be called from any task; commands are applied on the
 * character's own tick in the LVGL task. The character never reads hardware.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reaction / animation states (not emotions). */
typedef enum {
    MAO_CHAR_IDLE = 0,
    MAO_CHAR_NOTICE,
    MAO_CHAR_FOLLOW,
    MAO_CHAR_DIZZY,
    MAO_CHAR_SLEEPY,
    MAO_CHAR_SURPRISED,
    MAO_CHAR_HAPPY,
    MAO_CHAR_STATE_COUNT,
} mao_character_state_t;

/* Dial speed classes, as classified by the application. */
typedef enum {
    MAO_DIAL_STILL = 0,
    MAO_DIAL_SLOW,
    MAO_DIAL_NORMAL,
    MAO_DIAL_FAST,
    MAO_DIAL_VERY_FAST,
} mao_dial_speed_t;

typedef enum {
    MAO_CHAR_REACT_NOTICE = 0,   /* attention: eyes widen briefly */
    MAO_CHAR_REACT_SURPRISED,    /* wide eyes + small "o" */
    MAO_CHAR_REACT_HAPPY,        /* eyes squint into a smile */
    MAO_CHAR_REACT_WAKE,         /* eyes open from closed/sleepy */
} mao_character_reaction_t;

/* Build the character inside parent (display lock held). Starts hidden
 * (eyes closed) until mao_character_set_present(true, ...) or a WAKE. */
esp_err_t mao_character_create(lv_obj_t *parent);

/* Dial geometry, so fast spins can follow the knob's real angle. */
void mao_character_set_detents_per_rev(uint8_t detents_per_rev);

/* Dial motion: signed detents (+ = clockwise) with the app's speed class.
 * reversing = the user is rapidly alternating direction. */
void mao_character_dial(int32_t detents, mao_dial_speed_t speed, bool reversing);

/* Knob held down (eyes compress) / released (they spring back with a bounce). */
void mao_character_press(bool down);

void mao_character_react(mao_character_reaction_t reaction);

/* Sleepy is an idle presentation only; any command that implies interaction
 * should be preceded by set_sleepy(false). */
void mao_character_set_sleepy(bool sleepy);

/* present=false moves the character out below the screen (e.g. for the menu);
 * true brings it back. */
void mao_character_set_present(bool present);

/* Development: continuous orbit for the given time to stress rendering. */
void mao_character_debug_stress(uint32_t duration_ms);

mao_character_state_t mao_character_get_state(void);
const char *mao_character_state_name(mao_character_state_t state);

#ifdef __cplusplus
}
#endif
