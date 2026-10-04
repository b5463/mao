/*
 * MAO perception engine (platform-independent core). See percept_engine.h.
 *
 * Conventions: times are uint32 ms and compared wrap-safe; a stored time of
 * 0 means "never" (stamp() nudges a real 0 to 1). Thresholds come from
 * pe_config_t; the few structural constants below are documented where
 * they are used and in docs/firmware/mao-a0-firmware.md.
 */
#include "percept_engine.h"

#include <math.h>
#include <string.h>

#define ZONES             MAO_PERCEPT_ZONE_COUNT
#define BIT(z)            ((uint8_t)(1u << (z)))
#define SIDE_MASK         (BIT(MAO_PERCEPT_ZONE_LEFT) | BIT(MAO_PERCEPT_ZONE_RIGHT))

/* Filters (time constants, ms). */
#define GRAVITY_TAU_MS    600.0f
#define ENERGY_TAU_MS     300.0f
#define TOF_GAIN          0.6f        /* EMA gain on the median-of-3 distance */
#define LUX_TAU_MS        1500.0f
#define LUX_REF_TAU_MS    30000.0f
#define AMBIENT_UP_MS     8000.0f     /* room level follows rises slowly ... */
#define AMBIENT_DOWN_MS   2500.0f     /* ... and falls faster */

/* Windows and timings (ms). */
#define IMU_GAP_MS        1500        /* longer gap: restart the motion filters */
#define REST_MS           1000        /* still this long = resting */
#define NUDGE_MAX_MS      1500
#define SHAKE_WINDOW_MS   1500
#define SHAKE_COUNT       4
#define UPSIDE_ENTER_MS   800
#define UPSIDE_LEAVE_MS   600
#define TOF_GAP_MS        800
#define NEAR_LEAVE_MS     400
#define WITHDRAW_MS       1200
#define COVER_ENTER_MS    1200
#define COVER_LEAVE_MS    800
#define LIGHT_ON_MS       2000
#define SIDE_PENDING_MS   150         /* wait for the other rim side: grip, not touch */
#define REPEAT_WINDOW_MS  2500
#define STROKE_MIN_MS     200
#define STROKE_WINDOW_MS  4000
#define NOISE_DECIDE_MS   180         /* wait for a knock that explains the noise */
#define KNOCK_NOISE_MS    300
#define MIC_GAP_MS        2000
#define QUIET_LEAVE_MS    4000
#define PRESS_WINDOW_MS   2000
#define LEVEL_STEP_MS     1200
#define BATTERY_REPEAT_MS (10u * 60u * 1000u)

/* Fiddling score (points) and decay half-lives. */
#define FIDDLE_L1         3.0f
#define FIDDLE_L2         6.0f
#define FIDDLE_L3         9.5f
#define FIDDLE_CALM       1.0f
#define FIDDLE_HYST       1.5f
#define FIDDLE_HALF_MS    6000.0f
#define RESIDUE_HALF_MS   60000.0f
#define DIAL_ENERGY_HALF_MS 1500.0f

/* Per-percept cooldowns (ms). State percepts have none: hysteresis governs them. */
static const uint32_t kCooldown[MAO_PERCEPT_COUNT] = {
    [MAO_PERCEPT_APPROACH_STARTED] = 5000,
    [MAO_PERCEPT_APPROACH_NEAR] = 3000,
    [MAO_PERCEPT_WITHDRAWN] = 3000,
    [MAO_PERCEPT_TOUCH] = 300,
    [MAO_PERCEPT_TOUCH_HOLD] = 1000,
    [MAO_PERCEPT_TOUCH_REPEAT] = 3000,
    [MAO_PERCEPT_GENTLE_PET] = 5000,
    [MAO_PERCEPT_PICKED_UP] = 1500,
    [MAO_PERCEPT_PUT_DOWN] = 1000,
    [MAO_PERCEPT_HARD_PUT_DOWN] = 1000,
    [MAO_PERCEPT_SHAKE] = 2500,
    [MAO_PERCEPT_NUDGED] = 4000,
    [MAO_PERCEPT_KNOCK] = 800,
    [MAO_PERCEPT_SUDDEN_NOISE] = 3000,
    [MAO_PERCEPT_REMOTE_SIGNAL] = 1500,
};

/* Event-like percepts share the global rate limit; state and body
 * percepts always get through (they are rare by construction). */
