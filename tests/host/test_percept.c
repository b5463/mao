/*
 * Perception engine scenarios. A small world simulator feeds every sense at
 * the rate mao_sense delivers it on the A0 (IMU 100 ms, ToF 200 ms, light
 * 1 s, touch 100 ms + changes, mic 32 ms) and ticks the engine every 50 ms,
 * as the ESP glue does. Scenarios then check which percepts came out, how
 * often, and in what order.
 */
#include <math.h>
#include <string.h>
#include "mini_test.h"
#include "percept_engine.h"

#define ALL_SENSES   (PE_SENSE_IMU | PE_SENSE_TOF | PE_SENSE_ALS | PE_SENSE_TOUCH | PE_SENSE_MIC)
#define ZB(z)        ((uint8_t)(1u << (z)))
#define LOG_MAX      512

typedef struct {
    pe_engine_t e;
    uint32_t t;
    /* world */
    float ax, ay, az, noise;
    int tof_mm;                 /* < 0: nothing in view */
    float lux;
    uint8_t touch;
    float mic_db;
    bool onset_next;
    uint8_t imu_event_next;
    bool imu_on, tof_on, als_on, touch_on, mic_on;
    uint32_t next_imu, next_tof, next_als, next_touch, next_mic, next_tick;
    uint8_t touch_sent;
    uint32_t rng;
    pe_out_t log[LOG_MAX];
    int n;
} sim_t;

static sim_t S;

