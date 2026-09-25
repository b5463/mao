/*
 * Procedural reactions: each generator composes a new timeline every time
 * its state plays, from a handful of randomised decisions - so a reaction
 * never looks like a clip being replayed. Dizzy picks how many loops, which
 * way, how fast it winds down, whether the eyes drift apart and how it ends;
 * a startle picks where the "noise" came from, how big the jolt is and how
 * she recovers; a head shake picks its swings; and so on. The engine adds its
 * own per-play variation on top (mirror, tempo, amplitude, key jitter).
 * Units: see mao_lark_author.h.
 */
#include "mao_lark_author.h"

#include <math.h>
#include <string.h>
#include "esp_random.h"

/* ---------------------------------------------------------------------- */
/* Building blocks                                                        */
/* ---------------------------------------------------------------------- */

float lark_rand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(esp_random() % 10000) / 10000.0f;
}

bool lark_chance(float p)
{
    return lark_rand(0.0f, 1.0f) < p;
}

static float sgn(void)
{
    return lark_chance(0.5f) ? 1.0f : -1.0f;
}

static uint32_t u32r(uint32_t lo, uint32_t hi)
{
    return lo + (uint32_t)lark_rand(0.0f, (float)(hi - lo) + 0.999f);
}

void lark_gen_begin(lark_gen_t *g)
{
    memset(g, 0, sizeof(*g));
}

int lark_gen_track(lark_gen_t *g, uint8_t ch)
{
    if (g->n_tracks >= LARK_GEN_TRACKS) {
        return -1;
    }
    const int t = g->n_tracks++;
    g->tracks[t] = (lark_track_t) { .ch = ch, .n = 0, .keys = g->keys[t] };
    return t;
}

void lark_gen_key(lark_gen_t *g, int track, uint32_t t_ms, float v, uint8_t ease)
{
    if (track < 0) {
        return;
    }
    lark_track_t *tr = &g->tracks[track];
    if (tr->n >= LARK_GEN_KEYS) {
        return;
    }
    /* Keys must be in time order; nudge a clash forward. */
    if (tr->n && t_ms <= g->keys[track][tr->n - 1].t_ms) {
        t_ms = g->keys[track][tr->n - 1].t_ms + 1;
    }
    g->keys[track][tr->n++] = (lark_key_t) { (uint16_t)t_ms, v, ease };
}

void lark_gen_end(lark_gen_t *g, uint32_t length_ms)
{
    g->length_ms = (uint16_t)length_ms;
    for (int t = 0; t < g->n_tracks; t++) {
        if (g->tracks[t].n == 0) {
            lark_gen_key(g, t, 0, 0.0f, LARK_LINEAR);
        }
    }
}

/* A track that holds v from t0 to t1 with soft ramps in and out. */
static void hold(lark_gen_t *g, uint8_t ch, uint32_t t0, uint32_t t1, float v, uint32_t ramp_in, uint32_t ramp_out)
{
    const int t = lark_gen_track(g, ch);
    lark_gen_key(g, t, 0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, t, t0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, t, t0 + ramp_in, v, LARK_OUT);
    lark_gen_key(g, t, t1, v, LARK_LINEAR);
    lark_gen_key(g, t, t1 + ramp_out, 0.0f, LARK_IN_OUT);
}

/* A track that stays at v for the whole timeline (loop-safe). */
static void constant(lark_gen_t *g, uint8_t ch, float v)
{
    lark_gen_key(g, lark_gen_track(g, ch), 0, v, LARK_LINEAR);
}

/* A head shake starting at t: n swings decaying from amp. Returns end time. */
static uint32_t shake(lark_gen_t *g, int tr, uint32_t t, int n, float amp, float decay, uint32_t period)
{
    float a = amp * sgn();
    lark_gen_key(g, tr, t, 0.0f, LARK_LINEAR);
    for (int i = 0; i < n; i++) {
        t += period / 2 + u32r(0, period / 5);
        lark_gen_key(g, tr, t, a, i == 0 ? LARK_OUT : LARK_IN_OUT);
        a = -a * decay;
    }
    t += period / 2;
    lark_gen_key(g, tr, t, 0.0f, LARK_BACK);
    return t;
}

/* ---------------------------------------------------------------------- */
/* Dizzy                                                                  */
/* ---------------------------------------------------------------------- */

