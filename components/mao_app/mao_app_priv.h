/* Private to mao_app: the authoritative application state. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_character.h"
#include "mao_percept.h"
#include "mao_power.h"
#include "mao_ui.h"

typedef struct {
    mao_view_t view;
    int menu_index;          /* selected menu entry (also names the open page) */
    int devices_index;       /* selected row in DEVICES */
    uint64_t device_id;      /* ODD id of the device open in DEVICE (0 = none) */
    int32_t dial_position;   /* cumulative signed detents since boot (unbounded) */
    bool awake;              /* false while sleepy */
    bool interacting;        /* knob touched since the last idle timeout */
    int64_t last_input_us;   /* input or presence (someone was with MAO) */
    /* What MAO makes of its surroundings and its mood (from percepts). */
    bool room_dark;
    bool room_quiet;
    bool upside_down;
    uint8_t annoyance;       /* 0 calm, 1 suspicious, 2 tsk, 3 cross */
    int64_t grudge_until_us; /* lingering after being properly annoyed */
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

void mao_state_set_room_dark(bool dark);
void mao_state_set_room_quiet(bool quiet);
void mao_state_set_upside_down(bool upside);
void mao_state_set_annoyance(uint8_t level);
void mao_state_set_grudge_until(int64_t until_us);

/* Record a dial event and classify the current motion. */
mao_dial_motion_t mao_state_dial(int32_t detents, int64_t now_us);

const char *mao_dial_speed_name(mao_dial_speed_t speed);

/* mao_app.c: shared by the behaviour files (dispatcher task only). */
void mao_app_wake(void);
void mao_app_go_sleepy(void);
void mao_app_note_activity(int64_t now_us);
void mao_app_refresh_brightness(void);

/* mao_app_percept.c */
void mao_app_percept_init(void);
void mao_app_on_percept(const mao_percept_msg_t *m, int64_t now_us);

/* mao_app_power.c */
/* Starts the policy tick; returns true if a quiet (dark) housekeeping wake
 * was accepted. */
bool mao_app_power_init(bool quiet_wake, int64_t now_us);
void mao_app_power_tick(int64_t now_us);
void mao_app_power_activity(int64_t now_us);
/* Returns the previous state. */
mao_power_state_t mao_app_power_on_state(int32_t value, int64_t now_us);
bool mao_app_power_quiet(void);
void mao_app_power_end_quiet(void);
void mao_app_power_hold(int64_t until_us);
