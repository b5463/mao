/*
 * Host tests for the relationship table (components/mao_relationships/
 * mao_rel_table.c) against a fake backend with failure injection.
 *
 * PERSISTENCE / MODEL TESTS ONLY, with synthetic device_ids. They prove the
 * storage rules, not multi-radio behaviour (one physical endpoint exists).
 */
#include <stdio.h>
#include <string.h>
#include "mao_rel_table.h"

/* ---- fake "NVS": one blob per slot, survives a simulated reboot ---- */
typedef struct {
    uint8_t blob[MAO_REL_MAX][64];
    size_t len[MAO_REL_MAX];
    bool fail_write, fail_erase;
    int writes, erases;
} fake_t;

static esp_err_t fake_write(void *ctx, int slot, const uint8_t b[MAO_REL_BLOB_LEN])
{
    fake_t *f = ctx;
    if (f->fail_write) {
        return ESP_FAIL;
    }
    memcpy(f->blob[slot], b, MAO_REL_BLOB_LEN);
    f->len[slot] = MAO_REL_BLOB_LEN;
    f->writes++;
    return ESP_OK;
}

static esp_err_t fake_erase(void *ctx, int slot)
{
    fake_t *f = ctx;
    if (f->fail_erase) {
        return ESP_FAIL;
    }
    f->len[slot] = 0;
    f->erases++;
    return ESP_OK;
}

static fake_t F;
static const mao_rel_backend_t BE = { fake_write, fake_erase, &F };

/* Simulated reboot: a fresh table loaded from whatever the fake holds. */
static void reboot(mao_rel_table_t *t, int *bad)
{
    mao_rel_table_init(t, &BE);
    int b = 0;
    for (int i = 0; i < MAO_REL_MAX; i++) {
        if (F.len[i]) {
            b += mao_rel_table_load_slot(t, i, F.blob[i], F.len[i]) != MAO_REL_LOAD_OK;
        }
    }
    if (bad) {
        *bad = b;
    }
}

#define ID(n) ((0x0DD0ull << 48) | (0xA0000000ull + (n)))