static float rnd(sim_t *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return ((float)((s->rng >> 8) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f;   /* -1..1 */
}

static void sim_init(sim_t *s, uint32_t senses, const pe_config_t *cfg)
{
    memset(s, 0, sizeof(*s));
    s->t = 1000;
    s->az = 1.0f;
    s->noise = 0.002f;
    s->tof_mm = -1;
    s->lux = 200.0f;
    s->mic_db = -45.0f;
    s->rng = 12345;
    s->imu_on = senses & PE_SENSE_IMU;
    s->tof_on = senses & PE_SENSE_TOF;
    s->als_on = senses & PE_SENSE_ALS;
    s->touch_on = senses & PE_SENSE_TOUCH;
    s->mic_on = senses & PE_SENSE_MIC;
    s->next_imu = s->next_tof = s->next_als = s->next_touch = s->next_mic = s->next_tick = s->t;
    pe_init(&s->e, cfg, senses, s->t);
}

static void drain(sim_t *s)
{
    pe_out_t o;
    while (pe_pop(&s->e, &o)) {
        if (s->n < LOG_MAX) {
            s->log[s->n++] = o;
        }
    }
}

static void feed_touch_now(sim_t *s)
{
    const pe_touch_t tt = { .t_ms = s->t, .touched = s->touch };
    pe_feed_touch(&s->e, &tt);
    s->touch_sent = s->touch;
}

/* Run the world for ms milliseconds. */
static void step(sim_t *s, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        s->t++;
        if (s->imu_on && s->t >= s->next_imu) {
            s->next_imu += 100;
            const pe_imu_t imu = {
                .t_ms = s->t,
                .ax = s->ax + s->noise * rnd(s),
                .ay = s->ay + s->noise * rnd(s),
                .az = s->az + s->noise * rnd(s),
                .accel_valid = true,
                .events = s->imu_event_next,
            };
            s->imu_event_next = 0;
            pe_feed_imu(&s->e, &imu);
        }
        if (s->tof_on && s->t >= s->next_tof) {
            s->next_tof += 200;
            const pe_tof_t tof = {
                .t_ms = s->t,
                .distance_mm = (uint16_t)(s->tof_mm < 0 ? 0 : s->tof_mm),
                .status = (uint8_t)(s->tof_mm < 0 ? 2 : 0),
            };
            pe_feed_tof(&s->e, &tof);
        }
        if (s->als_on && s->t >= s->next_als) {
            s->next_als += 1000;
            pe_feed_als(&s->e, s->t, s->lux);
        }
        if (s->touch_on && (s->t >= s->next_touch || s->touch != s->touch_sent)) {
            if (s->t >= s->next_touch) {
                s->next_touch += 100;
            }
            feed_touch_now(s);
        }
        if (s->mic_on && s->t >= s->next_mic) {
            s->next_mic += 32;
            const pe_mic_t mic = { .t_ms = s->t, .rms_dbfs = s->mic_db + 0.8f * rnd(s), .onset = s->onset_next };
            s->onset_next = false;
            pe_feed_mic(&s->e, &mic);
        }
        if (s->t >= s->next_tick) {
            s->next_tick += 50;
            pe_tick(&s->e, s->t);
        }
        drain(s);
    }
}

static int count(const sim_t *s, mao_percept_t p)
{
    int n = 0;
    for (int i = 0; i < s->n; i++) {
        n += s->log[i].percept == p;
    }
    return n;
}

static int count_detail(const sim_t *s, mao_percept_t p, uint16_t detail)
{
    int n = 0;
    for (int i = 0; i < s->n; i++) {
        n += s->log[i].percept == p && s->log[i].detail == detail;
    }
    return n;
}

static int index_of(const sim_t *s, mao_percept_t p)
{
    for (int i = 0; i < s->n; i++) {
        if (s->log[i].percept == p) {
            return i;
        }
    }
    return -1;
}

static const pe_out_t *find(const sim_t *s, mao_percept_t p)
{
    const int i = index_of(s, p);
    return i >= 0 ? &s->log[i] : NULL;
}

static void clear_log(sim_t *s)
{
    s->n = 0;
}

static void dump(const sim_t *s)
{
    for (int i = 0; i < s->n; i++) {
        printf("      %6u ms %-20s conf %3u detail %u\n", (unsigned)s->log[i].t_ms,
               mao_percept_name(s->log[i].percept), s->log[i].confidence, s->log[i].detail);
    }
}

/* Lift MAO off the desk: tilt to deg over ms with hand motion. */
static void pick_up(sim_t *s, float deg, uint32_t ms)
{
    const int n = (int)(ms / 100);
    for (int i = 1; i <= n; i++) {
        const float a = deg * 3.14159265f / 180.0f * (float)i / (float)n;
        s->ax = sinf(a) + ((i & 1) ? 0.15f : -0.15f);
        s->az = cosf(a);
        step(s, 100);
    }
    s->ax = sinf(deg * 3.14159265f / 180.0f);
    s->noise = 0.05f;    /* a hand is never perfectly still */
}

static void set_down(sim_t *s)
{
    s->noise = 0.03f;
    s->ax = 0.0f;
    s->az = 1.0f;
    step(s, 300);
    s->noise = 0.002f;
}

/* ------------------------------------------------------------------------ */

static void test_pack_roundtrip(void)
{
    const int32_t v = mao_percept_pack(MAO_PERCEPT_TOUCH_HOLD, 87, MAO_PERCEPT_ZONE_TOP);
    CHECK(v >= 0);
    const mao_percept_msg_t m = mao_percept_unpack(v);
    CHECK_EQ(m.percept, MAO_PERCEPT_TOUCH_HOLD);
    CHECK_EQ(m.confidence, 87);
    CHECK_EQ(m.detail, MAO_PERCEPT_ZONE_TOP);
    const mao_percept_msg_t big = mao_percept_unpack(mao_percept_pack(MAO_PERCEPT_LONG_ABSENCE, 250, 60000));
    CHECK_EQ(big.confidence, 100);
    CHECK_EQ(big.detail, MAO_PERCEPT_DETAIL_MAX);
    for (int p = 0; p < MAO_PERCEPT_COUNT; p++) {
        CHECK(strcmp(mao_percept_name((mao_percept_t)p), "?") != 0);
    }
    CHECK(strcmp(mao_percept_name(MAO_PERCEPT_COUNT), "?") == 0);
}

static void test_quiet_desk_no_spam(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 120000);       /* two minutes on the desk, nothing happening */
    if (S.n) {
        dump(&S);
    }
    CHECK_EQ(S.n, 0);
}

