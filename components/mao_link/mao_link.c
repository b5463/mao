/*
 * MAO link security glue (see mao_link.h, docs/link_security.md).
 *
 * Threads: the radio path (rx / tx / hint / HELLO_ACK) runs on the
 * mao_devices task; all X25519 work, pairing, HELLO retries and revocation
 * timers run on the "mao_link" task (6 KB stack) - never in the ESP-NOW
 * callback, the LVGL task or the console task. One mutex guards the peers.
 */
#include "mao_link.h"

#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mao_events.h"
#include "mao_radio.h"
#include "mao_system.h"
#include "odd_bus.h"
#include "odd_link.h"
#include "odd_link_auth.h"
#include "odd_link_crypto.h"
#include "odd_link_selftest.h"

static const char *TAG = "MAO_LINK";

#define LINK_TASK_STACK   6144
#define LINK_TASK_PRIO    4
#define LINK_QUEUE_LEN    8
#define PEERS_MAX         8
#define HELLO_RETRY_MS    300
#define HELLO_TRIES       4
#define HINT_FRESH_MS     5000      /* heard within: a failed hello means FAILED, not OFFLINE */
#define FAILED_RETRY_MS   10000     /* re-try a device that could not prove itself */
#define STALE_AUTH_MS     3000      /* a hint after this much authenticated silence: probe */
#define PROBE_WAIT_MS     800       /* an unanswered secure probe: the device lost the session, re-key */
#define REVOKE_RETRY_MS   400
#define REVOKE_TRIES      4
#define HELLO_BUF         72        /* a HELLO frame is 52 B */
#define JOB_BUF           96        /* the largest pairing frame (COMMIT) is 92 B */
_Static_assert(HELLO_BUF >= ODL_HEAD_LEN + 16 + ODL_NONCE_LEN + ODL_TAG_LEN, "HELLO buffer");
_Static_assert(JOB_BUF >= ODL_HEAD_LEN + ODL_TXID_LEN + 16 + ODL_PUB_LEN + ODL_HASH_LEN, "pairing job buffer");

typedef struct {
    bool used;
    uint64_t id;
    uint8_t mac[6];
    uint8_t key[ODL_KEY_LEN];
    mao_link_state_t st;
    odl_session_t sess;
    uint8_t nonce_c[ODL_NONCE_LEN];
    uint8_t hello[HELLO_BUF];
    size_t hello_len;
    uint32_t hello_last_ms;
    uint8_t tries;
    uint32_t last_hint_ms, last_auth_ms, retry_after_ms;
    uint32_t probe_ms;                  /* secure probe sent (0 = none) */
    uint32_t sessions, auth_fail, env_drop;
} peer_t;

typedef enum {
    JOB_SELFTEST = 1, JOB_BENCH, JOB_PAIR_START, JOB_PAIR_FRAME, JOB_PAIR_CONFIRM, JOB_PAIR_CANCEL,
    JOB_DEV_CALL,                            /* data: dev_call_t */
} job_type_t;

typedef struct {
    void (*fn)(uint64_t arg);
    uint64_t arg;
    uint32_t n;
    char what[24];
} dev_call_t;
_Static_assert(sizeof(dev_call_t) <= JOB_BUF, "dev call job");

typedef struct {
    uint8_t type;
    uint8_t mac[6];
    uint8_t len;
    uint8_t data[JOB_BUF];
} job_t;

static QueueHandle_t s_jobs;
static SemaphoreHandle_t s_lock;
static peer_t s_peers[PEERS_MAX];
static void (*s_secure_cb)(uint64_t, const uint8_t *);
static void (*s_probe_cb)(uint64_t, const uint8_t *);
static mao_link_persist_fn s_persist;

static odl_pairc_t s_pair;
static struct {
    uint64_t dev_id;
    uint8_t mac[6];
    char name[ODD_NAME_MAX + 1];
    uint16_t type;
} s_pair_dev;

static struct {
    bool busy, done, confirmed;
    uint64_t id;
    uint32_t last_ms;
    uint8_t tries;
} s_revoke;

static struct {
    uint32_t plain_refused_tx, env_drop, hello_ok, hello_fail, ack_bad;
} s_st;

#if CONFIG_MAO_DEV_CONSOLE
/* Development only: copies of frames already sent, for replay tests. */
static struct {
    uint8_t f[JOB_BUF];
    uint8_t n;
} s_rec_hello, s_rec_pair[6];
static struct {
    uint8_t f[ODL_MAX_FRAME];
    uint8_t n;
} s_rec_data;
static uint8_t s_rec_pair_n;
#define DEV_KEEP(slot, frame, len) do { memcpy((slot).f, (frame), (len)); (slot).n = (uint8_t)(len); } while (0)
#else
#define DEV_KEEP(slot, frame, len) do { } while (0)
#endif

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static void post_job(const job_t *j)
{
    if (s_jobs && xQueueSend(s_jobs, j, 0) != pdTRUE) {
        ESP_LOGW(TAG, "link job queue full");
    }
}

