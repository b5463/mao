/*
 * Test endpoint link security glue (see lamp_link.h, docs/link_security.md).
 *
 *  - NVS "odd_sec": "auth" (the authorized controller) and "pend" (a
 *    replacement from a completed ceremony), schema 1, 48 B each.
 *  - Receive gate: link frames are handled here; plaintext ODD from anyone
 *    is limited to DISCOVER once a controller is authorized; operational ODD
 *    only arrives inside a valid envelope from that controller's radio.
 *  - Transmit gate: enveloped unicast to the session controller, otherwise
 *    plaintext BROADCAST (a unicast plaintext frame could hit a stale
 *    encrypted peer entry), and an authorized device says only ANNOUNCE in
 *    plaintext.
 */
#include "lamp_link.h"

#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "nvs.h"
#include "odd_bus.h"
#include "odd_link.h"
#include "odd_link_auth.h"
#include "odd_link_pair.h"
#include "odd_link_selftest.h"

static const char *TAG = "LINK";

#define NVS_NS        "odd_sec"
#define REC_SCHEMA    1
#define REC_LEN       48      /* schema(1) rsvd(1) ctrl_id(8) ctrl_mac(6) k_link(32) */

static const uint8_t kBcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static odl_devauth_t s_auth;
static odl_paird_t s_pair;
static nvs_handle_t s_nvs;
static bool s_nvs_ok;
static bool s_rx_auth;                /* the ODD frame being handled came in an envelope */
static int64_t s_revoke_at_us;        /* erase after the REVOKE_ACK went out */
static odl_d_state_t s_last_state;

static struct {
    uint32_t env_drop[ODL_RX_REPLAY + 1];
    uint32_t plain_refused, plain_tx_refused, malformed, bcast_data;
} s_st;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---------------------------------------------------------------------- */
/* Persistence                                                            */
/* ---------------------------------------------------------------------- */

static const char *slot_key(odl_slot_t s)
{
    return s == ODL_SLOT_ACTIVE ? "auth" : "pend";
}

static bool nvs_store(void *ctx, odl_slot_t slot, const odl_credential_t *c)
{
    (void)ctx;
    uint8_t b[REC_LEN] = { REC_SCHEMA, 0 };
    for (int i = 0; i < 8; i++) {
        b[2 + i] = (uint8_t)(c->peer_id >> (56 - 8 * i));
    }
    memcpy(&b[10], c->peer_mac, 6);
    memcpy(&b[16], c->k_link, ODL_KEY_LEN);
    esp_err_t e = s_nvs_ok ? nvs_set_blob(s_nvs, slot_key(slot), b, sizeof(b)) : ESP_ERR_INVALID_STATE;
    if (e == ESP_OK) {
        e = nvs_commit(s_nvs);
    }
    memset(b, 0, sizeof(b));
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "credential %s NOT stored (%s)", slot_key(slot), esp_err_to_name(e));
    }
    return e == ESP_OK;
}

static bool nvs_erase(void *ctx, odl_slot_t slot)
{
    (void)ctx;
    esp_err_t e = s_nvs_ok ? nvs_erase_key(s_nvs, slot_key(slot)) : ESP_ERR_INVALID_STATE;
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        e = ESP_OK;
    }
    if (e == ESP_OK) {
        e = nvs_commit(s_nvs);
    }
    return e == ESP_OK;
}

