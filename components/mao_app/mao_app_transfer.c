/*
 * MAO's side of the connection / transfer experience (the app's state
 * machine; the character runs the visuals, see mao_character_transfer.c).
 *
 *   IDLE -> CONNECTING     intentional connect towards a logical edge: MAO
 *                          notices the edge and listens (no spinner). The
 *                          handshake is real: a reachability probe on the
 *                          radio must be answered by the target device.
 *   CONNECTING -> EXITING  the device answered: MAO leaves through the edge.
 *   EXITING -> AWAY        off screen; a dim seam marks the departure edge.
 *                          The controller stays usable: press = come back,
 *                          long press = abort, the dial is acknowledged only.
 *   AWAY -> RETURNING      MAO comes back in from the same edge; the same
 *                          mind resumes (no drives or memory are reset).
 *   CONNECTING -> FAILING  no answer: MAO tries to leave anyway and finds
 *                          the glass. Three escalating attempts, then a dry
 *                          "apparently not". One sequence per attempt - no
 *                          automatic wall-bashing loop.
 *
 * Every step is guarded by a transfer id, so a stale timer from an aborted
 * or superseded transfer can never restart an obsolete one.
 */
#include "mao_app.h"
#include "mao_app_priv.h"

#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mao_character.h"
#include "mao_devices.h"
#include "mao_events.h"
#include "mao_system.h"
#include "mao_ui.h"

static const char *TAG = "MAO_TRANSFER";

/* Readiness is event-driven (MAO_EVENT_DEVICE_PROBED, typically 5-10 ms on
 * this radio); the steps below only re-knock and time out. The visual beat
 * is separate: however fast the radio answers, MAO gets a moment to be seen
 * checking before it commits. */
#define PROBE_STEP_MS        220     /* re-knock cadence while waiting */
#define MIN_BEAT_MS          260     /* evaluation the user can actually read */
#define TIMEOUT_ONLINE_MS    700     /* known-online device: several quick knocks */
#define TIMEOUT_RECENT_MS    1100    /* recently offline: a little more patience */
#define TIMEOUT_UNKNOWN_MS   1500    /* nothing known: the discovery path */
#define LOST_AWAY_HOLD_MS    500     /* away, device gone: a beat before coming home */

typedef enum { TR_IDLE = 0, TR_CONNECTING, TR_EXITING, TR_AWAY, TR_RETURNING, TR_FAILING } tr_state_t;

static const char *const kNames[] = { "IDLE", "CONNECTING", "EXITING", "AWAY", "RETURNING", "FAILING" };

static struct {
    tr_state_t st;
    uint32_t id;                /* transfer_id: stale steps are ignored */
    int8_t dx, dy;
    uint64_t dev;
    uint32_t probe_t0;          /* ms, when the reachability probe started */
    uint32_t timeout_ms;        /* policy chosen from the device's known state */
    bool launch_armed;          /* probe answered; waiting out the visual beat */
    bool fail_on_return;        /* restrained FAIL once MAO is back home */
    esp_timer_handle_t timer;
} s_tr;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void timer_cb(void *arg)
{
    (void)arg;
    /* esp_timer task: only post; the dispatcher calls mao_transfer_step(). */
    mao_event_post(MAO_EVENT_TRANSFER_STEP, (int32_t)s_tr.id);
}

static void schedule(uint32_t ms)
{
    if (!s_tr.timer) {
        const esp_timer_create_args_t args = { .callback = timer_cb, .name = "mao_transfer" };
        if (esp_timer_create(&args, &s_tr.timer) != ESP_OK) {
            return;
        }
    }
    esp_timer_stop(s_tr.timer);
    esp_timer_start_once(s_tr.timer, (uint64_t)ms * 1000);
}

static void enter(tr_state_t st)
{
    if (st != s_tr.st) {
        ESP_LOGI(TAG, "%s -> %s", kNames[s_tr.st], kNames[st]);
        s_tr.st = st;
    }
}

bool mao_transfer_active(void)
{
    return s_tr.st != TR_IDLE;
}

/* The connection target: the device open in the DEVICE view if any,
 * otherwise the first known device. */
static uint64_t pick_target(void)
{
    if (mao_state()->device_id) {
        return mao_state()->device_id;
    }
    for (int i = 0; i < MAO_DEVICES_MAX; i++) {
        mao_device_t d;
        if (mao_devices_get(i, &d)) {
            return d.info.id;
        }
    }
    return 0;
}