void lark_gen_dizzy(lark_gen_t *g)
{
    lark_gen_begin(g);
    const int gx = lark_gen_track(g, CH_GAZE_X), gy = lark_gen_track(g, CH_GAZE_Y), tl = lark_gen_track(g, CH_TILT);
    const float dir = sgn();
    const float loops = lark_rand(1.5f, 3.6f);
    float period = lark_rand(360.0f, 560.0f);
    const float slow = lark_rand(1.08f, 1.35f);          /* each loop slower than the last */
    float r = lark_rand(9.0f, 13.0f);
    const float shrink = lark_rand(0.72f, 0.92f);
    const float ell = lark_rand(0.65f, 1.1f);            /* round or flattened circles */
    const float phase0 = (float)u32r(0, 3);
    uint32_t t = 0;
    const int quarters = (int)(loops * 4.0f);
    for (int q = 0; q <= quarters; q++) {
        const float ang = (phase0 + (float)q) * 1.5708f * dir;
        const float rr = r * powf(shrink, (float)q / 4.0f);
        /* Quarter points of a circle, eased like a sine. */
        lark_gen_key(g, gx, t, rr * sinf(ang), (q & 1) ? LARK_IN : LARK_OUT);
        lark_gen_key(g, gy, t, -rr * ell * cosf(ang), (q & 1) ? LARK_OUT : LARK_IN);
        if ((q & 1) == 0) {
            lark_gen_key(g, tl, t, 3.5f * sinf(ang * 0.5f) * rr / 12.0f, LARK_IN_OUT);
        }
        t += (uint32_t)(period * 0.25f * powf(slow, (float)q / 4.0f));
    }
    const uint32_t spin_end = t;
    lark_gen_key(g, gx, t + 250, 0.0f, LARK_IN_OUT);
    lark_gen_key(g, gy, t + 250, 0.0f, LARK_IN_OUT);
    lark_gen_key(g, tl, t + 300, 0.0f, LARK_IN_OUT);
    /* Sometimes the eyes lose each other for a moment. */
    if (lark_chance(0.5f)) {
        hold(g, CH_WOBBLE, spin_end / 3, spin_end, lark_rand(2.0f, 5.0f), 300, 400);
    } else if (lark_chance(0.4f)) {
        hold(g, CH_CROSS, spin_end / 2, spin_end + 200, lark_rand(3.0f, 7.0f) * sgn(), 250, 250);
    }
    hold(g, CH_OPEN, 0, spin_end, -lark_rand(0.05f, 0.14f), 120, 300);
    /* The ending: shake it off, blink it away, or sag. */
    t += 120;
    const uint32_t end_kind = u32r(0, 2);
    if (end_kind == 0) {
        const int fx = lark_gen_track(g, CH_FACE_X);
        t = shake(g, fx, t, (int)u32r(2, 4), lark_rand(12.0f, 18.0f), lark_rand(0.55f, 0.75f), u32r(180, 240));
    } else if (end_kind == 1) {
        const int cl = lark_gen_track(g, CH_CLOSE);
        lark_gen_key(g, cl, 0, 0.0f, LARK_LINEAR);
        lark_gen_key(g, cl, t, 0.0f, LARK_LINEAR);
        const int n = (int)u32r(2, 4);
        for (int i = 0; i < n; i++) {
            lark_gen_key(g, cl, t + 70, 0.95f, LARK_IN);
            lark_gen_key(g, cl, t + 160, 0.0f, LARK_OUT);
            t += u32r(200, 280);
        }
    } else {
        hold(g, CH_NARROW, t, t + u32r(500, 900), lark_rand(0.25f, 0.4f), 200, 400);
        hold(g, CH_FACE_Y, t, t + u32r(400, 800), lark_rand(8.0f, 14.0f), 250, 450);
        t += 1100;
    }
    lark_gen_end(g, t + 300);
}

/* ---------------------------------------------------------------------- */
/* Startle                                                                */
/* ---------------------------------------------------------------------- */

