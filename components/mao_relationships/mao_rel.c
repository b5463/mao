/*
 * MAO device relationships: NVS backend, locking, change events and the
 * development commands around the backend-agnostic table (mao_rel_table.c).
 *
 * NVS namespace "mao_rel", one blob per slot ("r0".."r7"). Nothing else is
 * ever written here, and nothing here erases other namespaces.
 */
#include "mao_rel.h"
#include "mao_rel_table.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "mao_devices.h"
#include "mao_link.h"
#include "odd_link.h"
#include "mao_events.h"
#include "mao_system.h"

static const char *TAG = "MAO_REL";

#define NVS_NS "mao_rel"

_Static_assert(MAO_REL_MAX_KNOWN == MAO_REL_MAX, "one relationship budget");
_Static_assert(128 > MAO_REL_BLOB_LEN, "the load buffer must exceed every known schema");

static mao_rel_table_t s_t;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;
static nvs_handle_t s_nvs;
static bool s_nvs_ok;
static bool s_fail_write, s_fail_erase;   /* development: one-shot failure injection */

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

static void slot_key(int slot, char key[4])
{
    key[0] = 'r';
    key[1] = (char)('0' + slot);
    key[2] = '\0';
}

static esp_err_t nvs_backend_write(void *ctx, int slot, const uint8_t blob[MAO_REL_BLOB_LEN])
{
    (void)ctx;
    if (s_fail_write) {
        s_fail_write = false;
        ESP_LOGW(TAG, "dev: injected write failure");
        return ESP_FAIL;
    }
    if (!s_nvs_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    char key[4];
    slot_key(slot, key);
    esp_err_t err = nvs_set_blob(s_nvs, key, blob, MAO_REL_BLOB_LEN);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

static esp_err_t nvs_backend_erase(void *ctx, int slot)
{
    (void)ctx;
    if (s_fail_erase) {
        s_fail_erase = false;
        ESP_LOGW(TAG, "dev: injected erase failure");
        return ESP_FAIL;
    }
    if (!s_nvs_ok) {
        return ESP_ERR_INVALID_STATE;
    }
    char key[4];
    slot_key(slot, key);
    esp_err_t err = nvs_erase_key(s_nvs, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;       /* already gone is what we wanted */
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

static const mao_rel_backend_t s_backend = { nvs_backend_write, nvs_backend_erase, NULL };

/* Every relationship in memory must be on flash, byte for byte (M4.1: a
 * paired device's record once vanished from flash without any forget).
 * Checked after every change and at the end of boot: a record that is
 * missing or different on flash is written back from memory and reported
 * loudly. It never removes or trusts anything that memory does not hold. */
static void verify_persisted(const char *when)
{
    if (!s_nvs_ok) {
        return;
    }
    lock();
    for (int slot = 0; slot < MAO_REL_MAX; slot++) {
        if (s_t.state[slot] != MAO_REL_SLOT_USED) {
            continue;
        }
        uint8_t want[MAO_REL_BLOB_LEN], have[128];
        mao_rel_encode(&s_t.rec[slot], want);
        char key[4];
        slot_key(slot, key);
        size_t len = sizeof(have);
        const esp_err_t err = nvs_get_blob(s_nvs, key, have, &len);
        const bool same = err == ESP_OK && len == MAO_REL_BLOB_LEN && memcmp(want, have, len) == 0;
        memset(have, 0, sizeof(have));
        if (!same) {
            ESP_LOGE(TAG, "relationship %s \"%s\" %s on flash (%s, %s): restoring it from memory", key,
                     s_t.rec[slot].name, err == ESP_ERR_NVS_NOT_FOUND ? "MISSING" : "DIFFERENT", esp_err_to_name(err),
                     when);
            esp_err_t w = nvs_set_blob(s_nvs, key, want, MAO_REL_BLOB_LEN);
            if (w == ESP_OK) {
                w = nvs_commit(s_nvs);
            }
            ESP_LOGE(TAG, "relationship %s restore: %s", key, esp_err_to_name(w));
        }
        memset(want, 0, sizeof(want));
    }
    unlock();
}

static void changed(void)
{
    verify_persisted("after a change");
    mao_event_post(MAO_EVENT_REL_CHANGED, 0);
}

/* ---------------------------------------------------------------------- */

bool mao_rel_is_known(uint64_t id)
{
    lock();
    const bool k = mao_rel_table_find(&s_t, id) >= 0;
    unlock();
    return k;
}

bool mao_rel_get(uint64_t id, mao_rel_info_t *out)
{
    lock();
    const int slot = mao_rel_table_find(&s_t, id);
    if (slot >= 0 && out) {
        const mao_rel_record_t *r = &s_t.rec[slot];
        *out = (mao_rel_info_t) { .id = r->id, .device_type = r->device_type, .order = r->order,
                                  .has_cred = r->has_cred };
        memcpy(out->name, r->name, sizeof(out->name));
    }
    unlock();
    return slot >= 0;
}

int mao_rel_count(void)
{
    lock();
    const int n = mao_rel_table_count(&s_t);
    unlock();
    return n;
}

int mao_rel_list(mao_rel_info_t out[MAO_REL_MAX_KNOWN])
{
    mao_rel_record_t recs[MAO_REL_MAX];
    lock();
    const int n = mao_rel_table_list(&s_t, recs);
    unlock();
    for (int i = 0; i < n; i++) {
        out[i] = (mao_rel_info_t) { .id = recs[i].id, .device_type = recs[i].device_type, .order = recs[i].order,
                                    .has_cred = recs[i].has_cred };
        memcpy(out[i].name, recs[i].name, sizeof(out[i].name));
    }
    memset(recs, 0, sizeof(recs));           /* the copies held keys */
    return n;
}

esp_err_t mao_rel_pair(uint64_t id, const char *name, uint16_t type)
{
    bool created = false;
    const int64_t t0 = esp_timer_get_time();
    lock();
    const esp_err_t err = mao_rel_table_pair(&s_t, id, name, type, &created);
    unlock();
    const long us = (long)(esp_timer_get_time() - t0);
    if (err == ESP_OK && created) {
        ESP_LOGI(TAG, "paired: %016" PRIx64 " \"%s\" (%s) in %ld us", id, name, odd_device_type_name(type), us);
        changed();
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "pair: %016" PRIx64 " already known", id);
    } else if (err == ESP_ERR_NO_MEM) {
        ESP_LOGW(TAG, "pair: %016" PRIx64 " refused, relationship list full (%d)", id, MAO_REL_MAX);
    } else {
        ESP_LOGW(TAG, "pair: %016" PRIx64 " NOT saved (%s): stays discovered", id, esp_err_to_name(err));
    }
    return err;
}

esp_err_t mao_rel_forget(uint64_t id)
{
    lock();
    const esp_err_t err = mao_rel_table_forget(&s_t, id);
    unlock();
    if (err == ESP_OK) {
        mao_link_clear(id);                  /* no hidden key survives a FORGET */
        ESP_LOGI(TAG, "forgot: %016" PRIx64 " (relationship, credential and secure session)", id);
        changed();
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "forget: %016" PRIx64 " NOT removed (%s): stays known", id, esp_err_to_name(err));
    }
    return err;
}

void mao_rel_note_live(uint64_t id, const char *name, uint16_t type)
{
    bool ch = false;
    mao_rel_record_t before = { 0 };
    lock();
    const int slot = mao_rel_table_find(&s_t, id);
    if (slot >= 0) {
        before = s_t.rec[slot];
    }
    const esp_err_t err = slot >= 0 ? mao_rel_table_note(&s_t, id, name, type, &ch) : ESP_ERR_NOT_FOUND;
    unlock();
    memset(before.k_link, 0, sizeof(before.k_link));
    if (ch) {
        ESP_LOGI(TAG, "metadata updated: %016" PRIx64 " \"%s\" (%s) -> \"%s\" (%s)", id, before.name,
                 odd_device_type_name(before.device_type), name, odd_device_type_name(type));
        changed();
    } else if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "metadata for %016" PRIx64 " not saved (%s)", id, esp_err_to_name(err));
    }
}

