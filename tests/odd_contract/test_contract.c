/*
 * Host tests for the ODD device contract (M4.0): codec structure rules,
 * contract evaluation (components/odd_bus/odd_contract.c) and the
 * capability -> control mapping (components/mao_devices/mao_device_view.c).
 *
 * SYNTHETIC TEST DATA ONLY: byte arrays and capability tables. Not a device
 * simulator; CAMERA 01 / LAMP 01 tables are the only product examples.
 */
#include <stdio.h>
#include <string.h>
#include "odd_bus.h"
#include "odd_contract.h"
#include "mao_devices.h"

static int s_pass, s_fail;
#define CHECK(c) do { if (c) { s_pass++; } else { s_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

#define ACT(id, sem) { (id), ODD_CAP_ACTION, ODD_CAP_F_WRITE, (sem), (sem), 1 }
#define READY(id)    { (id), ODD_CAP_READY, ODD_CAP_F_READ | ODD_CAP_F_NOTIFY, 0, 1, 1 }
#define STORAGE(id)  { (id), ODD_CAP_STORAGE, ODD_CAP_F_READ | ODD_CAP_F_NOTIFY, 0, 100, 1 }
#define POWER(id)    { (id), ODD_CAP_POWER, ODD_CAP_F_READ | ODD_CAP_F_WRITE | ODD_CAP_F_NOTIFY, 0, 1, 1 }
#define LEVEL(id)    { (id), ODD_CAP_LEVEL, ODD_CAP_F_READ | ODD_CAP_F_WRITE | ODD_CAP_F_NOTIFY, 0, 100, 1 }

/* The bench profiles, exactly as devices/lamp_01_test declares them. */
static const odd_capability_t CAMERA[] = { ACT(1, ODD_ACTION_CAPTURE), ACT(2, ODD_ACTION_IDENTIFY),
                                           ACT(3, ODD_ACTION_SYNC_TEST), READY(4), STORAGE(5) };
static const odd_capability_t LIGHT[] = { POWER(1), LEVEL(2), ACT(3, ODD_ACTION_IDENTIFY) };

static uint8_t F[ODD_MAX_FRAME];

/* Encode a CAPABILITIES frame through the real encoder. */
static size_t caps_frame(const odd_capability_t *c, int n, bool desc, uint8_t major, uint8_t minor)
{
    odd_message_t b = { 0 };
    b.u.caps.count = (uint8_t)n;
    b.u.caps.has_descriptor = desc;
    b.u.caps.major = major;
    b.u.caps.minor = minor;
    memcpy(b.u.caps.cap, c, sizeof(*c) * (size_t)n);
    const odd_header_t h = { .type = ODD_MSG_CAPABILITIES, .seq = 7, .src_id = 0x0DD0A0764E1D86D4ull };
    return odd_encode(&h, &b, F, sizeof(F));
}

/* A frame from raw payload bytes (for layouts the encoder cannot produce). */
static size_t raw_frame(uint8_t type, const uint8_t *payload, size_t n)
{
    memset(F, 0, sizeof(F));
    F[0] = 'O'; F[1] = 'D'; F[2] = ODD_BUS_PROTOCOL_VERSION; F[3] = type;
    F[4] = 9; F[7] = (uint8_t)n;
    F[8] = 0x42;                                     /* src id != 0 */
    memcpy(F + ODD_HEADER_LEN, payload, n);
    const uint16_t crc = odd_crc16(F, ODD_HEADER_LEN + n);
    F[ODD_HEADER_LEN + n] = (uint8_t)crc;
    F[ODD_HEADER_LEN + n + 1] = (uint8_t)(crc >> 8);
    return ODD_HEADER_LEN + n + 2;
}

static odd_message_t M;
static odd_contract_eval_t E;

static odd_decode_result_t decode_eval(size_t n)
{
    const odd_decode_result_t r = odd_decode(F, n, &M);
    if (r == ODD_DECODE_OK) {
        odd_contract_evaluate(&M.u.caps, &E);
    }
    return r;
}

/* The controls MAO would offer for the decoded set. */
static mao_device_controls_t controls(void)
{
    mao_device_t d = { .cap_count = M.u.caps.count, .online = true, .described = true };
    for (int i = 0; i < M.u.caps.count; i++) {
        d.caps[i].cap = M.u.caps.cap[i];
        d.caps[i].usable = (E.usable >> i) & 1;
    }
    mao_device_controls_t c;
    mao_device_controls(&d, &c);
    return c;
}

