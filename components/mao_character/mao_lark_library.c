/*
 * Library registry: the parts are concatenated into one index space.
 * Index 0 ("neutral", first in kLarkDaily) is the resting state.
 */
#include "mao_lark_author.h"

typedef struct {
    const lark_state_t *states;
    const int *count;
} part_t;

static const part_t kParts[] = {
    { kLarkDaily, &kLarkDailyCount },
    { kLarkMoods, &kLarkMoodsCount },
    { kLarkLife, &kLarkLifeCount },
    { kLarkCat, &kLarkCatCount },
    { kLarkMore, &kLarkMoreCount },
    { kLarkCtrl, &kLarkCtrlCount },
};

const lark_state_t *mao_lark_state(int i)
{
    if (i >= 0) {
        for (size_t p = 0; p < sizeof(kParts) / sizeof(kParts[0]); p++) {
            if (i < *kParts[p].count) {
                return &kParts[p].states[i];
            }
            i -= *kParts[p].count;
        }
    }
    return &kLarkDaily[0];
}

int mao_lark_state_count(void)
{
    int n = 0;
    for (size_t p = 0; p < sizeof(kParts) / sizeof(kParts[0]); p++) {
        n += *kParts[p].count;
    }
    return n;
}
