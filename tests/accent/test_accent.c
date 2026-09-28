/*
 * Host tests for MAO's accent (components/mao_character/mao_character_accent.c):
 * pink at rest, a mood leans it as strongly as the mood plays, asleep is
 * lavender, a change fades without overshoot and lands exactly, and every
 * mood it names is a real state in the expression library.
 */
#include <stdio.h>
#include <string.h>
#include "mao_character_accent.h"
#include "mao_lark.h"

/* The library links against the harness shim: nothing here plays a state. */
void harness_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
uint32_t esp_random(void) { return 4u; }

static int s_fail, s_pass;
#define CHECK(cond) do { if (cond) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >> 8) & 0xFF)
#define B(c) ((c) & 0xFF)

static int between(uint32_t v, uint32_t a, uint32_t b)
{
    return (a <= b) ? (v + 1 >= a && v <= b + 1) : (v + 1 >= b && v <= a + 1);
}

int main(void)
{
    /* rest */
    CHECK(mao_accent_for_state("neutral") == MAO_ACCENT_BASE);
    CHECK(mao_accent_for_state("curious") == MAO_ACCENT_BASE);
    CHECK(mao_accent_for_state(NULL) == MAO_ACCENT_BASE);
    CHECK(mao_accent_for_state("no_such_state") == MAO_ACCENT_BASE);

    /* moods lean, by family */
    const uint32_t mad = mao_accent_for_state("mad");
    CHECK(mad != MAO_ACCENT_BASE && R(mad) > G(mad) && R(mad) > B(mad));      /* red */
    CHECK(mao_accent_for_state("dizzy_mad") == mad);
    const uint32_t sad = mao_accent_for_state("sad");
    CHECK(B(sad) > R(sad));                                                  /* blue */
    CHECK(mao_accent_for_state("hum") != MAO_ACCENT_BASE);
    CHECK(mao_accent_for_state("zoned") == mao_accent_for_state("cat_blank"));
    CHECK(mao_accent_for_state("cat_glare") == mao_accent_for_state("glare"));

    /* weight: none = base, full = the mood; asleep = lavender whatever plays */
    CHECK(mao_accent_target("mad", false, 0.0f) == MAO_ACCENT_BASE);
    CHECK(mao_accent_target("mad", false, 1.0f) == mad);
    CHECK(mao_accent_target("mad", false, 2.0f) == mad);
    const uint32_t half = mao_accent_target("mad", false, 0.5f);
    CHECK(between(R(half), R(MAO_ACCENT_BASE), R(mad)) && between(G(half), G(MAO_ACCENT_BASE), G(mad)) &&
          between(B(half), B(MAO_ACCENT_BASE), B(mad)));
    const uint32_t lav = mao_accent_target("neutral", true, 1.0f);
    CHECK(lav == mao_accent_target("mad", true, 0.0f) && B(lav) > G(lav));

    /* first step lands at once (boot is pink, not a fade from black) */
    mao_accent_t a = { 0 };
    CHECK(mao_accent_color(&a) == MAO_ACCENT_BASE);
    mao_accent_step(&a, MAO_ACCENT_BASE, 0.016f);
    CHECK(mao_accent_color(&a) == MAO_ACCENT_BASE);

    /* a fade: monotone per channel, never past the target, lands exactly */
    uint32_t prev = MAO_ACCENT_BASE;
    int frames = 0, mono = 1;
    while (mao_accent_color(&a) != mad && frames < 1000) {
        mao_accent_step(&a, mad, 0.016f);
        const uint32_t c = mao_accent_color(&a);
        mono &= between(R(c), R(prev), R(mad)) && between(G(c), G(prev), G(mad)) && between(B(c), B(prev), B(mad));
        prev = c;
        frames++;
    }
    CHECK(mono);
    CHECK(frames * 16 <= 2000 && frames * 16 >= 300);   /* a fade, neither a cut nor a crawl */
    CHECK(mao_accent_color(&a) == mad);
    /* landed: holding still costs nothing and stays exact */
    mao_accent_step(&a, mad, 0.016f);
    CHECK(mao_accent_color(&a) == mad);
    /* and back to rest, exactly - but slower than it came: the feeling lingers */
    int back = 0;
    while (mao_accent_color(&a) != MAO_ACCENT_BASE && back < 2000) {
        mao_accent_step(&a, MAO_ACCENT_BASE, 0.016f);
        back++;
    }
    CHECK(mao_accent_color(&a) == MAO_ACCENT_BASE);
    CHECK(back > frames * 2);                          /* calming takes over twice as long */
    CHECK(back * 16 <= 10000);                         /* ... but it does calm down */
    /* a stalled tick (dt 0) moves nothing */
    mao_accent_step(&a, sad, 0.0f);
    CHECK(mao_accent_color(&a) == MAO_ACCENT_BASE);

    /* the inner disc: the base keeps its own, others are a paler accent */
    CHECK(mao_accent_disc(MAO_ACCENT_BASE, 0xFCE4EE) == 0xFCE4EE);
    const uint32_t d = mao_accent_disc(sad, 0xFCE4EE);
    CHECK(R(d) >= R(sad) && G(d) >= G(sad) && B(d) >= B(sad) && d != sad);

    /* every state the accent names is one MAO can actually play */
    int named = 0, missing = 0;
    for (int i = 0; mao_accent_mapped_state(i); i++) {
        const char *s = mao_accent_mapped_state(i);
        int found = 0;
        for (int k = 0; k < mao_lark_state_count(); k++) {
            found |= strcmp(mao_lark_state(k)->name, s) == 0;
        }
        if (!found) {
            printf("  accent names '%s', which is not in the library\n", s);
        }
        missing += !found;
        named++;
    }
    CHECK(named > 20 && missing == 0);

    printf("accent: %d checks, %d failed\n", s_pass + s_fail, s_fail);
    return s_fail ? 1 : 0;
}
