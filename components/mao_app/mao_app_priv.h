/* Private to mao_app: the authoritative application state. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_events.h"
#include "mao_ui.h"

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