static bool nvs_load(odl_slot_t slot, odl_credential_t *c)
{
    uint8_t b[64];
    size_t n = sizeof(b);
    if (!s_nvs_ok || nvs_get_blob(s_nvs, slot_key(slot), b, &n) != ESP_OK) {
        return false;
    }
    if (n != REC_LEN || b[0] != REC_SCHEMA) {
        ESP_LOGW(TAG, "credential %s skipped (schema %u, %u bytes): pairing required", slot_key(slot), b[0],
                 (unsigned)n);
        return false;
    }
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < 8; i++) {
        c->peer_id = (c->peer_id << 8) | b[2 + i];
    }
    memcpy(c->peer_mac, &b[10], 6);
    memcpy(c->k_link, &b[16], ODL_KEY_LEN);
    memset(b, 0, sizeof(b));
    uint8_t acc = 0;
    for (int i = 0; i < ODL_KEY_LEN; i++) {
        acc |= c->k_link[i];
    }
    if ((uint16_t)(c->peer_id >> 48) != 0x0DD0 || acc == 0) {
        ESP_LOGW(TAG, "credential %s skipped (malformed): pairing required", slot_key(slot));
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Radio                                                                  */
/* ---------------------------------------------------------------------- */

static void raw_bcast(void *ctx, const uint8_t *f, size_t n)
{
    (void)ctx;
    esp_now_send(kBcast, f, n);
}

static void set_peer(void *ctx, const uint8_t mac[6], const uint8_t *lmk)
{
    (void)ctx;
    if (!lmk) {
        esp_now_del_peer(mac);
        return;
    }
    esp_now_peer_info_t p = { .channel = 0, .ifidx = WIFI_IF_STA, .encrypt = true };
    memcpy(p.peer_addr, mac, 6);
    memcpy(p.lmk, lmk, ODL_LMK_LEN);
    const esp_err_t e = esp_now_is_peer_exist(mac) ? esp_now_mod_peer(&p) : esp_now_add_peer(&p);
    memset(p.lmk, 0, sizeof(p.lmk));
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "encrypted peer not installed (%s)", esp_err_to_name(e));
    }
}

/* ---------------------------------------------------------------------- */
/* Pairing callbacks                                                      */
/* ---------------------------------------------------------------------- */

static bool pair_persist(void *ctx, const odl_credential_t *c)
{
    (void)ctx;
    return odl_devauth_set_pending(&s_auth, c);
}

static void pair_changed(void *ctx)
{
    (void)ctx;
    const odl_d_state_t st = s_pair.st;
    if (st == s_last_state && st != ODL_D_SAS_READY) {
        return;
    }
    if (st == ODL_D_SAS_READY && s_last_state != ODL_D_SAS_READY) {
        char sas[8];
        odl_sas_text(s_pair.keys.sas, sas);
        ESP_LOGW(TAG, "PAIR CODE  %s   controller %016" PRIx64 " - compare with MAO, then 'cam pair accept' "
                 "or 'cam pair reject'", sas, s_pair.t.ctrl_id);
    } else if (st == ODL_D_SAS_READY && s_pair.remote_confirmed && !s_pair.local_accepted) {
        ESP_LOGI(TAG, "pairing: MAO confirmed the code; waiting for local accept");
    } else if (st == ODL_D_ACCEPTED) {
        ESP_LOGW(TAG, "pairing accepted: pending credential stored (fp %08" PRIx32 "); it becomes the "
                 "authorization on the controller's first secure hello", odl_fingerprint(s_auth.pending.k_link));
    } else if (st == ODL_D_FAILED) {
        ESP_LOGW(TAG, "pairing failed: %s (no credential stored)", odl_fail_name(s_pair.fail));
    } else if (st == ODL_D_LOCKED && s_last_state != ODL_D_LOCKED) {
        ESP_LOGI(TAG, "pairing mode closed");
    } else if (st == ODL_D_EXCHANGING) {
        ESP_LOGI(TAG, "pairing: key exchange with controller %016" PRIx64, s_pair.t.ctrl_id);
    }
    s_last_state = st;
}

/* ---------------------------------------------------------------------- */

