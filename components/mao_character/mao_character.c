/*
 * MAO character: command intake and the per-tick pipeline. Runs on an LVGL
 * timer; commands arrive through a queue (see docs/character_architecture.md).
 *
 * On top of the dial physics (mao_character_dial.c) runs the Lark-style
 * expression layer (mao_lark.c): authored states chosen by a small mood
 * model, blended additively and weighted down while the user is
 * interacting; and the inner life (mao_life.c): drives and impulses that make
 * MAO act on its own, real saccades, cat mode, and reactions to people that
 * depend on its mood.
 */
#include "mao_character_internal.h"

#include "esp_log.h"

static const char *TAG = "MAO_CHARACTER";

/* The motion tick must not share the display's 33 ms refresh period: two
 * unsynchronised 33 ms timers beat, a refresh regularly found no fresh pose
 * and motion stuttered to 66 ms steps (measured: median gap 38 ms, max 67).
 * At 16 ms the median gap fell to 20 ms (~50 fps) but render-busy rose to
 * ~39 %; 25 ms lands the target ~30 fps with a uniform cadence. */
#define TICK_MS        25
#define CMD_QUEUE_LEN  16

static const mao_look_t kLooks[] = MAO_LOOKS;
#define LOOK_COUNT ((int)(sizeof(kLooks) / sizeof(kLooks[0])))

/* The one owner of all character state. Zero-initialised (.bss, not a
 * multi-KB .data image); the few non-zero defaults are set in create(). */
static mao_char_t s_c;

static const char *const kPreviewNames[MAO_CHAR_PREVIEW_COUNT] = {
    "idle", "blink", "follow", "fast", "vfast", "dizzy", "press", "happy", "sleepy", "leave", "hide",
};

const char *mao_character_preview_name(mao_character_preview_t p)
{
    return p < MAO_CHAR_PREVIEW_COUNT ? kPreviewNames[p] : "?";
}

mao_character_state_t mao_character_get_state(void)
{
    return s_c.state;
}

int mao_character_look_count(void)
{
    return LOOK_COUNT;
}

/* ---------------------------------------------------------------------- */
/* Tick                                                                   */
/* ---------------------------------------------------------------------- */

static void preview(mao_char_t *mc, mao_character_preview_t p, uint32_t now);

static void apply(mao_char_t *mc, const cmd_t *c, uint32_t now)
{
    switch (c->type) {
    case CMD_DIAL:       mao_char_on_dial(mc, c->value, now); break;
    case CMD_PRESS:      mao_char_on_press(mc, c->flag, now); break;
    case CMD_REACT:      mao_char_react(mc, (mao_character_reaction_t)c->arg, now); break;
    case CMD_APPEAR:     mao_char_appear(mc, c->arg, now); break;
    case CMD_SLEEPY:     mao_char_set_sleepy(mc, c->flag, now); break;
    case CMD_LEAVE:      mao_char_leave(mc, now); break;
    case CMD_RETURN:     mao_char_come_back(mc, now); break;
    case CMD_PREVIEW:    preview(mc, (mao_character_preview_t)c->arg, now); break;
    case CMD_DEBUG_DIAL: mc->dbg_dps = c->f; mc->dbg_acc = 0.0f; mc->dbg_dial_until = now + (uint32_t)c->value; break;
    case CMD_LOOK:       mc->m.look = kLooks[c->arg]; ESP_LOGI(TAG, "look '%s'", kLooks[c->arg].name); break;
    case CMD_EXPRESSION: mao_lark_switch(&mc->lark, c->arg, now); break;
    case CMD_TRANSFER: {
        const int dx = c->value / 10, dy = c->value % 10;
        if (c->arg == (int8_t)MAO_TR_NONE) {
            mao_transfer_begin(&mc->transfer, &mc->m, MAO_TR_NONE, 0, 0, now);
            mao_idle_schedule(&mc->idle, now, mc->sleepy, true);
        } else {
            if (!mc->visible) {
                mao_char_appear(mc, 0, now);
            }
            mao_char_wake(mc, now);
            mc->fb_pending = -1;   /* a transfer outranks queued feedback */
            mao_transfer_begin(&mc->transfer, &mc->m, (uint8_t)c->arg, dx, dy, now);
        }
        break;
    }
    case CMD_PEEK:       mao_char_peek_set(mc, c->flag, now); break;
    case CMD_GATHER:     mao_char_gather(mc, now); break;
    case CMD_ATTEND: {
        /* Screen target -> gaze within the face. The eyes sit near the rim,
         * so a target above them means looking up. */
        const float x = (float)(int16_t)(c->value >> 16), y = (float)(int16_t)(c->value & 0xFFFF);
        mc->attend_gx = clampf(x * MAO_ATTEND_GAIN_X, -MAO_GAZE_MAX, MAO_GAZE_MAX);
        mc->attend_gy = clampf((y - MAO_PEEK_DROP) * MAO_ATTEND_GAIN_Y, -MAO_ATTEND_UP_MAX, 4.0f);
        mc->attend_fx = clampf(x * MAO_ATTEND_FACE, -MAO_ATTEND_FACE_MAX, MAO_ATTEND_FACE_MAX);
        break;
    }
    case CMD_MIND:
        if (c->arg == 0) {
            mao_life_debug_interest(&mc->life, (uint8_t)c->value);
        } else {
            mao_life_debug_novelty(&mc->life, (uint8_t)c->value);
        }
        break;
    default: break;
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    mao_char_t *const mc = &s_c;
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - mc->last_tick_ms) / 1000.0f;
    mc->last_tick_ms = now;
    dt = clampf(dt, 0.005f, 0.05f);   /* after a stall, don't let springs jump */

    cmd_t c;
    while (xQueueReceive(mc->cmds, &c, 0) == pdTRUE) {
        apply(mc, &c, now);
    }
    if (before(now, mc->dbg_dial_until)) {
        mc->dbg_acc += mc->dbg_dps * dt;
        const int32_t n = (int32_t)mc->dbg_acc;
        if (n) {
            mc->dbg_acc -= (float)n;
            mao_char_on_dial(mc, n, now);
        }
    }

    mao_char_dial_update(mc, dt, now);
    mao_char_timed_reactions(mc, now);
    mao_transfer_tick(&mc->transfer, &mc->m, &mc->draw, now);
    if (mc->visible && mao_char_current_prio(mc, now) == PRIO_IDLE) {
        /* Idle behaviour now comes from the mind (mao_life.c). */
    } else if (mao_char_current_prio(mc, now) != PRIO_IDLE) {
        /* Something more important is happening: push idle back. */
        mao_idle_schedule(&mc->idle, now, mc->sleepy, true);
    }
    mao_char_update_state(mc, now);

    /* ATTENTION: controller feedback and the mind's expression layers. */
    mao_char_attention_update(mc, now);

    /* MOTION + RENDER */
    mao_motion_step(&mc->m, dt, now);
    if (mc->visible) {
        mao_pose_t pose;
        mao_motion_pose(&mc->m, mc->mouth, now, &pose);
        mao_char_draw_apply(&mc->draw, &pose);
        mao_char_draw_star(&mc->draw, &pose);
    }
}