static void notify(int what)
{
    mao_event_post(MAO_EVENT_LINK_CHANGED, what);
}

static void self_identity(uint64_t *id, uint8_t mac[6])
{
    mao_radio_get_mac(mac);
    *id = odd_id_from_mac(mac);
}

static peer_t *peer_by_id(uint64_t id)
{
    for (int i = 0; i < PEERS_MAX; i++) {
        if (s_peers[i].used && s_peers[i].id == id) {
            return &s_peers[i];
        }
    }
    return NULL;
}

static peer_t *peer_by_mac(const uint8_t mac[6])
{
    for (int i = 0; i < PEERS_MAX; i++) {
        if (s_peers[i].used && memcmp(s_peers[i].mac, mac, 6) == 0) {
            return &s_peers[i];
        }
    }
    return NULL;
}

static void set_state(peer_t *p, mao_link_state_t st)
{
    if (p->st != st) {
        ESP_LOGI(TAG, "%016" PRIx64 ": %s -> %s", p->id, mao_link_state_name(p->st), mao_link_state_name(st));
        p->st = st;
        notify(MAO_LINK_EV_STATE);
    }
}

/* ---------------------------------------------------------------------- */
/* Credentials                                                            */
/* ---------------------------------------------------------------------- */

void mao_link_set_persist(mao_link_persist_fn fn)
{
    s_persist = fn;
}

static void start_hello_locked(peer_t *p, uint32_t now);

void mao_link_set_credential(uint64_t id, const uint8_t mac[6], const uint8_t key[32])
{
    lock();
    peer_t *p = peer_by_id(id);
    if (!p) {
        for (int i = 0; i < PEERS_MAX && !p; i++) {
            if (!s_peers[i].used) {
                p = &s_peers[i];
            }
        }
    }
    if (!p) {
        unlock();
        ESP_LOGE(TAG, "no room for a credential");
        return;
    }
    const bool replaced = p->used;
    if (replaced && p->sess.valid) {
        mao_radio_set_peer_key(p->mac, NULL);
    }
    odl_session_end(&p->sess);
    memset(p, 0, sizeof(*p));
    p->used = true;
    p->id = id;
    memcpy(p->mac, mac, 6);
    memcpy(p->key, key, ODL_KEY_LEN);
    p->st = MAO_LINK_OFFLINE;
    const uint32_t fp = odl_fingerprint(key);
    unlock();
    ESP_LOGI(TAG, "credential %s for %016" PRIx64 " (key fp %08" PRIx32 ")", replaced ? "replaced" : "loaded", id,
             fp);
    notify(MAO_LINK_EV_STATE);
}

void mao_link_clear(uint64_t id)
{
    lock();
    peer_t *p = peer_by_id(id);
    if (p) {
        if (p->sess.valid) {
            mao_radio_set_peer_key(p->mac, NULL);
        }
        odl_session_end(&p->sess);
        olc_wipe(p, sizeof(*p));
    }
    unlock();
    if (p) {
        ESP_LOGI(TAG, "credential and session for %016" PRIx64 " removed", id);
        notify(MAO_LINK_EV_STATE);
    }
}

bool mao_link_requires_auth(uint64_t id)
{
    lock();
    const bool r = peer_by_id(id) != NULL;
    unlock();
    return r;
}

mao_link_state_t mao_link_state(uint64_t id)
{
    lock();
    const peer_t *p = peer_by_id(id);
    const mao_link_state_t st = p ? p->st : MAO_LINK_NONE;
    unlock();
    return st;
}

const char *mao_link_state_name(mao_link_state_t s)
{
    static const char *const k[] = { "NONE", "OFFLINE", "VERIFYING", "SECURE", "FAILED" };
    return (unsigned)s < sizeof(k) / sizeof(k[0]) ? k[s] : "?";
}

void mao_link_set_secure_cb(void (*cb)(uint64_t id, const uint8_t mac[6]))
{
    s_secure_cb = cb;
}

void mao_link_set_probe_cb(void (*cb)(uint64_t id, const uint8_t mac[6]))
{
    s_probe_cb = cb;
}

void mao_link_foreach_secure(void (*cb)(uint64_t id, const uint8_t mac[6]))
{
    uint64_t ids[PEERS_MAX];
    uint8_t macs[PEERS_MAX][6];
    int n = 0;
    lock();
    for (int i = 0; i < PEERS_MAX; i++) {
        if (s_peers[i].used && s_peers[i].st == MAO_LINK_SECURE) {
            ids[n] = s_peers[i].id;
            memcpy(macs[n++], s_peers[i].mac, 6);
        }
    }
    unlock();
    for (int i = 0; i < n; i++) {
        cb(ids[i], macs[i]);
    }
}

/* ---------------------------------------------------------------------- */
/* Link sessions                                                          */
/* ---------------------------------------------------------------------- */