void lark_gen_startle(lark_gen_t *g)
{
    lark_gen_begin(g);
    const float jolt = lark_rand(12.0f, 26.0f);
    const uint32_t hit = u32r(50, 100);
    const uint32_t hold_ms = u32r(350, 1100);
    const float side = sgn();
    const bool look_first = lark_chance(0.6f);
    /* The jolt. */
    const int fy = lark_gen_track(g, CH_FACE_Y);
    lark_gen_key(g, fy, 0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, fy, hit, -jolt, LARK_OUT);
    lark_gen_key(g, fy, hit + hold_ms, -jolt * lark_rand(0.6f, 0.9f), LARK_LINEAR);
    hold(g, CH_OPEN, 0, hit + hold_ms, lark_rand(0.14f, 0.3f), hit, 350);
    hold(g, CH_NARROW, 0, hit + hold_ms, -0.26f, hit, 400);
    hold(g, CH_PUPIL, 0, hit + hold_ms, -lark_rand(0.35f, 0.8f), hit, 450);
    hold(g, CH_SQUASH, 0, hit, -lark_rand(0.1f, 0.25f), hit / 2, 200);
    /* Where did it come from? */
    if (look_first) {
        const int gx = lark_gen_track(g, CH_GAZE_X), gyt = lark_gen_track(g, CH_GAZE_Y);
        const float tx = side * lark_rand(7.0f, 12.0f), ty = lark_rand(-8.0f, 3.0f);
        const uint32_t tl = hit + u32r(60, 220);
        lark_gen_key(g, gx, 0, 0.0f, LARK_LINEAR);
        lark_gen_key(g, gx, tl, tx, LARK_OUT);
        lark_gen_key(g, gyt, 0, 0.0f, LARK_LINEAR);
        lark_gen_key(g, gyt, tl, ty, LARK_OUT);
        lark_gen_key(g, gx, hit + hold_ms, tx, LARK_LINEAR);
        lark_gen_key(g, gyt, hit + hold_ms, ty, LARK_LINEAR);
        lark_gen_key(g, gx, hit + hold_ms + 180, 0.0f, LARK_OUT);
        lark_gen_key(g, gyt, hit + hold_ms + 180, 0.0f, LARK_OUT);
    }
    /* Recovery: a sigh, a double blink, or just settling. */
    uint32_t t = hit + hold_ms;
    const uint32_t rec = u32r(0, 2);
    if (rec == 0) {
        lark_gen_key(g, fy, t + 350, lark_rand(6.0f, 12.0f), LARK_IN_OUT);
        lark_gen_key(g, fy, t + 900, 0.0f, LARK_IN_OUT);
        hold(g, CH_CLOSE, t + 100, t + 500, lark_rand(0.3f, 0.5f), 250, 350);
        t += 950;
    } else if (rec == 1) {
        const int cl = lark_gen_track(g, CH_CLOSE);
        lark_gen_key(g, cl, 0, 0.0f, LARK_LINEAR);
        lark_gen_key(g, cl, t, 0.0f, LARK_LINEAR);
        lark_gen_key(g, cl, t + 70, 0.95f, LARK_IN);
        lark_gen_key(g, cl, t + 150, 0.0f, LARK_OUT);
        lark_gen_key(g, cl, t + 230, 0.95f, LARK_IN);
        lark_gen_key(g, cl, t + 320, 0.0f, LARK_OUT);
        lark_gen_key(g, fy, t + 400, 0.0f, LARK_IN_OUT);
        t += 450;
    } else {
        lark_gen_key(g, fy, t + 500, 0.0f, LARK_IN_OUT);
        t += 500;
    }
    lark_gen_end(g, t + 150);
}

/* ---------------------------------------------------------------------- */
/* Happy / pleased                                                        */
/* ---------------------------------------------------------------------- */