/* ---------------------------------------------------------------------- */
/* Development previews                                                   */
/* ---------------------------------------------------------------------- */

static void preview(mao_char_t *mc, mao_character_preview_t p, uint32_t now)
{
    ESP_LOGI(TAG, "preview '%s'", mao_character_preview_name(p));
    if (p == MAO_CHAR_PREVIEW_HIDE) {
        mc->visible = false;
        mc->m.ch[CH_OPEN].x = 0.0f;
        mc->m.ch[CH_OPEN].v = 0.0f;
        mao_motion_set(&mc->m, CH_OPEN, 0.0f);
        mao_char_draw_hide(&mc->draw);
        return;
    }
    if (!mc->visible) {
        mao_char_appear(mc, 0, now);
    }
    switch (p) {
    case MAO_CHAR_PREVIEW_IDLE:
        mc->disturb = 0.0f;
        mc->dbg_dial_until = 0;
        mao_char_wake(mc, now);
        break;
    case MAO_CHAR_PREVIEW_BLINK:     mao_motion_blink(&mc->m, now, 150, 1); break;
    case MAO_CHAR_PREVIEW_FOLLOW:    mc->dbg_dps = 6.0f;  mc->dbg_dial_until = now + 2500; break;
    case MAO_CHAR_PREVIEW_FAST:      mc->dbg_dps = 35.0f; mc->dbg_dial_until = now + 3000; break;
    case MAO_CHAR_PREVIEW_VERY_FAST: mc->dbg_dps = 75.0f; mc->dbg_dial_until = now + 3000; break;
    case MAO_CHAR_PREVIEW_DIZZY:
        for (int k = 0; k < 4; k++) {
            mao_char_on_dial(mc, k % 2 ? 1 : -1, now);
        }
        break;
    case MAO_CHAR_PREVIEW_PRESS:     mao_char_on_press(mc, true, now); mc->dbg_release_at = now + 700; break;
    case MAO_CHAR_PREVIEW_WARM:      mao_char_react(mc, MAO_CHAR_REACT_WARM, now); break;
    case MAO_CHAR_PREVIEW_SLEEPY:    mao_char_set_sleepy(mc, !mc->sleepy, now); break;
    case MAO_CHAR_PREVIEW_LEAVE:     mao_char_leave(mc, now); mc->dbg_return_at = now + 1200; break;
    default: break;
    }
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

esp_err_t mao_character_create(lv_obj_t *parent)
{
    mao_char_t *const mc = &s_c;
    mc->fb_pending = -1;
    mc->present = true;
    mc->cmds = xQueueCreate(CMD_QUEUE_LEN, sizeof(cmd_t));
    if (!mc->cmds) {
        return ESP_ERR_NO_MEM;
    }
    mao_motion_init(&mc->m, &kLooks[MAO_LOOK_DEFAULT]);
    mao_lark_init(&mc->lark, lv_tick_get());
    mao_life_init(&mc->life, lv_tick_get());
    esp_err_t err = mao_char_draw_create(&mc->draw, parent);
    if (err != ESP_OK) {
        return err;
    }
    mc->last_tick_ms = lv_tick_get();
    mao_idle_schedule(&mc->idle, mc->last_tick_ms, false, true);
    if (!lv_timer_create(tick_cb, TICK_MS, NULL)) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "character ready: look '%s', %d ms motion tick", kLooks[MAO_LOOK_DEFAULT].name, TICK_MS);
    return ESP_OK;
}

