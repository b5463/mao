idf_component_register(
    SRCS "mao_app.c" "mao_app_home.c" "mao_hold.c" "mao_fiddle.c" "mao_quirks.c" "mao_state.c" "mao_app_transfer.c" "mao_app_rel.c" "mao_app_power.c" "mao_app_power_dev.c" "mao_power.c" "mao_app_percept.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_system mao_ui mao_character mao_display mao_input mao_audio mao_led mao_devices mao_relationships mao_link mao_radio mao_board mao_battery mao_haptics mao_ir mao_perception mao_selftest mao_sense esp_timer esp_hw_support esp_hal_wdt esp_driver_usb_serial_jtag
)
