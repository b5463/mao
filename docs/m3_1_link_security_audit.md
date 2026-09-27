# M3.1 link security: platform audit (engineering note)

This is the first deliverable of M3.1: what the ESP-IDF build in this
repository actually provides, what was measured on the bench, where the gaps
are, and the design decision the gaps force. Protocol behaviour has not
changed yet.

Sources: headers, Kconfig and docs of the project's own IDF tree
(`%USERPROFILE%/esp/v6.0.3/esp-idf`), the project `sdkconfig`, and a
two-board experiment (MAO 98:88:e0:d4:d0:90, LOLIN 60:55:f9:23:53:24). The
experiment firmware was temporary and is not committed.

## 1. Versions

| Item | Value |
|---|---|
| ESP-IDF | **v6.0.3** (`version.txt`) |
| mbedTLS | **4.1.1** on **TF-PSA-Crypto 1.1.1** |

The public crypto API of this generation is **PSA Crypto** (`psa/crypto.h`).
The legacy `mbedtls_ecdh_*` and `mbedtls_hkdf` interfaces are not the
supported surface. IDF runs `psa_crypto_init()` itself at boot
(`components/mbedtls/port/esp_psa_crypto_init.c`, `ESP_SYSTEM_INIT_FN`).

## 2–6. ESP-NOW security (`esp_wifi/include/esp_now.h`, `docs/.../esp_now.rst`)

| Question | Answer |
|---|---|
| Protection | **CCMP** (IEEE 802.11-2012) on the vendor-specific action frame, i.e. AES-128-CCM |
| Per-peer key | **LMK**, `ESP_NOW_KEY_LEN` = **16 B**, set in `esp_now_peer_info_t.lmk` with `encrypt = true` |
| PMK | 16 B, `esp_now_set_pmk()`; "used to encrypt LMK with AES-128"; a built-in default is used if unset |
| Broadcast / multicast | **cannot be encrypted** ("Encrypting multicast vendor-specific action frame is not supported") |
| Encrypted peers | `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM` = **7** in this sdkconfig. The range on C3 is 0–17; the hardware keys are shared with SoftAP clients, which MAO does not use. The header's `ESP_NOW_MAX_ENCRYPT_PEER_NUM` of 6 is stale; the runtime limit is the Kconfig value. |
| Receive metadata | `esp_now_recv_info_t` = `src_addr`, `des_addr`, `rx_ctrl`. **There is no "was encrypted" flag.** |

## 7–8. Measured on the bench (two boards, IDF 6.0.3)

Each peer-mode change was confirmed by its log line before sending. There
were 10 trials per row, and each frame is tagged with its sender's peer mode.

| Case | Delivered to the receive callback |
|---|---|
| Both peers encrypted, same LMK (MAO → LOLIN / LOLIN → MAO) | 10/10 / 10/10 |
| Plaintext unicast from a MAC the receiver holds as an **encrypted** peer (both directions) | **0/10 / 0/10** |
| Encrypted with a **wrong LMK** (both directions) | **0/10 / 0/10** |
| Neither side encrypted (baseline) | 10/10 |
| Plaintext **broadcast** while the peers are encrypted | delivered (both directions) |
| Same LMK, **different PMK** on the two sides | **0/10** (same PMK: 10/10) |

What this establishes:

* **7. Plaintext vs encrypted from the same MAC.** The driver drops plaintext
  and wrong-key unicast from a MAC registered as an encrypted peer before the
  callback. This is observed behaviour of the closed Wi-Fi library in this
  IDF version. It is **not documented**, and the callback cannot confirm it per
  frame (no flag). A plaintext unicast from a *different* MAC, or any
  broadcast, is still delivered with any ODD `src_id` in its header.
* **8. Discovery.** It can stay plaintext broadcast. `des_addr` separates
  broadcast from unicast, so the application can tell the two paths apart.
* **PMK.** It is part of the effective key: peers must share it. It is an
  ecosystem-wide constant, not a per-device secret. Per-pair secrecy comes
  from the LMK. Leaving the undocumented built-in default would make security
  depend on a value we neither control nor document.

## 9–10. Crypto primitives (PSA, enabled in this build)