void lark_gen_happy(lark_gen_t *g)
{
    lark_gen_begin(g);
    const int fy = lark_gen_track(g, CH_FACE_Y), sq = lark_gen_track(g, CH_SQUASH);
    const int hops = (int)u32r(1, 3);
    float h = lark_rand(12.0f, 20.0f);
    uint32_t t = u32r(60, 140);
    lark_gen_key(g, fy, 0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, sq, 0, 0.0f, LARK_LINEAR);
    for (int i = 0; i < hops; i++) {
        const uint32_t up = u32r(140, 200), down = u32r(150, 200);
        lark_gen_key(g, fy, t, 0.0f, LARK_LINEAR);
        lark_gen_key(g, fy, t + up, -h, LARK_OUT);
        lark_gen_key(g, fy, t + up + down, 0.0f, LARK_IN);
        lark_gen_key(g, sq, t + up + down, 0.0f, LARK_LINEAR);
        lark_gen_key(g, sq, t + up + down + 60, lark_rand(0.12f, 0.28f), LARK_OUT);
        lark_gen_key(g, sq, t + up + down + 170, 0.0f, LARK_IN_OUT);
        t += up + down + u32r(20, 90);
        h *= lark_rand(0.5f, 0.75f);
    }
    const uint32_t len = t + u32r(700, 1300);
    hold(g, CH_SMILE, 0, len, lark_rand(0.85f, 1.0f), 160, 0);   /* ^ ^, held into purr */
    hold(g, CH_SHINE, 0, len - 450, lark_rand(0.3f, 0.7f), 160, 450);
    if (lark_chance(0.6f)) {
        hold(g, CH_TILT, u32r(100, 300), len - 500, lark_rand(2.0f, 4.5f) * sgn(), 250, 450);
    }

    lark_gen_end(g, len);
}

void lark_gen_pleased(lark_gen_t *g)
{
    lark_gen_begin(g);
    const uint32_t len = u32r(1600, 2600);
    hold(g, CH_SMILE, u32r(0, 200), len - 500, lark_rand(0.3f, 0.5f), 300, 500);
    hold(g, CH_NARROW, 0, len - 500, lark_rand(0.05f, 0.18f), 300, 500);
    if (lark_chance(0.5f)) {
        hold(g, CH_TILT, 0, len - 500, lark_rand(1.5f, 3.5f) * sgn(), 300, 500);
    }
    if (lark_chance(0.5f)) {
        const int fy = lark_gen_track(g, CH_FACE_Y);
        lark_gen_key(g, fy, 0, 0.0f, LARK_LINEAR);
        lark_gen_key(g, fy, 250, -lark_rand(5.0f, 10.0f), LARK_OUT);
        lark_gen_key(g, fy, len, 0.0f, LARK_IN_OUT);
    } else {
        hold(g, CH_GAZE_X, 200, len - 700, lark_rand(4.0f, 9.0f) * sgn(), 250, 400);   /* a pleased look away */
    }
    lark_gen_end(g, len);
}

/* ---------------------------------------------------------------------- */
/* Tsk / mad                                                              */
/* ---------------------------------------------------------------------- */

void lark_gen_tsk(lark_gen_t *g)
{
    lark_gen_begin(g);
    const int fx = lark_gen_track(g, CH_FACE_X);
    const uint32_t t0 = u32r(0, 120);
    const uint32_t end = shake(g, fx, t0, (int)u32r(2, 4), lark_rand(10.0f, 16.0f), lark_rand(0.55f, 0.8f), u32r(200, 280));
    hold(g, CH_NARROW, 0, end + u32r(100, 400), lark_rand(0.2f, 0.36f), 150, 250);
    hold(g, CH_LID_ANGLE, 0, end, lark_rand(0.2f, 0.45f), 150, 300);
    hold(g, CH_GAZE_Y, 0, end, lark_rand(1.0f, 4.0f), 150, 250);
    if (lark_chance(0.4f)) {
        hold(g, CH_SQUINT, 0, end, lark_rand(0.3f, 0.6f) * sgn(), 150, 250);
    }
    lark_gen_end(g, end + 450);
}

void lark_gen_mad(lark_gen_t *g)
{
    lark_gen_begin(g);
    const int fx = lark_gen_track(g, CH_FACE_X);
    const uint32_t end = shake(g, fx, 0, (int)u32r(3, 5), lark_rand(14.0f, 20.0f), lark_rand(0.6f, 0.8f), u32r(180, 240));
    const uint32_t glare = end + u32r(1100, 2400);
    hold(g, CH_NARROW, 0, glare, lark_rand(0.18f, 0.3f), 100, 500);
    hold(g, CH_PUPIL, 0, glare, -lark_rand(0.25f, 0.45f), 100, 500);
    hold(g, CH_TINT_RED, 0, glare - 300, lark_rand(0.5f, 0.85f), 150, 600);
    hold(g, CH_LID_ANGLE, 0, glare, lark_rand(0.55f, 0.85f), 120, 500);
    hold(g, CH_GAZE_Y, 0, glare, lark_rand(2.0f, 5.0f), 200, 400);
    hold(g, CH_FACE_Y, end, glare, -lark_rand(4.0f, 8.0f), 250, 400);
    if (lark_chance(0.5f)) {
        hold(g, CH_SQUASH, end, glare, lark_rand(0.08f, 0.18f), 200, 400);   /* puffed up */
    }
    lark_gen_end(g, glare + 550);
}

