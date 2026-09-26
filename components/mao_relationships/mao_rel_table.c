/*
 * Relationship table (see mao_rel_table.h). Plain C, no ESP-IDF runtime:
 * compiled unchanged into the host tests (tests/relationships).
 */
#include "mao_rel_table.h"

#include <string.h>

static bool valid_id(uint64_t id)
{
    return (uint16_t)(id >> 48) == MAO_REL_ID_MARK && (id & 0xFFFFFFFFFFFFull) != 0;
}

static bool valid_name(const char *name)
{
    return name && name[0] != '\0' && strlen(name) <= ODD_NAME_MAX;
}

void mao_rel_encode(const mao_rel_record_t *r, uint8_t out[MAO_REL_BLOB_LEN])
{
    memset(out, 0, MAO_REL_BLOB_LEN);
    out[0] = MAO_REL_SCHEMA;
    out[2] = (uint8_t)r->device_type;
    out[3] = (uint8_t)(r->device_type >> 8);
    for (int i = 0; i < 4; i++) {
        out[4 + i] = (uint8_t)(r->order >> (8 * i));
    }
    for (int i = 0; i < 8; i++) {
        out[8 + i] = (uint8_t)(r->id >> (8 * i));
    }
    memcpy(&out[16], r->name, strnlen(r->name, ODD_NAME_MAX));   /* NUL-padded */
}

mao_rel_load_t mao_rel_decode(const uint8_t *blob, size_t len, mao_rel_record_t *out)
{
    if (len >= 1 && blob[0] != MAO_REL_SCHEMA) {
        return MAO_REL_LOAD_UNKNOWN_SCHEMA;    /* checked first: a newer schema may be longer */
    }
    if (len != MAO_REL_BLOB_LEN) {
        return MAO_REL_LOAD_BAD_SIZE;
    }
    mao_rel_record_t r = { 0 };
    r.device_type = (uint16_t)(blob[2] | (blob[3] << 8));
    for (int i = 0; i < 4; i++) {
        r.order |= (uint32_t)blob[4 + i] << (8 * i);
    }
    for (int i = 0; i < 8; i++) {
        r.id |= (uint64_t)blob[8 + i] << (8 * i);
    }
    if (!valid_id(r.id)) {
        return MAO_REL_LOAD_BAD_ID;
    }
    if (blob[16 + ODD_NAME_MAX] != '\0') {
        return MAO_REL_LOAD_BAD_NAME;          /* the last byte is the terminator */
    }
    memcpy(r.name, &blob[16], ODD_NAME_MAX + 1);
    if (!valid_name(r.name)) {
        return MAO_REL_LOAD_BAD_NAME;
    }
    *out = r;
    return MAO_REL_LOAD_OK;
}

const char *mao_rel_load_name(mao_rel_load_t r)
{
    static const char *const kNames[] = { "ok", "bad size", "unknown schema", "bad id", "bad name", "duplicate" };
    return (unsigned)r < sizeof(kNames) / sizeof(kNames[0]) ? kNames[r] : "?";
}

void mao_rel_table_init(mao_rel_table_t *t, const mao_rel_backend_t *be)
{
    memset(t, 0, sizeof(*t));
    t->be = be;
}

int mao_rel_table_find(const mao_rel_table_t *t, uint64_t id)
{
    for (int i = 0; i < MAO_REL_MAX; i++) {
        if (t->state[i] == MAO_REL_SLOT_USED && t->rec[i].id == id) {
            return i;
        }
    }
    return -1;
}

mao_rel_load_t mao_rel_table_load_slot(mao_rel_table_t *t, int slot, const uint8_t *blob, size_t len)
{
    if (slot < 0 || slot >= MAO_REL_MAX) {
        return MAO_REL_LOAD_BAD_SIZE;
    }
    mao_rel_record_t r;
    const mao_rel_load_t res = mao_rel_decode(blob, len, &r);
    if (res == MAO_REL_LOAD_UNKNOWN_SCHEMA) {
        t->state[slot] = MAO_REL_SLOT_RESERVED;
        return res;
    }
    if (res != MAO_REL_LOAD_OK) {
        return res;
    }
    if (mao_rel_table_find(t, r.id) >= 0) {
        return MAO_REL_LOAD_DUPLICATE;         /* first one wins; this slot stays reusable */
    }
    t->state[slot] = MAO_REL_SLOT_USED;
    t->rec[slot] = r;
    if (r.order >= t->next_order) {
        t->next_order = r.order + 1;
    }
    return MAO_REL_LOAD_OK;
}