static void start_hello_locked(peer_t *p, uint32_t now)
{
    uint64_t self_id;
    uint8_t self_mac[6];
    self_identity(&self_id, self_mac);
    const odl_credential_t cred = { .peer_id = p->id };
    odl_credential_t c = cred;
    memcpy(c.peer_mac, p->mac, 6);
    memcpy(c.k_link, p->key, ODL_KEY_LEN);
    uint8_t f[ODL_MAX_FRAME];
    const size_t n = odl_hello_build(&c, self_id, self_mac, p->nonce_c, f);
    olc_wipe(&c, sizeof(c));
    p->hello_len = n <= sizeof(p->hello) ? n : 0;
    if (p->hello_len) {
        memcpy(p->hello, f, n);
    }
    if (!p->hello_len) {
        return;
    }
    p->tries = 1;
    p->hello_last_ms = now;
    DEV_KEEP(s_rec_hello, p->hello, p->hello_len);
    mao_radio_send(NULL, p->hello, p->hello_len);   /* broadcast: reaches whatever peer entry it holds */
    /* A periodic re-try of a device that could not prove itself stays FAILED
     * (the REPAIR page must not flicker to OFFLINE); success makes it SECURE. */
    if (p->st != MAO_LINK_SECURE && p->st != MAO_LINK_FAILED) {
        set_state(p, MAO_LINK_VERIFYING);
    }
}

void mao_link_hint(uint64_t id, const uint8_t mac[6])
{
    const uint32_t now = now_ms();
    lock();
    peer_t *p = peer_by_id(id);
    if (p) {
        p->last_hint_ms = now;
        const bool stale = p->st == MAO_LINK_SECURE && now - p->last_auth_ms > STALE_AUTH_MS;
        const bool idle = p->st == MAO_LINK_OFFLINE;
        const bool retry = p->st == MAO_LINK_FAILED && (int32_t)(now - p->retry_after_ms) >= 0;
        if (stale && !p->probe_ms && s_probe_cb) {
            /* The session may still be fine (a public ANNOUNCE is normal): ask
             * inside it first; only silence means the device lost it. */
            p->probe_ms = now ? now : 1;
            uint8_t pm[6];
            memcpy(pm, p->mac, 6);
            const uint64_t pid = p->id;
            unlock();
            s_probe_cb(pid, pm);
            return;
        }
        if ((idle || retry) && !p->hello_len) {
            if (memcmp(mac, p->mac, 6) != 0) {
                ESP_LOGW(TAG, "%016" PRIx64 " announced from another radio: proof required from the paired one", id);
            }
            start_hello_locked(p, now);
        }
    }
    unlock();
}

static void on_hello_ack(const odl_msg_t *m, const uint8_t src[6])
{
    uint64_t self_id;
    uint8_t self_mac[6];
    self_identity(&self_id, self_mac);
    const uint32_t now = now_ms();
    lock();
    peer_t *p = peer_by_id(m->dev_id);
    if (!p || !p->hello_len) {
        unlock();
        return;                              /* nothing asked: a replay or a stranger */
    }
    odl_credential_t c = { .peer_id = p->id };
    memcpy(c.peer_mac, p->mac, 6);
    memcpy(c.k_link, p->key, ODL_KEY_LEN);
    odl_session_keys_t keys;
    const bool ok = odl_hello_ack_verify(&c, self_id, self_mac, p->nonce_c, m, src, &keys);
    olc_wipe(&c, sizeof(c));
    if (!ok) {
        s_st.ack_bad++;
        p->auth_fail++;
        unlock();
        ESP_LOGW(TAG, "hello answer for %016" PRIx64 " did not verify (wrong key or radio)", m->dev_id);
        return;
    }
    odl_session_end(&p->sess);
    odl_session_start(&p->sess, &keys, 'C');
    const esp_err_t e = mao_radio_set_peer_key(p->mac, keys.lmk);     /* this session's fresh LMK */
    olc_wipe(&keys, sizeof(keys));
    p->hello_len = 0;
    p->last_auth_ms = now;
    p->sessions++;
    s_st.hello_ok++;
    uint8_t mac[6];
    memcpy(mac, p->mac, 6);
    const uint64_t id = p->id;
    const uint8_t s0 = p->sess.sid[0], s1 = p->sess.sid[1];
    if (e != ESP_OK) {
        odl_session_end(&p->sess);
        unlock();
        ESP_LOGE(TAG, "encrypted peer not installed (%s): no secure session", esp_err_to_name(e));
        return;
    }
    set_state(p, MAO_LINK_SECURE);
    unlock();
    ESP_LOGI(TAG, "secure session with %016" PRIx64 " (fresh LMK, session %02x%02x..)", id, s0, s1);
    if (s_secure_cb) {
        s_secure_cb(id, mac);
    }
}

