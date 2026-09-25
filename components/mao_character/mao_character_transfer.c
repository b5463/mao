/*
 * The transfer: MAO's side of connecting to another ODD JOBS device.
 *
 * On a confirmed connection MAO leaves the controller: the eyes know first
 * (gaze locks on the chosen edge), a tiny backward anticipation, then a
 * purposeful accelerating exit with a directional stretch, clipped by the
 * circular boundary. No fade, no icon, no text - the absence is the state.
 * The tone is "Fine. I'm going to inspect this", not a cartoon jump.
 *
 * On a failed connection MAO tries to leave anyway. The screen edge is a
 * physical wall: three escalating attempts (push - quicker bounce - decisive
 * THUNK with a couple of small rim ticks), then stillness, a skeptical
 * second look at the offending edge, and a dry settle. Determination, failed
 * physics, "apparently not". Never tragic, and never red - an unavailable
 * device is not a catastrophe.
 *
 * The scripts drive the ordinary motion springs, so everything stays
 * interruptible: aborting simply retargets home. While a phase is active the
 * transfer owns the pose (the Lark layer and the mind are weighted to zero
 * by mao_character.c). Timings that the app must know (when the character is
 * actually gone / back) are the MAO_CHAR_TRANSFER_*_MS constants in
 * mao_character.h.
 */
#include "mao_character_priv.h"

#include "esp_log.h"
#include "mao_audio.h"

static const char *TAG = "MAO_TRANSFER";

#define MARKS_MS 170

/* Set the travel target along the transfer direction (px out of the circle). */
static void travel(mao_transfer_t *t, mao_motion_t *m, float v)
{
    if (t->dx) {
        mao_motion_set(m, CH_EXIT_X, (float)t->dx * v);
    } else {
        mao_motion_set(m, CH_AWAY, (float)t->dy * v);
    }
}

static void travel_profile(mao_transfer_t *t, mao_motion_t *m, mao_spring_profile_t p)
{
    mao_motion_profile(m, t->dx ? CH_EXIT_X : CH_AWAY, p);
}

static void travel_kick(mao_transfer_t *t, mao_motion_t *m, float v)
{
    if (t->dx) {
        mao_motion_kick(m, CH_EXIT_X, (float)t->dx * v);
    } else {
        mao_motion_kick(m, CH_AWAY, (float)t->dy * v);
    }
}

static void look_at_edge(mao_transfer_t *t, mao_motion_t *m, float px)
{
    mao_motion_set(m, CH_GAZE_X, (float)t->dx * px);
    mao_motion_set(m, CH_GAZE_Y, (float)t->dy * px * 0.7f);
}

/* The directional stretch: purposeful, not cartoonish. */
static void stretch(mao_transfer_t *t, mao_motion_t *m, float k)
{
    mao_motion_set(m, CH_EYE_W, t->dx ? 0.30f * k : -0.10f * k);
    mao_motion_set(m, CH_EYE_H, t->dy ? 0.25f * k : -0.10f * k);
}

static void restore(mao_motion_t *m)
{
    mao_motion_profile(m, CH_EXIT_X, MAO_SPRING_HEAVY);
    mao_motion_profile(m, CH_AWAY, MAO_P_AWAY);
    mao_motion_set(m, CH_EXIT_X, 0.0f);
    mao_motion_set(m, CH_AWAY, 0.0f);
    mao_motion_set(m, CH_EYE_W, 0.0f);
    mao_motion_set(m, CH_EYE_H, 0.0f);
    mao_motion_set(m, CH_SQUASH, 0.0f);
    mao_motion_set(m, CH_SQUINT, 0.0f);
    mao_motion_set(m, CH_NARROW, MAO_REST_NARROW);
    mao_motion_set(m, CH_GAZE_X, 0.0f);
    mao_motion_set(m, CH_GAZE_Y, 0.0f);
    mao_motion_set(m, CH_OPEN, 1.0f);
}

