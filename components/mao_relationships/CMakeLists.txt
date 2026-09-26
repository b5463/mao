idf_component_register(
    SRCS "mao_rel_table.c" "mao_rel.c" "mao_world.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES odd_bus mao_link
    PRIV_REQUIRES odd_link mao_devices mao_system nvs_flash esp_timer
)