/* The pairing ceremony's commit point on MAO (registered with mao_link):
 * relationship + credential in one committed write, then the link layer. */
static bool persist_credential(uint64_t id, const char *name, uint16_t type, const uint8_t mac[6],
                               const uint8_t key[32])
{
    bool created = false;
    const int64_t t0 = esp_timer_get_time();
    lock();
    const esp_err_t err = mao_rel_table_set_cred(&s_t, id, name, type, mac, key, &created);
    unlock();
    const long us = (long)(esp_timer_get_time() - t0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "credential for %016" PRIx64 " NOT saved (%s): not paired", id, esp_err_to_name(err));
        return false;
    }
    mao_link_set_credential(id, mac, key);
    ESP_LOGI(TAG, "%s: %016" PRIx64 " \"%s\" authenticated (key fp %08" PRIx32 ", committed in %ld us)",
             created ? "paired" : "credential replaced", id, name ? name : "", odl_fingerprint(key), us);
    changed();
    return true;
}

bool mao_rel_peer_mac(uint64_t id, uint8_t mac[6])
{
    lock();
    const int slot = mao_rel_table_find(&s_t, id);
    const bool ok = slot >= 0 && s_t.rec[slot].has_cred;
    if (ok) {
        memcpy(mac, s_t.rec[slot].peer_mac, 6);
    }
    unlock();
    return ok;
}