int mao_rel_table_count(const mao_rel_table_t *t)
{
    int n = 0;
    for (int i = 0; i < MAO_REL_MAX; i++) {
        n += t->state[i] == MAO_REL_SLOT_USED;
    }
    return n;
}

int mao_rel_table_list(const mao_rel_table_t *t, mao_rel_record_t out[MAO_REL_MAX])
{
    int n = 0;
    for (int i = 0; i < MAO_REL_MAX; i++) {
        if (t->state[i] == MAO_REL_SLOT_USED) {
            /* insertion sort by pair order: at most 8 records */
            int j = n++;
            while (j > 0 && out[j - 1].order > t->rec[i].order) {
                out[j] = out[j - 1];
                j--;
            }
            out[j] = t->rec[i];
        }
    }
    return n;
}

static esp_err_t store(mao_rel_table_t *t, int slot, const mao_rel_record_t *r)
{
    uint8_t blob[MAO_REL_BLOB_LEN];
    mao_rel_encode(r, blob);
    const esp_err_t err = t->be->write(t->be->ctx, slot, blob);
    if (err == ESP_OK) {
        t->writes++;
    }
    return err;
}

static void copy_name(char dst[ODD_NAME_MAX + 1], const char *src)
{
    memset(dst, 0, ODD_NAME_MAX + 1);
    memcpy(dst, src, strnlen(src, ODD_NAME_MAX));
}

esp_err_t mao_rel_table_note(mao_rel_table_t *t, uint64_t id, const char *name, uint16_t type, bool *changed)
{
    if (changed) {
        *changed = false;
    }
    const int slot = mao_rel_table_find(t, id);
    if (slot < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!valid_name(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    mao_rel_record_t r = t->rec[slot];
    if (strcmp(r.name, name) == 0 && r.device_type == type) {
        return ESP_OK;                          /* nothing new: no flash write */
    }
    copy_name(r.name, name);
    r.device_type = type;
    const esp_err_t err = store(t, slot, &r);
    if (err == ESP_OK) {
        t->rec[slot] = r;
        if (changed) {
            *changed = true;
        }
    }
    return err;
}

esp_err_t mao_rel_table_pair(mao_rel_table_t *t, uint64_t id, const char *name, uint16_t type, bool *created)
{
    if (created) {
        *created = false;
    }
    if (!valid_id(id) || !valid_name(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mao_rel_table_find(t, id) >= 0) {
        return mao_rel_table_note(t, id, name, type, NULL);   /* already known: idempotent */
    }
    int slot = -1;
    for (int i = 0; i < MAO_REL_MAX && slot < 0; i++) {
        if (t->state[i] == MAO_REL_SLOT_FREE) {
            slot = i;
        }
    }
    if (slot < 0) {
        return ESP_ERR_NO_MEM;                  /* full: never evict another relationship */
    }
    mao_rel_record_t r = { .id = id, .device_type = type, .order = t->next_order };
    copy_name(r.name, name);
    const esp_err_t err = store(t, slot, &r);
    if (err != ESP_OK) {
        return err;                             /* not persisted: stays DISCOVERED */
    }
    t->state[slot] = MAO_REL_SLOT_USED;
    t->rec[slot] = r;
    t->next_order++;
    if (created) {
        *created = true;
    }
    return ESP_OK;
}

esp_err_t mao_rel_table_forget(mao_rel_table_t *t, uint64_t id)
{
    const int slot = mao_rel_table_find(t, id);
    if (slot < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    const esp_err_t err = t->be->erase(t->be->ctx, slot);
    if (err != ESP_OK) {
        return err;                             /* not removed: stays KNOWN */
    }
    t->writes++;
    t->state[slot] = MAO_REL_SLOT_FREE;
    memset(&t->rec[slot], 0, sizeof(t->rec[slot]));
    return ESP_OK;
}
