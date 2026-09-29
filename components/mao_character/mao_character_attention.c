/*
 * MAO character - ATTENTION stage: what MAO pays attention to, and how much.
 *
 * Controller feedback (ack / busy / done / fail / device on-off) goes to the
 * mind and a one-slot queue. Every tick the queued feedback, the Lark
 * expression layer and the mind's own layer are mixed into the pose,
 * weighted by what the user is doing (priority).
 * The character only reports: nothing here gates or delays an action.
 */
#include "mao_character_internal.h"

#include "esp_log.h"

static const char *TAG = "MAO_CHARACTER";

static void grudge(mao_char_t *mc, mao_character_reaction_t r, uint32_t now);

void mao_char_feedback(mao_char_t *mc, mao_character_reaction_t r, uint32_t now)
{
    /* Controller feedback: queued, and played as soon as MAO is on
     * screen - even mid-dial, at full strength. The mind hears about it
     * too (interest, habituation, the analytical EVALUATE phase). */
    static const char *const kFb[] = {
        [MAO_CHAR_REACT_ACK] = "ack", [MAO_CHAR_REACT_BUSY] = "busy", [MAO_CHAR_REACT_DONE] = "done",
        [MAO_CHAR_REACT_FAIL] = "fail", [MAO_CHAR_REACT_BACK] = "back",
        [MAO_CHAR_REACT_DEVICE_ON] = "device_on", [MAO_CHAR_REACT_DEVICE_OFF] = "device_off",
        [MAO_CHAR_REACT_UNSURE] = "neutral",   /* unreachable: handled above */
        [MAO_CHAR_REACT_IDLE] = "neutral",
        [MAO_CHAR_REACT_FIDDLE_NOTICE] = "suspicious", [MAO_CHAR_REACT_FIDDLE_ANNOYED] = "tsk",
        [MAO_CHAR_REACT_FIDDLE_FED_UP] = "mad",
    };
    mao_char_wake(mc, now);
    mc->fb_play_at = 0;
    switch (r) {
    case MAO_CHAR_REACT_DEVICE_ON:
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_DEVICE_NEW, now);
        break;
    case MAO_CHAR_REACT_DEVICE_OFF:
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_DEVICE_LOST, now);
        break;
    case MAO_CHAR_REACT_ACK:
        /* Pending attention: focused waiting, no celebration. */
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_WAIT, now);
        break;
    case MAO_CHAR_REACT_UNSURE:
        /* No verdict exists: EVALUATE, a later second look, and back to
         * work. No lark state at all - the mind carries it. */
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_UNSURE, now);
        return;
    case MAO_CHAR_REACT_DONE:
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_OK, now);
        break;
    case MAO_CHAR_REACT_FAIL:
        /* Analysis first: freeze and study for a beat, then the verdict. */
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_FAIL, now);
        mc->fb_play_at = now + 450;
        break;
    case MAO_CHAR_REACT_BUSY:
        mao_life_event(&mc->life, &mc->lark, LIFE_EV_CMD_BUSY, now);
        if (mc->life.habit_busy > 0.65f) {
            /* The third "busy" in a row barely registers: a slight
             * narrowing from the mind's evaluation, nothing more. */
            ESP_LOGI(TAG, "busy barely noted (habituated %.2f)", (double)mc->life.habit_busy);
            return;
        }
        break;
    default:
        break;
    }
    if (r >= MAO_CHAR_REACT_FIDDLE_NOTICE && r <= MAO_CHAR_REACT_FIDDLE_ONGOING) {
        const uint8_t lv = r == MAO_CHAR_REACT_FIDDLE_ONGOING ? 0 : (uint8_t)(r - MAO_CHAR_REACT_FIDDLE_NOTICE + 1);
        /* already this cross (or crosser): it does not start over - it holds on */
        const bool teasing = mc->grudge_state >= 0 && lv <= mc->grudge_level;
        grudge(mc, r, now);
        if (teasing && mc->grudge_level == 3) {
            /* glaring and teased again: it barely moves - only the lids tighten */
            mao_motion_kick(&mc->m, CH_NARROW, MAO_TEASE_SQUEEZE);
        }
        if (r == MAO_CHAR_REACT_FIDDLE_ONGOING || teasing) {
            return;                           /* the mood lasts; nothing new plays */
        }
    }
    mc->fb_pending = mao_lark_find(kFb[r]);
    mc->fb_hold = r == MAO_CHAR_REACT_BUSY;
}

