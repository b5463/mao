idf_component_register(
    SRCS "mao_sense.c" "mao_sense_imu.c" "mao_sense_tof.c" "mao_sense_tof_platform.c"
         "vl53l4cd/vl53l4cd_api.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES freertos
    PRIV_REQUIRES mao_board mao_battery mao_system esp_driver_gpio esp_driver_i2c esp_timer esp_rom
)