| Need | PSA facility | Enabled by |
|---|---|---|
| Ephemeral ECDH | `psa_generate_key` + `psa_raw_key_agreement(PSA_ALG_ECDH)` | `CONFIG_MBEDTLS_ECDH_C` → `PSA_WANT_ALG_ECDH` |
| Curves | X25519 (`PSA_WANT_ECC_MONTGOMERY_255`), P-256 (`PSA_WANT_ECC_SECP_R1_256`) | `CONFIG_MBEDTLS_ECP_DP_CURVE25519/SECP256R1_ENABLED` |
| HKDF | `PSA_ALG_HKDF(PSA_ALG_SHA_256)`, also EXTRACT / EXPAND | TF-PSA default `crypto_config.h` |
| HMAC | `PSA_ALG_HMAC(PSA_ALG_SHA_256)`, `psa_mac_compute/verify` | default |
| SHA-256 | `psa_hash_*`, **hardware SHA** | `CONFIG_MBEDTLS_HARDWARE_SHA` |
| AEAD (if needed) | AES-CCM, **hardware AES** | `CONFIG_MBEDTLS_CCM_C`, `CONFIG_MBEDTLS_HARDWARE_AES` |
| Randomness | `psa_generate_random` → `esp_fill_random` (hardware RNG) | IDF port |

ESP32-C3 hardware: AES, SHA, MPI, HMAC and RNG are present; there is **no ECC
accelerator** (`soc_caps.h`). The HMAC peripheral is eFuse-keyed and is out of
scope (no eFuses).

**Curve choice.** X25519 is recommended:

* 32-byte public keys fit the 250-byte frame with room for the transcript;
* every 32-byte string is a valid public key, so there is no point-validation
  path to get wrong (the all-zero shared secret must still be rejected);
* the implementation is constant-time;
* no ECC hardware exists to favour P-256.

P-256 remains a supported alternative. The code-size difference will be
measured when implemented. The current map already references PSA/ECP code
(Wi-Fi supplicant), but link status is not yet measured.

## 11. Runtime and flash implications

* **Flash:** the pairing code plus PSA key agreement and HKDF. It will be
  measured on the first implementation commit.
* **RAM:** each encrypted peer entry is 16 B of LMK plus driver state; the
  RAM delta will be measured when changing `MAX_ENCRYPT_NUM`.
* **Time:** pairing costs one X25519 key generation plus one agreement per
  side, in software (no ECC hardware), well under a second on a C3. Normal
  operation adds no public-key work.

## Gaps in native ESP-NOW encryption for operational ODD traffic

**G1. Enforcement is observed, not documented.** Plaintext and wrong-key
drops (§7) are real on this build but carry no per-frame signal. Mitigation:

* accept operational traffic only as unicast (`des_addr` == own MAC);
* require the frame's radio MAC to equal the credential's bound MAC;
* require that MAC to hold an installed encrypted peer;
* keep the §7 experiment as a hardware regression, to rerun on every IDF
  upgrade.

**G2. Replay and nonce semantics are undocumented.** CCMP's packet number
(PN) and receive replay check are driver internals. With a **static** LMK it
is unknown whether the PN restarts after reboot. If it does, a reused
CCM nonce under the same key is a confidentiality break. It cannot be
observed with this bench (no sniffer). Mitigation: never run CCMP on a
static key. Derive a **fresh LMK per link session** from the long-term link
key and fresh nonces from both peers (see the recommendation). Within one
session, ODD's incarnation/sequence rules (M2.x) already reject replayed
commands at the application layer.

**G3. Capacity: 7 encrypted peers < 8 relationships.** Raise
`CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM` to 8 (MAO has no SoftAP), or install
peers on demand. The endpoint needs only one.

**G4. The PMK must match across devices.** Set one documented ODD-wide PMK
constant (not secret, not the built-in default) and state that security
rests on the per-pair LMK.

**G5. The ODD header `src_id` is not bound to the radio.** Any sender can
claim any `device_id`. Every credential must bind the peer MAC as well as the
device_id (both are in the pairing transcript), and the check must use the
radio source address, not the header.

## Decision required (§5): A versus B

**A. Native ESP-NOW CCMP plus a small authenticated link envelope.**

