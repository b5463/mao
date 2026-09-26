# ODD link security: shared by MAO and every ODD JOBS device firmware, like
# odd_bus. Must not depend on any MAO component. See docs/link_security.md.
idf_component_register(
    SRCS "odd_link_crypto_psa.c" "odd_link_kdf.c" "odd_link_frame.c" "odd_link_env.c" "odd_link_pair.c" "odd_link_selftest.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mbedtls esp_timer log
)
