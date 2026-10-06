idf_component_register(
    SRCS "mao_ir.c" "mao_ir_nec.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mao_board mao_system esp_driver_rmt esp_timer
)