static void test_static_scene_no_spam(void)
{
    /* A monitor 40 cm in front, flickering light, a murmuring room: things
     * are there, but nothing happens. */
    sim_init(&S, ALL_SENSES, NULL);
    S.mic_db = -48.0f;
    for (int i = 0; i < 600; i++) {
        S.tof_mm = 400 + (int)(15.0f * rnd(&S));
        S.lux = 150.0f + 25.0f * rnd(&S);
        step(&S, 200);
    }
    if (S.n) {
        dump(&S);
    }
    CHECK_EQ(S.n, 0);
}

static void test_pickup_and_put_down(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 3000);
    pick_up(&S, 30.0f, 600);
    step(&S, 2000);
    CHECK_EQ(count(&S, MAO_PERCEPT_PICKED_UP), 1);
    CHECK(pe_is_held(&S.e));
    CHECK(find(&S, MAO_PERCEPT_PICKED_UP) && find(&S, MAO_PERCEPT_PICKED_UP)->confidence >= 55);
    set_down(&S);
    step(&S, 2000);
    CHECK_EQ(count(&S, MAO_PERCEPT_PUT_DOWN), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_HARD_PUT_DOWN), 0);
    CHECK(!pe_is_held(&S.e));
    CHECK(index_of(&S, MAO_PERCEPT_PICKED_UP) < index_of(&S, MAO_PERCEPT_PUT_DOWN));
    CHECK_EQ(count(&S, MAO_PERCEPT_NUDGED), 0);
    CHECK_EQ(count(&S, MAO_PERCEPT_SHAKE), 0);
}

static void test_grip_raises_pickup_confidence(void)
{
    /* Lifted straight up (little tilt) but held by both rim sides. */
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 3000);
    S.touch = ZB(MAO_PERCEPT_ZONE_LEFT) | ZB(MAO_PERCEPT_ZONE_RIGHT);
    step(&S, 200);
    pick_up(&S, 5.0f, 600);
    step(&S, 500);
    CHECK_EQ(count(&S, MAO_PERCEPT_PICKED_UP), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_TOUCH), 0);    /* a grip is not a touch */
    const pe_out_t *p = find(&S, MAO_PERCEPT_PICKED_UP);
    CHECK(p && p->confidence >= 65);
}

static void test_hard_put_down_and_drop(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 3000);
    pick_up(&S, 30.0f, 600);
    step(&S, 1000);
    S.noise = 0.002f;
    S.ax = 0.0f;
    S.az = 2.6f;            /* landing shock */
    step(&S, 100);
    S.az = 1.0f;
    step(&S, 2500);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_HARD_PUT_DOWN, 0), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_PUT_DOWN), 0);

    /* Knocked off the desk: free fall, landing, rest. */
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 3000);
    S.imu_event_next = PE_IMU_FREE_FALL;
    S.az = 0.05f;
    step(&S, 300);
    S.az = 3.0f;
    step(&S, 100);
    S.az = 1.0f;
    step(&S, 2500);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_HARD_PUT_DOWN, 1), 1);
}

static void test_nudge_is_not_pickup(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 3000);
    S.ax = 0.2f;
    step(&S, 200);
    S.ax = 0.0f;
    step(&S, 3000);
    CHECK_EQ(count(&S, MAO_PERCEPT_NUDGED), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_PICKED_UP), 0);
    /* Another bump right away: cooldown. */
    S.ax = 0.2f;
    step(&S, 200);
    S.ax = 0.0f;
    step(&S, 1500);
    CHECK_EQ(count(&S, MAO_PERCEPT_NUDGED), 1);
}

