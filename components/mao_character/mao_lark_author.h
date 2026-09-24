/*
 * Authoring macros for MAO's Lark-style library (mao_lark_states_*.c).
 *
 * Channel values are OFFSETS added to the procedural pose:
 *   gaze px (pupils move MAO_PUPIL_GAIN x; ~12 reaches the eye's edge),
 *   face px (x the look's motion_scale, 0.4 for the big eyes), tilt px,
 *   narrow = flat half-lid (0.26 is the resting lid, -0.26 opens fully),
 *   close = round cover (1 = closed to a crescent), wink (+left / -right),
 *   squint (+left / -right lid), pupil = dilation (+ wide, - pinpoint),
 *   shine = catchlight gain, open, squash, tint_* 0..1.
 *   smile = lower lids rising (smiling eyes, 0..1),
 *   slit = cat pupil 0..1, eye_w / eye_h = eye shape offsets (+0.2 = 20 %),
 *   star = gold star pupils 0..1, dark = blank dark irises 0..1,
 *   cross = px the pupils converge.
 */
#pragma once

#include "mao_lark.h"

#define K(t, v, e) { (t), (v), LARK_##e }
#define TRACK(ch, arr) { (ch), (uint8_t)(sizeof(arr) / sizeof((arr)[0])), (arr) }
#define NTR(arr) (uint8_t)(sizeof(arr) / sizeof((arr)[0])), (arr)

/* STATE(name, length, in_ms, curve, flags, base, bored, agit, aff, next, tracks) */
#define STATE(nm, len, in, ie, fl, b, bo, ag, af, nx, tr) \
    { (nm), (len), (in), LARK_##ie, (fl), { (b), (bo), (ag), (af) }, (nx), NTR(tr), NULL }
/* GEN(name, in_ms, curve, flags, base, bored, agit, aff, next, generator): procedural. */
#define GEN(nm, in, ie, fl, b, bo, ag, af, nx, fn) \
    { (nm), 0, (in), LARK_##ie, (fl), { (b), (bo), (ag), (af) }, (nx), 0, NULL, (fn) }
#define ONE LARK_ONESHOT
#define EVT (LARK_ONESHOT | LARK_NO_PICK)

/* Generators (mao_lark_gen.c). */
void lark_gen_dizzy(lark_gen_t *g);
void lark_gen_startle(lark_gen_t *g);
void lark_gen_happy(lark_gen_t *g);
void lark_gen_pleased(lark_gen_t *g);
void lark_gen_tsk(lark_gen_t *g);
void lark_gen_mad(lark_gen_t *g);
void lark_gen_sniff(lark_gen_t *g);
void lark_gen_doubletake(lark_gen_t *g);
void lark_gen_eyeroll(lark_gen_t *g);
void lark_gen_flustered(lark_gen_t *g);
void lark_gen_anxious(lark_gen_t *g);
void lark_gen_keen(lark_gen_t *g);

/* Parts of the library (registered in mao_lark_library.c). */
extern const lark_state_t kLarkDaily[];
extern const int kLarkDailyCount;
extern const lark_state_t kLarkMoods[];
extern const int kLarkMoodsCount;
extern const lark_state_t kLarkLife[];
extern const int kLarkLifeCount;
extern const lark_state_t kLarkCat[];
extern const int kLarkCatCount;
extern const lark_state_t kLarkMore[];
extern const int kLarkMoreCount;