/* ---------------------------------------------------------------------- */
/* Sniff                                                                  */
/* ---------------------------------------------------------------------- */

void lark_gen_sniff(lark_gen_t *g)
{
    lark_gen_begin(g);
    const int fy = lark_gen_track(g, CH_FACE_Y), fx = lark_gen_track(g, CH_FACE_X);
    const int n = (int)u32r(3, 8);
    uint32_t t = u32r(80, 200);
    float x = 0.0f;
    lark_gen_key(g, fy, 0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, fx, 0, 0.0f, LARK_LINEAR);
    for (int i = 0; i < n; i++) {
        const uint32_t step = u32r(110, 210);
        lark_gen_key(g, fy, t, -lark_rand(5.0f, 10.0f), LARK_OUT);
        lark_gen_key(g, fy, t + step / 2 + 20, -lark_rand(0.0f, 3.0f), LARK_IN);
        t += step;
        if (lark_chance(0.25f)) {
            t += u32r(150, 380);                          /* a pause to think about it */
            x += lark_rand(6.0f, 12.0f) * sgn();          /* and follow the scent */
            lark_gen_key(g, fx, t, x, LARK_IN_OUT);
        }
    }
    lark_gen_key(g, fy, t + 250, 0.0f, LARK_IN_OUT);
    lark_gen_key(g, fx, t + 300, 0.0f, LARK_IN_OUT);
    hold(g, CH_CLOSE, 0, t, lark_rand(0.3f, 0.55f), 180, 250);
    hold(g, CH_GAZE_Y, 0, t, -lark_rand(1.0f, 4.0f), 180, 250);
    lark_gen_end(g, t + 350);
}

/* ---------------------------------------------------------------------- */
/* Double take / eye roll / keen                                          */
/* ---------------------------------------------------------------------- */

void lark_gen_doubletake(lark_gen_t *g)
{
    lark_gen_begin(g);
    const int gx = lark_gen_track(g, CH_GAZE_X);
    const float side = sgn();
    const uint32_t away = u32r(180, 260), stay = u32r(250, 600);
    lark_gen_key(g, gx, 0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, gx, away, side * lark_rand(8.0f, 12.0f), LARK_OUT);
    lark_gen_key(g, gx, away + stay, side * lark_rand(7.0f, 12.0f), LARK_LINEAR);
    const uint32_t snap = away + stay + u32r(70, 110);
    lark_gen_key(g, gx, snap, 0.0f, LARK_OUT);
    uint32_t end = snap + u32r(500, 900);
    if (lark_chance(0.3f)) {                               /* ...and a second check */
        lark_gen_key(g, gx, snap + 350, side * lark_rand(4.0f, 8.0f), LARK_OUT);
        lark_gen_key(g, gx, snap + 550, 0.0f, LARK_OUT);
        end += 500;
    }
    hold(g, CH_OPEN, snap - 40, end - 400, lark_rand(0.14f, 0.25f), 90, 400);
    hold(g, CH_NARROW, snap - 40, end - 400, -0.26f, 90, 400);
    hold(g, CH_PUPIL, snap - 40, end - 500, -lark_rand(0.25f, 0.5f), 90, 400);
    hold(g, CH_FACE_Y, snap - 40, end - 600, -lark_rand(8.0f, 15.0f), 90, 500);
    lark_gen_end(g, end);
}

