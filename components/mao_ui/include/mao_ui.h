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
} mao_view_t;

/* Build all views with initial_view visible. Call before mao_display_start()
 * so the first frame the panel shows is already correct. */
esp_err_t mao_ui_init(mao_view_t initial_view);

/* HOME boot sequence: the "MAO" wordmark gives way to the character. */
void mao_ui_boot(void);

/* Transition from the current view to view. menu_index selects the menu
 * entry (MENU) or names the page (PLACEHOLDER). */
void mao_ui_show(mao_view_t view, int menu_index);

/* Menu: move the selection (animated, retargets continuously). */
void mao_ui_menu_select(int index);

/* Menu: the dial pushed past an end (small elastic nudge). */
void mao_ui_menu_bump(int direction);

int mao_ui_menu_count(void);
const char *mao_ui_menu_label(int index);
const char *mao_ui_view_name(mao_view_t view);

#ifdef __cplusplus
}
#endif
