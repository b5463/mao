/*
 * DEVICE page relationship layer (M3.0). Decides which page a device gets and
 * owns the pages that are about the relationship rather than the device:
 *
 *   NEW      heard nearby, not part of MAO's setup: name, NEW, PAIR.
 *            No remote control is offered until PAIR (MAO product policy,
 *            not network security: the transport is not authenticated).
 *   CONTROL  KNOWN and online: the normal generic device page (mao_app.c).
 *
 * PAIR is one deliberate press. The device becomes KNOWN only after the
 * record is committed; the page then turns into the normal device page in
 * place. It never connects, identifies or controls anything by itself.
 * Runs in the dispatcher task.
 */
#include "mao_app_priv.h"

#include <inttypes.h>
#include "esp_log.h"
#include "mao_audio.h"
#include "mao_led.h"
#include "mao_rel.h"

static const char *TAG = "MAO_APP";

static const char *s_note;      /* PAIR outcome on the NEW page ("FULL" / "NOT SAVED") */
static uint64_t s_note_dev;

mao_devpage_t mao_devpage(uint64_t id, mao_world_entry_t *w)
{
    if (!id || !mao_world_get(id, w)) {
        return MAO_DEVPAGE_NONE;
    }
    return w->known ? MAO_DEVPAGE_CONTROL : MAO_DEVPAGE_NEW;
}

void mao_rel_page_opened(void)
{
    s_note = NULL;
}

void mao_rel_page_draw(mao_devpage_t kind, const mao_world_entry_t *w)
{
    if (kind != MAO_DEVPAGE_NEW) {
        return;
    }
    const bool noted = s_note && s_note_dev == w->id;
    mao_ui_device_t m = {
        .title = w->name,
        .primary = "PAIR",            /* the one thing this page offers */
        .online = w->online,
        .described = true,
        .focus = 0,
        .connect_hidden = true,
        .status_l = noted ? s_note : "NEW",
        .status_r = w->online ? NULL : "OFFLINE",
    };
    mao_ui_device_update(&m);
}

static void pair(const mao_world_entry_t *w)
{
    mao_ui_device_feedback(MAO_UI_FB_PENDING);    /* a short persistence beat */
    const esp_err_t err = mao_rel_pair(w->id, w->name, w->device_type);
    if (err == ESP_OK) {
        s_note = NULL;
        ESP_LOGI(TAG, "'%s' is now part of MAO's setup", w->name);
        mao_ui_device_feedback(MAO_UI_FB_DONE);  /* the word resolves into the device's controls */
        mao_audio_confirm();
        mao_led_pulse(MAO_LED_PULSE_CONFIRM);
    } else {
        s_note = err == ESP_ERR_NO_MEM ? "FULL" : "NOT SAVED";
        s_note_dev = w->id;
        mao_ui_device_feedback(MAO_UI_FB_FAILED);
        mao_audio_back();
    }
    mao_app_dev_refresh();
}

bool mao_rel_page_input(mao_devpage_t kind, const mao_world_entry_t *w, const mao_event_t *ev, bool guarded)
{
    if (ev->type == MAO_EVENT_INPUT_LONG_PRESS) {
        mao_app_go_devices();                    /* BACK, as on every device page */
        return true;
    }
    if (kind != MAO_DEVPAGE_NEW) {
        return kind == MAO_DEVPAGE_NONE;         /* nothing to operate on a vanished device */
    }
    switch (ev->type) {
    case MAO_EVENT_INPUT_PRESS:
        mao_ui_device_feedback(MAO_UI_FB_PRESS);
        break;
    case MAO_EVENT_INPUT_RELEASE:
        mao_ui_device_feedback(MAO_UI_FB_REST);
        break;
    case MAO_EVENT_INPUT_CLICK:
        if (guarded) {
            ESP_LOGI(TAG, "click ignored: page just opened");
        } else {
            pair(w);
        }
        break;
    default:
        break;                                   /* one word: the dial has nothing to move */
    }
    return true;
}