void lark_gen_eyeroll(lark_gen_t *g)
{
    lark_gen_begin(g);
    const int gx = lark_gen_track(g, CH_GAZE_X), gy = lark_gen_track(g, CH_GAZE_Y);
    const float dir = sgn();
    const float span = lark_rand(0.8f, 1.2f);
    const uint32_t step = u32r(160, 260);
    const float top = -lark_rand(11.0f, 15.0f);
    lark_gen_key(g, gx, 0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, gy, 0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, gx, step, -dir * 9.0f * span, LARK_IN_OUT);
    lark_gen_key(g, gy, step, top * 0.8f, LARK_IN_OUT);
    lark_gen_key(g, gx, step * 2, 0.0f, LARK_IN_OUT);
    lark_gen_key(g, gy, step * 2, top, LARK_IN_OUT);
    lark_gen_key(g, gx, step * 3, dir * 9.0f * span, LARK_IN_OUT);
    lark_gen_key(g, gy, step * 3, top * 0.8f, LARK_IN_OUT);
    const uint32_t land = step * 4;
    lark_gen_key(g, gx, land, dir * lark_rand(2.0f, 6.0f), LARK_IN_OUT);
    lark_gen_key(g, gy, land, lark_rand(0.0f, 3.0f), LARK_IN_OUT);
    const uint32_t end = land + u32r(400, 800);
    lark_gen_key(g, gx, end, 0.0f, LARK_IN_OUT);
    lark_gen_key(g, gy, end, 0.0f, LARK_IN_OUT);
    hold(g, CH_NARROW, land - 150, end - 150, lark_rand(0.26f, 0.4f), 150, 250);
    if (lark_chance(0.5f)) {
        hold(g, CH_TILT, step, land, lark_rand(2.0f, 4.0f) * dir, 200, 300);
    }
    if (lark_chance(0.35f)) {                             /* with a sigh */
        hold(g, CH_FACE_Y, land, end, lark_rand(6.0f, 12.0f), 200, 300);
    }
    lark_gen_end(g, end + 100);
}

void lark_gen_keen(lark_gen_t *g)
{
    lark_gen_begin(g);
    const uint32_t len = u32r(1300, 2200);
    const uint32_t pop = u32r(60, 110);
    hold(g, CH_NARROW, 0, len - 400, -0.26f, pop, 400);
    hold(g, CH_OPEN, 0, len / 2, lark_rand(0.1f, 0.22f), pop, 500);
    hold(g, CH_PUPIL, 0, len / 2, -lark_rand(0.2f, 0.45f), pop, 500);
    hold(g, CH_FACE_Y, 0, len / 2, -lark_rand(6.0f, 12.0f), pop + 40, 500);
    if (lark_chance(0.5f)) {
        hold(g, CH_TILT, pop, len - 500, lark_rand(2.0f, 4.0f) * sgn(), 200, 400);
    }
    if (lark_chance(0.4f)) {
        hold(g, CH_GAZE_Y, pop, len - 500, -lark_rand(2.0f, 5.0f), 150, 400);
    }
    lark_gen_end(g, len);
}

/* ---------------------------------------------------------------------- */
/* Flustered / anxious (looping: a new cycle every time round)            */
/* ---------------------------------------------------------------------- */

/* Darting saccades: n fixations of random length on alternating sides. */
static uint32_t darts(lark_gen_t *g, int n, float amp, uint32_t fix_lo, uint32_t fix_hi)
{
    const int gx = lark_gen_track(g, CH_GAZE_X), gy = lark_gen_track(g, CH_GAZE_Y);
    float side = sgn();
    uint32_t t = 0;
    lark_gen_key(g, gx, 0, 0.0f, LARK_LINEAR);
    lark_gen_key(g, gy, 0, 0.0f, LARK_LINEAR);
    for (int i = 0; i < n; i++) {
        if (lark_chance(0.7f)) {
            side = -side;
        }
        const float x = side * lark_rand(amp * 0.4f, amp), y = lark_rand(-amp * 0.3f, amp * 0.3f);
        lark_gen_key(g, gx, t + 70, x, LARK_OUT);
        lark_gen_key(g, gy, t + 70, y, LARK_OUT);
        t += u32r(fix_lo, fix_hi);
        lark_gen_key(g, gx, t, x, LARK_LINEAR);
        lark_gen_key(g, gy, t, y, LARK_LINEAR);
    }
    lark_gen_key(g, gx, t + 90, 0.0f, LARK_OUT);
    lark_gen_key(g, gy, t + 90, 0.0f, LARK_OUT);
    return t + 120;
}

/* A fine tremble on face x across the whole cycle. */
static void tremble(lark_gen_t *g, uint32_t len, float amp, uint32_t period)
{
    const int fx = lark_gen_track(g, CH_FACE_X);
    uint32_t t = 0;
    float s = 1.0f;
    while (t < len && g->tracks[fx].n < LARK_GEN_KEYS - 1) {
        lark_gen_key(g, fx, t, s * amp * lark_rand(0.5f, 1.0f), LARK_LINEAR);
        s = -s;
        t += period / 2 + u32r(0, period / 3);
    }
    lark_gen_key(g, fx, len, 0.0f, LARK_LINEAR);
}