static void begin_exit(void)
{
    enter(TR_EXITING);
    mao_character_transfer_exit(s_tr.dx, s_tr.dy);
    schedule(MAO_CHAR_TRANSFER_EXIT_MS);
}

static void begin_fail(const char *why)
{
    enter(TR_FAILING);
    ESP_LOGI(TAG, "failed_escape <- %s", why);
    mao_character_transfer_fail(s_tr.dx, s_tr.dy);
    schedule(MAO_CHAR_TRANSFER_BASH_MS);
}

static void begin_return(const char *why)
{
    mao_ui_away(false, s_tr.dx, s_tr.dy);
    enter(TR_RETURNING);
    ESP_LOGI(TAG, "return <- %s", why);
    mao_character_transfer_return(s_tr.dx, s_tr.dy);
    schedule(MAO_CHAR_TRANSFER_ENTER_MS);
}

static void finish(void)
{
    enter(TR_IDLE);
    mao_ui_device_connect_hot(0.0f);
    /* Discovery pacing goes back to whatever the current view wants. */
    const mao_view_t v = mao_state()->view;
    mao_devices_set_active(v == MAO_VIEW_DEVICES || v == MAO_VIEW_DEVICE);
    if (s_tr.fail_on_return) {
        s_tr.fail_on_return = false;
        mao_character_react(MAO_CHAR_REACT_FAIL);   /* restrained, after the fact */
    }
}

static void start(int8_t dx, int8_t dy, int mode)
{
    if (s_tr.st != TR_IDLE) {
        ESP_LOGW(TAG, "transfer already running (%s)", kNames[s_tr.st]);
        return;
    }
    s_tr.id++;
    s_tr.dx = dx;
    s_tr.dy = dy;
    s_tr.launch_armed = false;
    s_tr.fail_on_return = false;
    mao_app_go_home();
    mao_devices_set_active(true);   /* fast liveness while a transfer runs */
    if (mode == 1) {                    /* synthetic success (animation work) */
        begin_exit();
        return;
    }
    if (mode == 2) {                    /* synthetic failure */
        begin_fail("dev command");
        return;
    }
    enter(TR_CONNECTING);
    s_tr.dev = pick_target();
    s_tr.probe_t0 = now_ms();
    mao_character_transfer_search(dx, dy);
    /* Patience follows what is known: an online device answers in a few
     * milliseconds or it isn't there; a recently-offline one gets a little
     * longer; with nothing known MAO waits out a discovery round. */
    mao_device_t dev;
    const int idx = s_tr.dev ? mao_devices_find(s_tr.dev) : -1;
    if (idx >= 0 && mao_devices_get(idx, &dev) && dev.online) {
        s_tr.timeout_ms = TIMEOUT_ONLINE_MS;
    } else if (idx >= 0) {
        s_tr.timeout_ms = TIMEOUT_RECENT_MS;
    } else {
        s_tr.timeout_ms = TIMEOUT_UNKNOWN_MS;
    }
    if (s_tr.dev) {
        ESP_LOGI(TAG, "connecting to %016llx (edge %+d%+d, timeout %" PRIu32 " ms)",
                 (unsigned long long)s_tr.dev, dx, dy, s_tr.timeout_ms);
        mao_devices_refresh(s_tr.dev);
    } else {
        ESP_LOGI(TAG, "connecting: no device known (edge %+d%+d)", dx, dy);
    }
    schedule(PROBE_STEP_MS);
}

void mao_transfer_connect(void)
{
    start(1, 0, 0);   /* devices live at the right edge */
}

void mao_transfer_probed(int registry_idx)
{
    if (s_tr.st != TR_CONNECTING || s_tr.launch_armed) {
        return;
    }
    mao_device_t d;
    if (!mao_devices_get(registry_idx, &d) || d.info.id != s_tr.dev) {
        return;
    }
    const uint32_t waited = now_ms() - s_tr.probe_t0;
    /* Technical readiness now; the launch waits out the visual beat. */
    s_tr.launch_armed = true;
    ESP_LOGI(TAG, "device answered in %" PRIu32 " ms: connection ready", waited);
    schedule(waited >= MIN_BEAT_MS ? 1 : MIN_BEAT_MS - waited);
}