static void post(cmd_t c)
{
    if (s_c.cmds) {
        xQueueSend(s_c.cmds, &c, 0);
    }
}

void mao_character_set_detents_per_rev(uint8_t n)
{
    if (n > 0) {
        s_c.detents_per_rev = n;
    }
}

void mao_character_dial(int32_t detents)        { post((cmd_t){ .type = CMD_DIAL, .value = detents }); }
void mao_character_press(bool down)             { post((cmd_t){ .type = CMD_PRESS, .flag = down }); }
void mao_character_react(mao_character_reaction_t r) { post((cmd_t){ .type = CMD_REACT, .arg = (int8_t)r }); }
void mao_character_appear(int direction)        { post((cmd_t){ .type = CMD_APPEAR, .arg = (int8_t)direction }); }
void mao_character_set_sleepy(bool sleepy)      { post((cmd_t){ .type = CMD_SLEEPY, .flag = sleepy }); }
void mao_character_leave(void)                  { post((cmd_t){ .type = CMD_LEAVE }); }
void mao_character_return(void)                 { post((cmd_t){ .type = CMD_RETURN }); }
void mao_character_gather(void)                 { post((cmd_t){ .type = CMD_GATHER }); }
void mao_character_peek(bool on)                { post((cmd_t){ .type = CMD_PEEK, .flag = on }); }

void mao_character_attend(int x, int y)
{
    x = x < -200 ? -200 : (x > 200 ? 200 : x);
    y = y < -200 ? -200 : (y > 200 ? 200 : y);
    post((cmd_t){ .type = CMD_ATTEND, .value = (int32_t)(((uint32_t)(uint16_t)x << 16) | (uint16_t)y) });
}

void mao_character_debug_preview(mao_character_preview_t p)
{
    post((cmd_t){ .type = CMD_PREVIEW, .arg = (int8_t)p });
}

static void post_transfer(mao_transfer_phase_t phase, int dx, int dy)
{
    post((cmd_t){ .type = CMD_TRANSFER, .arg = (int8_t)phase, .value = dx * 10 + dy });
}

void mao_character_transfer_search(int dx, int dy) { post_transfer(MAO_TR_SEARCH, dx, dy); }
void mao_character_transfer_exit(int dx, int dy)   { post_transfer(MAO_TR_EXIT, dx, dy); }
void mao_character_transfer_fail(int dx, int dy)   { post_transfer(MAO_TR_BASH, dx, dy); }
void mao_character_transfer_return(int dx, int dy) { post_transfer(MAO_TR_ENTER, dx, dy); }
void mao_character_transfer_abort(void)            { post_transfer(MAO_TR_NONE, 0, 0); }

void mao_character_debug_interest(uint8_t pct)
{
    post((cmd_t){ .type = CMD_MIND, .arg = 0, .value = pct > 100 ? 100 : pct });
}

void mao_character_debug_novelty(uint8_t pct)
{
    post((cmd_t){ .type = CMD_MIND, .arg = 1, .value = pct > 100 ? 100 : pct });
}

void mao_character_debug_dial(float dps, uint32_t ms)
{
    post((cmd_t){ .type = CMD_DEBUG_DIAL, .f = dps, .value = (int32_t)ms });
}

bool mao_character_debug_look(int index)
{
    if (index < 0 || index >= LOOK_COUNT) {
        return false;
    }
    post((cmd_t){ .type = CMD_LOOK, .arg = (int8_t)index });
    return true;
}

bool mao_character_debug_expression(int index)
{
    if (index < 0 || index >= mao_lark_state_count()) {
        return false;
    }
    post((cmd_t){ .type = CMD_EXPRESSION, .arg = (int8_t)index });
    return true;
}

int mao_character_expression_count(void)
{
    return mao_lark_state_count();
}

const char *mao_character_expression_name(int index)
{
    return index >= 0 && index < mao_lark_state_count() ? mao_lark_state(index)->name : "?";
}

const char *mao_character_reaction_name(mao_character_reaction_t r)
{
    static const char *const kNames[MAO_CHAR_REACT_COUNT] = {
        "notice", "attend", "warm", "wake", "ack", "busy", "done", "fail", "back", "device_on", "device_off",
        "unsure", "idle",
    };
    return r < MAO_CHAR_REACT_COUNT ? kNames[r] : "?";
}
