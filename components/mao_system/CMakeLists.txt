idf_component_register(
    SRCS "mao_system.c" "mao_events.c" "mao_settings.c" "mao_devcmd.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_timer spi_flash esp_hw_support esp_app_format nvs_flash esp_hal_usb
                  esp_driver_usb_serial_jtag
)
