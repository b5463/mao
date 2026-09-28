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
    mc->fb_pending = mao_lark_find(kFb[r]);
    mc->fb_hold = r == MAO_CHAR_REACT_BUSY;
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
    float gain = feedback ? 1.0f : kLayerGain[prio];
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
        gain *= feedback ? 1.0f : 0.25f;          /* its own reaction to the fiddling plays in full */
        mao_motion_set(&mc->m, CH_GAZE_X, mc->look_gx);
        mao_motion_set(&mc->m, CH_GAZE_Y, mc->look_gy);
    }
    mc->m.contain_r = looking || before(now, mc->contain_until) ? MAO_HOME_CONTAIN_R : 0.0f;
    mao_lark_update(&mc->lark, now, mc->visible && prio == PRIO_IDLE, mc->sleepy, gain, mc->m.layer);
    /* The accent leans with what plays, as strongly as it plays. */
    const float adt = mc->accent_ms ? (float)(now - mc->accent_ms) / 1000.0f : 0.0f;
    mc->accent_ms = now;
    mao_accent_step(&mc->accent,
                    mao_accent_target(mao_lark_state(mc->lark.cur)->name, mc->sleepy, clampf(mc->lark.gain.x, 0.0f, 1.0f)),
                    adt > 0.1f ? 0.1f : adt);
    mc->m.accent = mao_accent_color(&mc->accent);
    mc->accent_pub = mc->m.accent;
    float life[CH_COUNT] = { 0 };
    mao_life_update(&mc->life, &mc->lark, &mc->m, now, mc->visible && prio == PRIO_IDLE, mc->sleepy, life);
    if (prio != PRIO_NAV) {
        const float lg = mc->peek ? MAO_PEEK_LAYER_GAIN : (looking ? 0.25f : 1.0f);
        for (int i = 0; i < CH_COUNT; i++) {
            mc->m.layer[i] += life[i] * lg;
        }
    }
}
