/*
 * MAO user interface: renders views chosen by the application.
 *
 * mao_ui owns LVGL objects and transitions only. It holds no application
 * state: the active view and selection are decided by mao_app and passed in.
 * All motion is spring-driven (mao_spring.h) on one shared 30 Hz tick, so
 * every transition is interruptible and retargets instantly.
 * All functions take the display lock internally; any task, never an ISR.
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

/* Build all views with initial_view visible (INTRO or HOME). Call before
 * mao_display_start() so the panel's first frame is already correct. */
esp_err_t mao_ui_init(mao_view_t initial_view);

/* HOME boot sequence: the "MAO" wordmark contracts into the eyes. */
void mao_ui_boot(void);

/* First encounter done: the prompt leaves towards the turn direction and
 * MAO appears there (direction -1 / +1). The view becomes HOME. */
void mao_ui_intro_exit(int direction);

/* Transition between HOME, MENU and PLACEHOLDER. menu_index is the selected
 * entry (MENU) or the entry whose page opens (PLACEHOLDER). */
void mao_ui_show(mao_view_t view, int menu_index);

/* Menu selection (spring-animated, retargets continuously). */
void mao_ui_menu_select(int index);
/* The dial pushed past an end: elastic resistance. */
void mao_ui_menu_bump(int direction);

/* Development: replay the HOME boot sequence (wordmark -> eyes). */
void mao_ui_debug_replay_boot(void);

int mao_ui_menu_count(void);
const char *mao_ui_menu_label(int index);
const char *mao_ui_view_name(mao_view_t view);

#ifdef __cplusplus
}
#endif
