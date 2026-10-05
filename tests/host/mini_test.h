/* Minimal host test harness for MAO's platform-independent logic. */
#pragma once

#include <stdio.h>

extern int g_checks;
extern int g_failures;
extern const char *g_current_test;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        g_checks++;                                                                      \
        if (!(cond)) {                                                                   \
            g_failures++;                                                                \
            printf("    FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, g_current_test, #cond); \
        }                                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        const long long va_ = (long long)(a), vb_ = (long long)(b);                     \
        g_checks++;                                                                      \
        if (va_ != vb_) {                                                                \
            g_failures++;                                                                \
            printf("    FAIL %s:%d [%s]: %s == %s (%lld != %lld)\n", __FILE__, __LINE__,  \
                   g_current_test, #a, #b, va_, vb_);                                    \
        }                                                                                \
    } while (0)

typedef void (*test_fn_t)(void);

void run_test(const char *name, test_fn_t fn);
#define RUN(fn) run_test(#fn, fn)

/* Suites */
void suite_percept(void);
void suite_policy(void);
void suite_backlight(void);
