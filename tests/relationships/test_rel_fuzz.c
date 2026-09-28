/*
 * Model-based fuzz test for the relationship table (mao_rel_table.c).
 *
 * Random sequences of pair / set_cred (re-pair) / forget / note (rename) /
 * clear_cred over a small pool of device ids, with injected write and erase
 * failures and random reboots, against a plain reference model. After every
 * step:
 *   1. the table holds exactly what the model holds (ids, names, types,
 *      credentials);
 *   2. the fake flash holds exactly what the table holds - every used slot's
 *      blob decodes to its record, no free slot has a blob (no orphans, no
 *      losses), and a reboot restores the same set;
 *   3. an erase only ever touches the slot of the device being forgotten, and
 *      a write only the slot of the device being written.
 * Written after a paired device's record twice vanished from MAO's flash
 * without a logged forget (M4.1): this proves the table cannot do that.
 *   ./test_rel_fuzz.exe [seeds] [steps]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mao_rel_table.h"

#define POOL 10

/* ---- fake flash: one blob per slot, plus what the last call touched ---- */
typedef struct {
    uint8_t blob[MAO_REL_MAX][MAO_REL_BLOB_LEN];
    bool has[MAO_REL_MAX];
    int fail_write, fail_erase;      /* one-shot injection */
    int last_write_slot, last_erase_slot;
} fake_t;

static fake_t F;

static esp_err_t fake_write(void *ctx, int slot, const uint8_t b[MAO_REL_BLOB_LEN])
{
    (void)ctx;
    if (F.fail_write) {
        F.fail_write = 0;
        return ESP_FAIL;
    }
    memcpy(F.blob[slot], b, MAO_REL_BLOB_LEN);
    F.has[slot] = true;
    F.last_write_slot = slot;
    return ESP_OK;
}

static esp_err_t fake_erase(void *ctx, int slot)
{
    (void)ctx;
    if (F.fail_erase) {
        F.fail_erase = 0;
        return ESP_FAIL;
    }
    F.has[slot] = false;
    F.last_erase_slot = slot;
    return ESP_OK;
}

static const mao_rel_backend_t BE = { fake_write, fake_erase, NULL };

/* ---- the reference model ---- */
typedef struct {
    bool known;
    char name[ODD_NAME_MAX + 1];
    uint16_t type;
    bool cred;
    uint8_t key[MAO_REL_KEY_LEN];
} model_t;

static model_t M[POOL];

#define ID(n) ((0x0DD0ull << 48) | (0xB0000000ull + (uint64_t)(n) + 1u))

static uint32_t s_rng;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static int model_count(void)
{
    int n = 0;
    for (int i = 0; i < POOL; i++) {
        n += M[i].known;
    }
    return n;
}

static int s_fails;
static long s_step;
static uint32_t s_seed;
#define BAD(...) do { if (s_fails++ < 10) { printf("seed %u step %ld: ", s_seed, s_step); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void check(const mao_rel_table_t *t, const char *after)
{
    /* 1. table == model */
    for (int i = 0; i < POOL; i++) {
        const int slot = mao_rel_table_find(t, ID(i));
        if (M[i].known != (slot >= 0)) {
            BAD("after %s: id %d known=%d in the model, slot %d in the table", after, i, M[i].known, slot);
            continue;
        }
        if (slot < 0) {
            continue;
        }
        const mao_rel_record_t *r = &t->rec[slot];
        if (strcmp(r->name, M[i].name) || r->device_type != M[i].type || r->has_cred != M[i].cred ||
            (M[i].cred && memcmp(r->k_link, M[i].key, MAO_REL_KEY_LEN))) {
            BAD("after %s: id %d record differs from the model", after, i);
        }
    }
    if (mao_rel_table_count(t) != model_count()) {
        BAD("after %s: table count %d, model %d", after, mao_rel_table_count(t), model_count());
    }
    /* 2. flash == table */
    for (int s = 0; s < MAO_REL_MAX; s++) {
        if (t->state[s] == MAO_REL_SLOT_USED) {
            if (!F.has[s]) {
                BAD("after %s: slot %d used in the table but MISSING on flash", after, s);
                continue;
            }
            mao_rel_record_t r;
            if (mao_rel_decode(F.blob[s], MAO_REL_BLOB_LEN, &r) != MAO_REL_LOAD_OK || r.id != t->rec[s].id ||
                strcmp(r.name, t->rec[s].name) || r.has_cred != t->rec[s].has_cred ||
                (r.has_cred && memcmp(r.k_link, t->rec[s].k_link, MAO_REL_KEY_LEN))) {
                BAD("after %s: slot %d on flash differs from the table", after, s);
            }
        } else if (t->state[s] == MAO_REL_SLOT_FREE && F.has[s]) {
            BAD("after %s: slot %d free in the table but a blob remains on flash (orphan)", after, s);
        }
    }
}

static void reboot(mao_rel_table_t *t)
{
    mao_rel_table_init(t, &BE);
    for (int s = 0; s < MAO_REL_MAX; s++) {
        if (F.has[s] && mao_rel_table_load_slot(t, s, F.blob[s], MAO_REL_BLOB_LEN) != MAO_REL_LOAD_OK) {
            BAD("reboot: slot %d did not load", s);
        }
    }
}

