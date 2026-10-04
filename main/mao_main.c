/*
 * MAO firmware entry point. Bring-up sequencing only; behaviour lives in
 * mao_app and the components below it.
 *
 * Every subsystem reports [OK] / [!!] / [--] and a failure never stops the
 * boot: MAO comes up with whatever works (brief section 44). Hardware a board does not have is
 * reported "[--] ... not fitted".
 */
#include "mao_system.h"
#include "mao_board.h"
#include "mao_display.h"
#include "mao_input.h"
#include "mao_audio.h"
#include "mao_haptics.h"
#include "mao_led.h"
#include "mao_power.h"
#include "mao_sense.h"
#include "mao_ir.h"
#include "mao_app.h"
#include "mao_devices.h"
#include "mao_perception.h"
#include "mao_selftest.h"

void app_main(void)
{
    mao_system_init();
    /* Board first: on the A0 this also brings up the I2C bus and the rail
     * expander with every rail off, which the drivers below rely on. */
    mao_system_report("board", mao_board_init());

    /* LED first so the "booting" indication covers display bring-up. On the
     * LCDkit GPIO8 is a strapping pin, but by app_main the straps have long
     * been latched. The A0 has no LED. */
    mao_system_report_optional("LED", mao_led_init());
    mao_system_report("display", mao_display_init());
    mao_system_report("input", mao_input_init());
    mao_system_report("audio", mao_audio_init());
    mao_system_report_optional("haptics", mao_haptics_init());
    /* Power before sense: battery state and the deep-sleep continuity
     * record (why and how long MAO slept) exist before the sensors start. */
    mao_system_report_optional("power", mao_power_init());
    mao_system_report_optional("sense", mao_sense_init());
    mao_ir_init();
    /* Radio + ODD BUS. Before the app, so device events have a listener
     * as soon as the dispatcher starts. */
    mao_system_report("devices", mao_devices_init());

    /* Perception: observations and input -> percepts for the app. Every
     * board runs it; without sensors it works from the dial and press. */
    mao_system_report("perception", mao_perception_init());
    /* Boot check over everything reported above (first boot of a board,
     * hardware faults kept in NVS) and the "mao selftest" factory test. */
    mao_system_report("self-test", mao_selftest_init());

    /* Builds the first view and lights the panel with it (or, after a
     * fatal hardware fault, a service code instead of the character). */
    mao_system_report("app", mao_app_init());
    mao_system_start();
}
