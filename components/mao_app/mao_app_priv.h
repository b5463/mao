/* Private to mao_app: the authoritative application state. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_events.h"
#include "mao_ui.h"
#include "mao_world.h"

/* Dial speed classes: used by the app for sound thinning and logs. The
 * character estimates speed continuously on its own. */
typedef enum {
    MAO_DIAL_STILL = 0,
    MAO_DIAL_SLOW,
    MAO_DIAL_NORMAL,
    MAO_DIAL_FAST,
    MAO_DIAL_VERY_FAST,
} mao_dial_speed_t;

typedef struct {
    mao_view_t view;
    int menu_index;          /* selected menu entry (also names the open page) */
    int devices_index;       /* selected row in DEVICES */
    uint64_t device_id;      /* ODD id of the device open in DEVICE (0 = none) */
    int32_t dial_position;   /* cumulative signed detents since boot (unbounded) */
    bool awake;              /* false while sleepy */
    bool interacting;        /* knob touched since the last idle timeout */
    int64_t last_input_us;
} mao_app_state_t;

/* Result of feeding one dial event to the velocity estimator. */
typedef struct {
    mao_dial_speed_t speed;
    bool reversing;
    float detents_per_s;
} mao_dial_motion_t;

void mao_state_init(mao_view_t initial_view);
const mao_app_state_t *mao_state(void);

void mao_state_set_view(mao_view_t view);
void mao_state_set_menu_index(int index);
void mao_state_set_devices_index(int index);
void mao_state_set_device(uint64_t id);
void mao_state_set_awake(bool awake);
void mao_state_note_input(int64_t now_us);
void mao_state_note_idle(void);
uint32_t mao_state_idle_ms(int64_t now_us);

/* Record a dial event and classify the current motion. */
mao_dial_motion_t mao_state_dial(int32_t detents, int64_t now_us);

const char *mao_dial_speed_name(mao_dial_speed_t speed);

/* ---------------------------------------------------------------------- */
/* Transfer (mao_app_transfer.c): MAO's connection state machine.         */
/* All functions run in the dispatcher task.                              */
/* ---------------------------------------------------------------------- */

bool mao_transfer_active(void);
/* Input while a transfer is running. Returns true when consumed. */
bool mao_transfer_input(const mao_event_t *ev);
/* MAO_EVENT_TRANSFER_STEP; value = transfer id (stale steps are ignored). */
void mao_transfer_step(int32_t id);
/* "mao transfer <...>": mao_transfer_cmd_t. */
void mao_transfer_devcmd(int cmd);
/* The DEVICE page's CONNECT gesture (targets the open device). */
void mao_transfer_connect(void);
/* A reachability probe was answered (MAO_EVENT_DEVICE_PROBED). */
void mao_transfer_probed(int registry_idx);
/* A device went offline; never strand the character on the far side. */
void mao_transfer_device_lost(uint64_t id);

/* Defined in mao_app.c for the transfer module: wake and show HOME. */
void mao_app_go_home(void);

/* ---------------------------------------------------------------------- */
/* Device relationships on the DEVICE page (mao_app_rel.c).               */
/* ---------------------------------------------------------------------- */

typedef enum {
    MAO_DEVPAGE_NONE = 0,    /* neither known nor heard (e.g. just forgotten while away) */
    MAO_DEVPAGE_CONTROL,     /* KNOWN and online: the normal device page */
    MAO_DEVPAGE_NEW,         /* DISCOVERED only: name, NEW, PAIR - no remote control */
    MAO_DEVPAGE_OFFLINE,     /* KNOWN but not reachable: name, OFFLINE, CONNECT - no stale controls */
    MAO_DEVPAGE_VERIFY,      /* remembered (M3.0) but never secured, heard: VERIFY - no controls */
    MAO_DEVPAGE_REPAIR,      /* paired, heard, cannot prove the stored identity: REPAIR - no controls */
    MAO_DEVPAGE_INCOMPATIBLE, /* M4.0: paired, proven, online - but MAO cannot operate this
                               * description (another contract major, or invalid): no controls */
} mao_devpage_t;

mao_devpage_t mao_devpage(uint64_t id, mao_world_entry_t *w);
/* The page opened, or its kind changed under the user: focus settles. */
void mao_rel_page_reset(void);
/* Draw a relationship page (no-op for CONTROL). */
void mao_rel_page_draw(mao_devpage_t kind, const mao_world_entry_t *w);
/* Input on a relationship page; true when consumed. guarded: a click inside
 * the page-entry guard. */
bool mao_rel_page_input(mao_devpage_t kind, const mao_world_entry_t *w, const mao_event_t *ev, bool guarded);

/* Relationship management on a KNOWN device page. Layout A: a quiet FORGET
 * word after CONNECT opens the confirmation directly. Layout B (default):
 * a quiet INFO word opens a details sheet; FORGET lives there. */
const char *mao_rel_word(void);
void mao_rel_word_activate(const mao_world_entry_t *w);
bool mao_rel_sheet_open(void);
void mao_rel_sheet_input(const mao_world_entry_t *w, const mao_event_t *ev);
void mao_rel_sheet_draw(const mao_world_entry_t *w);
void mao_rel_init_dev(void);
/* MAO_EVENT_LINK_CHANGED: ceremony outcomes, revocation completion. */
void mao_rel_on_link_event(int32_t what);

/* Defined in mao_app.c for mao_app_rel.c. */
void mao_app_dev_refresh(void);
void mao_app_go_devices(void);

/* Holding on a device page (M4.1): a long press, or turning while held,
 * brings MAO's options up - BACK in the middle, the right option when turned
 * right, the left one when turned left; letting go chooses. Every device
 * page (control or relationship) feeds its input through this first. */
typedef enum {
    MAO_HOLD_PASS = 0,   /* not the hold's: the page handles the event */
    MAO_HOLD_EATEN,      /* the hold took it (the options moved): redraw */
    MAO_HOLD_BACK,       /* let go in the middle */
    MAO_HOLD_RIGHT,      /* let go on the right option */
    MAO_HOLD_LEFT,       /* let go on the left option */
} mao_hold_t;
mao_hold_t mao_app_hold(const mao_event_t *ev, bool has_right, bool has_left);
bool mao_app_hold_menu(int8_t *sel);   /* the options are up (sel: -1 BACK, 0 right, 1 left) */
void mao_app_hold_reset(void);