void mao_transfer_begin(mao_transfer_t *t, mao_motion_t *m, uint8_t phase, int dx, int dy, uint32_t now)
{
    if (phase == MAO_TR_NONE) {
        t->phase = MAO_TR_NONE;
        restore(m);
        return;
    }
    t->phase = phase;
    t->dx = (int8_t)(dx > 0 ? 1 : (dx < 0 ? -1 : 0));
    t->dy = t->dx ? 0 : (int8_t)(dy >= 0 ? 1 : -1);
    t->t0 = now;
    t->step = 0;
    t->marks_until = 0;

    switch (phase) {
    case MAO_TR_SEARCH:
        /* Notice the edge: analytical curiosity, a slight lean, hold. */
        look_at_edge(t, m, 12.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW + 0.10f);
        if (t->dx) {
            mao_motion_set(m, CH_FACE_X, (float)t->dx * 9.0f);
        } else {
            mao_motion_set(m, CH_FACE_Y, (float)t->dy * 7.0f);
        }
        break;
    case MAO_TR_GONE:
        travel(t, m, 300.0f);
        break;
    case MAO_TR_ENTER: {
        /* Already at the far side of the edge, coming in with velocity.
         * Home is easier than leaving: MAO knows the way. */
        mao_spring_t *s = &m->ch[t->dx ? CH_EXIT_X : CH_AWAY];
        s->x = (float)(t->dx ? t->dx : t->dy) * 300.0f;
        s->v = 0.0f;
        travel_profile(t, m, (mao_spring_profile_t){ .k = 190.0f, .zeta = 0.60f });   /* small overshoot */
        travel(t, m, 0.0f);
        mao_motion_set(m, CH_OPEN, 1.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW);
        look_at_edge(t, m, -8.0f);     /* eyes already on where it's going */
        break;
    }
    default:
        break;
    }
}

/* Scripted timelines: kDue[i] is when action i fires (ms since t0), each
 * action runs exactly once. The last entry ends the phase. */
/* The search pose has already fixed the gaze on the edge, so the exit only
 * needs the anticipation and the launch; the failed escape keeps its three
 * acts but at controller pace (see the milestone: exploratory / analytical /
 * decisive, roughly 0.55 + 0.7 + 0.75 s plus a dry aftermath). */
static const uint16_t kExitDue[]  = { 0, 90, 180, 480 };
static const uint16_t kEnterDue[] = { 260, 520 };
static const uint16_t kBashDue[]  = { 0, 140, 340, 450, 560, 760, 900, 1040, 1130, 1300,
                                      1560, 1700, 1790, 2000, 2350, 2650, 2820, 3120, 3400 };
#define N_OF(a) ((uint8_t)(sizeof(a) / sizeof(a[0])))

static void exit_step(mao_transfer_t *t, mao_motion_t *m, uint32_t now)
{
    switch (t->step) {
    case 0:   /* the eyes know first */
        look_at_edge(t, m, 13.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW + 0.12f);
        break;
    case 1:   /* tiny backward anticipation */
        travel_kick(t, m, -46.0f);
        mao_motion_set(m, CH_SQUASH, 0.08f);
        break;
    case 2:   /* launch: controlled acceleration, directional stretch */
        travel_profile(t, m, (mao_spring_profile_t){ .k = 85.0f, .zeta = 1.0f });
        travel(t, m, 320.0f);
        stretch(t, m, 1.0f);
        mao_motion_set(m, CH_SQUASH, -0.10f);
        mao_audio_depart();
        break;
    default:  /* fully outside the boundary */
        t->phase = MAO_TR_GONE;
        ESP_LOGI(TAG, "gone <- exit complete");
        break;
    }
}

static void enter_step(mao_transfer_t *t, mao_motion_t *m, uint32_t now)
{
    switch (t->step) {
    case 0:   /* through the edge: reorient towards the room */
        look_at_edge(t, m, 3.0f);
        stretch(t, m, 0.0f);
        break;
    default:  /* settled: the same mind resumes (nothing was reset) */
        t->phase = MAO_TR_NONE;
        restore(m);
        ESP_LOGI(TAG, "home <- return complete");
        break;
    }
}

