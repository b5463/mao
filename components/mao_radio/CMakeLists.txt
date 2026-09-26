idf_component_register(
    SRCS "mao_radio.c" "mao_espnow.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_event mao_system odd_link
)
