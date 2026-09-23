/*
 * MAO firmware entry point. Bring-up sequencing only; behaviour lives in
 * mao_app and the components below it.
 */
#include "mao_system.h"
#include "mao_board.h"
#include "mao_display.h"
#include "mao_input.h"
#include "mao_audio.h"
#include "mao_led.h"
#include "mao_ir.h"
#include "mao_app.h"

void app_main(void)
{
    mao_system_init();
    mao_system_report("board", mao_board_init());

    /* LED first so the "booting" indication covers display bring-up. GPIO8 is
     * a strapping pin, but by app_main the straps have long been latched. */
    mao_system_report("LED", mao_led_init());
    mao_system_report("display", mao_display_init());
    mao_system_report("input", mao_input_init());
    mao_system_report("audio", mao_audio_init());
    mao_ir_init();

    /* Builds the first view and lights the panel with it. */
    mao_system_report("app", mao_app_init());
    mao_system_start();
}
