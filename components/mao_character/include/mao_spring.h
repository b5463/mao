/*
 * MAO motion system: every moving value in MAO (character and UI) is a
 * damped spring chasing a target, using one of a few named profiles. Reactions
 * set targets or kick velocities; they never run fixed-duration tweens, so
 * everything stays interruptible and retargetable.
 *
 *   SOFT  - idle drift, settling, slow presence (gentle, slight overshoot)
 *   SNAP  - acknowledgements: press, blink-open, gaze (fast, springy)
 *   HEAVY - view transitions: things with "mass" that travel (no wobble)
 *
 * Approximate 90 % settle times: SOFT ~450 ms, SNAP ~150 ms, HEAVY ~300 ms.
 */
#pragma once

#include <math.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float k;      /* stiffness, 1/s^2 */
    float zeta;   /* damping ratio (<1 overshoots) */
} mao_spring_profile_t;

#define MAO_SPRING_SOFT   ((mao_spring_profile_t){ .k = 110.0f, .zeta = 0.62f })
#define MAO_SPRING_SNAP   ((mao_spring_profile_t){ .k = 520.0f, .zeta = 0.42f })
#define MAO_SPRING_HEAVY  ((mao_spring_profile_t){ .k = 190.0f, .zeta = 0.86f })

typedef struct {
    float x;         /* value */
    float v;         /* velocity, units/s */
    float target;
    mao_spring_profile_t p;
} mao_spring_t;

static inline void mao_spring_init(mao_spring_t *s, float x, mao_spring_profile_t p)
{
    s->x = x;
    s->v = 0.0f;
    s->target = x;
    s->p = p;
}

/* Semi-implicit Euler in <= 10 ms sub-steps: stable for SNAP at any tick. */
static inline void mao_spring_step(mao_spring_t *s, float dt)
{
    int n = (int)ceilf(dt / 0.010f);
    if (n < 1) {
        n = 1;
    }
    const float h = dt / (float)n;
    const float c = 2.0f * s->p.zeta * sqrtf(s->p.k);
    for (int i = 0; i < n; i++) {
        const float a = s->p.k * (s->target - s->x) - c * s->v;
        s->v += a * h;
        s->x += s->v * h;
    }
}

static inline bool mao_spring_settled(const mao_spring_t *s, float eps)
{
    return fabsf(s->target - s->x) < eps && fabsf(s->v) < eps * 10.0f;
}

#ifdef __cplusplus
}
#endif