esp_err_t mao_rel_debug_drop_key(uint64_t id)
{
    lock();
    const esp_err_t err = mao_rel_table_clear_cred(&s_t, id);
    unlock();
    if (err == ESP_OK) {
        mao_link_clear(id);
        ESP_LOGW(TAG, "dev: credential of %016" PRIx64 " dropped (relationship kept, unverified)", id);
        changed();
    }
    return err;
}

esp_err_t mao_rel_debug_corrupt_key(uint64_t id)
{
    lock();
    const int slot = mao_rel_table_find(&s_t, id);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    uint8_t key[32], mac[6];
    if (slot >= 0 && s_t.rec[slot].has_cred) {
        memcpy(key, s_t.rec[slot].k_link, 32);
        memcpy(mac, s_t.rec[slot].peer_mac, 6);
        key[0] ^= 0x5A;
        key[31] ^= 0xA5;
        err = mao_rel_table_set_cred(&s_t, id, NULL, 0, mac, key, NULL);
    }
    unlock();
    if (err == ESP_OK) {
        mao_link_set_credential(id, mac, key);
        ESP_LOGW(TAG, "dev: stored key of %016" PRIx64 " corrupted (fp now %08" PRIx32 ")", id, odl_fingerprint(key));
    }
    memset(key, 0, sizeof(key));
    return err;
}

uint32_t mao_rel_write_count(void)
{
    lock();
    const uint32_t n = s_t.writes;
    unlock();
    return n;
}

/* ---------------------------------------------------------------------- */
/* Development commands: "mao rel <...>"                                  */
/* ---------------------------------------------------------------------- */

#if CONFIG_MAO_DEV_CONSOLE

/* Synthetic test ids: OUI byte 0xFF has the multicast bit set, which no real
 * device MAC (hence no real ODD device_id) can have. */
#define TEST_ID_PREFIX 0x0DD0FF0000000000ull
#define TEST_ID_MASK   0xFFFFFF0000000000ull

static void dev_list(void)
{
    mao_rel_info_t l[MAO_REL_MAX_KNOWN];
    const int n = mao_rel_list(l);
    ESP_LOGI(TAG, "rel: %d known, %" PRIu32 " writes since boot", n, mao_rel_write_count());
    for (int i = 0; i < n; i++) {
        const int slot = mao_devices_find(l[i].id);
        mao_device_t d;
        const bool online = slot >= 0 && mao_devices_get(slot, &d) && d.online;
        ESP_LOGI(TAG, "  [%d] %016" PRIx64 " \"%s\" %s order %" PRIu32 " %s %s link %s", i, l[i].id, l[i].name,
                 odd_device_type_name(l[i].device_type), l[i].order, online ? "ONLINE" : "OFFLINE",
                 l[i].has_cred ? "PAIRED" : "UNVERIFIED", mao_link_state_name(mao_link_state(l[i].id)));
    }
}

