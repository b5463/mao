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

#define PROBE_INTERVAL_MS   300      /* poll for the probe's answer */
#define PROBE_RETRY_AT      2        /* second probe on this poll */
#define CONNECT_TIMEOUT_MS  1700     /* then the edge turns out to be a wall */

typedef enum { TR_IDLE = 0, TR_CONNECTING, TR_EXITING, TR_AWAY, TR_RETURNING, TR_FAILING } tr_state_t;

static const char *const kNames[] = { "IDLE", "CONNECTING", "EXITING", "AWAY", "RETURNING", "FAILING" };

static struct {
    tr_state_t st;
    uint32_t id;                /* transfer_id: stale steps are ignored */
    int8_t dx, dy;
    uint64_t dev;
    uint32_t probe_t0;          /* ms, when the reachability probe started */
    uint8_t polls;
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

static void start(int8_t dx, int8_t dy, int mode)
{
    if (s_tr.st != TR_IDLE) {
        ESP_LOGW(TAG, "transfer already running (%s)", kNames[s_tr.st]);
        return;
    }
    s_tr.id++;
    s_tr.dx = dx;
    s_tr.dy = dy;
    mao_app_go_home();
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
    s_tr.polls = 0;
    mao_character_transfer_search(dx, dy);
    if (s_tr.dev) {
        ESP_LOGI(TAG, "connecting to %016llx (edge %+d%+d)", (unsigned long long)s_tr.dev, dx, dy);
        mao_devices_refresh(s_tr.dev);
    } else {
        ESP_LOGI(TAG, "connecting: no device known (edge %+d%+d)", dx, dy);
    }
    schedule(PROBE_INTERVAL_MS);
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
    if (s_tr.st == TR_AWAY || s_tr.st == TR_EXITING) {
        begin_return(why);
        return;
    }
    mao_ui_away(false, s_tr.dx, s_tr.dy);
    mao_character_transfer_abort();
    enter(TR_IDLE);
}

void mao_transfer_step(int32_t id)
{
    if ((uint32_t)id != s_tr.id) {
        return;   /* a stale timer from an aborted / superseded transfer */
    }
    switch (s_tr.st) {
    case TR_CONNECTING: {
        const uint32_t waited = now_ms() - s_tr.probe_t0;
        mao_device_t d;
        const int idx = s_tr.dev ? mao_devices_find(s_tr.dev) : -1;
        const bool answered = idx >= 0 && mao_devices_get(idx, &d) && d.online &&
                              (int32_t)(d.last_seen_ms - s_tr.probe_t0) >= 0;
        if (answered) {
            ESP_LOGI(TAG, "device answered in %" PRIu32 " ms: connection ready", waited);
            begin_exit();
            break;
        }
        if (waited >= CONNECT_TIMEOUT_MS) {
            begin_fail("connect_timeout");
            break;
        }
        if (++s_tr.polls == PROBE_RETRY_AT && s_tr.dev) {
            mao_devices_refresh(s_tr.dev);   /* one more knock */
        }
        schedule(PROBE_INTERVAL_MS);
        break;
    }
    case TR_EXITING:
        enter(TR_AWAY);
        mao_ui_away(true, s_tr.dx, s_tr.dy);
        break;
    case TR_RETURNING:
    case TR_FAILING:
        enter(TR_IDLE);
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
         * driving anything yet. Acknowledge quietly. */
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
