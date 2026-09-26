idf_component_register(
    SRCS "mao_link.c"
    INCLUDE_DIRS "include"
    REQUIRES odd_link
    PRIV_REQUIRES mao_system log
)