static int s_fail, s_pass;
#define CHECK(cond) do { if (cond) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    mao_rel_table_t t;
    mao_rel_record_t list[MAO_REL_MAX];
    bool created, changed;
    int bad;

    /* 0 devices */
    memset(&F, 0, sizeof(F));
    reboot(&t, &bad);
    CHECK(mao_rel_table_count(&t) == 0 && bad == 0);
    CHECK(mao_rel_table_find(&t, ID(1)) < 0);
    CHECK(mao_rel_table_forget(&t, ID(1)) == ESP_ERR_NOT_FOUND);
    CHECK(mao_rel_table_note(&t, ID(1), "X", 6, &changed) == ESP_ERR_NOT_FOUND && F.writes == 0);

    /* 1 device: pair, lookup, survives reboot */
    CHECK(mao_rel_table_pair(&t, ID(1), "CAMERA 01", ODD_DEVICE_CAMERA, &created) == ESP_OK && created);
    CHECK(mao_rel_table_find(&t, ID(1)) >= 0 && F.writes == 1);
    reboot(&t, &bad);
    CHECK(mao_rel_table_count(&t) == 1 && bad == 0);
    CHECK(mao_rel_table_list(&t, list) == 1 && list[0].id == ID(1) && strcmp(list[0].name, "CAMERA 01") == 0 &&
          list[0].device_type == ODD_DEVICE_CAMERA);

    /* duplicate id: pair again is idempotent - no write, no second record, order kept */
    const uint32_t order1 = list[0].order;
    CHECK(mao_rel_table_pair(&t, ID(1), "CAMERA 01", ODD_DEVICE_CAMERA, &created) == ESP_OK && !created);
    CHECK(mao_rel_table_count(&t) == 1 && F.writes == 1);

    /* metadata update: profile change (type) and rename under the same id */
    CHECK(mao_rel_table_note(&t, ID(1), "CAMERA 01", ODD_DEVICE_CAMERA, &changed) == ESP_OK && !changed && F.writes == 1);
    CHECK(mao_rel_table_note(&t, ID(1), "LAMP 01", ODD_DEVICE_LIGHT, &changed) == ESP_OK && changed && F.writes == 2);
    CHECK(mao_rel_table_note(&t, ID(1), "CAMERA LAB", ODD_DEVICE_CAMERA, &changed) == ESP_OK && changed);
    reboot(&t, NULL);
    mao_rel_table_list(&t, list);
    CHECK(mao_rel_table_count(&t) == 1 && strcmp(list[0].name, "CAMERA LAB") == 0 && list[0].order == order1);

    /* 2 devices, then full capacity (8) */
    CHECK(mao_rel_table_pair(&t, ID(2), "LAMP 02", ODD_DEVICE_LIGHT, &created) == ESP_OK && created);
    for (int n = 3; n <= MAO_REL_MAX; n++) {
        char name[17];
        snprintf(name, sizeof(name), "DEV %d", n);
        CHECK(mao_rel_table_pair(&t, ID(n), name, ODD_DEVICE_LIGHT, &created) == ESP_OK && created);
    }
    CHECK(mao_rel_table_count(&t) == MAO_REL_MAX);
    const int w_full = F.writes;
    CHECK(mao_rel_table_pair(&t, ID(99), "NINTH", ODD_DEVICE_LIGHT, &created) == ESP_ERR_NO_MEM && !created);
    CHECK(mao_rel_table_count(&t) == MAO_REL_MAX && F.writes == w_full && mao_rel_table_find(&t, ID(99)) < 0);
    for (int n = 1; n <= MAO_REL_MAX; n++) {
        CHECK(mao_rel_table_find(&t, ID(n)) >= 0);   /* nothing was evicted */
    }

    /* enumeration is in stable pair order */
    CHECK(mao_rel_table_list(&t, list) == MAO_REL_MAX);
    bool ordered = true;
    for (int i = 1; i < MAO_REL_MAX; i++) {
        ordered = ordered && list[i - 1].order < list[i].order;
    }
    CHECK(ordered && list[0].id == ID(1) && list[1].id == ID(2));

    /* forget middle, reboot, order of the rest unchanged; re-pair goes last */
    CHECK(mao_rel_table_forget(&t, ID(4)) == ESP_OK && mao_rel_table_find(&t, ID(4)) < 0);
    reboot(&t, NULL);
    CHECK(mao_rel_table_count(&t) == MAO_REL_MAX - 1 && mao_rel_table_find(&t, ID(4)) < 0);
    mao_rel_table_list(&t, list);
    CHECK(list[2].id == ID(3) && list[3].id == ID(5));
    CHECK(mao_rel_table_pair(&t, ID(4), "DEV 4", ODD_DEVICE_LIGHT, &created) == ESP_OK && created);
    mao_rel_table_list(&t, list);
    CHECK(list[MAO_REL_MAX - 1].id == ID(4));   /* a fresh relationship, not the old one */

    /* write failure: pair stays DISCOVERED, forget stays KNOWN, note keeps old metadata */
    CHECK(mao_rel_table_forget(&t, ID(8)) == ESP_OK);
    F.fail_write = true;
    CHECK(mao_rel_table_pair(&t, ID(8), "DEV 8", ODD_DEVICE_LIGHT, &created) == ESP_FAIL && !created);
    CHECK(mao_rel_table_find(&t, ID(8)) < 0);
    CHECK(mao_rel_table_note(&t, ID(1), "RENAMED", ODD_DEVICE_CAMERA, &changed) == ESP_FAIL && !changed);
    F.fail_write = false;
    CHECK(strcmp(t.rec[mao_rel_table_find(&t, ID(1))].name, "CAMERA LAB") == 0);
    F.fail_erase = true;
    CHECK(mao_rel_table_forget(&t, ID(2)) == ESP_FAIL && mao_rel_table_find(&t, ID(2)) >= 0);
    F.fail_erase = false;
    reboot(&t, NULL);
    CHECK(mao_rel_table_find(&t, ID(8)) < 0 && mao_rel_table_find(&t, ID(2)) >= 0);   /* RAM == flash */

    /* invalid input */
    CHECK(mao_rel_table_pair(&t, 0x1234ull, "BAD ID", 2, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(mao_rel_table_pair(&t, ID(8), "", 2, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(mao_rel_table_pair(&t, ID(8), "SEVENTEEN CHARS!!", 2, NULL) == ESP_ERR_INVALID_ARG);

    /* bad records: skipped, the rest still loads, boot never fails */
    memset(&F, 0, sizeof(F));
    reboot(&t, NULL);
    mao_rel_table_pair(&t, ID(1), "GOOD 1", 6, NULL);
    mao_rel_table_pair(&t, ID(2), "GOOD 2", 2, NULL);
    mao_rel_table_pair(&t, ID(3), "TRUNC", 2, NULL);
    mao_rel_table_pair(&t, ID(4), "BADID", 2, NULL);
    mao_rel_table_pair(&t, ID(5), "NONUL", 2, NULL);
    mao_rel_table_pair(&t, ID(6), "FUTURE", 2, NULL);
    F.len[2] = 20;                                   /* truncated */
    memset(&F.blob[3][8], 0, 8);                     /* id 0 */
    memset(&F.blob[4][16], 'A', 17);                 /* name without terminator */
    F.blob[5][0] = 2;                                /* unknown (newer) schema */
    memcpy(F.blob[6], F.blob[0], MAO_REL_BLOB_LEN);  /* duplicate of slot 0's id */
    F.len[6] = MAO_REL_BLOB_LEN;
    reboot(&t, &bad);
    CHECK(bad == 5 && mao_rel_table_count(&t) == 2);
    CHECK(mao_rel_table_find(&t, ID(1)) == 0 && mao_rel_table_find(&t, ID(2)) == 1);
    CHECK(mao_rel_decode(F.blob[5], F.len[5], &list[0]) == MAO_REL_LOAD_UNKNOWN_SCHEMA);
    CHECK(t.state[5] == MAO_REL_SLOT_RESERVED);
    /* new pairs reuse malformed/duplicate slots but never the future-schema one */
    for (int n = 10; n < 20; n++) {
        mao_rel_table_pair(&t, ID(n), "FILL", 2, NULL);
    }
    CHECK(mao_rel_table_count(&t) == MAO_REL_MAX - 1 && F.blob[5][0] == 2 && F.len[5] != 0);
    CHECK(mao_rel_table_pair(&t, ID(30), "ONE MORE", 2, NULL) == ESP_ERR_NO_MEM);

    printf("relationship table: %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