/* The grudge: which mood follows each reaction, how long it outlasts the
 * fiddling, the colour it wears, and - when it runs out - how long it holds
 * as the next milder mood on the way down (glaring -> sulky -> wary ->
 * itself), so it never ends in one step. gaze: how much the eyes follow the
 * scale in that mood (wary tracks closely, sulky withdraws, glaring stares
 * at you instead); still: how much of the mind's idle motion it suppresses. */
static const struct {
    const char *state, *accent;
    uint32_t hold_ms, ease_ms;
    float gaze, still;
} kGrudge[4] = {
    [0] = { NULL, NULL, 0, 0, 1.00f, 0.0f },
    [1] = { "suspicious", "suspicious", 7000, 2500, 1.25f, 0.2f },
    [2] = { "sulky", "tsk", 11000, 3000, 0.55f, 0.35f },
    [3] = { "cat_glare", "mad", 16000, 0, 0.30f, 0.75f },
};

static void grudge(mao_char_t *mc, mao_character_reaction_t r, uint32_t now)
{
    if (r != MAO_CHAR_REACT_FIDDLE_ONGOING) {
        const uint8_t lv = (uint8_t)(r - MAO_CHAR_REACT_FIDDLE_NOTICE + 1);
        if (lv > mc->grudge_level || mc->grudge_state < 0) {
            mc->grudge_forgiven = mc->grudge_state >= 0 && mc->grudge_forgiven && lv <= mc->grudge_level;
            mc->grudge_level = lv;
            mc->grudge_state = mao_lark_find(kGrudge[lv].state);
        }
    } else if (mc->grudge_state < 0) {
        return;                               /* nothing to prolong */
    }
    const uint32_t until = now + kGrudge[mc->grudge_level].hold_ms;
    if (!before(until, mc->grudge_until) || !mc->grudge_until) {
        mc->grudge_until = until;
    }
}

/* Keep the grudge's mood on screen; end it when it has run out. */
static void grudge_update(mao_char_t *mc, bool feedback, uint32_t now)
{
    if (mc->grudge_state < 0) {
        return;
    }
    if (!before(now, mc->grudge_until) && !mc->sleepy && !mc->grudge_forgiven && mc->grudge_level > 1) {
        /* it eases down a step: the next milder mood, held a little */
        mc->grudge_level--;
        mc->grudge_state = mao_lark_find(kGrudge[mc->grudge_level].state);
        mc->grudge_until = now + kGrudge[mc->grudge_level].ease_ms;
        ESP_LOGI(TAG, "grudge eases to %s", kGrudge[mc->grudge_level].state);
    }
    if (!before(now, mc->grudge_until) || mc->sleepy) {
        ESP_LOGI(TAG, "grudge over");
        mc->grudge_forgiven = false;
        if (mc->life.holding == mc->grudge_state) {
            mc->life.holding = -1;
        }
        mc->life.hold_locked = false;
        if (mc->lark.cur == mc->grudge_state) {
            mao_lark_switch(&mc->lark, 0, now);   /* back to her own face, through its transition */
        }
        mc->grudge_state = -1;
        mc->grudge_level = 0;
        mc->grudge_until = 0;
        return;
    }
    mc->life.holding = (int8_t)mc->grudge_state;   /* the mind leaves it alone ... */
    mc->life.hold_locked = true;                   /* ... and so does turning */
    if (!feedback && mc->fb_pending < 0 && mc->lark.cur != mc->grudge_state) {
        mao_lark_switch(&mc->lark, mc->grudge_state, now);
    }
}

/* Quirks: the app says when (mao_quirks.c); here is what they look like.
 * Each is bounded - a timed stillness, a look, or one expression. */
