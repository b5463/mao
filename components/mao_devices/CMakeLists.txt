idf_component_register(
    SRCS "mao_devices.c" "mao_device_view.c"
    INCLUDE_DIRS "include"
    REQUIRES odd_bus
    PRIV_REQUIRES mao_radio mao_link mao_system esp_timer
)
