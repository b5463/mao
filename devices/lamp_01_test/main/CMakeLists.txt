idf_component_register(
    SRCS "lamp_main.c" "lamp_output.c" "lamp_console.c" "lamp_link.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES odd_bus odd_link esp_wifi esp_event nvs_flash esp_timer esp_driver_ledc esp_driver_uart
                  esp_hal_usb led_strip
)