static void test_upside_down_hysteresis(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 2000);
    for (int i = 1; i <= 10; i++) {
        const float a = 3.14159265f * (float)i / 10.0f;
        S.ax = sinf(a);
        S.az = cosf(a);
        step(&S, 100);
    }
    S.ax = 0.0f;
    S.az = -1.0f;
    step(&S, 3000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_UPSIDE_DOWN, 1), 1);
    /* Wobbling near the threshold does not flip it back. */
    S.az = -0.6f;
    S.ax = 0.8f;
    step(&S, 1500);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_UPSIDE_DOWN, 0), 0);
    S.ax = 0.0f;
    S.az = 1.0f;
    step(&S, 3000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_UPSIDE_DOWN, 0), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_UPSIDE_DOWN), 2);
}

static void test_shake_with_cooldown(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 2000);
    for (int i = 0; i < 15; i++) {
        S.ax = (i & 1) ? 1.2f : -1.2f;
        step(&S, 100);
    }
    CHECK_EQ(count(&S, MAO_PERCEPT_SHAKE), 1);
    for (int i = 0; i < 20; i++) {
        S.ax = (i & 1) ? 1.2f : -1.2f;
        step(&S, 100);
    }
    CHECK_EQ(count(&S, MAO_PERCEPT_SHAKE), 2);
    CHECK(pe_fiddle_score(&S.e) > 2.0f);   /* being shaken is part of fiddling */
}

static void test_own_vibration_is_ignored(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 3000);
    pe_note_self_motion(&S.e, S.t + 1000);   /* MAO plays a haptic */
    for (int i = 0; i < 8; i++) {
        S.ax = (i & 1) ? 0.6f : -0.6f;
        S.imu_event_next = i == 2 ? PE_IMU_TAP : 0;
        step(&S, 100);
    }
    S.ax = 0.0f;
    step(&S, 3000);
    CHECK_EQ(count(&S, MAO_PERCEPT_SHAKE), 0);
    CHECK_EQ(count(&S, MAO_PERCEPT_NUDGED), 0);
    CHECK_EQ(count(&S, MAO_PERCEPT_KNOCK), 0);
    CHECK_EQ(count(&S, MAO_PERCEPT_PICKED_UP), 0);
}

static void test_approach_near_withdrawn(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 2000);
    for (int d = 900; d > 120; d -= 60) {
        S.tof_mm = d;
        step(&S, 200);
    }
    S.tof_mm = 120;
    step(&S, 2000);
    S.tof_mm = -1;
    step(&S, 3000);
    CHECK_EQ(count(&S, MAO_PERCEPT_APPROACH_STARTED), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_APPROACH_NEAR), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_WITHDRAWN), 1);
    CHECK(index_of(&S, MAO_PERCEPT_APPROACH_STARTED) < index_of(&S, MAO_PERCEPT_APPROACH_NEAR));
    CHECK(index_of(&S, MAO_PERCEPT_APPROACH_NEAR) < index_of(&S, MAO_PERCEPT_WITHDRAWN));
    const pe_out_t *near = find(&S, MAO_PERCEPT_APPROACH_NEAR);
    CHECK(near && near->confidence >= 75);    /* came closer AND is near: fused */
}

static void test_tof_outlier_and_hover(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    S.tof_mm = 800;
    step(&S, 2000);
    S.tof_mm = 150;          /* one stray reading */
    step(&S, 200);
    S.tof_mm = 800;
    step(&S, 2000);
    CHECK_EQ(count(&S, MAO_PERCEPT_APPROACH_NEAR), 0);

    /* Hovering around the threshold: one NEAR, not a stream. */
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 1000);
    for (int i = 0; i < 40; i++) {
        S.tof_mm = 150 + (i % 3) * 50;   /* 150, 200, 250 */
        step(&S, 200);
    }
    CHECK_EQ(count(&S, MAO_PERCEPT_APPROACH_NEAR), 1);
}