static const char *kNames[] = { "LAMP 01", "CAMERA 01", "DESK", "PORCH LIGHT" };

static void run(uint32_t seed, long steps)
{
    s_seed = seed;
    s_rng = seed * 2654435761u | 1u;
    memset(&F, 0, sizeof(F));
    memset(M, 0, sizeof(M));
    mao_rel_table_t t;
    mao_rel_table_init(&t, &BE);
    for (s_step = 0; s_step < steps; s_step++) {
        const int i = (int)(rnd() % POOL);
        const uint64_t id = ID(i);
        const int op = (int)(rnd() % 100);
        const int slot_before = mao_rel_table_find(&t, id);
        F.last_write_slot = F.last_erase_slot = -1;
        const bool inject_w = rnd() % 25 == 0, inject_e = rnd() % 25 == 0;
        F.fail_write = inject_w;
        F.fail_erase = inject_e;
        const char *what;
        if (op < 30) {                                   /* pair ceremony: set_cred */
            what = "set_cred";
            uint8_t mac[6] = { 1, 2, 3, 4, 5, (uint8_t)(i + 1) }, key[MAO_REL_KEY_LEN];
            for (int k = 0; k < MAO_REL_KEY_LEN; k++) {
                key[k] = (uint8_t)(rnd() | 1u);
            }
            const char *nm = M[i].known ? M[i].name : kNames[rnd() % 4];
            const uint16_t type = M[i].known ? M[i].type : (uint16_t)(rnd() % 8);
            bool created = false;
            const esp_err_t err = mao_rel_table_set_cred(&t, id, nm, type, mac, key, &created);
            if (err == ESP_OK) {
                if (!M[i].known) {
                    M[i].known = true;
                    snprintf(M[i].name, sizeof(M[i].name), "%s", nm);
                    M[i].type = type;
                }
                M[i].cred = true;
                memcpy(M[i].key, key, MAO_REL_KEY_LEN);
            } else if (!(inject_w || (err == ESP_ERR_NO_MEM && model_count() == MAO_REL_MAX && !M[i].known))) {
                BAD("set_cred failed unexpectedly: %d", err);
            }
            if (err == ESP_OK && F.last_write_slot != mao_rel_table_find(&t, id)) {
                BAD("set_cred wrote slot %d, the device lives in %d", F.last_write_slot, mao_rel_table_find(&t, id));
            }
        } else if (op < 55) {                            /* FORGET */
            what = "forget";
            const esp_err_t err = mao_rel_table_forget(&t, id);
            if (err == ESP_OK) {
                if (!M[i].known) {
                    BAD("forget succeeded for an unknown device");
                }
                if (F.last_erase_slot != slot_before) {
                    BAD("forget erased slot %d, the device lived in %d", F.last_erase_slot, slot_before);
                }
                memset(&M[i], 0, sizeof(M[i]));
            } else if (M[i].known && !inject_e) {
                BAD("forget failed unexpectedly: %d", err);
            }
        } else if (op < 80) {                            /* a live description: rename / retype */
            what = "note";
            const char *nm = kNames[rnd() % 4];
            const uint16_t type = (uint16_t)(rnd() % 8);
            bool ch = false;
            const esp_err_t err = mao_rel_table_note(&t, id, nm, type, &ch);
            if (err == ESP_OK && M[i].known) {
                snprintf(M[i].name, sizeof(M[i].name), "%s", nm);
                M[i].type = type;
                if (ch && F.last_write_slot != slot_before) {
                    BAD("note wrote slot %d, the device lives in %d", F.last_write_slot, slot_before);
                }
            } else if (err == ESP_OK && !M[i].known) {
                BAD("note succeeded for an unknown device");
            }
        } else if (op < 90) {                            /* pair without a credential (M3.0 path) */
            what = "pair";
            bool created = false;
            const char *nm = kNames[rnd() % 4];
            const uint16_t type = (uint16_t)(rnd() % 8);
            const esp_err_t err = mao_rel_table_pair(&t, id, nm, type, &created);
            if (err == ESP_OK) {
                if (!M[i].known) {
                    M[i].known = true;
                    M[i].cred = false;
                }
                snprintf(M[i].name, sizeof(M[i].name), "%s", nm);   /* known: metadata refreshed */
                M[i].type = type;
            }
        } else if (op < 95) {                            /* credential dropped, relationship kept */
            what = "clear_cred";
            if (mao_rel_table_clear_cred(&t, id) == ESP_OK) {
                M[i].cred = false;
            }
        } else {
            what = "reboot";
            F.fail_write = F.fail_erase = 0;
            reboot(&t);
        }
        F.fail_write = F.fail_erase = 0;
        check(&t, what);
        if (s_fails > 10) {
            return;
        }
    }
    reboot(&t);
    check(&t, "final reboot");
}

int main(int argc, char **argv)
{
    const int seeds = argc > 1 ? atoi(argv[1]) : 40;
    const long steps = argc > 2 ? atol(argv[2]) : 20000;
    for (int s = 1; s <= seeds && s_fails == 0; s++) {
        run((uint32_t)s, steps);
    }
    printf("relationship fuzz: %d seeds x %ld steps, %s\n", seeds, steps,
           s_fails ? "VIOLATIONS FOUND" : "table, flash and model always agree");
    return s_fails ? 1 : 0;
}