static int popcount(uint8_t v)
{
    int n = 0;
    for (; v; v &= (uint8_t)(v - 1)) {
        n++;
    }
    return n;
}

int main(void)
{
    mao_device_controls_t c;

    /* ---- existing devices, legacy (no descriptor) = contract 1.0 ---- */
    size_t n = caps_frame(CAMERA, 5, false, 0, 0);
    CHECK(F[ODD_HEADER_LEN] == 5);                                   /* legacy bytes: count first */
    CHECK(decode_eval(n) == ODD_DECODE_OK && M.u.caps.major == 1 && M.u.caps.minor == 0 && !M.u.caps.has_descriptor);
    CHECK(E.compat == ODD_COMPAT_COMPATIBLE && E.usable == 0x1F && E.unknown == 0 && E.ambiguous == 0);
    c = controls();
    CHECK(c.action_count == 3 && c.primary_action >= 0 && c.action_sem[c.primary_action] == ODD_ACTION_CAPTURE);
    CHECK(c.primary_idx == 0 && c.ready_idx == 3 && c.storage_idx == 4 && c.level_idx < 0 && c.toggle_idx < 0);
    n = caps_frame(LIGHT, 3, false, 0, 0);
    CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_COMPATIBLE && E.usable == 0x07);
    c = controls();
    CHECK(c.level_idx == 1 && c.toggle_idx == 0 && c.primary_idx == 1 && c.action_count == 1 && c.primary_action < 0);

    /* ---- version matrix (MAO implements 1.1) ---- */
    n = caps_frame(CAMERA, 5, true, 1, 1);                          /* same */
    CHECK(F[ODD_HEADER_LEN] == ODD_CAPS_DESCRIPTOR && decode_eval(n) == ODD_DECODE_OK && M.u.caps.has_descriptor);
    CHECK(M.u.caps.major == 1 && M.u.caps.minor == 1 && E.compat == ODD_COMPAT_COMPATIBLE && !E.newer_minor);
    n = caps_frame(CAMERA, 5, true, 1, 0);                          /* older minor */
    CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_COMPATIBLE);
    {   /* newer minor 1.4 with descriptor fields this build does not know */
        uint8_t p[64] = { ODD_CAPS_DESCRIPTOR, 5, 1, 4, 0xAA, 0xBB, 0xCC, 3 };
        size_t k = 8;
        for (int i = 0; i < 3; i++) {
            const odd_capability_t *r = &LIGHT[i];
            p[k++] = r->id; p[k++] = r->type; p[k++] = r->flags;
            for (int j = 0; j < 4; j++) { p[k + j] = (uint8_t)(r->min >> (8 * j)); } k += 4;
            for (int j = 0; j < 4; j++) { p[k + j] = (uint8_t)(r->max >> (8 * j)); } k += 4;
            for (int j = 0; j < 4; j++) { p[k + j] = (uint8_t)(r->step >> (8 * j)); } k += 4;
        }
        n = raw_frame(ODD_MSG_CAPABILITIES, p, k);
        CHECK(decode_eval(n) == ODD_DECODE_OK && M.u.caps.minor == 4 && M.u.caps.count == 3);
        CHECK(E.compat == ODD_COMPAT_LIMITED && E.newer_minor && E.usable == 0x07);   /* known subset, honestly */
        c = controls();
        CHECK(c.level_idx == 1 && c.toggle_idx == 0);
    }
    {   /* major 2: whatever follows is never interpreted as v1 records */
        const uint8_t p[] = { ODD_CAPS_DESCRIPTOR, 2, 2, 0, 0xFF, 0x01, 0x02, 0x03 };
        n = raw_frame(ODD_MSG_CAPABILITIES, p, sizeof(p));
        CHECK(decode_eval(n) == ODD_DECODE_OK && M.u.caps.major == 2 && M.u.caps.count == 0);
        CHECK(E.compat == ODD_COMPAT_INCOMPATIBLE && E.usable == 0);
        c = controls();
        CHECK(c.primary_idx < 0 && c.action_count == 0 && c.level_idx < 0 && c.toggle_idx < 0);
    }
    n = caps_frame(LIGHT, 3, true, 0, 9);                           /* major 0 */
    CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_INCOMPATIBLE && E.usable == 0);
    {   /* descriptor structure */
        const uint8_t shortd[] = { ODD_CAPS_DESCRIPTOR, 1, 1, 0 };
        CHECK(odd_decode(F, raw_frame(ODD_MSG_CAPABILITIES, shortd, sizeof(shortd)), &M) == ODD_DECODE_MALFORMED);
        const uint8_t longd[] = { ODD_CAPS_DESCRIPTOR, ODD_CAPS_DESC_MAX + 1, 1, 0 };
        CHECK(odd_decode(F, raw_frame(ODD_MSG_CAPABILITIES, longd, sizeof(longd)), &M) == ODD_DECODE_MALFORMED);
        const uint8_t trunc[] = { ODD_CAPS_DESCRIPTOR, 4, 1, 0 };
        CHECK(odd_decode(F, raw_frame(ODD_MSG_CAPABILITIES, trunc, sizeof(trunc)), &M) == ODD_DECODE_MALFORMED);
        const uint8_t nocount[] = { ODD_CAPS_DESCRIPTOR, 2, 1, 1 };   /* same major, records missing */
        CHECK(odd_decode(F, raw_frame(ODD_MSG_CAPABILITIES, nocount, sizeof(nocount)), &M) == ODD_DECODE_MALFORMED);
    }
    /* the largest set fits, with the descriptor, inside the M3.1 envelope */
    {
        odd_capability_t big[ODD_MAX_CAPS];
        for (int i = 0; i < ODD_MAX_CAPS; i++) {
            big[i] = (odd_capability_t) { (uint8_t)(i + 1), (uint8_t)(40 + i), ODD_CAP_F_READ, 0, 1, 1 };
        }
        n = caps_frame(big, ODD_MAX_CAPS, true, 1, 1);
        CHECK(n > 0 && n <= 218 && decode_eval(n) == ODD_DECODE_OK);
    }

    /* ---- unknown capabilities: carried, never interpreted ---- */
    {
        const odd_capability_t future[] = { ACT(1, ODD_ACTION_CAPTURE), STORAGE(2),
                                            { 3, 42, ODD_CAP_F_WRITE, 9, -9, 0 } };   /* its own semantics */
        n = caps_frame(future, 3, true, 1, 1);
        CHECK(decode_eval(n) == ODD_DECODE_OK);                                   /* G5 */
        CHECK(E.compat == ODD_COMPAT_LIMITED && E.usable == 0x03 && E.unknown == 1);
        c = controls();
        CHECK(c.primary_idx == 0 && c.storage_idx == 1 && c.toggle_idx < 0 && c.level_idx < 0 && c.action_count == 1);
    }
    {
        const odd_capability_t sneaky[] = { { 1, 9, ODD_CAP_F_READ | ODD_CAP_F_WRITE, 0, 1, 1 }, LEVEL(2) };
        n = caps_frame(sneaky, 2, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && !(E.usable & 1));
        c = controls();
        CHECK(c.toggle_idx < 0 && c.level_idx == 1);                              /* G1: never a POWER */
    }
    {
        const odd_capability_t only[] = { { 1, 40, 1, 0, 1, 1 }, { 2, 41, 3, 0, 0, 0 } };
        n = caps_frame(only, 2, true, 1, 1);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_LIMITED && E.usable == 0 && E.unknown == 2);
        c = controls();
        CHECK(c.primary_idx < 0 && c.action_count == 0 && c.ready_idx < 0);
    }
    {
        const odd_capability_t unk_sem[] = { ACT(1, 7), ACT(2, ODD_ACTION_IDENTIFY) };
        n = caps_frame(unk_sem, 2, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.usable == 0x02 && E.unknown == 1);   /* G3 */
        c = controls();
        CHECK(c.action_count == 1 && c.action_sem[0] == ODD_ACTION_IDENTIFY);
    }
    n = caps_frame(NULL, 0, true, 1, 1);                           /* nothing at all */
    CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_LIMITED && E.usable == 0);

    /* ---- malformed known capabilities: the whole set is refused ---- */
    {
        odd_capability_t bad[5];
        memcpy(bad, CAMERA, sizeof(CAMERA));
        bad[0].max = ODD_ACTION_CAPTURE + 1;                       /* CAPTURE min != max */
        CHECK(odd_decode(F, caps_frame(bad, 5, false, 0, 0), &M) == ODD_DECODE_MALFORMED);
        memcpy(bad, CAMERA, sizeof(CAMERA));
        bad[0].flags = ODD_CAP_F_READ;                             /* an ACTION one cannot invoke */
        CHECK(odd_decode(F, caps_frame(bad, 5, false, 0, 0), &M) == ODD_DECODE_MALFORMED);
        memcpy(bad, CAMERA, sizeof(CAMERA));
        bad[3].flags |= ODD_CAP_F_WRITE;                           /* READY writable */
        CHECK(odd_decode(F, caps_frame(bad, 5, false, 0, 0), &M) == ODD_DECODE_MALFORMED);
        memcpy(bad, CAMERA, sizeof(CAMERA));
        bad[4].id = 1;                                             /* duplicate cap id (G6) */
        CHECK(odd_decode(F, caps_frame(bad, 5, false, 0, 0), &M) == ODD_DECODE_MALFORMED);
        memcpy(bad, CAMERA, sizeof(CAMERA));
        bad[1].id = 0;
        CHECK(odd_decode(F, caps_frame(bad, 5, false, 0, 0), &M) == ODD_DECODE_MALFORMED);
        odd_capability_t lb[3];
        memcpy(lb, LIGHT, sizeof(LIGHT));
        lb[0].max = 5;                                             /* POWER is 0..1 (G7) */
        CHECK(odd_decode(F, caps_frame(lb, 3, false, 0, 0), &M) == ODD_DECODE_MALFORMED);
        memcpy(lb, LIGHT, sizeof(LIGHT));
        lb[1].min = 100;
        lb[1].max = 0;                                             /* LEVEL min > max */
        CHECK(odd_decode(F, caps_frame(lb, 3, false, 0, 0), &M) == ODD_DECODE_MALFORMED);
        memcpy(lb, LIGHT, sizeof(LIGHT));
        lb[1].step = 0;
        CHECK(odd_decode(F, caps_frame(lb, 3, false, 0, 0), &M) == ODD_DECODE_MALFORMED);
    }

    /* ---- ambiguity: decided on the set, never by packet order (G2) ---- */
    {
        const odd_capability_t two[] = { ACT(1, ODD_ACTION_CAPTURE), ACT(2, ODD_ACTION_CAPTURE),
                                         ACT(3, ODD_ACTION_IDENTIFY), READY(4) };
        n = caps_frame(two, 4, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.ambiguous == 2 && E.usable == 0x0C && E.compat == ODD_COMPAT_LIMITED);
        c = controls();
        CHECK(c.primary_action < 0 && c.action_count == 1 && c.action_sem[0] == ODD_ACTION_IDENTIFY);
        const odd_capability_t lv[] = { LEVEL(1), POWER(2), LEVEL(3) };
        const odd_capability_t lv_swapped[] = { LEVEL(3), LEVEL(1), POWER(2) };
        n = caps_frame(lv, 3, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.ambiguous == 2);
        c = controls();
        const int toggle_a = c.toggle_idx, level_a = c.level_idx;
        n = caps_frame(lv_swapped, 3, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.ambiguous == 2);
        c = controls();
        CHECK(level_a < 0 && c.level_idx < 0 && toggle_a == 1 && c.toggle_idx == 2 && c.primary_idx == 2);
        const odd_capability_t rr[] = { READY(1), READY(2), ACT(3, ODD_ACTION_CAPTURE) };
        n = caps_frame(rr, 3, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.usable == 0x04);
        c = controls();
        CHECK(c.ready_idx < 0 && c.primary_idx == 2);
    }

    /* ---- optional capabilities and the primary control ---- */
    {
        const odd_capability_t cam_min[] = { ACT(1, ODD_ACTION_CAPTURE), STORAGE(2) };   /* no SYNC, no IDENTIFY */
        n = caps_frame(cam_min, 2, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_COMPATIBLE);
        c = controls();
        CHECK(c.primary_idx == 0 && c.action_count == 1);
        const odd_capability_t light_min[] = { POWER(1), LEVEL(2) };                    /* no IDENTIFY */
        n = caps_frame(light_min, 2, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_COMPATIBLE);
        const odd_capability_t power_only[] = { POWER(5) };                             /* no LEVEL */
        n = caps_frame(power_only, 1, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_COMPATIBLE);
        c = controls();
        CHECK(c.primary_idx == 0 && c.toggle_idx == 0);
        const odd_capability_t id_only[] = { ACT(1, ODD_ACTION_IDENTIFY) };             /* no primary */
        n = caps_frame(id_only, 1, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.compat == ODD_COMPAT_COMPATIBLE);
        c = controls();
        CHECK(c.primary_idx < 0 && c.action_count == 1);
        const odd_capability_t flat[] = { { 1, ODD_CAP_LEVEL, ODD_CAP_F_WRITE, 5, 5, 1 }, POWER(2) };
        n = caps_frame(flat, 2, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK && E.unusable == 1 && E.compat == ODD_COMPAT_LIMITED);
        c = controls();
        CHECK(c.level_idx < 0 && c.toggle_idx == 1);
        const odd_capability_t ro_level[] = { { 1, ODD_CAP_LEVEL, ODD_CAP_F_READ, 0, 100, 1 } };
        n = caps_frame(ro_level, 1, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK);
        c = controls();
        CHECK(c.level_idx < 0 && c.primary_idx < 0);                               /* a fact, not a dial */
    }
    /* unusable capabilities never reach the controls */
    {
        n = caps_frame(CAMERA, 5, false, 0, 0);
        CHECK(decode_eval(n) == ODD_DECODE_OK);
        E.usable = 0;
        c = controls();
        CHECK(c.action_count == 0 && c.ready_idx < 0 && c.storage_idx < 0 && c.primary_idx < 0);
    }

    /* ---- profile change: each description stands alone ---- */
    n = caps_frame(CAMERA, 5, false, 0, 0);
    CHECK(decode_eval(n) == ODD_DECODE_OK && popcount(E.usable) == 5);
    n = caps_frame(LIGHT, 3, false, 0, 0);
    CHECK(decode_eval(n) == ODD_DECODE_OK && E.usable == 0x07 && E.compat == ODD_COMPAT_COMPATIBLE);
    c = controls();
    CHECK(c.action_count == 1 && c.ready_idx < 0 && c.storage_idx < 0 && c.level_idx == 1);   /* nothing of CAMERA */

    /* ---- device type never gates controls ---- */
    {
        odd_message_t a = { 0 };
        a.u.info.device_type = 99;                   /* a type this build does not know */
        a.u.info.cap_count = 2;
        strcpy(a.u.info.name, "LAMP 01");
        const odd_header_t h = { .type = ODD_MSG_ANNOUNCE, .src_id = 0x0DD0A0764E1D86D4ull };
        n = odd_encode(&h, &a, F, sizeof(F));
        CHECK(odd_decode(F, n, &M) == ODD_DECODE_OK && M.u.info.device_type == 99);
        /* ...and its capabilities are evaluated exactly like anyone's: the
         * evaluation has no device-type input at all. */
    }

    /* ---- statuses and messages this build does not know ---- */
    {
        const uint8_t ack[] = { 7, 0, 42, 3, 0, 0, 0, 0 };                 /* status 42 */
        CHECK(odd_decode(F, raw_frame(ODD_MSG_ACK, ack, sizeof(ack)), &M) == ODD_DECODE_OK && M.u.ack.status == 42);
        uint8_t res[] = { 1, 0, 0, 0, 0, 0, 0, 0, 3, 7, 0, 5 };           /* incarnation 1, cap 3, seq 7, result 5 */
        size_t k = raw_frame(ODD_MSG_ACTION_RESULT, res, sizeof(res));
        F[6] = ODD_FRAME_F_INCARNATION;
        uint16_t crc = odd_crc16(F, k - 2);
        F[k - 2] = (uint8_t)crc; F[k - 1] = (uint8_t)(crc >> 8);
        CHECK(odd_decode(F, k, &M) == ODD_DECODE_OK && M.u.action_result.result == 5);
        res[11] = 0;                                                       /* 0 stays invalid */
        k = raw_frame(ODD_MSG_ACTION_RESULT, res, sizeof(res));
        F[6] = ODD_FRAME_F_INCARNATION;
        crc = odd_crc16(F, k - 2);
        F[k - 2] = (uint8_t)crc; F[k - 1] = (uint8_t)(crc >> 8);
        CHECK(odd_decode(F, k, &M) == ODD_DECODE_MALFORMED);
        CHECK(odd_decode(F, raw_frame(0x0C, NULL, 0), &M) == ODD_DECODE_MALFORMED);   /* unknown message */
        k = raw_frame(ODD_MSG_GET_CAPS, NULL, 0);
        F[2] = 2;                                                          /* another wire version */
        CHECK(odd_decode(F, k, &M) == ODD_DECODE_BAD_VERSION);
    }

    CHECK(strcmp(odd_compat_name(ODD_COMPAT_LIMITED), "LIMITED") == 0 && strcmp(odd_compat_name(99), "?") == 0);
    printf("odd contract: %d checks passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
