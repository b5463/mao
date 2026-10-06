# Perception: the platform-independent engine (core/, host-tested in
# tests/host) plus the ESP-IDF glue.
idf_component_register(
    SRCS "mao_perception.c" "core/percept_engine.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "core"
    REQUIRES mao_system
    PRIV_REQUIRES mao_sense mao_battery mao_audio mao_haptics mao_ir esp_timer freertos
)
