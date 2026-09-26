/* On-target link crypto self-test and micro-benchmarks (odd_link_selftest.c). */
#pragma once

/* Returns the number of passed checks; *total receives the number run. */
int odl_selftest(int *total);
/* Logs X25519 / HKDF / HMAC timings. */
void odl_bench(void);
