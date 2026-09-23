/* Private to mao_app: the authoritative application state. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mao_character.h"
#include "mao_ui.h"

typedef struct {
    mao_view_t view;
    int menu_index;          /* selected menu entry (also names the open page) */
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
void mao_state_set_awake(bool awake);
void mao_state_note_input(int64_t now_us);
void mao_state_note_idle(void);
uint32_t mao_state_idle_ms(int64_t now_us);

/* Record a dial event and classify the current motion. */
mao_dial_motion_t mao_state_dial(int32_t detents, int64_t now_us);

const char *mao_dial_speed_name(mao_dial_speed_t speed);