static void hello_tick(uint32_t now)
{
    lock();
    for (int i = 0; i < PEERS_MAX; i++) {
        peer_t *p = &s_peers[i];
        if (p->used && p->probe_ms && now - p->probe_ms >= PROBE_WAIT_MS) {
            p->probe_ms = 0;
            if (!p->hello_len) {
                ESP_LOGI(TAG, "%016" PRIx64 ": secure probe unanswered - re-keying", p->id);
                start_hello_locked(p, now);  /* the device rebooted or lost the session */
            }
        }
        if (!p->used || !p->hello_len || now - p->hello_last_ms < HELLO_RETRY_MS) {
            continue;
        }
        if (p->tries < HELLO_TRIES) {
            p->tries++;
            p->hello_last_ms = now;
            mao_radio_send(NULL, p->hello, p->hello_len);   /* the same HELLO: the device repeats its ACK */
            continue;
        }
        p->hello_len = 0;
        s_st.hello_fail++;
        const bool heard = now - p->last_hint_ms < HINT_FRESH_MS;
        if (p->st == MAO_LINK_SECURE && p->sess.valid && now - p->last_auth_ms < STALE_AUTH_MS) {
            continue;                        /* the old session still carries traffic */
        }
        if (p->sess.valid) {
            mao_radio_set_peer_key(p->mac, NULL);
            odl_session_end(&p->sess);
        }
        p->retry_after_ms = now + FAILED_RETRY_MS;
        /* Heard but silent to the secure hello: it cannot prove the stored
         * identity (wrong key, other controller, or an imitation). */
        set_state(p, heard ? MAO_LINK_FAILED : MAO_LINK_OFFLINE);
    }
    unlock();
}

/* ---------------------------------------------------------------------- */
/* Radio path                                                             */
/* ---------------------------------------------------------------------- */

static void revoke_acked(uint64_t id);

mao_link_rx_t mao_link_rx(const uint8_t mac[6], const uint8_t *f, size_t len, bool bcast, const uint8_t **odd,
                          size_t *odd_len, uint64_t *peer_id)
{
    if (!odl_is_link(f, len)) {
        *odd = f;
        *odd_len = len;
        return MAO_LINK_RX_ODD_PLAIN;        /* the caller decides what plaintext may do */
    }
    if (f[3] == ODL_DATA) {
        if (bcast) {
            return MAO_LINK_RX_DROP;
        }
        lock();
        peer_t *p = peer_by_mac(mac);        /* bound to the paired radio */
        if (!p || !p->sess.valid) {
            unlock();
            s_st.env_drop++;
            return MAO_LINK_RX_DROP;
        }
        const odl_rx_t r = odl_unwrap(&p->sess, f, len, odd, odd_len);
        if (r != ODL_RX_OK) {
            p->env_drop++;
            s_st.env_drop++;
            unlock();
            if (p->env_drop < 5) {
                ESP_LOGW(TAG, "secure frame from %016" PRIx64 " dropped: %s", p->id, odl_rx_name(r));
            }
            return MAO_LINK_RX_DROP;
        }
        p->last_auth_ms = now_ms();
        p->probe_ms = 0;                     /* the session answered */
        const uint64_t id = p->id;
        unlock();
        if (*odd_len >= 3 && (*odd)[0] == ODL_CTL_MAGIC0 && (*odd)[1] == ODL_CTL_MAGIC1) {
            if ((*odd)[2] == ODL_CTL_REVOKE_ACK) {
                revoke_acked(id);
            }
            return MAO_LINK_RX_CONSUMED;
        }
        *peer_id = id;
        return MAO_LINK_RX_ODD_AUTH;
    }
    odl_msg_t m;
    if (!odl_decode(f, len, &m)) {
        return MAO_LINK_RX_DROP;
    }
    if (m.type == ODL_HELLO_ACK) {
        on_hello_ack(&m, mac);
    } else if (m.type >= ODL_PAIR_START && m.type <= ODL_PAIR_ABORT && len <= JOB_BUF) {
        job_t j = { .type = JOB_PAIR_FRAME, .len = (uint8_t)len };
        memcpy(j.mac, mac, 6);
        memcpy(j.data, f, len);
        post_job(&j);                        /* X25519 happens on the link task */
    }
    return MAO_LINK_RX_CONSUMED;
}

esp_err_t mao_link_tx(const uint8_t *dst, const uint8_t *frame, size_t len)
{
    if (!dst) {
        return mao_radio_send(NULL, frame, len);
    }
    lock();
    peer_t *p = peer_by_mac(dst);
    if (p && p->sess.valid) {
        uint8_t f[ODL_MAX_FRAME];
        const size_t n = odl_wrap(&p->sess, frame, len, f);
        unlock();
        if (n) {
            DEV_KEEP(s_rec_data, f, n);
        }
        return n ? mao_radio_send(dst, f, n) : ESP_FAIL;
    }
    unlock();
    if (p) {
        s_st.plain_refused_tx++;             /* paired but not proven: never plaintext */
        return ESP_ERR_INVALID_STATE;
    }
    return mao_radio_send(NULL, frame, len); /* strangers: plaintext, addressed by dst_id */
}

/* ---------------------------------------------------------------------- */
/* Revocation                                                             */
/* ---------------------------------------------------------------------- */

