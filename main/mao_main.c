/*
 * MAO firmware entry point. Bring-up sequencing only; behaviour lives in
 * mao_app and the components below it.
 *
 * Every subsystem reports [OK] / [!!] / [--] and a failure never stops the
 * boot: MAO comes up with whatever works. Hardware a board does not have
 * (the LCDkit has no haptics, battery, sensors; the A1 has no LED) is
 * reported "[--] ... not fitted" and stays untouched.
 */
#include "mao_system.h"
#include "mao_board.h"
#include "mao_display.h"
#include "mao_input.h"
#include "mao_audio.h"
#include "mao_haptics.h"
#include "mao_led.h"
#include "mao_battery.h"
#include "mao_sense.h"
#include "mao_ir.h"
#include "mao_app.h"
#include "mao_devices.h"
#include "mao_rel.h"
#include "mao_link.h"
#include "mao_perception.h"
#include "mao_selftest.h"

void app_main(void)
{
    mao_system_init();
    /* Board first: on the A1 this also brings up the I2C bus with every
     * rail off, which the drivers below rely on. */
    mao_system_report("board", mao_board_init());

    /* LED first so the "booting" indication covers display bring-up. On the
     * LCDkit GPIO8 is a strapping pin, but by app_main the straps have long
     * been latched. The A1 has no LED. */
    mao_system_report_optional("LED", mao_led_init());
    mao_system_report("display", mao_display_init());
    mao_system_report("input", mao_input_init());
    mao_system_report("audio", mao_audio_init());
    mao_system_report_optional("haptics", mao_haptics_init());
    /* Battery before sense: the charge limit's temperature source (the IMU
     * die) registers with it as the sensors come up. */
    mao_system_report_optional("battery", mao_battery_init());
    mao_system_report_optional("sense", mao_sense_init());
    mao_ir_init();
    /* Known devices first: DEVICES can show them (offline) before the radio
     * has heard anything. Never blocks on the network. */
    mao_system_report("link security", mao_link_init());
    mao_system_report("relationships", mao_rel_init());
    /* Radio + ODD BUS. Before the app, so device events have a listener
     * as soon as the dispatcher starts. */
    mao_system_report("devices", mao_devices_init());

    /* Perception: observations and input -> percepts for the app (boards
     * with sensors). */
    mao_system_report_optional("perception", mao_perception_init());
    /* Boot check over everything reported above (boards with MAO hardware)
     * and the "mao selftest" factory test. */
    mao_system_report_optional("self-test", mao_selftest_init());

    /* Builds the first view and lights the panel with it (or, after a fatal
     * hardware fault, the service code instead of the character). */
    mao_system_report("app", mao_app_init());
    mao_system_start();
}
