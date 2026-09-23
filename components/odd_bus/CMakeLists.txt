# ODD BUS: shared by MAO and every ODD JOBS device firmware (e.g.
# devices/lamp_01_test). Must not depend on any MAO component.
idf_component_register(
    SRCS "odd_bus.c" "odd_codec.c" "odd_discovery.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_hw_support
)