static void test_touch_debounce_and_zones(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 1000);
    S.touch = ZB(MAO_PERCEPT_ZONE_TOP);
    step(&S, 20);            /* glitch */
    S.touch = 0;
    step(&S, 500);
    CHECK_EQ(count(&S, MAO_PERCEPT_TOUCH), 0);

    S.touch = ZB(MAO_PERCEPT_ZONE_TOP);
    step(&S, 300);
    S.touch = 0;
    step(&S, 500);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_TOUCH, MAO_PERCEPT_ZONE_TOP), 1);

    S.touch = ZB(MAO_PERCEPT_ZONE_LEFT);
    step(&S, 400);
    S.touch = 0;
    step(&S, 500);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_TOUCH, MAO_PERCEPT_ZONE_LEFT), 1);

    /* Both rim sides within the grip window: a grip, no touch. */
    clear_log(&S);
    S.touch = ZB(MAO_PERCEPT_ZONE_RIGHT);
    step(&S, 80);
    S.touch |= ZB(MAO_PERCEPT_ZONE_LEFT);
    step(&S, 1500);
    S.touch = 0;
    step(&S, 500);
    CHECK_EQ(count(&S, MAO_PERCEPT_TOUCH), 0);
    CHECK_EQ(count(&S, MAO_PERCEPT_TOUCH_HOLD), 0);
}

static void test_touch_repeat(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 1000);
    for (int i = 0; i < 5; i++) {
        S.touch = ZB(MAO_PERCEPT_ZONE_RIGHT);
        step(&S, 120);
        S.touch = 0;
        step(&S, 250);
    }
    CHECK_EQ(count(&S, MAO_PERCEPT_TOUCH_REPEAT), 1);
    CHECK(count(&S, MAO_PERCEPT_TOUCH) <= 2);
    CHECK(pe_fiddle_score(&S.e) > 1.5f);
}

static void test_gentle_pet_fusion(void)
{
    /* Quiet room, MAO at rest, a hand above the window, calm top touch. */
    sim_init(&S, ALL_SENSES, NULL);
    S.mic_db = -66.0f;
    step(&S, 3000);
    S.tof_mm = 90;
    step(&S, 1000);
    clear_log(&S);
    S.touch = ZB(MAO_PERCEPT_ZONE_TOP);
    step(&S, 1600);
    S.touch = 0;
    step(&S, 500);
    CHECK_EQ(count(&S, MAO_PERCEPT_GENTLE_PET), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_TOUCH_HOLD), 0);
    CHECK(index_of(&S, MAO_PERCEPT_TOUCH) < index_of(&S, MAO_PERCEPT_GENTLE_PET));

    /* Same touch in a loud room with nobody near: just a held touch. */
    sim_init(&S, ALL_SENSES, NULL);
    S.mic_db = -30.0f;
    step(&S, 3000);
    S.touch = ZB(MAO_PERCEPT_ZONE_TOP);
    step(&S, 1600);
    S.touch = 0;
    step(&S, 500);
    CHECK_EQ(count(&S, MAO_PERCEPT_GENTLE_PET), 0);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_TOUCH_HOLD, MAO_PERCEPT_ZONE_TOP), 1);

    /* Two slow strokes also count as a pet. */
    sim_init(&S, ALL_SENSES, NULL);
    S.mic_db = -66.0f;
    S.tof_mm = 90;
    step(&S, 3000);
    for (int i = 0; i < 2; i++) {
        S.touch = ZB(MAO_PERCEPT_ZONE_TOP);
        step(&S, 450);
        S.touch = 0;
        step(&S, 600);
    }
    CHECK_EQ(count(&S, MAO_PERCEPT_GENTLE_PET), 1);
}

static void test_covered_peekaboo(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 15000);
    S.tof_mm = 20;
    S.lux = 3.0f;
    step(&S, 6000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_COVERED, 1), 1);
    S.tof_mm = -1;
    S.lux = 200.0f;
    step(&S, 5000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_COVERED, 0), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_WITHDRAWN), 0);
    CHECK_EQ(count(&S, MAO_PERCEPT_DARK_ROOM), 0);
}

