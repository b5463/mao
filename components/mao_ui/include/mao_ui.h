/*
 * MAO user interface: renders views chosen by the application.
 *
 * mao_ui owns LVGL objects and transitions only. It holds no application
 * state: which view is active, which menu entry is selected etc. are decided
 * by mao_app and passed in. All functions take the display lock internally
 * and may be called from any task (not from ISRs).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAO_VIEW_INTRO = 0,      /* first encounter: "MAO / TURN" */
    MAO_VIEW_HOME,
    MAO_VIEW_MENU,
    MAO_VIEW_PLACEHOLDER,    /* a menu entry's (future) page */
    MAO_VIEW_DEVICES,        /* nearby ODD devices */
    MAO_VIEW_DEVICE,         /* generic control view for one device */
} mao_view_t;

/* Menu entries (index into the shell). */
#define MAO_MENU_DEVICES 0

#define MAO_UI_DEVICES_MAX 8

/* What the DEVICES list shows (built by the app from the registry). */
typedef struct {
    int count;
    const char *name[MAO_UI_DEVICES_MAX];
    bool online[MAO_UI_DEVICES_MAX];
    int selected;
} mao_ui_devices_t;

/* What the device view shows (built by the app from capabilities). */
typedef struct {
    const char *title;
    bool has_level;
    int32_t level;
    bool has_toggle;
    bool on;
    bool online;
    bool problem;        /* recent commands unconfirmed */
    bool described;      /* capabilities/state known */
} mao_ui_device_t;

/* Build all views with initial_view visible. Call before mao_display_start()
 * so the first frame the panel shows is already correct. */
esp_err_t mao_ui_init(mao_view_t initial_view);

/* HOME boot sequence: the "MAO" wordmark gives way to the character. */
void mao_ui_boot(void);

/* Back from deep sleep: call before mao_display_start() so the first frame
 * has no wordmark ... */
void mao_ui_skip_wordmark(void);
/* ... then, instead of mao_ui_boot(), the eyes simply open again. */
void mao_ui_resume(void);

/* Fatal hardware fault: wordmark plus a service code (e.g. "SERVICE 02"),
 * no character. */
void mao_ui_fault(const char *code);

/* Transition from the current view to view. menu_index selects the menu
 * entry (MENU) or names the page (PLACEHOLDER). */
void mao_ui_show(mao_view_t view, int menu_index);

/* Menu: move the selection (animated, retargets continuously). */
void mao_ui_menu_select(int index);

/* Menu: the dial pushed past an end (small elastic nudge). */
void mao_ui_menu_bump(int direction);

/* Device views: push content (call before or after mao_ui_show). */
void mao_ui_devices_update(const mao_ui_devices_t *model);
void mao_ui_devices_bump(int direction);
void mao_ui_device_update(const mao_ui_device_t *model);

int mao_ui_menu_count(void);
const char *mao_ui_menu_label(int index);
const char *mao_ui_view_name(mao_view_t view);

#ifdef __cplusplus
}
#endif