void lamp_link_init(uint64_t self_id, const uint8_t self_mac[6])
{
    if (nvs_open(NVS_NS, NVS_READWRITE, &s_nvs) == ESP_OK) {
        s_nvs_ok = true;
    } else {
        ESP_LOGE(TAG, "security store unavailable: no controller can be authorized this boot");
    }
    static const uint8_t pmk[16] = ODL_ESPNOW_PMK;
    esp_now_set_pmk(pmk);                          /* the documented ODD PMK, not the IDF default */
    esp_now_peer_info_t bp = { .channel = 0, .ifidx = WIFI_IF_STA, .encrypt = false };
    memcpy(bp.peer_addr, kBcast, 6);
    if (!esp_now_is_peer_exist(kBcast)) {
        esp_now_add_peer(&bp);
    }
    odl_credential_t act, pend;
    const bool has_act = nvs_load(ODL_SLOT_ACTIVE, &act);
    const bool has_pend = nvs_load(ODL_SLOT_PENDING, &pend);
    const odl_devauth_ops_t ops = { nvs_store, nvs_erase, set_peer, raw_bcast, NULL };
    odl_devauth_init(&s_auth, &ops, self_id, self_mac, has_act ? &act : NULL, has_pend ? &pend : NULL);
    const odl_pair_ops_t pops = { raw_bcast, pair_persist, pair_changed, NULL };
    odl_paird_init(&s_pair, &pops, self_id, self_mac);
    if (has_act) {
        ESP_LOGI(TAG, "authorized controller %016" PRIx64 " (key fp %08" PRIx32 ")%s", act.peer_id,
                 odl_fingerprint(act.k_link), has_pend ? ", replacement pending" : "");
    } else {
        ESP_LOGI(TAG, "no authorized controller%s: open (unpaired) mode", has_pend ? " (pending one)" : "");
    }
    memset(&act, 0, sizeof(act));
    memset(&pend, 0, sizeof(pend));
}

static void handle_ctl(const uint8_t *in, size_t len)
{
    if (len >= 3 && in[2] == ODL_CTL_REVOKE) {
        const uint8_t ack[3] = { ODL_CTL_MAGIC0, ODL_CTL_MAGIC1, ODL_CTL_REVOKE_ACK };
        uint8_t f[ODL_MAX_FRAME];
        const size_t n = odl_devauth_wrap(&s_auth, ack, sizeof(ack), f);
        if (n) {
            esp_now_send(s_auth.sess_mac, f, n);
        }
        ESP_LOGW(TAG, "revocation received from the controller: acknowledged, erasing");
        s_revoke_at_us = esp_timer_get_time() + 150 * 1000;   /* after the ACK is on air */
    }
}