* **Pairing:** X25519 ECDH, both nonces, transaction id, SHA-256 transcript,
  HKDF, SAS, HMAC key confirmation, two-sided confirmation. It runs on
  plaintext broadcast bootstrap frames, accepted only in pair mode.
* **Each link session:** an HMAC-authenticated `LINK_HELLO` /
  `LINK_HELLO_ACK` exchange (broadcast, so it reaches a peer whose entry is
  encrypted) carrying fresh nonces. Both sides install
  `LMK = HKDF(link key, nonces, "ODD-LINK-LMK-v1")[0:16]`. The verified
  `HELLO_ACK` is the secure proof that makes a device ONLINE.
* **Operational ODD traffic:** unicast only, over the encrypted peer, with the
  G1 checks; ODD itself unchanged.
* **Pros:** uses Espressif's own protection (hardware AES, driver drops
  plaintext and wrong-key frames, verified); no per-frame overhead; the ODD
  frames and the 64-test suite stay untouched; the only new crypto code is in
  pairing and session setup, all built from PSA primitives.
* **Cons:** per-frame enforcement rests on observed driver behaviour (G1,
  mitigated plus a regression test); replay protection *inside* a session
  relies on the ODD layer; the encrypted-peer budget must be managed (G3).

**B. Standard AEAD at the application layer (PSA AES-CCM), plaintext
ESP-NOW.**

* The pairing is the same, then per-session keys come from HKDF with both
  nonces. Every operational frame is AES-128-CCM with a 13-byte nonce from a
  per-direction counter, a 16-byte tag, and a receive replay window.
* **Pros:** every check lives in our code and is host-testable with standard
  vectors; no dependence on undocumented driver behaviour; no encrypted-peer
  limit.
* **Cons:** about 24–28 B more per frame; nonce, counter and replay
  management for every frame (a nonce mistake is fatal for CCM); more new
  code on the hot path; it duplicates what the radio already does; ODD
  framing changes; contrary to the brief's preference for official
  facilities.

**Recommendation: A.** It is the official facility, measured to enforce on
this build; per-session LMKs remove the one real unknown (G2); the new crypto
is confined to two small, standard, host-testable handshakes built from PSA
primitives; and the operational path and ODD wire format stay as approved.
If later testing ever shows G1 failing on some IDF version, a per-frame HMAC
tag (A+) can be added without redesign.

Out of scope, as the brief states: jamming and DoS, physical flash
extraction (NVS is not encrypted, so keys are readable with physical
access), compromised firmware, secure boot, flash encryption, eFuse
provisioning, cloud and account identity.

## Decision (after review)

**A+**, as the full M3.1 brief (§142, §148, §163) specifies: native CCMP
with a **fresh LMK per link session** (closes G2), plus an **authenticated
envelope on every operational frame** (session id, counter, HMAC tag),
verified by the application (closes G1 without relying on undocumented
driver behaviour). The encrypted-peer limit is raised (G3); an ODD-wide
documented PMK constant is used (G4); every credential binds the radio MAC
and the check uses the receive metadata (G5). The design is in
[link_security.md](link_security.md).

## Validation (M3.1 campaigns A–F)

Bench: MAO (ESP32-C3-LCDkit) and one LOLIN C3 Mini endpoint (CAMERA 01 /
LAMP 01 profiles), ESP-IDF 6.0.3. The encrypted-peer limit was raised from
the audited 7 to **10**. Every dev hook used is development-only.

**Results**