static void bash_step(mao_transfer_t *t, mao_motion_t *m, mao_char_draw_t *d, uint32_t now)
{
    switch (t->step) {
    /* Attempt 1: gaze, approach, push, mild squash, retreat. */
    case 0:
        look_at_edge(t, m, 12.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW + 0.08f);
        break;
    case 1:
        travel_profile(t, m, (mao_spring_profile_t){ .k = 150.0f, .zeta = 0.85f });
        travel(t, m, 64.0f);
        break;
    case 2:   /* push into the glass */
        travel(t, m, 90.0f);
        break;
    case 3:   /* contact */
        mao_motion_set(m, CH_SQUASH, 0.16f);
        mao_audio_bump(1);
        break;
    case 4:   /* retreat, considering */
        travel(t, m, 34.0f);
        mao_motion_set(m, CH_SQUASH, 0.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW + 0.16f);
        break;

    /* Attempt 2: brief assessment, small backup, quicker impact, bounce. */
    case 5:
        mao_motion_set(m, CH_SQUINT, 0.15f * (float)(t->dx ? t->dx : 1));
        break;
    case 6:
        travel(t, m, 14.0f);
        mao_motion_set(m, CH_SQUINT, 0.0f);
        break;
    case 7:   /* quicker run at it */
        travel_profile(t, m, (mao_spring_profile_t){ .k = 240.0f, .zeta = 0.9f });
        travel(t, m, 96.0f);
        break;
    case 8:   /* harder contact, bounce off */
        mao_motion_set(m, CH_SQUASH, 0.32f);
        mao_audio_bump(2);
        travel_kick(t, m, -150.0f);
        break;
    case 9:
        travel_profile(t, m, MAO_SPRING_SOFT);
        travel(t, m, 40.0f);
        mao_motion_set(m, CH_SQUASH, 0.0f);
        break;

    /* Attempt 3: short run-up, decisive impact, THUNK, strongest squash. */
    case 10:  /* wind up */
        travel(t, m, 6.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW + 0.2f);
        break;
    case 11:  /* charge */
        travel_profile(t, m, (mao_spring_profile_t){ .k = 320.0f, .zeta = 0.95f });
        travel(t, m, 104.0f);
        break;
    case 12:  /* THUNK */
        mao_motion_set(m, CH_SQUASH, 0.5f);
        mao_audio_bump(3);
        mao_char_draw_marks(d, t->dx, t->dy, true);
        t->marks_until = now + MARKS_MS;
        t->marks_on = true;
        travel_kick(t, m, -180.0f);
        mao_motion_blink(m, now, 150, 1);
        break;
    case 13:  /* slide back down the glass */
        travel_profile(t, m, MAO_SPRING_SOFT);
        travel(t, m, 28.0f);
        mao_motion_set(m, CH_SQUASH, 0.06f);
        break;

    /* Aftermath: stillness, the skeptical second look, "apparently not." */
    case 14:
        mao_motion_set(m, CH_SQUASH, 0.0f);
        look_at_edge(t, m, 11.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW + 0.18f);
        break;
    case 15:  /* tiny release... */
        look_at_edge(t, m, 4.0f);
        break;
    case 16:  /* ...and one more look, narrower. Apparently not. */
        look_at_edge(t, m, 10.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW + 0.26f);
        mao_motion_set(m, CH_SQUINT, 0.12f * (float)(t->dx ? t->dx : 1));
        break;
    case 17:  /* dry settle, home */
        travel(t, m, 0.0f);
        mao_motion_set(m, CH_SQUINT, 0.0f);
        mao_motion_set(m, CH_NARROW, MAO_REST_NARROW);
        mao_motion_set(m, CH_GAZE_X, 0.0f);
        mao_motion_set(m, CH_GAZE_Y, 0.0f);
        break;
    default:
        t->phase = MAO_TR_NONE;
        restore(m);
        ESP_LOGI(TAG, "settled <- failed escape complete");
        break;
    }
}

bool mao_transfer_tick(mao_transfer_t *t, mao_motion_t *m, mao_char_draw_t *d, uint32_t now)
{
    if (t->marks_on && (int32_t)(now - t->marks_until) >= 0) {
        t->marks_on = false;
        mao_char_draw_marks(d, 0, 0, false);
    }
    if (t->phase == MAO_TR_NONE) {
        return false;
    }
    if (t->phase == MAO_TR_SEARCH || t->phase == MAO_TR_GONE) {
        return true;   /* holding a pose; the app decides what happens next */
    }
    const uint32_t el = now - t->t0;
    for (;;) {
        const uint16_t *due = kExitDue;
        uint8_t count = N_OF(kExitDue);
        if (t->phase == MAO_TR_ENTER) {
            due = kEnterDue;
            count = N_OF(kEnterDue);
        } else if (t->phase == MAO_TR_BASH) {
            due = kBashDue;
            count = N_OF(kBashDue);
        }
        const uint32_t at = t->step < count ? due[t->step] : due[count - 1];
        if (el < at) {
            return true;
        }
        if (t->phase == MAO_TR_EXIT) {
            exit_step(t, m, now);
        } else if (t->phase == MAO_TR_ENTER) {
            enter_step(t, m, now);
        } else {
            bash_step(t, m, d, now);
        }
        t->step++;
        if (t->phase == MAO_TR_NONE || t->phase == MAO_TR_GONE || t->step > count) {
            return t->phase != MAO_TR_NONE;
        }
    }
}