static void test_dark_room(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 10000);
    S.lux = 0.5f;            /* lights off, nothing in front of MAO */
    step(&S, 15000);
    CHECK_EQ(count(&S, MAO_PERCEPT_DARK_ROOM), 0);   /* not yet: has to stay dark */
    step(&S, 15000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_DARK_ROOM, 1), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_COVERED), 0);
    S.lux = 300.0f;
    step(&S, 5000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_DARK_ROOM, 0), 1);
}

static void test_noise_knock_and_self_sound(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    S.mic_db = -60.0f;
    step(&S, 5000);
    S.mic_db = -28.0f;
    S.onset_next = true;
    step(&S, 64);
    S.mic_db = -60.0f;
    step(&S, 1000);
    CHECK_EQ(count(&S, MAO_PERCEPT_SUDDEN_NOISE), 1);

    /* A knock on MAO: heard and felt. A knock, not a noise. */
    sim_init(&S, ALL_SENSES, NULL);
    S.mic_db = -60.0f;
    step(&S, 5000);
    S.mic_db = -30.0f;
    S.onset_next = true;
    step(&S, 40);
    S.imu_event_next = PE_IMU_TAP;
    step(&S, 100);
    S.mic_db = -60.0f;
    step(&S, 1000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_KNOCK, 1), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_SUDDEN_NOISE), 0);
    const pe_out_t *k = find(&S, MAO_PERCEPT_KNOCK);
    CHECK(k && k->confidence >= 75);

    /* MAO's own sound. */
    sim_init(&S, ALL_SENSES, NULL);
    S.mic_db = -60.0f;
    step(&S, 5000);
    pe_note_self_sound(&S.e, S.t + 400);
    S.mic_db = -25.0f;
    S.onset_next = true;
    step(&S, 150);
    S.mic_db = -60.0f;
    step(&S, 1000);
    CHECK_EQ(count(&S, MAO_PERCEPT_SUDDEN_NOISE), 0);
}

static void test_quiet_room(void)
{
    pe_config_t cfg;
    pe_config_default(&cfg);
    cfg.quiet_ms = 10000;
    sim_init(&S, ALL_SENSES, &cfg);
    S.mic_db = -66.0f;
    step(&S, 15000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_QUIET_ROOM, 1), 1);
    S.mic_db = -40.0f;
    step(&S, 15000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_QUIET_ROOM, 0), 1);
    CHECK_EQ(count(&S, MAO_PERCEPT_QUIET_ROOM), 2);
}

/* Fiddle with the dial: back and forth every 150 ms. */
static void dial_fiddle(sim_t *s, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 150) {
        pe_feed_dial(&s->e, s->t, ((t / 150) & 1) ? 2 : -2);
        step(s, 150);
    }
}

static void test_fiddling_dial_only_lcdkit(void)
{
    /* LCDkit: no senses at all, only the dial and the press. */
    sim_init(&S, 0, NULL);
    dial_fiddle(&S, 8000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_FIDDLING_ESCALATION, 1), 1);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_FIDDLING_ESCALATION, 2), 1);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_FIDDLING_ESCALATION, 3), 0);   /* the dial alone is not enough */
    CHECK(index_of(&S, MAO_PERCEPT_FIDDLING_ESCALATION) >= 0 &&
          S.log[index_of(&S, MAO_PERCEPT_FIDDLING_ESCALATION)].detail == 1);
    /* Escalation is stepwise, not a jump. */
    int last = 0;
    uint32_t last_t = 0;
    for (int i = 0; i < S.n; i++) {
        if (S.log[i].percept == MAO_PERCEPT_FIDDLING_ESCALATION && S.log[i].detail > 0) {
            CHECK_EQ(S.log[i].detail, last + 1);
            if (last > 0) {
                CHECK(S.log[i].t_ms - last_t >= 1200);
            }
            last = S.log[i].detail;
            last_t = S.log[i].t_ms;
        }
    }
    /* ... plus pressing the face over and over: now it gets cross. */
    for (int i = 0; i < 10; i++) {
        pe_feed_press(&S.e, S.t);
        dial_fiddle(&S, 300);
    }
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_FIDDLING_ESCALATION, 3), 1);
    /* Left alone it calms down, once, but not instantly (residue). */
    step(&S, 15000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_FIDDLING_ESCALATION, 0), 0);
    step(&S, 90000);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_FIDDLING_ESCALATION, 0), 1);
    for (int i = 0; i < S.n; i++) {
        CHECK(S.log[i].percept == MAO_PERCEPT_FIDDLING_ESCALATION);
    }
}