void mao_char_quirk(mao_char_t *mc, mao_character_quirk_t q, uint32_t now)
{
    if (!mc->visible || !mc->present || mc->sleepy || mc->transfer.phase != MAO_TR_NONE) {
        return;
    }
    const char *st = NULL;
    switch (q) {
    case MAO_QUIRK_HOLD:
        if (mc->grudge_state < 0 || mc->grudge_level != 1) {
            return;                           /* only while wary: it was waiting for the next move */
        }
        mc->still_until = now + MAO_QUIRK_HOLD_MS;   /* waiting to see what you do next */
        break;
    case MAO_QUIRK_FORGIVE:
        if (mc->grudge_state < 0) {
            return;
        }
        mc->still_until = now + MAO_QUIRK_WATCH_MS;  /* it watches that clean move ... */
        mc->grudge_until = now + MAO_QUIRK_WATCH_MS; /* ... and lets it go, without the ladder */
        mc->grudge_forgiven = true;
        break;
    case MAO_QUIRK_CURIOUS:
        st = "curious";
        break;
    case MAO_QUIRK_DELIGHT:
        st = "glint";
        break;
    case MAO_QUIRK_OFFSCREEN_LEFT:
    case MAO_QUIRK_OFFSCREEN_RIGHT:
        if (mc->peek || mc->grudge_state >= 0 || mc->look_on) {
            return;                           /* only an idle MAO has attention to spare */
        }
        mc->offscreen_dir = q == MAO_QUIRK_OFFSCREEN_LEFT ? -1 : 1;
        mc->offscreen_until = now + MAO_QUIRK_OFFSCREEN_MS;
        break;
    case MAO_QUIRK_GRUMPY:
        st = "grumpywake";
        break;
    case MAO_QUIRK_WAKEPOP:
        st = "wakepop";
        break;
    default:
        return;
    }
    if (st) {
        mc->fb_pending = mao_lark_find(st);
    }
    ESP_LOGI(TAG, "quirk: %s", mao_character_quirk_name(q));
}

