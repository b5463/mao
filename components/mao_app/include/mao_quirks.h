/*
 * Quirks (M4.1): when MAO's small hidden behaviours may happen. The moment
 * each one belongs to is found by the app; this decides whether it happens
 * this time - a cooldown so it never repeats on cue, and a rarity so it
 * stays a surprise. The face itself is mao_character_quirk().
 *
 * Nothing here is shown or counted anywhere: behaviour is experienced, not
 * explained. Rarity takes a roll (0..999) from the caller, so tests are
 * deterministic; the device rolls esp_random().
 *
 * Pure: host-tested (tests/quirks).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MAO_QK_HOLD = 0,        /* fiddling stopped just before the tsk */
    MAO_QK_FORGIVE,         /* a clean setting after real annoyance */
    MAO_QK_REVISIT,         /* the same device opened again and again */
    MAO_QK_DELIGHT,         /* something went well (a frame taken) */
    MAO_QK_SERIES,          /* many frames in one camera visit */
    MAO_QK_OFFSCREEN,       /* a known device came back while MAO idles at HOME */
    MAO_QK_GRUMPY,          /* fiddled with right after waking */
    MAO_QK_COUNT,
} mao_qk_t;

typedef struct {
    uint32_t cooldown_ms;
    uint16_t chance;        /* per mille */
} mao_qk_rule_t;

extern const mao_qk_rule_t mao_qk_rules[MAO_QK_COUNT];

#define MAO_QK_REVISIT_N      3        /* opens of the same device ... */
#define MAO_QK_REVISIT_MS     25000u   /* ... within this */
#define MAO_QK_SERIES_N       5        /* frames in one visit */
#define MAO_QK_GRUMPY_MS      15000u   /* fiddled with this soon after waking */

typedef struct {
    uint32_t last_ms[MAO_QK_COUNT];
    bool ever[MAO_QK_COUNT];
    uint64_t revisit_id;
    uint32_t revisit_ms[MAO_QK_REVISIT_N];
    uint8_t revisit_n;
} mao_quirks_t;

void mao_quirks_reset(mao_quirks_t *s);

/* The moment for `k` has come: does it happen? Yes at most once per
 * cooldown, and then with its chance (roll 0..999). A yes starts the
 * cooldown. */
bool mao_quirks_roll(mao_quirks_t *s, mao_qk_t k, uint32_t now_ms, uint32_t roll);

/* A device page was opened: true when it is the MAO_QK_REVISIT_N-th open of
 * the same device within MAO_QK_REVISIT_MS (the moment for MAO_QK_REVISIT). */
bool mao_quirks_opened(mao_quirks_t *s, uint64_t id, uint32_t now_ms);