void lamp_link_rx(const uint8_t mac[6], const uint8_t *data, size_t len, int8_t rssi, bool bcast)
{
    if (odl_is_link(data, len)) {
        if (len >= ODL_HEAD_LEN && data[3] == ODL_DATA) {
            if (bcast) {
                s_st.bcast_data++;
                return;
            }
            const uint8_t *in;
            size_t il;
            const odl_rx_t r = odl_devauth_unwrap(&s_auth, mac, data, len, &in, &il);
            if (r != ODL_RX_OK) {
                if (s_st.env_drop[r]++ < 5) {
                    ESP_LOGW(TAG, "secure frame dropped: %s", odl_rx_name(r));
                }
                return;
            }
            if (il >= 3 && in[0] == ODL_CTL_MAGIC0 && in[1] == ODL_CTL_MAGIC1) {
                handle_ctl(in, il);
                return;
            }
            s_rx_auth = true;
            odd_bus_input(mac, in, il, rssi);
            s_rx_auth = false;
            return;
        }
        odl_msg_t m;
        if (!odl_decode(data, len, &m)) {
            s_st.malformed++;
            return;
        }
        if (m.type == ODL_HELLO) {
            const odl_hello_result_t r = odl_devauth_hello(&s_auth, &m, mac);
            if (r == ODL_HELLO_PROMOTED) {
                ESP_LOGW(TAG, "controller %016" PRIx64 " is now the authorized controller (key fp %08" PRIx32
                         "); any previous controller is no longer accepted", s_auth.active.peer_id,
                         odl_fingerprint(s_auth.active.k_link));
            }
            if (r == ODL_HELLO_PROMOTED || r == ODL_HELLO_NEW_SESSION) {
                ESP_LOGI(TAG, "secure session with %016" PRIx64 " (fresh LMK, session %02x%02x..)",
                         s_auth.active.peer_id, s_auth.sess.sid[0], s_auth.sess.sid[1]);
            } else if (r == ODL_HELLO_REFUSED || r == ODL_HELLO_REPLAYED) {
                ESP_LOGW(TAG, "hello refused: %s", odl_hello_result_name(r));
            }
            return;
        }
        const int64_t t0 = esp_timer_get_time();
        odl_paird_rx(&s_pair, &m, mac, now_ms());
        const int64_t us = esp_timer_get_time() - t0;
        if (us > 20000) {
            ESP_LOGI(TAG, "pairing step (type %02x): %lld us", m.type, (long long)us);
        }
        return;
    }
    /* plaintext ODD: now always broadcast on air, so address it by dst_id
     * (header offset 16, little-endian): ANY or us, nothing else. */
    if (len >= 24) {
        uint64_t dst = 0;
        for (int i = 7; i >= 0; i--) {
            dst = (dst << 8) | data[16 + i];
        }
        if (dst != 0 && dst != s_auth.self_id) {
            return;
        }
    }
    if (odl_devauth_locked(&s_auth) && (len < 4 || data[3] != ODD_MSG_DISCOVER)) {
        if (s_st.plain_refused++ < 5) {
            ESP_LOGW(TAG, "plaintext ODD type %u refused: operational traffic needs the secure link",
                     len >= 4 ? data[3] : 0);
        }
        return;
    }
    s_rx_auth = false;
    odd_bus_input(mac, data, len, rssi);
}

bool lamp_link_rx_trusted(void)
{
    return s_rx_auth || !odl_devauth_locked(&s_auth);
}

int lamp_link_tx(const uint8_t *dst, const uint8_t *frame, size_t len)
{
    /* A plaintext DISCOVER is answered in plaintext (a public ANNOUNCE, and
     * the controller's "candidate seen" hint after either side rebooted);
     * everything else to the session controller goes in the envelope. */
    const bool public_reply = !s_rx_auth && len >= 4 && frame[3] == ODD_MSG_ANNOUNCE;
    if (dst && !public_reply && odl_devauth_is_session_peer(&s_auth, dst)) {
        uint8_t f[ODL_MAX_FRAME];
        const size_t n = odl_devauth_wrap(&s_auth, frame, len, f);
        return n ? esp_now_send(dst, f, n) : ESP_FAIL;
    }
    if (odl_devauth_locked(&s_auth) && (len < 4 || frame[3] != ODD_MSG_ANNOUNCE)) {
        s_st.plain_tx_refused++;
        return ESP_ERR_INVALID_STATE;     /* no plaintext fallback for an authorized device */
    }
    return esp_now_send(kBcast, frame, len);
}

uint32_t lamp_link_tick(void)
{
    odl_paird_tick(&s_pair, now_ms());
    if (s_revoke_at_us && esp_timer_get_time() >= s_revoke_at_us) {
        s_revoke_at_us = 0;
        odl_devauth_revoke(&s_auth);
        ESP_LOGW(TAG, "authorization revoked: no authorized controller (open mode)");
    }
    return (odl_paird_in_mode(&s_pair) || s_revoke_at_us) ? 100 : 1000;
}

