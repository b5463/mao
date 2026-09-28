/*
 * ODD device contract evaluation (see odd_contract.h).
 */
#include "odd_contract.h"

bool odd_contract_understood(const odd_capability_t *c)
{
    if (!odd_cap_type_known(c->type)) {
        return false;
    }
    if (c->type == ODD_CAP_ACTION) {
        const int32_t s = odd_action_semantic_of(c);
        return s == ODD_ACTION_IDENTIFY || s == ODD_ACTION_CAPTURE || s == ODD_ACTION_SYNC_TEST;
    }
    return true;
}

/* What a record means to the controller. Two usable records must never mean
 * the same thing: MAO would have to guess which one to drive. */
static int32_t semantic_key(const odd_capability_t *c)
{
    return c->type == ODD_CAP_ACTION ? 0x100 + odd_action_semantic_of(c) : c->type;
}

void odd_contract_evaluate(const odd_caps_msg_t *caps, odd_contract_eval_t *out)
{
    *out = (odd_contract_eval_t) { 0 };
    if (caps->major != ODD_CONTRACT_MAJOR) {
        out->compat = ODD_COMPAT_INCOMPATIBLE;
        return;
    }
    out->newer_minor = caps->minor > ODD_CONTRACT_MINOR;
    uint8_t candidate = 0;
    for (int i = 0; i < caps->count && i < ODD_MAX_CAPS; i++) {
        const odd_capability_t *c = &caps->cap[i];
        if (!odd_contract_understood(c)) {
            out->unknown++;
        } else if (c->type == ODD_CAP_LEVEL && c->max <= c->min) {
            out->unusable++;                 /* a level with nowhere to go */
        } else {
            candidate |= (uint8_t)(1u << i);
        }
    }
    /* Ambiguity is decided on the whole set, never by packet order. */
    for (int i = 0; i < caps->count && i < ODD_MAX_CAPS; i++) {
        if (!(candidate & (1u << i))) {
            continue;
        }
        bool repeated = false;
        for (int j = 0; j < caps->count && j < ODD_MAX_CAPS; j++) {
            repeated = repeated || (j != i && (candidate & (1u << j)) &&
                                    semantic_key(&caps->cap[j]) == semantic_key(&caps->cap[i]));
        }
        if (repeated) {
            out->ambiguous++;
        } else {
            out->usable |= (uint8_t)(1u << i);
        }
    }
    const bool complete = out->unknown == 0 && out->ambiguous == 0 && out->unusable == 0;
    out->compat = complete && !out->newer_minor && out->usable ? ODD_COMPAT_COMPATIBLE : ODD_COMPAT_LIMITED;
}

const char *odd_compat_name(odd_compat_t c)
{
    static const char *const names[] = { "UNKNOWN", "DESCRIBING", "COMPATIBLE", "LIMITED", "INCOMPATIBLE",
                                         "INVALID" };
    return (unsigned)c < sizeof(names) / sizeof(names[0]) ? names[c] : "?";
}
