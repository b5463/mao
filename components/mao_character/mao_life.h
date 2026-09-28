/*
 * MAO's mind: a causal behaviour engine. Nothing happens "because a timer
 * fired" - everything MAO does traces back to a stimulus or to an internal
 * state that a stimulus (or its absence) produced.
 *
 *   STIMULI      every dial turn and press is remembered with its side,
 *                strength and time; so are spins, returns from the menu and
 *                long absences.
 *   HABITUATION  a stimulus like the ones just before it is less salient; a
 *                strong one after quiet is more salient (and can startle).
 *   AROUSAL      salient stimuli raise it, it decays slowly. It continuously
 *                sets the lids (calm = her heavy deadpan lid), the pupils,
 *                the blink rate and how much the eyes move. Calm is still.
 *   ATTENTION    she looks where the last stimulus came from, keeps watching
 *                that side for a while (anticipation), then lets it go.
 *                Gaze moves in saccades between long fixations.
 *   DRIVES       energy (drains awake, refills asleep), boredom (grows
 *                without stimuli), irritation (never from controller use),
 *                affection (gentle touch, long press). They shape
 *                the resting expression and, when one crosses a threshold,
 *                trigger a behaviour (yawn, doze, sigh, sulk, watch the knob,
 *                a quiet pleased look) - each with its own refractory time.
 *   INTEREST     different from arousal: high arousal + low interest is
 *                alert but not engaged; low arousal + high interest is calm
 *                intense scrutiny. Raised by novel devices, unexpected
 *                results and unusual actions; habituates. It locks the gaze,
 *                widens the pupils, slows and delays blinks and stills the
 *                idle wander.
 *   EVALUATION   uncertain stimuli get a short analytical phase before the
 *                verdict: NOTICE -> EVALUATE (gaze fixed, lids narrowed a
 *                touch, one eye a fraction more, blink held) -> RESOLVE.
 *                Surprising-but-not-dramatic things earn a "second look"
 *                shortly after, instead of a startle.
 * Reactions to people pick an authored or generated Lark state according to
 * the stimulus and the current state. Private to mao_character.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_character_priv.h"
#include "mao_lark.h"

typedef enum {
    LIFE_EV_FIRST_TOUCH = 0,   /* first input after a quiet spell */
    LIFE_EV_TOUCH,             /* a press */
    LIFE_EV_INPUT,             /* any dial or press */
    LIFE_EV_REVERSAL,
    LIFE_EV_DIZZY,
    LIFE_EV_WARM,              /* long press */
    LIFE_EV_WOKEN,             /* woken from sleep by the user */
    /* ODD BUS: the controller's world. The character reacts to devices and
     * to how remote work turns out - never to the user's inputs. */
    LIFE_EV_DEVICE_NEW,        /* a genuinely new device appeared */
    LIFE_EV_DEVICE_BACK,       /* a known device returned */
    LIFE_EV_DEVICE_LOST,       /* a device went away: confirm, move on */
    LIFE_EV_CMD_WAIT,          /* remote work started: focused waiting */
    LIFE_EV_CMD_OK,            /* it worked (dry: "yes, obviously") */
    LIFE_EV_CMD_FAIL,          /* it failed: analysis first, aimed at the device */
    LIFE_EV_CMD_BUSY,          /* the device says busy: skeptical, habituates */
    LIFE_EV_CMD_UNSURE,        /* outcome unknowable: analytical uncertainty, then move on */
} life_event_t;

enum {
    LIFE_B_YAWN = 0, LIFE_B_DOZE, LIFE_B_SIGH, LIFE_B_SULK, LIFE_B_WATCH, LIFE_B_CONTENT, LIFE_B_GRUMBLE,
    LIFE_B_EXAMINE,
    LIFE_B_COUNT,
};

typedef struct {
    /* Drives and arousal, 0..1. */
    float energy, boredom, irritation, affection, arousal;
    float interest;                 /* engaged scrutiny; habituates and decays */
    float habit_device, habit_busy; /* habituation to device events, decays slowly */

    /* Evaluation transient and the second look. */
    uint32_t eval_until;            /* analytical phase: gaze fixed, lids narrowed */
    int8_t eval_side;               /* asymmetry: which eye narrows a fraction more */
    uint32_t second_look_at;        /* a scheduled quiet re-inspection (0 = none) */
    uint32_t last_fail_ms;          /* recent context: repeated failures irritate */

    /* Stimulus memory. */
    uint32_t last_ms, last_input_ms, last_stim_ms;
    int8_t stim_side;               /* -1 / 0 / +1: where the last stimulus came from */
    float habit_dial, habit_press;  /* habituation to each kind, decays */
    float dial_rate;                /* detents/s, smoothed */
    uint32_t dial_last_ms;          /* previous detent batch */
    uint32_t slow_since;            /* careful slow turning since (0 = not) */

    /* Attention and gaze. */
    float gx, gy;                   /* commanded gaze (a saccade = a jump here) */
    float tx, ty;                   /* current fixation target */
    uint32_t watch_until;           /* keep an eye on the stimulus side until */
    uint32_t next_fix_ms;           /* next fixation change */
    uint32_t next_blink_ms;

    /* Behaviours. */
    uint32_t refract[LIFE_B_COUNT]; /* earliest next time for each */
    int8_t holding;                 /* a looping behaviour is being held (-1 none) */
    bool hold_locked;               /* the character core holds it (a grudge): input does not end it */
    const char *doing;

    /* Cat mode (caused: deep contentment after affection). */
    uint32_t cat_until;
    mao_spring_t catness;

    mao_spring_t gain;              /* backs off while the user is in charge */
} mao_life_t;

void mao_life_init(mao_life_t *l, uint32_t now);
void mao_life_event(mao_life_t *l, mao_lark_t *lark, life_event_t ev, uint32_t now);
/* Every dial detent batch (signed), for side, rate and habituation. */
void mao_life_dial(mao_life_t *l, int32_t detents, uint32_t now);
/* idle: MAO is free to act. Adds gaze and expression offsets to add[] and
 * drives the blinks. */
void mao_life_update(mao_life_t *l, mao_lark_t *lark, mao_motion_t *m, uint32_t now, bool idle, bool sleepy,
                     float add[CH_COUNT]);
bool mao_life_is_cat(const mao_life_t *l, uint32_t now);
/* Development: force interest / novelty (0..100). Novelty clears device
 * habituation at 100 and saturates it at 0. */
void mao_life_debug_interest(mao_life_t *l, uint8_t pct);
void mao_life_debug_novelty(mao_life_t *l, uint8_t pct);