void lamp_link_command(int sub, int32_t v)
{
    switch (sub) {
    case LINK_CMD_PAIRMODE:
        if (v > 0) {
            if (odl_devauth_locked(&s_auth) && s_auth.has_active) {
                ESP_LOGW(TAG, "pairing mode: %ld s - replacing controller %016" PRIx64 " if a ceremony completes "
                         "(it stays authorized until then)", (long)v, s_auth.active.peer_id);
            } else {
                ESP_LOGW(TAG, "pairing mode: %ld s", (long)v);
            }
            odl_paird_mode(&s_pair, (uint32_t)v * 1000u, now_ms());
        } else {
            odl_paird_mode(&s_pair, 0, now_ms());
        }
        break;
    case LINK_CMD_ACCEPT:
        if (s_pair.st == ODL_D_SAS_READY) {
            ESP_LOGI(TAG, "pairing: accepted locally");
            odl_paird_accept(&s_pair, now_ms());
        } else {
            ESP_LOGW(TAG, "pair accept: no code to accept (%s)", odl_d_state_name(s_pair.st));
        }
        break;
    case LINK_CMD_REJECT:
        ESP_LOGI(TAG, "pairing: rejected locally");
        odl_paird_reject(&s_pair);
        break;
    case LINK_CMD_STATUS:
        ESP_LOGI(TAG, "security: %s%s, pair %s, session %s | hello ok=%" PRIu32 " bad=%" PRIu32 " promoted=%" PRIu32
                 " | env drop tag=%" PRIu32 " replay=%" PRIu32 " session=%" PRIu32 " none=%" PRIu32
                 " | plaintext refused rx=%" PRIu32 " tx=%" PRIu32,
                 s_auth.has_active ? "authorized" : "open", s_auth.has_pending ? " (+pending)" : "",
                 odl_d_state_name(s_pair.st), s_auth.sess.valid ? "yes" : "no", s_auth.hello_ok, s_auth.hello_bad,
                 s_auth.promoted, s_st.env_drop[ODL_RX_BAD_TAG], s_st.env_drop[ODL_RX_REPLAY],
                 s_st.env_drop[ODL_RX_WRONG_SESSION], s_st.env_drop[ODL_RX_NO_SESSION], s_st.plain_refused,
                 s_st.plain_tx_refused);
        if (s_auth.has_active) {
            ESP_LOGI(TAG, "  controller %016" PRIx64 " key fp %08" PRIx32, s_auth.active.peer_id,
                     odl_fingerprint(s_auth.active.k_link));
        }
        break;
    case LINK_CMD_RESET:
        odl_paird_mode(&s_pair, 0, now_ms());
        odl_devauth_revoke(&s_auth);
        ESP_LOGW(TAG, "dev: endpoint security reset - no authorized controller");
        break;
    case LINK_CMD_CORRUPT:
        if (s_auth.has_active) {
            s_auth.active.k_link[0] ^= 0x5A;
            s_auth.active.k_link[31] ^= 0xA5;
            nvs_store(NULL, ODL_SLOT_ACTIVE, &s_auth.active);
            ESP_LOGW(TAG, "dev: link key corrupted (fp now %08" PRIx32 ")", odl_fingerprint(s_auth.active.k_link));
        }
        break;
    case LINK_CMD_FORCESAS:
        s_pair.debug_mismatch = v != 0;
        ESP_LOGW(TAG, "dev: forced SAS mismatch %s", v ? "ON" : "off");
        break;
    case LINK_CMD_PEERPLAIN:
        if (s_auth.sess.valid) {
            esp_now_peer_info_t p = { .channel = 0, .ifidx = WIFI_IF_STA, .encrypt = false };
            memcpy(p.peer_addr, s_auth.sess_mac, 6);
            esp_now_mod_peer(&p);
            ESP_LOGW(TAG, "dev: controller peer entry is now PLAINTEXT (driver filtering off: app gate only)");
        }
        break;
    case LINK_CMD_SELFTEST: {
        int total = 0;
        const int pass = odl_selftest(&total);
        ESP_LOGI(TAG, "link selftest: %d/%d", pass, total);
        break;
    }
    default:
        break;
    }
}