| Campaign | What | Result |
|---|---|---|
| A | 10 pair → FORGET → re-pair; 5 CANCEL, 5 REJECT, 5 forced SAS mismatch, 5 pair mode off; 10 MAO + 10 endpoint reboots | all pass; 10 distinct credentials; revoke 4–14 ms; no reboot re-paired |
| B1 | MAO credential dropped (5×) | relationship kept, never SECURE again from id/MAC/name, REPAIR required, old key refused |
| B2 | endpoint credential reset / corrupted (5×) | same; commit-last proven with and without an old credential |
| B3 | runtime LMK broken (5×, `sec peerplain`) | re-keyed from K_link, no re-pair, 0 writes |
| B4 | online FORGET (5×) | authenticated REVOKE, 4–29 ms, both ends erased, revoked key refused |
| B5 | offline FORGET (3×) | local trust gone, revoke honestly "not confirmed", returns as NEW, stale remote credential replaced commit-last |
| C | 10 DATA replays; 3 fuzz runs (192 frames); 30 plaintext ACTION / SET_VALUE / SESSION_OPEN injections; 10 wrong-radio spoofs with the real credential | 0 ODD deliveries, 0 executions, 0 credential or metadata changes, 0 resets |
| D | CAMERA and LIGHT regression, profile swap both ways, exactly-once under ACK/RESULT loss, CONNECT, input grammar | pass; credential unchanged by profile swaps; security counters flat during normal use |
| E | measurements (below) | pass |
| F | host suites, OpenSSL oracle, on-target self-tests | link security 220/220, relationships 84/84, character harness 36/36 identical, OpenSSL (pyca 50.0.1, OpenSSL 4.0.2) regenerates the committed vectors unchanged, on-target 47/47, ODD 64 checks |

Fixes found by validation: dev security hooks moved to the `mao_link` task
(console stack 264 → ≥1040 B free); no operational plaintext queries to
VERIFY / REPAIR peers; a failing re-try no longer flickers REPAIR → OFFLINE.

**Measurements**

| Item | Value |
|---|---|
| App image, DEV / release | M2.6 1,245,952 / 1,186,336 B; M3.0 1,259,840 / 1,195,200 B; **M3.1 1,310,976 / 1,234,464 B** (app partition 1,835,008 B: 28.5 % / 32.7 % free) |
| Release growth M3.0 → M3.1 | TF-PSA crypto +14.7 KB, odd_link +11.4 KB, mao_link +8.6 KB, esp_stdio +2.0 KB, mao_app +1.6 KB, espnow +1.5 KB, relationships +1.3 KB, rest < 1 KB |
| Idle heap | HOME ≈152.0 KB, DEVICES 151.9 KB, CAMERA page 151.9 KB, SECURE idle 151.4–151.7 KB; absolute low 147,764 B (at boot) |
| Pair / repair | minimum never went below the boot low; idle after settling unchanged |
| X25519 | generate 104.8 ms, agree 104.6 ms |
| HKDF (88 B) / HMAC (64 B) | 0.60 ms / 0.14 ms (per-frame envelope cost is of the HMAC order) |
| Credential NVS commit | 1.2 ms |
| Pair: press → SAS | 449 ms machine time |
| PAIRED → SECURE | 13 ms (HELLO, LMK install, first envelope) |
| MAO boot → SECURE | 482–501 ms (SEEN → SECURE 8–16 ms, median 10) |
| Endpoint reboot → SECURE | 1.29–1.37 s (8 of 10) or 10.7 s (2 of 10), see recovery phasing |
| Envelope | 32 B fixed (16 header + 16 tag); largest wrapped frame 179 B of 250 |
| Load (stress orbit / 50 Hz ODD flood) | 33–36 fps, frame avg 10.8–12.0 ms; 0 event, inbox or malformed drops |
| Lowest task stacks | mao_devices 1156 B, mao_link 2916 B (self-test), mao_devcmd 1040 B, dispatcher 2648 B, LVGL 2924 B, wifi 1828 B |
| NVS writes | 0 in normal use and re-keys; 1 per metadata name/type change; pair / repair / forget only otherwise |

**Not directly tested** (no existing hook, deliberately not added):
previous-session DATA replay, previous-session HELLO replay, per-class fuzz
outcome (aggregate count only), corrupted-tag / unknown-session /
bad-counter DATA envelopes, right-radio / wrong-credential spoof. Transfer-id
stale guarding was only observed as "abort mid-transfer, no stale advance".

**Limitations:** no secure boot, no flash encryption, no eFuse key store, no
tamper resistance (flash extraction exposes credentials); no jamming or DoS
protection; plaintext discovery leaks presence and basic metadata; first-pair
MITM resistance depends on the user comparing the SAS; the endpoint's serial
console stands in for a product's local-presence UI; one physical endpoint
only, so multi-device authenticated operation is not hardware-validated.
