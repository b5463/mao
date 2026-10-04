/*
 * MAO host unit tests: the perception engine and the power policy, which
 * are plain C with no ESP-IDF dependency. Run with tests/host/run.sh.
 */
#include "mini_test.h"

int g_checks;
int g_failures;
const char *g_current_test = "";

void run_test(const char *name, test_fn_t fn)
{
    const int before = g_failures;
    g_current_test = name;
    fn();
    printf("  %-44s %s\n", name, g_failures == before ? "ok" : "FAILED");
}

int main(void)
{
    printf("perception engine\n");
    suite_percept();
    printf("power policy\n");
    suite_policy();
    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    printf("%s\n", g_failures ? "HOST TESTS FAILED" : "HOST TESTS PASSED");
    return g_failures ? 1 : 0;
}