static bool rate_limited(mao_percept_t p)
{
    switch (p) {
    case MAO_PERCEPT_APPROACH_STARTED:
    case MAO_PERCEPT_APPROACH_NEAR:
    case MAO_PERCEPT_WITHDRAWN:
    case MAO_PERCEPT_TOUCH:
    case MAO_PERCEPT_TOUCH_HOLD:
    case MAO_PERCEPT_TOUCH_REPEAT:
    case MAO_PERCEPT_SHAKE:
    case MAO_PERCEPT_NUDGED:
    case MAO_PERCEPT_KNOCK:
    case MAO_PERCEPT_SUDDEN_NOISE:
    case MAO_PERCEPT_REMOTE_SIGNAL:
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

static inline uint32_t stamp(uint32_t t)
{
    return t ? t : 1u;
}

static inline uint32_t since(uint32_t now, uint32_t t)
{
    return now - t;
}

/* t is set and no more than window ms ago. */
static inline bool within(uint32_t now, uint32_t t, uint32_t window)
{
    return t != 0 && since(now, t) <= window;
}

/* now is before until (wrap-safe). */
static inline bool before(uint32_t now, uint32_t until)
{
    return (int32_t)(until - now) > 0;
}

static inline float alpha(uint32_t dt_ms, float tau_ms)
{
    return (float)dt_ms / (tau_ms + (float)dt_ms);
}

/* Multiplicative decay for a half-life. */
static inline float decay(uint32_t dt_ms, float half_ms)
{
    return expf(-0.69314718f * (float)dt_ms / half_ms);
}

static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float minf(float a, float b)
{
    return a < b ? a : b;
}

static void advance(pe_engine_t *e, uint32_t t)
{
    if ((int32_t)(t - e->now) > 0) {
        e->now = t;
    }
}

static bool has(const pe_engine_t *e, uint32_t sense)
{
    return (e->senses & sense) != 0;
}

/* ------------------------------------------------------------------------ */
/* Output                                                                   */
/* ------------------------------------------------------------------------ */

static bool emit(pe_engine_t *e, mao_percept_t p, int confidence, uint32_t detail)
{
    const uint32_t now = e->now;
    if (p == MAO_PERCEPT_NONE || (unsigned)p >= (unsigned)MAO_PERCEPT_COUNT) {
        return false;
    }
    if (e->emitted[p] && kCooldown[p] && since(now, e->last_emit[p]) < kCooldown[p]) {
        return false;
    }
    if (rate_limited(p)) {
        uint8_t recent = 0;
        for (int i = 0; i < PE_RATE_HISTORY; i++) {
            if (within(now, e->rate_t[i], 999)) {
                recent++;
            }
        }
        if (recent >= e->cfg.max_percepts_per_s) {
            e->dropped++;
            return false;
        }
        e->rate_t[e->rate_head] = stamp(now);
        e->rate_head = (uint8_t)((e->rate_head + 1) % PE_RATE_HISTORY);
    }
    if (e->out_count == PE_OUT_QUEUE) {
        e->out_head = (uint8_t)((e->out_head + 1) % PE_OUT_QUEUE);   /* drop the oldest */
        e->out_count--;
        e->dropped++;
    }
    const uint8_t slot = (uint8_t)((e->out_head + e->out_count) % PE_OUT_QUEUE);
    e->out[slot] = (pe_out_t) {
        .percept = p,
        .confidence = (uint8_t)clamp_i(confidence, 0, 100),
        .detail = (uint16_t)(detail > MAO_PERCEPT_DETAIL_MAX ? MAO_PERCEPT_DETAIL_MAX : detail),
        .t_ms = now,
    };
    e->out_count++;
    e->emitted[p] = true;
    e->last_emit[p] = now;
    return true;
}

bool pe_pop(pe_engine_t *e, pe_out_t *out)
{
    if (!e->out_count) {
        return false;
    }
    if (out) {
        *out = e->out[e->out_head];
    }
    e->out_head = (uint8_t)((e->out_head + 1) % PE_OUT_QUEUE);
    e->out_count--;
    return true;
}

/* Someone is (still) around. Coming back after a long time is news in
 * itself, announced before whatever brought them back. */
static void presence(pe_engine_t *e)
{
    const uint32_t now = e->now;
    uint64_t away = (uint64_t)since(now, e->last_presence) + e->absence_carry_ms;
    if (away >= e->cfg.long_absence_ms) {
        const uint32_t minutes = (uint32_t)(away / 60000u);
        emit(e, MAO_PERCEPT_LONG_ABSENCE, 60 + clamp_i((int)(minutes / 30u), 0, 35), minutes);
    }
    e->last_presence = now;
    e->absence_carry_ms = 0;
}

/* ------------------------------------------------------------------------ */
/* Init / config                                                            */
/* ------------------------------------------------------------------------ */

void pe_config_default(pe_config_t *cfg)
{
    *cfg = (pe_config_t) {
        .upright_sign = 1.0f,
        .still_g = 0.025f,
        .move_g = 0.06f,
        .shake_g = 0.5f,
        .shock_g = 0.8f,
        .pickup_ms = 350,
        .settle_ms = 700,
        .near_mm = 200,
        .near_exit_mm = 300,
        .far_mm = 700,
        .cover_mm = 35,
        .approach_mm_s = 120.0f,
        .dark_lux = 3.0f,
        .light_lux = 12.0f,
        .dark_ms = 20000,
        .touch_debounce_ms = 40,
        .hold_ms = 1200,
        .pet_ms = 700,
        .quiet_dbfs = -58.0f,
        .quiet_ms = 120000,
        .noise_rise_db = 15.0f,
        .noise_min_dbfs = -50.0f,
        .long_absence_ms = 2u * 60u * 60u * 1000u,
        .usb_debounce_ms = 400,
        .max_percepts_per_s = 6,
    };
}

void pe_init(pe_engine_t *e, const pe_config_t *cfg, uint32_t senses, uint32_t now_ms)
{
    memset(e, 0, sizeof(*e));
    if (cfg) {
        e->cfg = *cfg;
    } else {
        pe_config_default(&e->cfg);
    }
    if (e->cfg.max_percepts_per_s == 0 || e->cfg.max_percepts_per_s > PE_RATE_HISTORY) {
        e->cfg.max_percepts_per_s = PE_RATE_HISTORY;
    }
    e->senses = senses;
    e->now = now_ms;
    e->last_tick = now_ms;
    e->last_presence = now_ms;
    e->f.residue_armed = true;
    e->t.pending_zone = -1;
}

void pe_set_usb_initial(pe_engine_t *e, bool present)
{
    e->usb.present = present;
    e->usb.pending = false;
}

void pe_set_absence(pe_engine_t *e, uint32_t absent_ms)
{
    e->absence_carry_ms = absent_ms;
}

void pe_note_self_sound(pe_engine_t *e, uint32_t until_ms)
{
    if (before(e->self_sound_until, until_ms) || e->self_sound_until == 0) {
        e->self_sound_until = stamp(until_ms);
    }
}

void pe_note_self_motion(pe_engine_t *e, uint32_t until_ms)
{
    if (before(e->self_motion_until, until_ms) || e->self_motion_until == 0) {
        e->self_motion_until = stamp(until_ms);
    }
}

/* ------------------------------------------------------------------------ */
/* Fiddling: several kinds of restless handling add up; it lingers          */
/* ------------------------------------------------------------------------ */

static float fiddle_score(const pe_engine_t *e)
{
    return e->f.dial + e->f.touch + e->f.motion + e->f.press + e->f.residue;
}

static int fiddle_target(float score)
{
    return score >= FIDDLE_L3 ? 3 : (score >= FIDDLE_L2 ? 2 : (score >= FIDDLE_L1 ? 1 : 0));
}

static void fiddle_caps(pe_engine_t *e)
{
    e->f.dial = minf(e->f.dial, 6.0f);
    e->f.touch = minf(e->f.touch, 4.0f);
    e->f.motion = minf(e->f.motion, 3.5f);
    e->f.press = minf(e->f.press, 3.0f);
    e->f.residue = minf(e->f.residue, 4.0f);
}

static void fiddle_evaluate(pe_engine_t *e)
{
    fiddle_caps(e);
    const float score = fiddle_score(e);
    const int target = fiddle_target(score);
    const uint32_t now = e->now;
    if (target > e->f.level &&
        (e->f.level == 0 || !within(now, e->f.last_level_up, LEVEL_STEP_MS - 1))) {
        /* One step at a time: suspicious, then tsk, then properly cross. */
        e->f.level++;
        e->f.last_level_up = stamp(now);
        if (e->f.level > e->f.peak) {
            e->f.peak = e->f.level;
        }
        if (e->f.level >= 2 && e->f.residue_armed) {
            e->f.residue += 1.5f;          /* it will remember this for a while */
            e->f.residue_armed = false;
        }
        int modalities = 0;
        modalities += e->f.dial > 0.5f;
        modalities += e->f.touch > 0.5f;
        modalities += e->f.motion > 0.5f;
        modalities += e->f.press > 0.5f;
        emit(e, MAO_PERCEPT_FIDDLING_ESCALATION, 45 + 12 * modalities + (e->f.residue > 0.5f ? 8 : 0),
             e->f.level);
    }
}

static void fiddle_tick(pe_engine_t *e, uint32_t dt)
{
    if (dt == 0) {
        return;
    }
    const float k = decay(dt, FIDDLE_HALF_MS);
    e->f.dial *= k;
    e->f.touch *= k;
    e->f.motion *= k;
    e->f.press *= k;
    e->f.residue *= decay(dt, RESIDUE_HALF_MS);
    e->f.dial_energy *= decay(dt, DIAL_ENERGY_HALF_MS);

    const float score = fiddle_score(e);
    /* Levels fall back quietly (hysteresis); calming down completely is
     * announced once, so the residue of the episode can be let go. */
    while (e->f.level > 0) {
        const float th = e->f.level == 3 ? FIDDLE_L3 : (e->f.level == 2 ? FIDDLE_L2 : FIDDLE_L1);
        if (score < th - FIDDLE_HYST) {
            e->f.level--;
        } else {
            break;
        }
    }
    if (e->f.peak > 0 && score < FIDDLE_CALM) {
        e->f.peak = 0;
        e->f.level = 0;
        e->f.residue_armed = true;
        emit(e, MAO_PERCEPT_FIDDLING_ESCALATION, 70, 0);
    }
    fiddle_evaluate(e);
}

/* ------------------------------------------------------------------------ */
/* Motion: IMU                                                              */
/* ------------------------------------------------------------------------ */

static float upright_component(const pe_engine_t *e)
{
    const float n = sqrtf(e->m.gx * e->m.gx + e->m.gy * e->m.gy + e->m.gz * e->m.gz);
    return n > 0.2f ? e->cfg.upright_sign * e->m.gz / n : 0.0f;
}

/* Angle between the current gravity estimate and the one at rest. */
static float tilt_from_rest_deg(const pe_engine_t *e)
{
    const float a = sqrtf(e->m.gx * e->m.gx + e->m.gy * e->m.gy + e->m.gz * e->m.gz);
    const float b = sqrtf(e->m.rest_gx * e->m.rest_gx + e->m.rest_gy * e->m.rest_gy + e->m.rest_gz * e->m.rest_gz);
    if (a < 0.2f || b < 0.2f) {
        return 0.0f;
    }
    float c = (e->m.gx * e->m.rest_gx + e->m.gy * e->m.rest_gy + e->m.gz * e->m.rest_gz) / (a * b);
    c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    return acosf(c) * 57.2957795f;
}

static void handle_tap(pe_engine_t *e, uint8_t kind)
{
    const uint32_t now = e->now;
    e->m.last_tap = stamp(now);
    e->m.last_tap_kind = kind;
    e->m.nudge_tap = true;
    /* A knock is a tap on a resting MAO that nobody is holding or touching,
     * and not the landing of a put-down. */
    if (e->m.held || e->t.stable || within(now, e->m.last_putdown, 800) ||
        (e->m.moving_since && !e->m.was_resting)) {
        return;
    }
    int conf = 55 + (kind == 2 ? 10 : 0);
    if (within(now, e->a.last_onset, KNOCK_NOISE_MS)) {
        conf += 25;                     /* heard it as well as felt it */
        e->a.pending_noise = 0;         /* the noise was the knock */
    }
    presence(e);
    emit(e, MAO_PERCEPT_KNOCK, conf, kind);
}

static void shake_push(pe_engine_t *e, uint32_t t)
{
    e->m.shake_t[e->m.shake_n % 6] = stamp(t);
    e->m.shake_n = (uint8_t)((e->m.shake_n + 1) % 6);
    int count = 0;
    for (int i = 0; i < 6; i++) {
        if (within(t, e->m.shake_t[i], SHAKE_WINDOW_MS)) {
            count++;
        }
    }
    if (count >= SHAKE_COUNT && emit(e, MAO_PERCEPT_SHAKE, 40 + 12 * count, 0)) {
        memset(e->m.shake_t, 0, sizeof(e->m.shake_t));
        e->f.motion += 2.5f;
        fiddle_evaluate(e);
    }
}

static void motion_classify(pe_engine_t *e, float dyn)
{
    const pe_config_t *c = &e->cfg;
    const uint32_t now = e->now;
    const bool moving = e->m.energy > c->move_g || dyn > 2.0f * c->move_g;
    const bool still = e->m.energy < c->still_g && dyn < c->move_g;

    if (still) {
        if (!e->m.still_since) {
            e->m.still_since = stamp(now);
        }
        if (since(now, e->m.still_since) >= REST_MS / 2 && !e->m.held) {
            e->m.rest_gx = e->m.gx;   /* where "flat on the desk" is right now */
            e->m.rest_gy = e->m.gy;
            e->m.rest_gz = e->m.gz;
        }
    } else {
        e->m.still_since = 0;
    }

    if (moving && !e->m.moving_since) {
        e->m.moving_since = stamp(now);
        e->m.was_resting = !e->m.held;
        if (e->m.was_resting) {
            e->m.nudge_start = stamp(now);
            e->m.nudge_peak = 0.0f;
            e->m.nudge_tap = within(now, e->m.last_tap, 300);
        }
    } else if (!moving && still) {
        e->m.moving_since = 0;
    }
    if (e->m.nudge_start && dyn > e->m.nudge_peak) {
        e->m.nudge_peak = dyn;
    }

    /* PICKED_UP: sustained motion from rest, with evidence of leaving the
     * desk (tilt) or of being held (both rim sides / the base touched). */
    if (!e->m.held && e->m.moving_since) {
        const uint32_t dur = since(now, e->m.moving_since);
        if (dur >= c->pickup_ms) {
            const float tilt = tilt_from_rest_deg(e);
            const bool grip = e->t.grip || (e->t.stable & BIT(MAO_PERCEPT_ZONE_REAR));
            int conf = 35;
            conf += clamp_i((int)(tilt * 1.5f), 0, 30);
            conf += grip ? 30 : 0;
            conf += clamp_i((int)((dur - c->pickup_ms) / 40u), 0, 20);
            conf += e->m.energy > 0.15f ? 10 : 0;
            /* Without tilt or grip only long, vigorous motion counts:
             * a vibrating desk is not a hand. */
            const bool evidence = tilt >= 12.0f || grip || (dur >= 900 && e->m.energy > 2.0f * c->move_g);
            if (evidence && conf >= 55) {
                e->m.held = true;
                e->m.held_since = stamp(now);
                e->m.nudge_start = 0;
                e->m.pickup_tilt = tilt;
                presence(e);
                emit(e, MAO_PERCEPT_PICKED_UP, conf, 0);
            }
        }
    }

    /* NUDGED: a short motion from rest that settles again by itself. */
    if (e->m.nudge_start && !e->m.held && still) {
        const uint32_t dur = since(now, e->m.nudge_start);
        if (dur <= NUDGE_MAX_MS && e->m.nudge_peak < 0.45f && !e->m.nudge_tap &&
            !within(now, e->m.last_tap, 400)) {
            emit(e, MAO_PERCEPT_NUDGED, 45 + clamp_i((int)(e->m.nudge_peak * 60.0f), 0, 20), 0);
        }
        e->m.nudge_start = 0;
    } else if (e->m.nudge_start && since(now, e->m.nudge_start) > NUDGE_MAX_MS) {
        e->m.nudge_start = 0;
    }

    /* PUT_DOWN: being held, then still and upright on the base. A shock or
     * tap just before landing makes it a hard one; free fall = dropped. */
    if (e->m.held && e->m.still_since && since(now, e->m.still_since) >= c->settle_ms) {
        const float zn = upright_component(e);
        e->m.held = false;
        if (zn > 0.8f) {
            /* The landing is the last real motion before the stillness: a
             * shock (or tap) at that moment makes it hard; a free fall just
             * before the landing means it was dropped. */
            const uint32_t land = e->m.last_motion_t ? e->m.last_motion_t : now;
            const bool shock = e->m.last_shock && since(land, e->m.last_shock) <= 500;
            const bool tap = e->m.last_tap && since(land, e->m.last_tap) <= 500;
            const bool hard = shock || tap;
            const bool dropped = shock && e->m.last_freefall && since(e->m.last_shock, e->m.last_freefall) <= 1500;
            e->m.last_putdown = stamp(now);
            if (dropped || hard) {
                emit(e, MAO_PERCEPT_HARD_PUT_DOWN, dropped ? 85 : 70, dropped ? 1 : 0);
                e->f.motion += 1.0f;
                fiddle_evaluate(e);
            } else {
                emit(e, MAO_PERCEPT_PUT_DOWN, 75, 0);
            }
        }
    }

    /* UPSIDE_DOWN with hysteresis on the upright component. */
    const float zn = upright_component(e);
    if (!e->m.upside) {
        if (zn < -0.7f) {
            if (!e->m.upside_cand) {
                e->m.upside_cand = stamp(now);
            } else if (since(now, e->m.upside_cand) >= UPSIDE_ENTER_MS) {
                e->m.upside = true;
                e->m.upside_cand = 0;
                emit(e, MAO_PERCEPT_UPSIDE_DOWN, 80, 1);
            }
        } else {
            e->m.upside_cand = 0;
        }
    } else {
        if (zn > -0.2f) {
            if (!e->m.upside_cand) {
                e->m.upside_cand = stamp(now);
            } else if (since(now, e->m.upside_cand) >= UPSIDE_LEAVE_MS) {
                e->m.upside = false;
                e->m.upside_cand = 0;
                emit(e, MAO_PERCEPT_UPSIDE_DOWN, 80, 0);
            }
        } else {
            e->m.upside_cand = 0;
        }
    }
}

void pe_feed_imu(pe_engine_t *e, const pe_imu_t *s)
{
    advance(e, s->t_ms);
    const uint32_t t = e->now;
    const bool gated = e->self_motion_until && before(t, e->self_motion_until);

    if (s->events & PE_IMU_FREE_FALL) {
        e->m.last_freefall = stamp(t);
        if (!e->m.held) {
            e->m.held = true;               /* in the air: a landing will follow */
            e->m.held_since = stamp(t);
        }
    }
    if (!gated && (s->events & (PE_IMU_TAP | PE_IMU_DOUBLE_TAP))) {
        handle_tap(e, (s->events & PE_IMU_DOUBLE_TAP) ? 2 : 1);
    }
    if (!s->accel_valid) {
        return;
    }

    if (!e->m.have || since(t, e->m.last_t) > IMU_GAP_MS) {
        e->m.have = true;
        e->m.last_t = t;
        e->m.gx = s->ax;
        e->m.gy = s->ay;
        e->m.gz = s->az;
        e->m.rest_gx = s->ax;
        e->m.rest_gy = s->ay;
        e->m.rest_gz = s->az;
        e->m.px = s->ax;
        e->m.py = s->ay;
        e->m.pz = s->az;
        e->m.energy = 0.0f;
        e->m.still_since = stamp(t);
        e->m.moving_since = 0;
        e->m.nudge_start = 0;
        return;
    }
    const uint32_t dt = clamp_u32(since(t, e->m.last_t), 5, 1000);
    e->m.last_t = t;

    const float ag = alpha(dt, GRAVITY_TAU_MS);
    e->m.gx += ag * (s->ax - e->m.gx);
    e->m.gy += ag * (s->ay - e->m.gy);
    e->m.gz += ag * (s->az - e->m.gz);
    if (gated) {
        e->m.px = s->ax;    /* MAO's own vibration: keep tracking gravity, judge nothing */
        e->m.py = s->ay;
        e->m.pz = s->az;
        return;
    }

    /* Motion = how much the acceleration changed since the last sample, or
     * how far its magnitude is from 1 g. Unlike the deviation from the
     * (slow) gravity estimate, this settles at once when MAO comes to rest
     * in a new orientation. */
    const float dx = s->ax - e->m.px, dy = s->ay - e->m.py, dz = s->az - e->m.pz;
    const float dd = sqrtf(dx * dx + dy * dy + dz * dz);
    const float mag = sqrtf(s->ax * s->ax + s->ay * s->ay + s->az * s->az);
    const float shock = fabsf(mag - 1.0f);
    const float dyn = dd > shock ? dd : shock;
    e->m.px = s->ax;
    e->m.py = s->ay;
    e->m.pz = s->az;
    e->m.last_dyn = dyn;
    if (dyn > e->cfg.move_g) {
        e->m.last_motion_t = stamp(t);
    }
    e->m.energy += alpha(dt, ENERGY_TAU_MS) * (dyn - e->m.energy);
    if (shock > e->cfg.shock_g) {
        e->m.last_shock = stamp(t);
    }
    if (dyn > e->cfg.shake_g || shock > e->cfg.shake_g) {
        shake_push(e, t);
    }
    motion_classify(e, dyn);
}

/* ------------------------------------------------------------------------ */
/* Proximity: ToF                                                           */
/* ------------------------------------------------------------------------ */

static bool approach_suppressed(const pe_engine_t *e)
{
    /* Being handled or covered: the sensor sees hands, not visitors. */
    return e->m.held || e->m.upside || e->c.covered;
}

static void near_enter(pe_engine_t *e, int conf)
{
    e->p.near = true;
    e->p.near_since = stamp(e->now);
    e->p.leave_since = 0;
    e->p.was_near = true;
    e->p.episode = true;
    e->p.gone_since = 0;
    if (!approach_suppressed(e)) {
        presence(e);
        emit(e, MAO_PERCEPT_APPROACH_NEAR, conf, 0);
    }
}

static uint16_t median3(const uint16_t *h, uint8_t n)
{
    if (n < 3) {
        return h[(n ? n : 1) - 1];
    }
    const uint16_t a = h[0], b = h[1], c = h[2];
    if ((a <= b && b <= c) || (c <= b && b <= a)) {
        return b;
    }
    if ((b <= a && a <= c) || (c <= a && a <= b)) {
        return a;
    }
    return c;
}

void pe_feed_tof(pe_engine_t *e, const pe_tof_t *s)
{
    advance(e, s->t_ms);
    const uint32_t t = e->now;
    const pe_config_t *c = &e->cfg;
    e->p.have = true;
    e->p.last_t = stamp(t);

    if (s->threshold) {
        /* Low-power approach detector (drowsy): the sensor itself decided
         * something is closer than its threshold. Less evidence, less
         * confidence. */
        if (!e->p.near) {
            near_enter(e, 60);
        }
        return;
    }

    const bool valid = s->status == 0 && s->distance_mm > 0 && s->distance_mm < 1300;
    if (!valid) {
        e->p.near_votes = 0;
        e->p.closing_votes = 0;
        if (e->p.near && !e->p.leave_since) {
            e->p.leave_since = stamp(t);
        }
        if (e->p.episode && !e->p.gone_since) {
            e->p.gone_since = stamp(t);
        }
        return;
    }

    if (e->p.hist_n < PE_TOF_MEDIAN) {
        e->p.hist[e->p.hist_n++] = s->distance_mm;
    } else {
        e->p.hist[0] = e->p.hist[1];
        e->p.hist[1] = e->p.hist[2];
        e->p.hist[2] = s->distance_mm;
    }
    const float med = (float)median3(e->p.hist, e->p.hist_n);
    if (!e->p.d_ok || !within(t, e->p.last_valid_t, TOF_GAP_MS)) {
        e->p.d = (float)s->distance_mm;     /* fresh track: trust this sample */
        e->p.v = 0.0f;
        e->p.d_ok = true;
        e->p.hist[0] = s->distance_mm;
        e->p.hist_n = 1;
    } else {
        const uint32_t dt = clamp_u32(since(t, e->p.last_valid_t), 20, 1000);
        const float nd = e->p.d + TOF_GAIN * (med - e->p.d);
        const float v = (nd - e->p.d) * 1000.0f / (float)dt;
        e->p.v += 0.5f * (v - e->p.v);
        e->p.d = nd;
    }
    e->p.last_valid_t = stamp(t);

    const bool in_range = e->p.d < (float)c->far_mm;
    if (in_range) {
        e->p.gone_since = 0;
        if (!e->p.episode) {
            e->p.episode = true;
            e->p.started_sent = false;
            e->p.closing_votes = 0;
            e->p.episode_far_d = e->p.d;
        }
    } else if (e->p.episode && !e->p.gone_since) {
        e->p.gone_since = stamp(t);
    }
    if (e->p.d > e->p.episode_far_d) {
        e->p.episode_far_d = e->p.d;
    }

    /* APPROACH_STARTED: closing in, consistently, by a real distance. */
    if (e->p.episode && in_range && !e->p.near && !e->p.started_sent) {
        if (e->p.v < -c->approach_mm_s) {
            e->p.closing_votes++;
        } else if (e->p.v > -c->approach_mm_s / 3.0f) {
            e->p.closing_votes = 0;
        }
        if (e->p.closing_votes >= 2 && e->p.episode_far_d - e->p.d >= 80.0f && !approach_suppressed(e)) {
            e->p.started_sent = true;
            presence(e);
            emit(e, MAO_PERCEPT_APPROACH_STARTED,
                 55 + clamp_i((int)((-e->p.v - c->approach_mm_s) / 10.0f), 0, 30), 0);
        }
    }

    /* NEAR with hysteresis and two agreeing samples. */
    if (!e->p.near) {
        if (e->p.d < (float)c->near_mm) {
            if (++e->p.near_votes >= 2) {
                e->p.near_votes = 0;
                near_enter(e, 60 + (e->p.started_sent ? 20 : 0) + (e->p.d < c->near_mm * 0.6f ? 10 : 0));
            }
        } else {
            e->p.near_votes = 0;
        }
    } else if (e->p.d > (float)c->near_exit_mm) {
        if (!e->p.leave_since) {
            e->p.leave_since = stamp(t);
        }
    } else {
        e->p.leave_since = 0;
    }
}

static void tof_tick(pe_engine_t *e)
{
    const uint32_t now = e->now;
    if (!e->p.have) {
        return;
    }
    /* The sensor stopped reporting (off, or ranging stalled): nothing there. */
    if (e->p.episode && !within(now, e->p.last_t, 1500) && !e->p.gone_since) {
        e->p.gone_since = stamp(now);
        if (e->p.near && !e->p.leave_since) {
            e->p.leave_since = stamp(now);
        }
    }
    if (e->p.near && e->p.leave_since && since(now, e->p.leave_since) >= NEAR_LEAVE_MS) {
        e->p.near = false;
        e->p.leave_since = 0;
    }
    if (e->p.episode && e->p.gone_since && since(now, e->p.gone_since) >= WITHDRAW_MS) {
        if (e->p.was_near && !approach_suppressed(e) && !within(now, e->c.last_uncover, 2000)) {
            emit(e, MAO_PERCEPT_WITHDRAWN, 65, 0);
        }
        e->p.episode = false;
        e->p.was_near = false;
        e->p.started_sent = false;
        e->p.near = false;
        e->p.leave_since = 0;
        e->p.gone_since = 0;
    }
}

/* ------------------------------------------------------------------------ */
/* Light                                                                    */
/* ------------------------------------------------------------------------ */

void pe_feed_als(pe_engine_t *e, uint32_t t_ms, float lux)
{
    advance(e, t_ms);
    const uint32_t t = e->now;
    if (lux < 0.0f) {
        lux = 0.0f;
    }
    if (!e->l.have || since(t, e->l.last_t) > 10000) {
        e->l.have = true;
        e->l.last_t = stamp(t);
        e->l.fast = lux;
        e->l.ref = lux;
        return;
    }
    const uint32_t dt = clamp_u32(since(t, e->l.last_t), 10, 10000);
    e->l.last_t = stamp(t);
    e->l.fast += alpha(dt, LUX_TAU_MS) * (lux - e->l.fast);
    /* Reference = the recent bright level; it holds through a short cover
     * (slow decay) so "it got much darker than it was" can be judged. */
    if (e->l.fast > e->l.ref) {
        e->l.ref = e->l.fast;
    } else {
        e->l.ref += alpha(dt, LUX_REF_TAU_MS) * (e->l.fast - e->l.ref);
    }
}

static void light_tick(pe_engine_t *e)
{
    const uint32_t now = e->now;
    if (!e->l.have || !within(now, e->l.last_t, 10000)) {
        e->l.cand_since = 0;
        return;
    }
    const pe_config_t *c = &e->cfg;
    if (!e->l.dark) {
        /* Dark only if it stays dark, and not because something covers MAO. */
        if (e->l.fast < c->dark_lux && !e->c.covered && !e->c.cand_since) {
            if (!e->l.cand_since) {
                e->l.cand_since = stamp(now);
            } else if (since(now, e->l.cand_since) >= c->dark_ms) {
                e->l.dark = true;
                e->l.cand_since = 0;
                emit(e, MAO_PERCEPT_DARK_ROOM, 75, 1);
            }
        } else {
            e->l.cand_since = 0;
        }
    } else {
        if (e->l.fast > c->light_lux) {
            if (!e->l.cand_since) {
                e->l.cand_since = stamp(now);
            } else if (since(now, e->l.cand_since) >= LIGHT_ON_MS) {
                e->l.dark = false;
                e->l.cand_since = 0;
                emit(e, MAO_PERCEPT_DARK_ROOM, 80, 0);
            }
        } else {
            e->l.cand_since = 0;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Covered: proximity + light + top touch                                   */
/* ------------------------------------------------------------------------ */

static void cover_tick(pe_engine_t *e)
{
    const uint32_t now = e->now;
    const bool tof = has(e, PE_SENSE_TOF);
    const bool als = has(e, PE_SENSE_ALS);
    if (!tof && !als) {
        return;
    }
    const float possible = (tof ? 0.5f : 0.0f) + (als ? 0.4f : 0.0f) + (has(e, PE_SENSE_TOUCH) ? 0.2f : 0.0f);
    float score = 0.0f;
    if (tof && e->p.d_ok && within(now, e->p.last_valid_t, 600) && e->p.d < (float)e->cfg.cover_mm) {
        score += 0.5f;
    }
    if (als && e->l.have && e->l.ref > 5.0f && e->l.fast < 0.3f * e->l.ref) {
        score += 0.4f;
    }
    if (e->t.stable & BIT(MAO_PERCEPT_ZONE_TOP)) {
        score += 0.2f;
    }
    const float frac = possible > 0.0f ? score / possible : 0.0f;

    if (e->m.held) {
        e->c.cand_since = 0;
        return;
    }
    if (!e->c.covered) {
        if (frac >= 0.7f) {
            if (!e->c.cand_since) {
                e->c.cand_since = stamp(now);
            } else if (since(now, e->c.cand_since) >= COVER_ENTER_MS) {
                e->c.covered = true;
                e->c.cand_since = 0;
                e->c.clear_since = 0;
                emit(e, MAO_PERCEPT_COVERED, (int)(frac * 100.0f), 1);
            }
        } else {
            e->c.cand_since = 0;
        }
    } else {
        if (frac <= 0.3f) {
            if (!e->c.clear_since) {
                e->c.clear_since = stamp(now);
            } else if (since(now, e->c.clear_since) >= COVER_LEAVE_MS) {
                e->c.covered = false;
                e->c.clear_since = 0;
                e->c.last_uncover = stamp(now);
                presence(e);
                emit(e, MAO_PERCEPT_COVERED, 75, 0);
            }
        } else {
            e->c.clear_since = 0;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Touch                                                                    */
/* ------------------------------------------------------------------------ */

static int pet_confidence(const pe_engine_t *e)
{
    const uint32_t now = e->now;
    if (e->t.grip || e->m.held) {
        return 0;
    }
    float c = 0.4f;   /* calm touch on the top: the caller checked that */
    if (has(e, PE_SENSE_IMU) && e->m.have) {
        c += e->m.energy < 1.6f * e->cfg.still_g ? 0.2f : 0.0f;
    } else {
        c += 0.1f;
    }
    if (has(e, PE_SENSE_MIC) && e->a.have && within(now, e->a.last_t, MIC_GAP_MS)) {
        c += (e->a.ambient < e->cfg.quiet_dbfs + 10.0f && !within(now, e->a.last_onset, 3000)) ? 0.2f : 0.0f;
    } else {
        c += 0.1f;
    }
    if (has(e, PE_SENSE_TOF) && e->p.have) {
        const bool hand = (e->p.d_ok && within(now, e->p.last_valid_t, 800) &&
                           e->p.d < (float)e->cfg.near_exit_mm) || e->c.covered;
        c += hand ? 0.2f : 0.0f;
    } else {
        c += 0.1f;
    }
    if (e->f.level >= 2) {
        c -= 0.25f;   /* an annoyed MAO does not take pets */
    }
    return (int)(c * 100.0f + 0.5f);
}

static bool try_pet(pe_engine_t *e)
{
    if (e->t.pet_done) {
        return false;
    }
    const int conf = pet_confidence(e);
    if (conf < 70) {
        return false;
    }
    e->t.pet_done = true;
    e->t.hold_done[MAO_PERCEPT_ZONE_TOP] = true;
    presence(e);
    if (emit(e, MAO_PERCEPT_GENTLE_PET, conf, 0)) {
        /* Being petted soothes: annoyance and its residue fade faster. */
        e->f.dial *= 0.5f;
        e->f.touch *= 0.5f;
        e->f.motion *= 0.5f;
        e->f.press *= 0.5f;
        e->f.residue *= 0.5f;
        return true;
    }
    return false;
}

static void touch_down(pe_engine_t *e, int z)
{
    const uint32_t now = e->now;
    e->t.down_since[z] = stamp(now);
    e->t.hold_done[z] = false;

    if ((e->t.stable & SIDE_MASK) == SIDE_MASK) {
        e->t.grip = true;                /* held between both rim sides */
        e->t.grip_since = stamp(now);
        e->t.pending_zone = -1;
    }
    presence(e);

    /* Restless poking: three or more on the same zone within the window. */
    e->t.starts_t[e->t.starts_head] = stamp(now);
    e->t.starts_zone[e->t.starts_head] = (uint8_t)z;
    e->t.starts_head = (uint8_t)((e->t.starts_head + 1) % PE_TOUCH_HISTORY);
    int same = 0;
    for (int i = 0; i < PE_TOUCH_HISTORY; i++) {
        if (e->t.starts_zone[i] == z && within(now, e->t.starts_t[i], REPEAT_WINDOW_MS)) {
            same++;
        }
    }
    if (within(now, e->f.last_dial_t, 1000)) {
        e->f.touch += 0.7f;              /* touching while dialling: fiddling */
    }
    if (same >= 3 && !e->m.held && !e->t.grip) {
        if (emit(e, MAO_PERCEPT_TOUCH_REPEAT, 55 + 10 * clamp_i(same - 3, 0, 4), (uint32_t)z)) {
            e->f.touch += 2.0f;
        }
        fiddle_evaluate(e);
        return;
    }
    fiddle_evaluate(e);
    if (e->m.held || e->t.grip) {
        return;   /* part of holding MAO, not a touch */
    }
    if (z == MAO_PERCEPT_ZONE_LEFT || z == MAO_PERCEPT_ZONE_RIGHT) {
        /* Wait briefly: if the other side follows it is a grip. */
        e->t.pending_zone = (int8_t)z;
        e->t.pending_t = stamp(now);
    } else {
        emit(e, MAO_PERCEPT_TOUCH, 70, (uint32_t)z);
    }
}

static void touch_up(pe_engine_t *e, int z)
{
    const uint32_t now = e->now;
    if (z == MAO_PERCEPT_ZONE_LEFT || z == MAO_PERCEPT_ZONE_RIGHT) {
        e->t.grip = false;
        if (e->t.pending_zone == z) {
            /* Released before the grip window ended: still a touch. */
            e->t.pending_zone = -1;
            if (!e->m.held) {
                emit(e, MAO_PERCEPT_TOUCH, 60, (uint32_t)z);
            }
        }
    }
    if (z == MAO_PERCEPT_ZONE_TOP) {
        const uint32_t dur = since(now, e->t.down_since[z]);
        if (dur >= STROKE_MIN_MS) {
            e->t.strokes_t[e->t.strokes_n % 3] = stamp(now);
            e->t.strokes_n = (uint8_t)((e->t.strokes_n + 1) % 3);
            int strokes = 0;
            for (int i = 0; i < 3; i++) {
                if (within(now, e->t.strokes_t[i], STROKE_WINDOW_MS)) {
                    strokes++;
                }
            }
            if (strokes >= 2) {
                try_pet(e);   /* a second slow stroke */
            }
        }
        e->t.pet_done = false;
    }
    e->t.down_since[z] = 0;
}

static void touch_commit(pe_engine_t *e)
{
    const uint32_t now = e->now;
    for (int z = 0; z < ZONES; z++) {
        const bool raw = (e->t.raw & BIT(z)) != 0;
        const bool stable = (e->t.stable & BIT(z)) != 0;
        if (raw != stable && since(now, e->t.raw_since[z]) >= e->cfg.touch_debounce_ms) {
            e->t.stable ^= BIT(z);
            if (raw) {
                touch_down(e, z);
            } else {
                touch_up(e, z);
            }
        }
    }
}

void pe_feed_touch(pe_engine_t *e, const pe_touch_t *s)
{
    advance(e, s->t_ms);
    for (int z = 0; z < ZONES; z++) {
        if (((s->touched ^ e->t.raw) & BIT(z)) != 0) {
            e->t.raw_since[z] = e->now;
        }
    }
    e->t.raw = s->touched & (uint8_t)((1u << ZONES) - 1u);
    touch_commit(e);
}

static void touch_tick(pe_engine_t *e)
{
    const uint32_t now = e->now;
    touch_commit(e);
    if (e->t.pending_zone >= 0 && since(now, e->t.pending_t) >= SIDE_PENDING_MS) {
        const int z = e->t.pending_zone;
        e->t.pending_zone = -1;
        if (!e->t.grip && !e->m.held) {
            emit(e, MAO_PERCEPT_TOUCH, 65, (uint32_t)z);
        }
    }
    for (int z = 0; z < ZONES; z++) {
        if (!(e->t.stable & BIT(z)) || !e->t.down_since[z]) {
            continue;
        }
        const uint32_t dur = since(now, e->t.down_since[z]);
        if (z == MAO_PERCEPT_ZONE_TOP && dur >= e->cfg.pet_ms) {
            try_pet(e);
        }
        if (!e->t.hold_done[z] && dur >= e->cfg.hold_ms) {
            e->t.hold_done[z] = true;
            if (!e->t.grip && !e->m.held && !(z == MAO_PERCEPT_ZONE_TOP && e->t.pet_done)) {
                emit(e, MAO_PERCEPT_TOUCH_HOLD, 70, (uint32_t)z);
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Sound                                                                    */
/* ------------------------------------------------------------------------ */

void pe_feed_mic(pe_engine_t *e, const pe_mic_t *s)
{
    advance(e, s->t_ms);
    const uint32_t t = e->now;
    if (!e->a.have || since(t, e->a.last_t) > MIC_GAP_MS) {
        e->a.have = true;
        e->a.last_t = stamp(t);
        e->a.ambient = s->rms_dbfs;
        e->a.quiet_cand = 0;
        return;
    }
    const uint32_t dt = clamp_u32(since(t, e->a.last_t), 1, 1000);
    e->a.last_t = stamp(t);
    if (e->self_sound_until && before(t, e->self_sound_until)) {
        return;     /* MAO is making the sound itself */
    }
    if (s->onset) {
        const float rise = s->rms_dbfs - e->a.ambient;
        e->a.last_onset = stamp(t);
        if (rise >= e->cfg.noise_rise_db && s->rms_dbfs >= e->cfg.noise_min_dbfs && !e->a.pending_noise &&
            !within(t, e->m.last_tap, KNOCK_NOISE_MS)) {
            e->a.pending_noise = stamp(t);
            e->a.pending_rise = rise;
        }
    }
    const float tau = s->rms_dbfs > e->a.ambient ? AMBIENT_UP_MS : AMBIENT_DOWN_MS;
    e->a.ambient += alpha(dt, tau) * (s->rms_dbfs - e->a.ambient);
}

static void sound_tick(pe_engine_t *e)
{
    const uint32_t now = e->now;
    if (e->a.pending_noise && since(now, e->a.pending_noise) >= NOISE_DECIDE_MS) {
        const uint32_t at = e->a.pending_noise;
        e->a.pending_noise = 0;
        /* Not if it was a knock on MAO itself, or MAO being handled. */
        const bool knock = e->m.last_tap && (since(at, e->m.last_tap) <= KNOCK_NOISE_MS ||
                                             since(e->m.last_tap, at) <= KNOCK_NOISE_MS);
        const bool handled = e->m.held || e->m.energy > e->cfg.move_g || within(now, e->m.last_shock, 400);
        if (!knock && !handled) {
            emit(e, MAO_PERCEPT_SUDDEN_NOISE, 50 + clamp_i((int)((e->a.pending_rise - e->cfg.noise_rise_db) * 3.0f), 0, 45), 0);
        }
    }

    if (!e->a.have || !within(now, e->a.last_t, MIC_GAP_MS)) {
        e->a.quiet_cand = 0;    /* mic not running: no judgement */
        return;
    }
    if (!e->a.quiet) {
        if (e->a.ambient < e->cfg.quiet_dbfs) {
            if (!e->a.quiet_cand) {
                e->a.quiet_cand = stamp(now);
            } else if (since(now, e->a.quiet_cand) >= e->cfg.quiet_ms) {
                e->a.quiet = true;
                e->a.quiet_cand = 0;
                emit(e, MAO_PERCEPT_QUIET_ROOM, 70, 1);
            }
        } else {
            e->a.quiet_cand = 0;
        }
    } else {
        if (e->a.ambient > e->cfg.quiet_dbfs + 8.0f) {
            if (!e->a.quiet_cand) {
                e->a.quiet_cand = stamp(now);
            } else if (since(now, e->a.quiet_cand) >= QUIET_LEAVE_MS) {
                e->a.quiet = false;
                e->a.quiet_cand = 0;
                emit(e, MAO_PERCEPT_QUIET_ROOM, 70, 0);
            }
        } else {
            e->a.quiet_cand = 0;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Input and body events                                                    */
/* ------------------------------------------------------------------------ */

void pe_feed_dial(pe_engine_t *e, uint32_t t_ms, int32_t detents)
{
    advance(e, t_ms);
    const uint32_t now = e->now;
    if (detents == 0) {
        return;
    }
    presence(e);
    const int8_t dir = detents > 0 ? 1 : -1;
    const float n = (float)(detents > 0 ? detents : -detents);
    if (e->f.last_dir && dir != e->f.last_dir && within(now, e->f.last_dial_t, 700)) {
        e->f.dial += 0.6f;                         /* back and forth */
    }
    e->f.dial_energy += n;
    if (e->f.dial_energy > 12.0f) {
        e->f.dial += 0.15f * n;                    /* spinning for the sake of it */
    }
    if (e->m.have && e->m.energy > e->cfg.move_g) {
        e->f.motion += 0.3f;                       /* dialling while waving MAO about */
    }
    e->f.last_dir = dir;
    e->f.last_dial_t = stamp(now);
    fiddle_evaluate(e);
}

void pe_feed_press(pe_engine_t *e, uint32_t t_ms)
{
    advance(e, t_ms);
    const uint32_t now = e->now;
    presence(e);
    e->f.press_t[e->f.press_head] = stamp(now);
    e->f.press_head = (uint8_t)((e->f.press_head + 1) % 4);
    int recent = 0;
    for (int i = 0; i < 4; i++) {
        if (within(now, e->f.press_t[i], PRESS_WINDOW_MS)) {
            recent++;
        }
    }
    if (recent >= 3) {
        e->f.press += 0.8f;
    }
    fiddle_evaluate(e);
}

void pe_feed_usb(pe_engine_t *e, uint32_t t_ms, bool present)
{
    advance(e, t_ms);
    if (present == e->usb.present) {
        e->usb.pending = false;       /* bounced back: nothing happened */
        return;
    }
    if (!e->usb.pending || e->usb.pending_value != present) {
        e->usb.pending = true;
        e->usb.pending_value = present;
        e->usb.pending_since = stamp(e->now);
    }
}

void pe_feed_battery(pe_engine_t *e, uint32_t t_ms, uint8_t level)
{
    advance(e, t_ms);
    const uint32_t now = e->now;
    if (level == 0) {
        e->battery_level = 0;
        return;
    }
    if (level > 2) {
        level = 2;
    }
    if (level > e->battery_level || !within(now, e->battery_reported_t, BATTERY_REPEAT_MS)) {
        e->battery_reported_t = stamp(now);
        emit(e, MAO_PERCEPT_LOW_BATTERY, level == 2 ? 95 : 85, level);
    }
    e->battery_level = level;
}

void pe_feed_ir(pe_engine_t *e, uint32_t t_ms, uint8_t command, bool repeat)
{
    advance(e, t_ms);
    if (repeat) {
        return;     /* a held remote button is one signal */
    }
    presence(e);
    emit(e, MAO_PERCEPT_REMOTE_SIGNAL, 70, command);
}

void pe_set_power(pe_engine_t *e, pe_power_t power)
{
    if (power == PE_POWER_DROWSY && e->power != PE_POWER_DROWSY) {
        /* Rates and sensors change; old tracks would be stale on waking. */
        e->p.near = false;
        e->p.episode = false;
        e->p.was_near = false;
        e->p.leave_since = 0;
        e->p.gone_since = 0;
        e->p.d_ok = false;
        e->a.quiet_cand = 0;
        e->a.pending_noise = 0;
        e->l.cand_since = 0;
        e->c.cand_since = 0;
        e->m.have = false;
    }
    e->power = power;
}

/* ------------------------------------------------------------------------ */
/* Tick                                                                     */
/* ------------------------------------------------------------------------ */

void pe_tick(pe_engine_t *e, uint32_t now_ms)
{
    advance(e, now_ms);
    const uint32_t now = e->now;
    const uint32_t dt = since(now, e->last_tick);
    e->last_tick = now;

    if (e->usb.pending && since(now, e->usb.pending_since) >= e->cfg.usb_debounce_ms) {
        e->usb.pending = false;
        e->usb.present = e->usb.pending_value;
        if (e->usb.present) {
            presence(e);   /* somebody plugged MAO in */
        }
        emit(e, MAO_PERCEPT_USB_CONNECTED, 90, e->usb.present ? 1 : 0);
    }
    touch_tick(e);
    tof_tick(e);
    cover_tick(e);
    light_tick(e);
    sound_tick(e);
    fiddle_tick(e, dt);
}

/* ------------------------------------------------------------------------ */
/* Introspection                                                            */
/* ------------------------------------------------------------------------ */

uint8_t pe_fiddle_level(const pe_engine_t *e)
{
    return e->f.level;
}

float pe_fiddle_score(const pe_engine_t *e)
{
    return fiddle_score(e);
}

bool pe_is_held(const pe_engine_t *e)
{
    return e->m.held;
}

bool pe_is_near(const pe_engine_t *e)
{
    return e->p.near;
}

float pe_ambient_dbfs(const pe_engine_t *e)
{
    return e->a.have ? e->a.ambient : -120.0f;
}

uint32_t pe_absence_ms(const pe_engine_t *e, uint32_t now_ms)
{
    return since(now_ms, e->last_presence) + e->absence_carry_ms;
}

/* ------------------------------------------------------------------------ */
/* Names                                                                    */
/* ------------------------------------------------------------------------ */

const char *mao_percept_name(mao_percept_t p)
{
    static const char *const kNames[MAO_PERCEPT_COUNT] = {
        [MAO_PERCEPT_NONE] = "NONE",
        [MAO_PERCEPT_APPROACH_STARTED] = "APPROACH_STARTED",
        [MAO_PERCEPT_APPROACH_NEAR] = "APPROACH_NEAR",
        [MAO_PERCEPT_WITHDRAWN] = "WITHDRAWN",
        [MAO_PERCEPT_TOUCH] = "TOUCH",
        [MAO_PERCEPT_TOUCH_HOLD] = "TOUCH_HOLD",
        [MAO_PERCEPT_TOUCH_REPEAT] = "TOUCH_REPEAT",
        [MAO_PERCEPT_GENTLE_PET] = "GENTLE_PET",
        [MAO_PERCEPT_PICKED_UP] = "PICKED_UP",
        [MAO_PERCEPT_PUT_DOWN] = "PUT_DOWN",
        [MAO_PERCEPT_HARD_PUT_DOWN] = "HARD_PUT_DOWN",
        [MAO_PERCEPT_UPSIDE_DOWN] = "UPSIDE_DOWN",
        [MAO_PERCEPT_SHAKE] = "SHAKE",
        [MAO_PERCEPT_NUDGED] = "NUDGED",
        [MAO_PERCEPT_KNOCK] = "KNOCK",
        [MAO_PERCEPT_QUIET_ROOM] = "QUIET_ROOM",
        [MAO_PERCEPT_SUDDEN_NOISE] = "SUDDEN_NOISE",
        [MAO_PERCEPT_COVERED] = "COVERED",
        [MAO_PERCEPT_DARK_ROOM] = "DARK_ROOM",
        [MAO_PERCEPT_USB_CONNECTED] = "USB_CONNECTED",
        [MAO_PERCEPT_LOW_BATTERY] = "LOW_BATTERY",
        [MAO_PERCEPT_REMOTE_SIGNAL] = "REMOTE_SIGNAL",
        [MAO_PERCEPT_LONG_ABSENCE] = "LONG_ABSENCE",
        [MAO_PERCEPT_FIDDLING_ESCALATION] = "FIDDLING_ESCALATION",
    };
    return ((unsigned)p < (unsigned)MAO_PERCEPT_COUNT && kNames[p]) ? kNames[p] : "?";
}

const char *mao_percept_zone_name(uint16_t zone)
{
    static const char *const kZones[ZONES] = { "right", "left", "top", "rear" };
    return zone < ZONES ? kZones[zone] : "?";
}