static void dev_dump(void)
{
    lock();
    for (int i = 0; i < MAO_REL_MAX; i++) {
        static const char *const kSt[] = { "free", "used", "reserved" };
        ESP_LOGI(TAG, "  slot r%d %s %016" PRIx64 " \"%s\"", i, kSt[s_t.state[i] % 3],
                 s_t.state[i] == MAO_REL_SLOT_USED ? s_t.rec[i].id : 0,
                 s_t.state[i] == MAO_REL_SLOT_USED ? s_t.rec[i].name : "");
    }
    ESP_LOGI(TAG, "  next order %" PRIu32 ", writes %" PRIu32 ", nvs %s", s_t.next_order, s_t.writes,
             s_nvs_ok ? "ok" : "UNAVAILABLE");
    unlock();
}

/* <n>: index in the known list; a 16-digit hex value: a device_id. */
static uint64_t dev_resolve(const char *arg)
{
    if (strlen(arg) >= 12) {
        return strtoull(arg, NULL, 16);
    }
    mao_rel_info_t l[MAO_REL_MAX_KNOWN];
    const int n = mao_rel_list(l);
    const int i = atoi(arg);
    return i >= 0 && i < n ? l[i].id : 0;
}

/* Run on the mao_link task (mao_link_dev_call), never on the console task. */
static void dev_dropkey(uint64_t id)
{
    if (mao_rel_debug_drop_key(id) != ESP_OK) {
        ESP_LOGW(TAG, "rel dropkey: no credential");
    }
}

static void dev_corrupt(uint64_t id)
{
    if (mao_rel_debug_corrupt_key(id) != ESP_OK) {
        ESP_LOGW(TAG, "rel corrupt: no credential");
    }
}

static void dev_command(char *arg)
{
    char *sub = arg ? arg : "";
    char *rest = strchr(sub, ' ');
    if (rest) {
        *rest++ = '\0';
    }
    if (strcmp(sub, "list") == 0) {
        dev_list();
    } else if (strcmp(sub, "dump") == 0) {
        dev_dump();
    } else if (strcmp(sub, "pair") == 0 && rest) {
        /* registry slot: the same metadata the PAIR word would use */
        mao_device_t d;
        if (mao_devices_get(atoi(rest), &d)) {
            mao_rel_pair(d.info.id, d.info.name, d.info.device_type);
        } else {
            ESP_LOGW(TAG, "rel pair <registry slot>: no such device");
        }
    } else if (strcmp(sub, "forget") == 0 && rest) {
        const uint64_t id = dev_resolve(rest);
        if (!id || mao_rel_forget(id) == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "rel forget <index|id>: not known");
        }
    } else if (strcmp(sub, "inject") == 0 && rest) {
        /* inject <low 40-bit hex> <type> <name...>: a synthetic TEST record */
        char *type = strchr(rest, ' ');
        char *name = type ? strchr(type + 1, ' ') : NULL;
        if (!name) {
            ESP_LOGW(TAG, "rel inject <hex> <type> <name>");
            return;
        }
        *type++ = '\0';
        *name++ = '\0';
        const uint64_t id = TEST_ID_PREFIX | (strtoull(rest, NULL, 16) & ~TEST_ID_MASK);
        mao_rel_pair(id, name, (uint16_t)atoi(type));
    } else if (strcmp(sub, "clear-test") == 0) {
        mao_rel_info_t l[MAO_REL_MAX_KNOWN];
        const int n = mao_rel_list(l);
        int removed = 0;
        for (int i = 0; i < n; i++) {
            if ((l[i].id & TEST_ID_MASK) == TEST_ID_PREFIX) {
                removed += mao_rel_forget(l[i].id) == ESP_OK;
            }
        }
        ESP_LOGI(TAG, "rel: removed %d synthetic test records (real relationships untouched)", removed);
    } else if (strcmp(sub, "spair") == 0 && rest) {
        /* dev: the secure ceremony from the registry (the UI's PAIR / VERIFY does the same) */
        mao_device_t d;
        if (mao_devices_get(atoi(rest), &d)) {
            mao_link_pair_start(d.info.id, d.mac, d.info.name, d.info.device_type);
        } else {
            ESP_LOGW(TAG, "rel spair <registry slot>: no such device");
        }
    } else if (strcmp(sub, "sconfirm") == 0) {
        mao_link_pair_confirm();
    } else if (strcmp(sub, "scancel") == 0) {
        mao_link_pair_cancel();
    } else if (strcmp(sub, "dropkey") == 0 && rest) {
        const uint64_t id = dev_resolve(rest);
        if (id) {
            mao_link_dev_call(dev_dropkey, id, "rel dropkey");   /* key / NVS / radio work: link task */
        } else {
            ESP_LOGW(TAG, "rel dropkey <index|id>: not known");
        }
    } else if (strcmp(sub, "corrupt") == 0 && rest) {
        const uint64_t id = dev_resolve(rest);
        if (id) {
            mao_link_dev_call(dev_corrupt, id, "rel corrupt");
        } else {
            ESP_LOGW(TAG, "rel corrupt <index|id>: not known");
        }
    } else if (strcmp(sub, "failnext") == 0 && rest) {
        s_fail_write = strcmp(rest, "write") == 0;
        s_fail_erase = strcmp(rest, "erase") == 0;
        ESP_LOGW(TAG, "rel: next %s will fail", s_fail_write ? "write" : (s_fail_erase ? "erase" : "(nothing)"));
    } else {
        ESP_LOGW(TAG, "rel list | dump | pair <slot> | forget <index|id> | inject <hex> <type> <name> | "
                 "clear-test | failnext <write|erase> | dropkey <i> | corrupt <i>");
    }
}