void mao_char_attention_update(mao_char_t *mc, uint32_t now)
{
    /* Expression layer: full in idle, faint while the user is in charge. */
    if (mc->fb_pending >= 0 && mc->visible && mc->present && !before(now, mc->away_until) &&
        mc->transfer.phase == MAO_TR_NONE && !before(now, mc->fb_play_at)) {
        const int st = mc->fb_pending;
        mc->fb_pending = -1;
        mc->lark.pending = -1;           /* controller feedback wins over a queued mood */
        mao_lark_switch(&mc->lark, st, now);
        const lark_state_t *ls = mao_lark_state(st);
        mc->fb_until = now + ((ls->flags & LARK_ONESHOT) ? (uint32_t)ls->length_ms + 300u : 0u);
    }
    if (mc->fb_hold && mc->lark.cur != mao_lark_find("busy")) {
        mc->fb_hold = false;             /* busy ended (done / fail / idle / input) */
    }
    static const float kLayerGain[] = { [PRIO_IDLE] = 1.0f, [PRIO_SYSTEM] = 0.5f, [PRIO_DIAL] = 0.25f,
                                        [PRIO_PRESS] = 0.45f, [PRIO_NAV] = 0.0f };
    const prio_t prio = mao_char_current_prio(mc, now);
    const bool feedback = mc->fb_hold || before(now, mc->fb_until);
    grudge_update(mc, feedback, now);
    const bool sulking = mc->grudge_state >= 0;
    float gain = feedback || sulking ? 1.0f : kLayerGain[prio];
    if (mc->peek) {
        gain *= MAO_PEEK_LAYER_GAIN;   /* the page is in charge; MAO observes */
    }
    /* HOME's look (the lamp scale): the knob is in charge, the eyes show it. */
    if (mc->look_on && !before(now, mc->look_until)) {
        mc->look_on = false;
        mc->contain_until = now + 900u;
    }
    if (!mc->look_on && (mc->look_gx != 0.0f || mc->look_gy != 0.0f)) {
        mc->look_gx = mc->look_gy = 0.0f;       /* let go: back to the centre */
        mao_motion_set(&mc->m, CH_GAZE_X, 0.0f);
        mao_motion_set(&mc->m, CH_GAZE_Y, 0.0f);
    }
    const bool looking = mc->look_on && !mc->peek && mc->transfer.phase == MAO_TR_NONE;
    if (looking) {
        /* how closely it follows the scale depends on its mood */
        const float gz = kGrudge[mc->grudge_state >= 0 ? mc->grudge_level : 0].gaze;
        mao_motion_set(&mc->m, CH_GAZE_X, mc->look_gx * gz);
        mao_motion_set(&mc->m, CH_GAZE_Y, mc->look_gy * gz);
    }
    /* A look past the rim: far to one side, the head turned so the far eye
     * tucks away, as if at something off the screen - then back. */
    if (mc->offscreen_until) {
        if (before(now, mc->offscreen_until) && !mc->look_on && mc->grudge_state < 0) {
            mao_motion_set(&mc->m, CH_GAZE_X, (float)mc->offscreen_dir * MAO_GAZE_MAX * 1.6f);
            mao_motion_set(&mc->m, CH_HEAD_YAW, (float)mc->offscreen_dir * 0.75f);
        } else {
            mc->offscreen_until = 0;
            mao_motion_set(&mc->m, CH_HEAD_YAW, 0.0f);
            if (!mc->look_on) {
                mao_motion_set(&mc->m, CH_GAZE_X, 0.0f);
            }
        }
    }
    {
        /* Stillness: a glare, or a held look, takes the mind's small idle
         * motion and blinks away; eased in quickly and out slowly. */
        const float sdt = mc->contain_ms ? fminf((float)(now - mc->contain_ms) / 1000.0f, 0.1f) : 0.0f;
        mc->still_target = fmaxf(mc->grudge_state >= 0 ? kGrudge[mc->grudge_level].still : 0.0f,
                                 before(now, mc->still_until) ? 1.0f : 0.0f);
        const float tau = mc->still_target > mc->still ? MAO_STILL_IN_S : MAO_STILL_OUT_S;
        mc->still += (mc->still_target - mc->still) * (1.0f - expf(-sdt / tau));
        if (mc->still > 0.6f && mc->life.next_blink_ms < now + 400u) {
            mc->life.next_blink_ms = now + 400u;    /* no blink in a stare */
        }
    }
    {
        /* While the eyes follow the scale the layers are quieter (the knob is
         * in charge); a reaction to the fiddling, and the mood after it, play
         * in full. Eased both ways: when the scale goes, whatever mood holds
         * the face comes back over MAO_LOOK_EASE_S, not in one frame. */
        const float kdt = mc->contain_ms ? fminf((float)(now - mc->contain_ms) / 1000.0f, 0.1f) : 0.0f;
        const float want[2] = { looking && !(feedback || sulking) ? 0.25f : 1.0f, looking ? 0.25f : 1.0f };
        for (int k = 0; k < 2; k++) {
            if (mc->look_k[k] <= 0.0f) {
                mc->look_k[k] = 1.0f;                  /* first tick */
            }
            mc->look_k[k] += (want[k] - mc->look_k[k]) * (1.0f - expf(-kdt / MAO_LOOK_EASE_S));
        }
        gain *= mc->look_k[0];
    }
    {
        /* Eased: the radius shrinks in fast and widens out slowly, so the
         * eyes never snap when the scale comes or goes. */
        const bool want = looking || before(now, mc->contain_until);
        const float cdt = mc->contain_ms ? fminf((float)(now - mc->contain_ms) / 1000.0f, 0.1f) : 0.0f;
        mc->contain_ms = now;
        if (mc->contain_now < MAO_HOME_CONTAIN_R) {
            mc->contain_now = MAO_CONTAIN_OFF_R;   /* first tick */
        }
        const float tgt = want ? MAO_HOME_CONTAIN_R : MAO_CONTAIN_OFF_R;
        const float tau = want ? MAO_CONTAIN_IN_S : MAO_CONTAIN_OUT_S;
        mc->contain_now += (tgt - mc->contain_now) * (1.0f - expf(-cdt / tau));
        if (fabsf(tgt - mc->contain_now) < 0.5f) {
            mc->contain_now = tgt;
        }
        mc->m.contain_r = mc->contain_now < MAO_CONTAIN_OFF_R - 0.5f ? mc->contain_now : 0.0f;
    }
    mao_lark_update(&mc->lark, now, mc->visible && prio == PRIO_IDLE, mc->sleepy, gain, mc->m.layer);
    /* The accent leans with what plays, as strongly as it plays. */
    const float adt = mc->accent_ms ? (float)(now - mc->accent_ms) / 1000.0f : 0.0f;
    mc->accent_ms = now;
    mao_accent_step(&mc->accent,
                    mao_accent_target(sulking && !feedback ? kGrudge[mc->grudge_level].accent : mao_lark_state(mc->lark.cur)->name,
                                      mc->sleepy, clampf(mc->lark.gain.x, 0.0f, 1.0f)),
                    adt > 0.1f ? 0.1f : adt);
    mc->m.accent = mao_accent_color(&mc->accent);
    mc->accent_pub = mc->m.accent;
    float life[CH_COUNT] = { 0 };
    mao_life_update(&mc->life, &mc->lark, &mc->m, now, mc->visible && prio == PRIO_IDLE, mc->sleepy, life);
    if (prio != PRIO_NAV) {
        const float lg = (mc->peek ? MAO_PEEK_LAYER_GAIN : mc->look_k[1]) * (1.0f - 0.85f * mc->still);
        for (int i = 0; i < CH_COUNT; i++) {
            mc->m.layer[i] += life[i] * lg;
        }
    }
}
