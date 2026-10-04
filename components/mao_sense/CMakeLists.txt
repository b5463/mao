idf_component_register(
    SRCS "mao_sense.c" "mao_sense_imu.c" "mao_sense_tof.c" "mao_sense_tof_platform.c"
         "mao_sense_als.c" "mao_sense_touch.c" "mao_sense_mic.c"
         "vl53l4cd/vl53l4cd_api.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES mao_board freertos
    PRIV_REQUIRES mao_power mao_system esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_touch_sens
                  esp_timer
)