void mao_transfer_device_lost(uint64_t id)
{
    if (id != s_tr.dev) {
        return;
    }
    if (s_tr.st == TR_AWAY) {
        /* Never strand the character: come home, then a restrained FAIL. */
        s_tr.id++;
        s_tr.fail_on_return = true;
        begin_return("remote device lost while away");
    } else if (s_tr.st == TR_EXITING) {
        s_tr.fail_on_return = true;   /* finish the committed exit; handled at AWAY */
    }
}

static void abort_transfer(const char *why)
{
    if (s_tr.st == TR_IDLE) {
        return;
    }
    ESP_LOGI(TAG, "abort <- %s (%s)", why, kNames[s_tr.st]);
    if (s_tr.timer) {
        esp_timer_stop(s_tr.timer);
    }
    s_tr.id++;   /* invalidate stale steps */
    s_tr.fail_on_return = false;
    if (s_tr.st == TR_AWAY || s_tr.st == TR_EXITING) {
        begin_return(why);
        return;
    }
    mao_ui_away(false, s_tr.dx, s_tr.dy);
    mao_character_transfer_abort();
    finish();
}

void mao_transfer_step(int32_t id)
{
    if ((uint32_t)id != s_tr.id) {
        return;   /* a stale timer from an aborted / superseded transfer */
    }
    switch (s_tr.st) {
    case TR_CONNECTING: {
        if (s_tr.launch_armed) {
            begin_exit();   /* the beat has read; go */
            break;
        }
        const uint32_t waited = now_ms() - s_tr.probe_t0;
        if (waited >= s_tr.timeout_ms) {
            begin_fail("connect_timeout");
            break;
        }
        if (s_tr.dev) {
            mao_devices_refresh(s_tr.dev);   /* knock again */
        }
        schedule(PROBE_STEP_MS);
        break;
    }
    case TR_EXITING: {
        enter(TR_AWAY);
        mao_ui_away(true, s_tr.dx, s_tr.dy);
        /* The exit was already committed when the device vanished: arrive,
         * take a beat, come home, and only then the restrained verdict. */
        mao_device_t d;
        const int idx = s_tr.dev ? mao_devices_find(s_tr.dev) : -1;
        const bool gone = idx < 0 || !mao_devices_get(idx, &d) || !d.online;
        if (s_tr.fail_on_return || (s_tr.dev && gone)) {
            s_tr.fail_on_return = true;
            schedule(LOST_AWAY_HOLD_MS);
        }
        break;
    }
    case TR_AWAY:
        if (s_tr.fail_on_return) {
            begin_return("remote device lost during exit");
        }
        break;
    case TR_RETURNING:
    case TR_FAILING:
        finish();
        break;
    default:
        break;
    }
}

bool mao_transfer_input(const mao_event_t *ev)
{
    if (s_tr.st == TR_IDLE) {
        return false;
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_LONG_PRESS:
        abort_transfer("long press");
        return true;
    case MAO_EVENT_INPUT_CLICK:
    case MAO_EVENT_INPUT_DOUBLE_CLICK:
        if (s_tr.st == TR_AWAY) {
            begin_return("pressed: called back");
        }
        return true;
    case MAO_EVENT_INPUT_CW:
    case MAO_EVENT_INPUT_CCW:
        /* The controller is alive while MAO is away; the dial just isn't
         * driving anything yet. The seam stirs, nothing more. */
        if (s_tr.st == TR_AWAY) {
            mao_ui_away_nudge(ev->type == MAO_EVENT_INPUT_CW ? 1 : -1);
        }
        return true;
    case MAO_EVENT_INPUT_PRESS:
    case MAO_EVENT_INPUT_RELEASE:
        return true;
    default:
        return false;
    }
}

void mao_transfer_devcmd(int cmd)
{
    switch ((mao_transfer_cmd_t)cmd) {
    case MAO_TRANSFER_CMD_LEFT:    start(-1, 0, 0); break;
    case MAO_TRANSFER_CMD_RIGHT:   start(1, 0, 0); break;
    case MAO_TRANSFER_CMD_UP:      start(0, -1, 0); break;
    case MAO_TRANSFER_CMD_DOWN:    start(0, 1, 0); break;
    case MAO_TRANSFER_CMD_SUCCESS: start(1, 0, 1); break;
    case MAO_TRANSFER_CMD_FAIL:    start(1, 0, 2); break;
    case MAO_TRANSFER_CMD_RETURN:
        if (s_tr.st == TR_AWAY) {
            begin_return("dev command");
        }
        break;
    case MAO_TRANSFER_CMD_ABORT:
        abort_transfer("dev command");
        break;
    default:
        break;
    }
}
