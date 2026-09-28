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
    MAO_VIEW_DEVICES,        /* nearby ODD devices */
    MAO_VIEW_DEVICE,         /* generic control view for one device */
} mao_view_t;

/* Semantic menu entry identities. Navigation decisions use these, never a
 * positional index: the menu can be reordered without breaking anything. */
typedef enum {
    MAO_MENU_ID_DEVICES = 0,
    MAO_MENU_ID_ACTIONS,
    MAO_MENU_ID_TOOLS,
    MAO_MENU_ID_SETUP,
} mao_menu_id_t;

/* The semantic id of the menu entry at a display position. */
mao_menu_id_t mao_ui_menu_id(int index);

#define MAO_UI_DEVICES_MAX 8

/* What the DEVICES list shows (built by the app from the registry). */
typedef struct {
    int count;
    const char *name[MAO_UI_DEVICES_MAX];
    bool online[MAO_UI_DEVICES_MAX];
    bool known[MAO_UI_DEVICES_MAX];     /* part of MAO's setup (else merely nearby: NEW) */
    uint8_t note[MAO_UI_DEVICES_MAX];   /* 0 none, 1 VERIFY (remembered, not yet secured), 2 NOT VERIFIED,
                                         * 3 INCOMPATIBLE, 4 INVALID (M4.0: trusted, not operable) */
    int selected;
} mao_ui_devices_t;

/* What the device view shows (built by the app from capabilities). */
#define MAO_UI_DEVICE_WORDS 3

/* What the device view shows, built by the app from capabilities alone.
 * The centre is the level value or the device's primary action word; the
 * words row holds the other controls; the facts line (READY / STORAGE...)
 * is status, never focusable. Focus 0 = centre, 1.. = words[focus - 1]. */
typedef struct {
    const char *title;
    bool has_level;
    int32_t level;
    bool has_toggle;
    bool on;
    bool online;
    bool problem;        /* recent commands unconfirmed */
    bool described;      /* capabilities/state known */
    const char *primary; /* centre word when there is no level value (else NULL) */
    const char *words[MAO_UI_DEVICE_WORDS];   /* other controls, left to right */
    int8_t word_count;
    const char *status_l;    /* read-only facts line (NULL = none); a lone fact is centred */
    const char *status_r;
    bool status_r_emph;      /* the fact deserves more presence (e.g. storage running low) */
    int8_t focus;            /* 0 centre, 1..word_count words, word_count + 1 = CONNECT */
    bool editing;        /* LEVEL edit: the dial changes the value */
    bool connect_hidden;     /* no CONNECT on this page (a device not yet part of the setup) */
    bool no_centre;          /* no live control to show: the status word takes the centre */
    const char *status_c;    /* with no_centre: that word (NULL = the reachability word) */
    const char *rel_word;    /* quiet relationship word below CONNECT (NULL = none);
                              * focus word_count + 2 */
} mao_ui_device_t;

/* A sheet over the device page: the page recedes, a question or a detail
 * appears with at most two words. Plain typography, near-black, no card. */
typedef struct {
    bool on;
    bool hide_title;         /* a question replaces the page heading */
    const char *line1;       /* small, quiet */
    const char *line2;       /* the subject */
    bool line2_big;          /* large type (a pairing code to compare) */
    const char *words[2];
    int8_t word_count;
    int8_t focus;            /* index into words */
} mao_ui_sheet_t;
void mao_ui_device_sheet(const mao_ui_sheet_t *sheet);

/* Tool feedback on the centre word: the word itself answers the physical
 * button, before and regardless of the character (no network latency). */
typedef enum {
    MAO_UI_FB_REST = 0,      /* stable */
    MAO_UI_FB_PRESS,         /* button down: the word compresses a few px */
    MAO_UI_FB_PENDING,       /* sent: held slightly low, motionless */
    MAO_UI_FB_DONE,          /* released: tiny overshoot, settle */
    MAO_UI_FB_BUSY,          /* could not move: barely yields, springs back */
    MAO_UI_FB_FAILED,        /* small lateral misalignment, settles */
} mao_ui_fb_t;
void mao_ui_device_feedback(mao_ui_fb_t fb);

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

/* Device views: push content (call before or after mao_ui_show). */
void mao_ui_devices_update(const mao_ui_devices_t *model);
void mao_ui_devices_bump(int direction);
void mao_ui_device_update(const mao_ui_device_t *model);
/* CONNECT word emphasis 0..1 (arming / starting; typography only). */
void mao_ui_device_connect_hot(float v);

/* Away (transfer): MAO is conceptually on another device. The screen goes
 * sparse near-black with only a very dim, slowly pulsing seam at the edge it
 * left through ((dx, dy) logical, one non-zero). MAO's absence is the state:
 * no CONNECTED text, no icons. */
void mao_ui_away(bool on, int dx, int dy);
/* The dial turned while away: the seam stirs 1-2 px (still alive). */
void mao_ui_away_nudge(int direction);

/* Development: replay the HOME boot sequence (wordmark -> eyes). */
void mao_ui_debug_replay_boot(void);

int mao_ui_menu_count(void);
const char *mao_ui_menu_label(int index);
const char *mao_ui_view_name(mao_view_t view);

#ifdef __cplusplus
}
#endif