void lark_gen_flustered(lark_gen_t *g)
{
    lark_gen_begin(g);
    const uint32_t len = darts(g, (int)u32r(3, 6), lark_rand(7.0f, 11.0f), 180, 420);
    constant(g, CH_OPEN, 0.14f);
    constant(g, CH_NARROW, -0.26f);
    constant(g, CH_PUPIL, -0.45f);
    constant(g, CH_LID_ANGLE, -0.45f);
    tremble(g, len, lark_rand(1.5f, 3.0f), u32r(100, 140));
    lark_gen_end(g, len);
}

void lark_gen_anxious(lark_gen_t *g)
{
    lark_gen_begin(g);
    const uint32_t len = darts(g, (int)u32r(3, 5), lark_rand(4.0f, 8.0f), 250, 600);
    constant(g, CH_CLOSE, lark_rand(0.3f, 0.45f));
    constant(g, CH_SMILE, 0.25f);
    constant(g, CH_PUPIL, -0.55f);
    constant(g, CH_LID_ANGLE, -0.5f);
    tremble(g, len, lark_rand(1.5f, 2.8f), u32r(90, 130));
    lark_gen_end(g, len);
}

/* ---------------------------------------------------------------------- */
/* Asleep (looping: every cycle its own little events)                    */
/* ---------------------------------------------------------------------- */

void lark_gen_asleep(lark_gen_t *g)
{
    lark_gen_begin(g);
    const uint32_t len = u32r(5000, 12000);
    const int cl = lark_gen_track(g, CH_CLOSE);
    lark_gen_key(g, cl, 0, 0.95f, LARK_LINEAR);
    /* Maybe one small event somewhere in this cycle. */
    const uint32_t at = u32r(1500, len - 2500);
    const float r = lark_rand(0.0f, 1.0f);
    if (r < 0.18f) {                                      /* a sleepy half-peek, then back under */
        lark_gen_key(g, cl, at, 0.95f, LARK_LINEAR);
        lark_gen_key(g, cl, at + 500, lark_rand(0.45f, 0.7f), LARK_IN_OUT);
        lark_gen_key(g, cl, at + 500 + u32r(400, 1200), lark_rand(0.45f, 0.7f), LARK_LINEAR);
        lark_gen_key(g, cl, at + 2200, 0.95f, LARK_IN_OUT);
        hold(g, CH_GAZE_X, at + 400, at + 1500, lark_rand(3.0f, 8.0f) * sgn(), 400, 500);
    } else if (r < 0.36f) {                               /* a twitch in a dream */
        const int fy = lark_gen_track(g, CH_FACE_Y);
        lark_gen_key(g, fy, 0, 0.0f, LARK_LINEAR);
        lark_gen_key(g, fy, at, 0.0f, LARK_LINEAR);
        lark_gen_key(g, fy, at + 60, -lark_rand(4.0f, 9.0f), LARK_OUT);
        lark_gen_key(g, fy, at + 260, 0.0f, LARK_IN);
        if (lark_chance(0.5f)) {
            lark_gen_key(g, fy, at + 420, -lark_rand(2.0f, 5.0f), LARK_OUT);
            lark_gen_key(g, fy, at + 600, 0.0f, LARK_IN);
        }
    } else if (r < 0.52f) {                               /* a smile in her sleep */
        hold(g, CH_SMILE, at, at + u32r(1200, 2500), lark_rand(0.2f, 0.4f), 600, 900);
    } else if (r < 0.68f) {                               /* rolls a little to one side */
        const float side = sgn();
        hold(g, CH_TILT, at, len - 200, side * lark_rand(2.0f, 4.0f), 1500, 200);
        hold(g, CH_FACE_X, at, len - 200, side * lark_rand(6.0f, 14.0f), 1500, 200);
    } else if (r < 0.78f) {                               /* a deep breath */
        hold(g, CH_FACE_Y, at, at + 900, -lark_rand(4.0f, 7.0f), 900, 1200);
    }                                                     /* otherwise: just sleeping */
    lark_gen_end(g, len);
}