#endif

/* ---------------------------------------------------------------------- */

esp_err_t mao_rel_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    mao_rel_table_init(&s_t, &s_backend);
    mao_link_set_persist(persist_credential);
    mao_devices_set_auth_gate(mao_rel_is_known);
#if CONFIG_MAO_DEV_CONSOLE
    mao_devcmd_register("rel", dev_command);
#endif
    const int64_t t0 = esp_timer_get_time();
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "relationships unavailable (nvs '%s': %s): no known devices this boot", NVS_NS,
                 esp_err_to_name(err));
        return ESP_OK;
    }
    s_nvs_ok = true;
    int loaded = 0, skipped = 0;
    for (int slot = 0; slot < MAO_REL_MAX; slot++) {
        char key[4];
        slot_key(slot, key);
        uint8_t blob[128];              /* > every schema this firmware knows (v2: 73 B) */
        size_t len = 0;
        err = nvs_get_blob(s_nvs, key, NULL, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if (err == ESP_OK && len > sizeof(blob)) {
            /* Larger than any schema this firmware knows: a newer one. Keep it. */
            s_t.state[slot] = MAO_REL_SLOT_RESERVED;
            ESP_LOGW(TAG, "relationship %s skipped: unknown schema (%u bytes), slot kept", key, (unsigned)len);
            skipped++;
            continue;
        }
        if (err == ESP_OK) {
            err = nvs_get_blob(s_nvs, key, blob, &len);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "relationship %s unreadable (%s): skipped", key, esp_err_to_name(err));
            skipped++;
            continue;
        }
        const uint8_t schema = blob[0];
        const mao_rel_load_t res = mao_rel_table_load_slot(&s_t, slot, blob, len);
        memset(blob, 0, sizeof(blob));
        if (res == MAO_REL_LOAD_OK) {
            const mao_rel_record_t *r = &s_t.rec[slot];
            ESP_LOGI(TAG, "relationship loaded: %016" PRIx64 " \"%s\" (%s) %s%s", r->id, r->name,
                     odd_device_type_name(r->device_type),
                     r->has_cred ? "PAIRED" : (schema == MAO_REL_SCHEMA_V1 ? "UNVERIFIED (M3.0 record)" : "UNVERIFIED"),
                     r->cred_dropped ? " - stored credential malformed, ignored" : "");
            if (r->has_cred) {
                mao_link_set_credential(r->id, r->peer_mac, r->k_link);
            }
            loaded++;
        } else {
            ESP_LOGW(TAG, "relationship %s skipped: %s", key, mao_rel_load_name(res));
            skipped++;
        }
    }
    ESP_LOGI(TAG, "%d known device%s loaded in %lld us%s", loaded, loaded == 1 ? "" : "s",
             (long long)(esp_timer_get_time() - t0), skipped ? " (some records skipped)" : "");
    verify_persisted("boot");
    return ESP_OK;
}