static bool send_ctl(uint64_t id, uint8_t ctl)
{
    const uint8_t inner[3] = { ODL_CTL_MAGIC0, ODL_CTL_MAGIC1, ctl };
    lock();
    peer_t *p = peer_by_id(id);
    bool ok = false;
    if (p && p->sess.valid) {
        uint8_t f[ODL_MAX_FRAME];
        const size_t n = odl_wrap(&p->sess, inner, sizeof(inner), f);
        ok = n && mao_radio_send(p->mac, f, n) == ESP_OK;
    }
    unlock();
    return ok;
}

esp_err_t mao_link_revoke(uint64_t id)
{
    if (mao_link_state(id) != MAO_LINK_SECURE || s_revoke.busy) {
        return ESP_ERR_INVALID_STATE;
    }
    s_revoke.busy = true;
    s_revoke.done = s_revoke.confirmed = false;
    s_revoke.id = id;
    s_revoke.tries = 1;
    s_revoke.last_ms = now_ms();
    send_ctl(id, ODL_CTL_REVOKE);
    ESP_LOGI(TAG, "revocation sent to %016" PRIx64, id);
    return ESP_OK;
}

static void revoke_finish(bool confirmed)
{
    s_revoke.busy = false;
    s_revoke.done = true;
    s_revoke.confirmed = confirmed;
    if (confirmed) {
        ESP_LOGI(TAG, "revocation confirmed by %016" PRIx64, s_revoke.id);
    } else {
        ESP_LOGW(TAG, "revocation of %016" PRIx64 " NOT confirmed: the device may keep a stale controller "
                 "credential until its next pairing", s_revoke.id);
    }
    notify(MAO_LINK_EV_REVOKED);
}

static void revoke_acked(uint64_t id)
{
    if (s_revoke.busy && id == s_revoke.id) {
        revoke_finish(true);
    }
}

static void revoke_tick(uint32_t now)
{
    if (!s_revoke.busy || now - s_revoke.last_ms < REVOKE_RETRY_MS) {
        return;
    }
    if (s_revoke.tries >= REVOKE_TRIES) {
        revoke_finish(false);                /* bounded: no infinite retry */
        return;
    }
    s_revoke.tries++;
    s_revoke.last_ms = now;
    send_ctl(s_revoke.id, ODL_CTL_REVOKE);
}

bool mao_link_revoke_busy(void)
{
    return s_revoke.busy;
}

bool mao_link_revoke_confirmed(uint64_t *id)
{
    if (id) {
        *id = s_revoke.id;
    }
    return s_revoke.done && s_revoke.confirmed;
}

/* ---------------------------------------------------------------------- */
/* Pairing ceremony (link task)                                           */
/* ---------------------------------------------------------------------- */

static void pair_send(void *ctx, const uint8_t *f, size_t n)
{
    (void)ctx;
#if CONFIG_MAO_DEV_CONSOLE
    if (n >= 4 && f[3] == ODL_PAIR_START) {
        s_rec_pair_n = 0;                    /* a new ceremony: keep its frames */
    }
    if (s_rec_pair_n < 6 && n <= JOB_BUF) {
        DEV_KEEP(s_rec_pair[s_rec_pair_n], f, n);
        s_rec_pair_n++;
    }
#endif
    mao_radio_send(NULL, f, n);
}

static bool pair_persist(void *ctx, const odl_credential_t *c)
{
    (void)ctx;
    if (!s_persist || c->peer_id != s_pair_dev.dev_id) {
        return false;
    }
    return s_persist(c->peer_id, s_pair_dev.name, s_pair_dev.type, c->peer_mac, c->k_link);
}

static void pair_changed(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "pairing: %s%s%s", odl_c_state_name(s_pair.st), s_pair.fail ? " - " : "",
             s_pair.fail ? odl_fail_name(s_pair.fail) : "");
    if (s_pair.st == ODL_C_SAS_READY) {
        char sas[8];
        odl_sas_text(s_pair.keys.sas, sas);
        ESP_LOGI(TAG, "PAIR CODE  %s   (compare with the device)", sas);
    }
    if (s_pair.st == ODL_C_PAIRED) {
        /* The device holds the new key as PENDING: the first HELLO promotes it. */
        lock();
        peer_t *p = peer_by_id(s_pair_dev.dev_id);
        if (p) {
            p->last_hint_ms = now_ms();
            start_hello_locked(p, now_ms());
        }
        unlock();
    }
    notify(MAO_LINK_EV_PAIR);
}

