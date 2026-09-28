/*
 * MAO character / presence layer.
 *
 * A procedural character built from two eyes - eyes only (M4.1), drawn
 * with plain LVGL objects. Behaviour is physical: every pose
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
    MAO_CHAR_EXIT,       /* transfer: leaving through the edge */
    MAO_CHAR_GONE,       /* transfer: on the other device */
    MAO_CHAR_ENTER,      /* transfer: coming back in */
    MAO_CHAR_BASH,       /* transfer failed: the edge is a wall */
    MAO_CHAR_STATE_COUNT,
} mao_character_state_t;

typedef enum {
    MAO_CHAR_REACT_NOTICE = 0,   /* system event: eyes widen briefly */
    MAO_CHAR_REACT_ATTEND,       /* something appeared: glance to the rim */
    MAO_CHAR_REACT_WARM,         /* long press: soft narrowing, lift, brief yellow */
    MAO_CHAR_REACT_WAKE,         /* restore full presence immediately */
    /* Controller feedback: MAO's face reports what the controller does.
     * Played at full strength, even while the dial is in use. */
    MAO_CHAR_REACT_ACK,          /* a command was taken: quick nod */
    MAO_CHAR_REACT_BUSY,         /* work in progress: held until DONE / FAIL / IDLE */
    MAO_CHAR_REACT_DONE,         /* it worked */
    MAO_CHAR_REACT_FAIL,         /* it failed: wince, red glint, head shake */
    MAO_CHAR_REACT_BACK,         /* stepped back a level */
    MAO_CHAR_REACT_DEVICE_ON,    /* a device appeared / connected */
    MAO_CHAR_REACT_DEVICE_OFF,   /* a device went away */
    MAO_CHAR_REACT_UNSURE,       /* outcome unknowable: brief analytical uncertainty, no verdict */
    MAO_CHAR_REACT_IDLE,         /* end a held feedback (busy) with no result */
    /* Fiddling with HOME's light (mao_fiddle.h): the light still follows
     * every detent; MAO only lets it show. */
    MAO_CHAR_REACT_FIDDLE_NOTICE,  /* "what are you doing": a suspicious look */
    MAO_CHAR_REACT_FIDDLE_ANNOYED, /* tsk */
    MAO_CHAR_REACT_FIDDLE_FED_UP,  /* mad */
    MAO_CHAR_REACT_FIDDLE_ONGOING, /* still at it: the mood it is in lasts longer (no new reaction) */
    MAO_CHAR_REACT_COUNT,
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

/* The system takes the screen (M4.1): the eyes close to two lines and draw
 * together into the centre, where the interface's dot field takes over
 * (~MAO_CHAR_GATHER_MS). return() opens them again from that point. */
#define MAO_CHAR_GATHER_MS 190
void mao_character_gather(void);

/* Rim presence (M4.1): the eyes step down below the lower rim, smaller and
 * partly clipped by the circle, still alive - utility has the screen, MAO
 * watches it. Motion damped, no orbiting; the mind keeps running at reduced
 * strength. peek(false) drops them fully out of sight (return() brings them
 * back to the centre). */
void mao_character_peek(bool on);

/* Where the interface's attention is (screen px from the centre, + = right
 * / down). While on the rim the eyes look there, lagging a little; it is
 * composition, not a pointer. Ignored at the centre (HOME owns the gaze). */
void mao_character_attend(int x, int y);

/* HOME (M4.1): the eyes look towards a screen point (px from the centre,
 * + = right / down) - the lit end of the lamp scale on the rim - and no part
 * of them comes within the rim while they do. on = false lets go (it also
 * lets go on its own ~2.5 s after the last call). */
void mao_character_look(int x, int y, bool on);

/* MAO's colour now (0xRRGGBB): pink at rest, leaning with the mood. The
 * whole screen uses it - the dot UI and the field follow the eyes. */
uint32_t mao_character_accent(void);

mao_character_state_t mao_character_get_state(void);
const char *mao_character_state_name(mao_character_state_t state);

/* ------------------------------------------------------------------------ */
/* Transfer: MAO's side of the physical connection experience. Directions   */
/* are logical (dx, dy), exactly one non-zero: (+1,0) = RIGHT edge.         */
/* The app owns the connection state machine; these run the visuals. The    */
/* *_MS constants say when each sequence is over (schedule the next step).  */
/* ------------------------------------------------------------------------ */

#define MAO_CHAR_TRANSFER_EXIT_MS   500   /* launch decision -> fully off screen */
#define MAO_CHAR_TRANSFER_ENTER_MS  560   /* edge -> settled home */
#define MAO_CHAR_TRANSFER_BASH_MS   3500  /* three attempts + dry aftermath */

/* Connecting: notice the chosen edge and hold there (analytical, no spinner). */
void mao_character_transfer_search(int dx, int dy);
/* Confirmed success: purposeful directional exit through the boundary. */
void mao_character_transfer_exit(int dx, int dy);
/* Real failure: MAO tries to leave anyway; the edge is a wall. */
void mao_character_transfer_fail(int dx, int dy);
/* Come back in from the same edge (the same mind resumes; nothing resets). */
void mao_character_transfer_return(int dx, int dy);
/* Cancel any transfer pose and restore the resting character. */
void mao_character_transfer_abort(void);

/* Development: force the mind's interest / device novelty (0..100). */
void mao_character_debug_interest(uint8_t pct);
void mao_character_debug_novelty(uint8_t pct);

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
const char *mao_character_reaction_name(mao_character_reaction_t reaction);

#ifdef __cplusplus
}
#endif