static void test_fiddling_multimodal(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    step(&S, 2000);
    for (int round = 0; round < 6; round++) {
        dial_fiddle(&S, 600);
        S.touch = ZB(MAO_PERCEPT_ZONE_LEFT);
        step(&S, 100);
        S.touch = 0;
        step(&S, 100);
        for (int i = 0; i < 5; i++) {
            S.ax = (i & 1) ? 1.0f : -1.0f;
            step(&S, 100);
        }
        S.ax = 0.0f;
    }
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_FIDDLING_ESCALATION, 3), 1);
    const pe_out_t *f = NULL;
    for (int i = 0; i < S.n; i++) {
        if (S.log[i].percept == MAO_PERCEPT_FIDDLING_ESCALATION && S.log[i].detail == 3) {
            f = &S.log[i];
        }
    }
    CHECK(f && f->confidence >= 70);   /* several kinds of fiddling agreed */
}

static void test_pet_soothes(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    S.mic_db = -66.0f;
    S.tof_mm = 90;
    step(&S, 3000);
    dial_fiddle(&S, 1500);
    step(&S, 2500);
    const float before = pe_fiddle_score(&S.e);
    CHECK(before > 2.0f);
    S.touch = ZB(MAO_PERCEPT_ZONE_TOP);
    step(&S, 1000);
    S.touch = 0;
    step(&S, 100);
    CHECK_EQ(count(&S, MAO_PERCEPT_GENTLE_PET), 1);
    CHECK(pe_fiddle_score(&S.e) < before * 0.6f);
}

static void test_long_absence(void)
{
    sim_init(&S, 0, NULL);
    pe_set_absence(&S.e, 3u * 3600u * 1000u);      /* slept three hours */
    pe_feed_dial(&S.e, S.t, 1);
    step(&S, 100);
    CHECK_EQ(count(&S, MAO_PERCEPT_LONG_ABSENCE), 1);
    const pe_out_t *a = find(&S, MAO_PERCEPT_LONG_ABSENCE);
    CHECK(a && a->detail >= 180 && a->detail <= 181);
    pe_feed_dial(&S.e, S.t, 1);
    step(&S, 100);
    CHECK_EQ(count(&S, MAO_PERCEPT_LONG_ABSENCE), 1);

    /* Natural absence, announced before what ended it. */
    pe_config_t cfg;
    pe_config_default(&cfg);
    cfg.long_absence_ms = 60000;
    sim_init(&S, ALL_SENSES, &cfg);
    step(&S, 70000);
    S.touch = ZB(MAO_PERCEPT_ZONE_TOP);
    step(&S, 200);
    CHECK_EQ(count(&S, MAO_PERCEPT_LONG_ABSENCE), 1);
    CHECK(index_of(&S, MAO_PERCEPT_LONG_ABSENCE) < index_of(&S, MAO_PERCEPT_TOUCH));
}

static void test_usb_debounce(void)
{
    sim_init(&S, 0, NULL);
    pe_set_usb_initial(&S.e, false);
    pe_feed_usb(&S.e, S.t, true);
    step(&S, 100);
    pe_feed_usb(&S.e, S.t, false);     /* connector bounce */
    step(&S, 1000);
    CHECK_EQ(count(&S, MAO_PERCEPT_USB_CONNECTED), 0);
    pe_feed_usb(&S.e, S.t, true);
    step(&S, 600);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_USB_CONNECTED, 1), 1);
    pe_feed_usb(&S.e, S.t, false);
    step(&S, 600);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_USB_CONNECTED, 0), 1);
}