esp_err_t mao_link_pair_start(uint64_t dev_id, const uint8_t dev_mac[6], const char *name, uint16_t type)
{
    if (odl_pairc_active(&s_pair)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_pair_dev.dev_id = dev_id;
    memcpy(s_pair_dev.mac, dev_mac, 6);
    memset(s_pair_dev.name, 0, sizeof(s_pair_dev.name));
    strncpy(s_pair_dev.name, name ? name : "", ODD_NAME_MAX);
    s_pair_dev.type = type;
    const job_t j = { .type = JOB_PAIR_START };
    post_job(&j);
    return ESP_OK;
}

void mao_link_pair_confirm(void)
{
    const job_t j = { .type = JOB_PAIR_CONFIRM };
    post_job(&j);
}

void mao_link_pair_cancel(void)
{
    const job_t j = { .type = JOB_PAIR_CANCEL };
    post_job(&j);
}

void mao_link_pair_reset(void)
{
    if (!odl_pairc_active(&s_pair)) {
        odl_pairc_reset(&s_pair);
    }
}

void mao_link_pair_status(mao_link_pair_status_t *out)
{
    out->dev_id = s_pair_dev.dev_id;
    out->st = s_pair.st;
    out->fail = s_pair.fail;
    out->sas = s_pair.st == ODL_C_SAS_READY || s_pair.st == ODL_C_WAIT_ACCEPT ? s_pair.keys.sas : 0;
}

static void run_job(const job_t *j)
{
    const uint32_t now = now_ms();
    switch (j->type) {
    case JOB_SELFTEST: {
        int total = 0;
        const int pass = odl_selftest(&total);
        ESP_LOGI(TAG, "link selftest: %d/%d (stack free %u B)", pass, total,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        break;
    }
    case JOB_BENCH:
        odl_bench();
        break;
    case JOB_PAIR_START: {
        uint64_t self_id;
        uint8_t self_mac[6];
        self_identity(&self_id, self_mac);
        const int64_t t0 = esp_timer_get_time();
        odl_pairc_start(&s_pair, self_id, self_mac, s_pair_dev.dev_id, s_pair_dev.mac, now);
        ESP_LOGI(TAG, "pairing started with %016" PRIx64 " (ephemeral key %lld us)", s_pair_dev.dev_id,
                 (long long)(esp_timer_get_time() - t0));
        break;
    }
    case JOB_PAIR_FRAME: {
        odl_msg_t m;
        if (odl_decode(j->data, j->len, &m)) {
            const int64_t t0 = esp_timer_get_time();
            odl_pairc_rx(&s_pair, &m, j->mac, now);
            const int64_t us = esp_timer_get_time() - t0;
            if (us > 20000) {
                ESP_LOGI(TAG, "pairing step (type %02x): %lld us", m.type, (long long)us);
            }
        }
        break;
    }
    case JOB_PAIR_CONFIRM:
        odl_pairc_confirm(&s_pair, now);
        break;
    case JOB_PAIR_CANCEL:
        odl_pairc_cancel(&s_pair);
        break;
    case JOB_DEV_CALL: {
        dev_call_t c;
        memcpy(&c, j->data, sizeof(c));
        c.fn(c.arg);
        ESP_LOGI(TAG, "dev job %" PRIu32 " done: %s", c.n, c.what);
        break;
    }
    default:
        break;
    }
}

static void link_task(void *arg)
{
    (void)arg;
    for (;;) {
        job_t j;
        if (xQueueReceive(s_jobs, &j, pdMS_TO_TICKS(100)) == pdTRUE) {
            run_job(&j);
        }
        const uint32_t now = now_ms();
        odl_pairc_tick(&s_pair, now);
        hello_tick(now);
        revoke_tick(now);
    }
}

/* ---------------------------------------------------------------------- */
/* Development                                                            */
/* ---------------------------------------------------------------------- */

#if CONFIG_MAO_DEV_CONSOLE
static uint8_t s_old_key[ODL_KEY_LEN];
static bool s_old_saved;

void mao_link_dev_call(void (*fn)(uint64_t arg), uint64_t arg, const char *what)
{
    static uint32_t s_n;
    job_t j = { .type = JOB_DEV_CALL };
    dev_call_t c = { .fn = fn, .arg = arg, .n = ++s_n };
    strncpy(c.what, what, sizeof(c.what) - 1);
    memcpy(j.data, &c, sizeof(c));
    ESP_LOGI(TAG, "dev job %" PRIu32 " queued: %s", c.n, c.what);
    post_job(&j);
}

static void dev_status(void)
{
    ESP_LOGI(TAG, "link: pair %s | hello ok=%" PRIu32 " fail=%" PRIu32 " bad-ack=%" PRIu32 " | env drop=%" PRIu32
             " | plaintext tx refused=%" PRIu32, odl_c_state_name(s_pair.st), s_st.hello_ok, s_st.hello_fail,
             s_st.ack_bad, s_st.env_drop, s_st.plain_refused_tx);
    lock();
    for (int i = 0; i < PEERS_MAX; i++) {
        const peer_t *p = &s_peers[i];
        if (p->used) {
            ESP_LOGI(TAG, "  %016" PRIx64 " %s key fp %08" PRIx32 " sessions=%" PRIu32 " tx=%" PRIu32
                     " rx_hi=%" PRIu32 " drops=%" PRIu32, p->id, mao_link_state_name(p->st),
                     odl_fingerprint(p->key), p->sessions, p->sess.tx_ctr, p->sess.rx_hi, p->env_drop);
        }
    }
    unlock();
}

/* A plaintext operational ODD frame towards the first paired device:
 * "bcast" as broadcast, "uni" as unicast with our peer entry briefly
 * plaintext. The device must refuse both. Development only. */
enum { INJ_UNI = 1, INJ_ACTION = 1 << 8, INJ_SESSION = 2 << 8 };

static void dev_inject(uint64_t arg)
{
    lock();
    peer_t *p = NULL;
    for (int i = 0; i < PEERS_MAX && !p; i++) {
        if (s_peers[i].used) {
            p = &s_peers[i];
        }
    }
    uint8_t mac[6];
    uint64_t id = 0;
    if (p) {
        memcpy(mac, p->mac, 6);
        id = p->id;
    }
    unlock();
    if (!id) {
        ESP_LOGW(TAG, "inject: no paired device");
        return;
    }
    uint64_t self_id;
    uint8_t self_mac[6];
    self_identity(&self_id, self_mac);
    odd_header_t h = { .version = ODD_BUS_PROTOCOL_VERSION, .seq = 0x7777, .src_id = self_id, .dst_id = id };
    odd_message_t b = { 0 };
    const uint64_t what = arg & 0xFF00;
    const bool uni = (arg & INJ_UNI) != 0;
    if (what == INJ_ACTION) {
        h.type = ODD_MSG_ACTION;
        h.flags = ODD_FRAME_F_INCARNATION;
        b.incarnation = 0x1122334455667788ull;
        b.u.action.cap_id = 1;
    } else if (what == INJ_SESSION) {
        h.type = ODD_MSG_SESSION_OPEN;
        h.flags = ODD_FRAME_F_INCARNATION;
        b.incarnation = 0x1122334455667788ull;
    } else {
        h.type = ODD_MSG_SET_VALUE;
        b.u.set.cap_id = 1;
        b.u.set.value = 1;
    }
    uint8_t f[ODD_MAX_FRAME];
    const size_t n = odd_encode(&h, &b, f, sizeof(f));
    if (!n) {
        ESP_LOGW(TAG, "inject: encode failed");
        return;
    }
    esp_err_t e;
    if (uni) {
        mao_radio_debug_peer_plain(mac, true);
        e = mao_radio_send(mac, f, n);
        vTaskDelay(pdMS_TO_TICKS(30));
        mao_radio_debug_peer_plain(mac, false);
    } else {
        e = mao_radio_send(NULL, f, n);
    }
    ESP_LOGW(TAG, "dev: injected PLAINTEXT %s (%s) towards %016" PRIx64 ": %s", odd_msg_type_name(h.type), uni ? "uni" : "bcast", id,
             esp_err_to_name(e));
}

/* Resend frames of an earlier exchange: the device must refuse all of them. */
enum { REPLAY_HELLO, REPLAY_DATA, REPLAY_PAIR };

static void dev_replay(uint64_t what)
{
    static const char *const kWhat[] = { "hello", "data", "pair" };
    lock();
    uint8_t mac[6] = { 0 };
    for (int i = 0; i < PEERS_MAX; i++) {
        if (s_peers[i].used) {
            memcpy(mac, s_peers[i].mac, 6);
            break;
        }
    }
    unlock();
    int sent = 0;
    if (what == REPLAY_HELLO && s_rec_hello.n) {
        mao_radio_send(NULL, s_rec_hello.f, s_rec_hello.n);
        sent = 1;
    } else if (what == REPLAY_DATA && s_rec_data.n) {
        mao_radio_send(mac, s_rec_data.f, s_rec_data.n);
        sent = 1;
    } else if (what == REPLAY_PAIR) {
        for (int i = 0; i < s_rec_pair_n; i++) {
            mao_radio_send(NULL, s_rec_pair[i].f, s_rec_pair[i].n);
            vTaskDelay(pdMS_TO_TICKS(20));
            sent++;
        }
    }
    ESP_LOGW(TAG, "dev: replayed %d old %s frame(s)", sent, kWhat[what]);
}

/* Malformed bootstrap / link frames (the device must survive, commit nothing). */
static void dev_fuzz(uint64_t unused)
{
    (void)unused;
    uint64_t self_id;
    uint8_t self_mac[6];
    self_identity(&self_id, self_mac);
    int sent = 0;
    for (int i = 0; i < 64; i++) {
        uint8_t f[ODL_MAX_FRAME];
        size_t n;
        odl_msg_t m = { .type = (uint8_t)(1 + i % 7), .ctrl_id = self_id, .dev_id = s_pair_dev.dev_id };
        olc_random(m.txid, sizeof(m.txid));
        olc_random(m.pub, sizeof(m.pub));
        olc_random(m.nonce, sizeof(m.nonce));
        olc_random(m.tag, sizeof(m.tag));
        n = odl_encode(&m, f);
        switch (i % 8) {
        case 0: n = n > 10 ? n - 7 : n; break;            /* truncated */
        case 1: f[2] = 9; break;                           /* wrong version */
        case 2: f[3] = 0x3F; break;                        /* unknown type */
        case 3: memset(&f[n], 0xAA, 20); n += 20; break;   /* oversized */
        case 4: memset(&f[28], 0, 32); break;              /* zero public key / nonce */
        case 5: n = 4; break;                              /* head only */
        case 6: olc_random(&f[4], n - 4); break;           /* random body, right length */
        default: break;                                    /* well formed, stale txid */
        }
        mao_radio_send(NULL, f, n);
        sent++;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    ESP_LOGW(TAG, "dev: sent %d malformed / stale link frames", sent);
}

/* save: remember the current key of the first peer; use: try it again (after
 * a re-pair it must be refused) - 'use' again swaps back. RAM only. */
static peer_t *first_peer_locked(void)
{
    for (int i = 0; i < PEERS_MAX; i++) {
        if (s_peers[i].used) {
            return &s_peers[i];
        }
    }
    return NULL;
}

static void dev_oldkey_save(uint64_t unused)
{
    (void)unused;
    lock();
    peer_t *p = first_peer_locked();
    if (p) {
        memcpy(s_old_key, p->key, ODL_KEY_LEN);
        s_old_saved = true;
    }
    unlock();
    if (p) {
        ESP_LOGW(TAG, "dev: key fp %08" PRIx32 " saved", odl_fingerprint(s_old_key));
    } else {
        ESP_LOGW(TAG, "dev: oldkey save: no paired device");
    }
}

static void dev_oldkey_use(uint64_t unused)
{
    (void)unused;
    lock();
    peer_t *p = first_peer_locked();
    uint32_t fp = 0;
    if (p && s_old_saved) {
        uint8_t tmp[ODL_KEY_LEN];
        memcpy(tmp, p->key, ODL_KEY_LEN);
        memcpy(p->key, s_old_key, ODL_KEY_LEN);
        memcpy(s_old_key, tmp, ODL_KEY_LEN);
        olc_wipe(tmp, sizeof(tmp));
        if (p->sess.valid) {
            mao_radio_set_peer_key(p->mac, NULL);
            odl_session_end(&p->sess);
        }
        p->st = MAO_LINK_OFFLINE;
        p->last_hint_ms = now_ms();
        start_hello_locked(p, now_ms());
        fp = odl_fingerprint(p->key);
    }
    unlock();
    if (fp) {
        ESP_LOGW(TAG, "dev: link now uses key fp %08" PRIx32 " (RAM only)", fp);
    } else {
        ESP_LOGW(TAG, "dev: oldkey use: %s", p ? "nothing saved (save first; RAM only)" : "no paired device");
    }
}

static void dev_command(char *arg)
{
    char *sub = arg ? arg : "";
    char *rest = strchr(sub, ' ');
    if (rest) {
        *rest++ = '\0';
    }
    job_t j = { 0 };
    if (!strcmp(sub, "selftest")) {
        j.type = JOB_SELFTEST;
        post_job(&j);
    } else if (!strcmp(sub, "bench")) {
        j.type = JOB_BENCH;
        post_job(&j);
    } else if (!strcmp(sub, "status")) {
        dev_status();
    } else if (!strcmp(sub, "forcesas") && rest) {
        s_pair.debug_mismatch = !strcmp(rest, "on");
        ESP_LOGW(TAG, "dev: forced SAS mismatch %s", s_pair.debug_mismatch ? "ON" : "off");
    } else if (!strcmp(sub, "oldkey") && rest && (!strcmp(rest, "save") || !strcmp(rest, "use"))) {
        const bool use = !strcmp(rest, "use");
        mao_link_dev_call(use ? dev_oldkey_use : dev_oldkey_save, 0, use ? "oldkey use" : "oldkey save");
    } else if (!strcmp(sub, "replay") && rest &&
               (!strcmp(rest, "hello") || !strcmp(rest, "data") || !strcmp(rest, "pair"))) {
        const uint64_t what = !strcmp(rest, "hello") ? REPLAY_HELLO : !strcmp(rest, "data") ? REPLAY_DATA : REPLAY_PAIR;
        mao_link_dev_call(dev_replay, what, "replay");
    } else if (!strcmp(sub, "fuzz")) {
        mao_link_dev_call(dev_fuzz, 0, "fuzz");
    } else if (!strcmp(sub, "inject") && rest) {
        char *what = strchr(rest, ' ');
        if (what) {
            *what++ = '\0';
        }
        uint64_t a = !strcmp(rest, "uni") ? INJ_UNI : 0;
        a |= what && !strcmp(what, "action") ? INJ_ACTION : what && !strcmp(what, "session") ? INJ_SESSION : 0;
        mao_link_dev_call(dev_inject, a, "inject");
    } else {
        ESP_LOGW(TAG, "link status | selftest | bench | forcesas on|off | oldkey save|use | "
                 "inject bcast|uni set|action|session | replay hello|data|pair | fuzz");
    }
}
#endif

esp_err_t mao_link_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_jobs = xQueueCreate(LINK_QUEUE_LEN, sizeof(job_t));
    const odl_pair_ops_t ops = { pair_send, pair_persist, pair_changed, NULL };
    odl_pairc_init(&s_pair, &ops);
    if (!s_lock || !s_jobs ||
        xTaskCreate(link_task, "mao_link", LINK_TASK_STACK, NULL, LINK_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_MAO_DEV_CONSOLE
    mao_devcmd_register("link", dev_command);
#endif
    return ESP_OK;
}