static void test_global_rate_limit(void)
{
    pe_config_t cfg;
    pe_config_default(&cfg);
    cfg.max_percepts_per_s = 2;
    sim_init(&S, ALL_SENSES, &cfg);
    step(&S, 3000);
    pe_feed_ir(&S.e, S.t, 0x10, false);
    S.touch = ZB(MAO_PERCEPT_ZONE_TOP);
    step(&S, 100);
    S.imu_event_next = PE_IMU_TAP;
    S.touch = 0;
    step(&S, 300);
    CHECK_EQ(count(&S, MAO_PERCEPT_REMOTE_SIGNAL) + count(&S, MAO_PERCEPT_TOUCH) + count(&S, MAO_PERCEPT_KNOCK), 2);
    CHECK(S.e.dropped >= 1);
}

static void test_battery_and_ir(void)
{
    sim_init(&S, 0, NULL);
    pe_feed_battery(&S.e, S.t, 1);
    step(&S, 100);
    pe_feed_battery(&S.e, S.t, 1);
    step(&S, 100);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_LOW_BATTERY, 1), 1);
    pe_feed_battery(&S.e, S.t, 2);
    step(&S, 100);
    CHECK_EQ(count_detail(&S, MAO_PERCEPT_LOW_BATTERY, 2), 1);

    pe_feed_ir(&S.e, S.t, 0x2A, false);
    pe_feed_ir(&S.e, S.t + 108, 0x2A, true);       /* NEC repeat */
    step(&S, 500);
    pe_feed_ir(&S.e, S.t, 0x2B, false);            /* within the cooldown */
    step(&S, 100);
    CHECK_EQ(count(&S, MAO_PERCEPT_REMOTE_SIGNAL), 1);
    CHECK_EQ(find(&S, MAO_PERCEPT_REMOTE_SIGNAL)->detail, 0x2A);
}

static void test_drowsy_resets_tracks(void)
{
    sim_init(&S, ALL_SENSES, NULL);
    S.tof_mm = 100;
    step(&S, 2000);
    CHECK(pe_is_near(&S.e));
    pe_set_power(&S.e, PE_POWER_DROWSY);
    CHECK(!pe_is_near(&S.e));
    /* Low-power threshold interrupt while drowsy: an approach. */
    clear_log(&S);
    S.t += 3500;
    const pe_tof_t thr = { .t_ms = S.t, .distance_mm = 120, .status = 0, .threshold = true };
    pe_feed_tof(&S.e, &thr);
    drain(&S);
    CHECK_EQ(count(&S, MAO_PERCEPT_APPROACH_NEAR), 1);
}

void suite_percept(void)
{
    RUN(test_pack_roundtrip);
    RUN(test_quiet_desk_no_spam);
    RUN(test_static_scene_no_spam);
    RUN(test_pickup_and_put_down);
    RUN(test_grip_raises_pickup_confidence);
    RUN(test_hard_put_down_and_drop);
    RUN(test_nudge_is_not_pickup);
    RUN(test_upside_down_hysteresis);
    RUN(test_shake_with_cooldown);
    RUN(test_own_vibration_is_ignored);
    RUN(test_approach_near_withdrawn);
    RUN(test_tof_outlier_and_hover);
    RUN(test_touch_debounce_and_zones);
    RUN(test_touch_repeat);
    RUN(test_gentle_pet_fusion);
    RUN(test_covered_peekaboo);
    RUN(test_dark_room);
    RUN(test_noise_knock_and_self_sound);
    RUN(test_quiet_room);
    RUN(test_fiddling_dial_only_lcdkit);
    RUN(test_fiddling_multimodal);
    RUN(test_pet_soothes);
    RUN(test_long_absence);
    RUN(test_usb_debounce);
    RUN(test_global_rate_limit);
    RUN(test_battery_and_ir);
    RUN(test_drowsy_resets_tracks);
}
