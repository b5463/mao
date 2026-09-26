# ODD link security (M3.1)

This turns "MAO remembers this device" (M3.0) into "MAO remembers this
device **and can prove it is the same peer it paired with**". It adds
authenticated pairing, persistent peer credentials, encrypted and
authenticated operational traffic, authorization, revocation and recovery.

The platform facts behind the design are in
[m3_1_link_security_audit.md](m3_1_link_security_audit.md).

> **Guarantees and non-guarantees.** This protects the *radio link* between a
> paired MAO and a paired device. Credentials sit in unencrypted NVS: anyone
> with physical access to the flash can read them. There is no secure boot,
> no flash encryption and no eFuse key storage. Jamming and denial of service
> are not addressed. First pairing is only as good as the user's comparison of
> the six-digit code. Nothing here is "unbreakable".

## Threat model

**Protected against:**
* passive reading of operational traffic;
* casual injection;
* spoofing a known device_id (with or without its name) without its credential;
* replay of operational frames and of pairing messages;
* a different controller controlling a paired endpoint;
* a different device impersonating a paired peer;
* a MITM during first pairing, **if** the user compares the code;
* credentials surviving a re-pair;
* stale ODD sessions and incarnations (already handled by M2.x).

**Not addressed:**
* jamming and DoS (for example, forged aborts or HELLO floods can interrupt
  pairing or force re-keying);
* physical flash extraction and invasive attacks;
* compromised firmware;
* secure boot, flash encryption and eFuses;
* cloud and account identity;
* presence and metadata leakage from plaintext discovery.

## Layers (A+)

```
persistent relationship (mao_relationships)        "the user chose this device"
  pair credential K_link (32 B, from the ceremony)  "and can prove it"
    link session (fresh per HELLO)                  session_id, fresh LMK, envelope keys
      ESP-NOW CCMP with the session LMK             confidentiality (native, hardware AES)
      authenticated envelope (HMAC-SHA-256/128)     authenticity, radio-source binding, replay
        ODD BUS frame, unchanged                    incarnation / session / seq (M2.x)
          SET_VALUE / ACTION transaction            exactly-once, unchanged
```

**Why both CCMP and an envelope.** ESP-NOW's receive API has no per-frame
"was encrypted" flag, and dropping plaintext from an encrypted peer is
observed behaviour (audit §7), not a documented contract. The envelope makes
authenticity and replay checks application-verifiable, and it stays valid
even if a future IDF delivers such frames.

**Why a fresh LMK per session.** CCMP's packet-number behaviour across reboots
with a static key is undocumented (audit G2). Every link session installs an
LMK nobody has used before, so a counter that restarts can never repeat a
nonce under the same key.

## Identities (all separate)

| Concept | What it is |
|---|---|
| DEVICE_ID | stable logical identity, 0x0DD0 plus the factory MAC. Not proof. |
| RADIO_IDENTITY | the radio MAC bound into the credential at pairing |
| RELATIONSHIP | the user chose to remember the peer (M3.0) |
| PAIR CREDENTIAL | K_link plus the bound ids and MACs; persisted |
| PAIR_TX_ID | 8 random bytes, one pairing attempt |
| LINK SESSION | one authenticated HELLO exchange: fresh keys |
| SESSION_ID | 8 bytes derived per link session |
| LINK FRAME COUNTER | per direction, orders frames inside one link session |
| CONTROLLER INCARNATION | one MAO boot epoch (ODD, M2.2) |
| ODD SESSION | runtime command session (ODD, M2.2) |
| ACTION SEQUENCE | ODD command identity |
| TRANSFER_ID | one CONNECT / visit interaction (UI) |

## Primitives (PSA Crypto, TF-PSA-Crypto 1.1.1 in IDF 6.0.3)

| Use | Primitive |
|---|---|
| Key agreement | **X25519** (RFC 7748), ephemeral per attempt; an all-zero shared secret is rejected |
| Hash | **SHA-256** (hardware) |
| KDF | **HKDF-SHA-256** (RFC 5869) |
| MAC | **HMAC-SHA-256** (RFC 2104). Confirmation tags use the full 32 B; HELLO and envelope tags are truncated to **16 B** (128 bit, allowed by RFC 2104 §5 and NIST SP 800-107) |
| Randomness | `psa_generate_random`, backed by the hardware RNG |
| Transport | ESP-NOW **CCMP** (AES-128-CCM) with a per-session LMK |

## Link frames

Link frames never start with ODD's "OD", so the two namespaces cannot
collide. Every link frame starts with `'L' 'K' version=1 type`. Integers are
big-endian. `ids` means `ctrl_id(8) | dev_id(8)`.

| Type | Name | Direction | Body after the 4-byte head |
|---|---|---|---|
| 0x01 | PAIR_START | C→D bcast | txid(8) ids pub_c(32) |
| 0x02 | PAIR_COMMIT | D→C bcast | txid ids pub_d(32) commit(32) |
| 0x03 | PAIR_NONCE | C→D bcast | txid ids nonce_c(16) |
| 0x04 | PAIR_REVEAL | D→C bcast | txid ids nonce_d(16) |
| 0x05 | PAIR_CONFIRM | C→D bcast | txid ids tag_c(32) |
| 0x06 | PAIR_ACCEPT | D→C bcast | txid ids tag_d(32) |
| 0x07 | PAIR_ABORT | either, bcast | txid ids reason(1) |
| 0x10 | HELLO | C→D bcast | ids nonce_c(16) tag(16) |
| 0x11 | HELLO_ACK | D→C bcast | ids nonce_c(16) nonce_d(16) tag(16) |
| 0x20 | DATA | unicast, encrypted peer | session_id(8) counter(4) inner… tag(16) |

Bootstrap and HELLO frames travel as plaintext broadcast. Broadcast always
reaches the other side, whatever peer entry it holds. The MACs used in the
cryptography are the **radio source addresses** from the receive metadata;
any address claimed in a payload is ignored.

## Pairing ceremony

The structure follows Bluetooth LE Secure Connections numeric comparison: the
responder **commits to its nonce before it sees the initiator's**, so a MITM
cannot grind a matching six-digit code.

```
MAO (C)                                         device (D), in PAIR MODE only
  PAIR_START  txid, ids, pub_c        ───────▶
                                      ◀───────  PAIR_COMMIT  pub_d, commit
  PAIR_NONCE  nonce_c                 ───────▶
                                      ◀───────  PAIR_REVEAL  nonce_d
  verify commit; both compute Z, TH, keys, SAS
  user compares SAS on MAO and on the device, confirms on BOTH
  PAIR_CONFIRM tag_c (after MATCH)    ───────▶  verify tag_c; wait for local ACCEPT
                                                persist PENDING credential
                                      ◀───────  PAIR_ACCEPT tag_d
  verify tag_d; persist credential
  HELLO under the new key             ───────▶  valid under PENDING: promote it
```

The derivations:

```
commit    = SHA-256("ODD-PAIR-COMMIT-v1" | txid | ids | pub_c | pub_d | nonce_d)
TH        = SHA-256("ODD-PAIR-TRANSCRIPT-v1" | ver | ids | mac_c | mac_d | txid |
                    pub_c | pub_d | nonce_c | nonce_d)
Z         = X25519(own ephemeral, peer pub)
K_confirm = HKDF(salt=TH, ikm=Z, info="ODD-PAIR-CONFIRM-v1", 32)
K_link    = HKDF(salt=TH, ikm=Z, info="ODD-LINK-KEY-v1",     32)
SAS       = HKDF(salt=TH, ikm=Z, info="ODD-PAIR-SAS-v1",      8)  -> big-endian u64 mod 10^6
tag_c     = HMAC(K_confirm, "ODD-PAIR-CONFIRM-C" | TH)
tag_d     = HMAC(K_confirm, "ODD-PAIR-CONFIRM-D" | TH)
```

* **SAS:** six digits, shown as `482 193`. The modulo bias is below 10⁻¹³.
  Both sides derive it independently; it is never transmitted.
* **Commit-last on the device.** The old credential stays active until a
  HELLO arrives that is valid under the new, pending one. If MAO never
  persisted the new key, its next HELLO still verifies under the old
  credential and the pending one is discarded. A failed re-pair therefore
  never bricks the device.
* **Commit on MAO.** MAO persists only after verifying `tag_d`, which the
  device sends only after persisting its pending credential. Lost frames are
  retried; every retry is idempotent for its `txid`.
* **Scope of a transaction:** one per side at a time. A new `txid` discards
  the old one. Timeouts are 60 s of pair mode and 45 s per attempt.
  Ephemeral keys, Z, K_confirm and the transcript are wiped on success,
  failure and timeout.

## Link session

```
HELLO     : nonce_c fresh;  tag = HMAC(K_hello, "H1" | ids | mac_c | mac_d | nonce_c)[:16]
HELLO_ACK : nonce_d fresh;  tag = HMAC(K_hello, "H2" | ids | mac_c | mac_d | nonce_c | nonce_d)[:16]
K_hello   = HKDF(salt="", ikm=K_link, info="ODD-LINK-HELLO-v1" | ids, 32)
session   = HKDF(salt=nonce_c | nonce_d, ikm=K_link,
                 info="ODD-LINK-SESSION-v1" | ids | mac_c | mac_d, 88)
          -> LMK(16) | K_c2d(32) | K_d2c(32) | session_id(8)
```

* Both sides install the peer as an encrypted ESP-NOW peer with the session
  LMK.
* A verified HELLO_ACK is MAO's **secure proof**: the device becomes
  ONLINE_AUTHENTICATED.
* Session keys are never persisted.
* Every new session has new nonces, so every key differs from all earlier
  sessions and a reboot cannot reuse an LMK.

## DATA envelope

```
'L' 'K' 01 20 | session_id(8) | counter(4) | inner (ODD frame, or link control) | tag(16)
tag = HMAC(K_dir, first 16 bytes | inner)[:16]      K_dir = K_c2d or K_d2c
```

* **Overhead:** 32 B. The largest inner frame is 218 B. The enclosed ODD
  frame is byte-for-byte what ODD BUS produced.
* **Acceptance:** unicast to us, from the session's bound radio MAC, with the
  current session_id, a valid tag, and a fresh counter. Anything else is
  dropped and counted.
* **Counter:** starts at 1 per direction and session. The receiver keeps the
  highest value plus a 32-frame bitmap window, and rejects duplicates and
  anything older than the window.
* **Link control inside the envelope:** `'L' 'C' type`, with REVOKE = 1 and
  REVOKE_ACK = 2.

## Operational authorization

**Paired peer:**
* All unicast ODD traffic travels only in DATA envelopes.
* Plaintext ODD from a paired id is limited to broadcast DISCOVER / ANNOUNCE.
  It is a **hint** only: it never makes the device online, never changes its
  metadata or capabilities, and never enables controls.

**Endpoint with an authorized controller:**
* It answers plaintext only with DISCOVER replies (as broadcast).
* SESSION_OPEN, SET_VALUE, ACTION and the rest are accepted only in a valid
  envelope from the authorized controller.
* Pair mode admits bootstrap frames and nothing else.

**MAO:**
* Operational controls require relationship + credential + a live secure
  session.
* There is no plaintext fallback anywhere.
* The M3.0 dev bypass (`mao odd-action`) is refused for devices that require
  the secure link.

## Credential records

**MAO: relationship schema v2** (NVS `mao_rel`, `r0`–`r7`). It is the v1
fields plus `auth(1) peer_mac(6) cred_gen(1) K_link(32)`.
* v1 records load as **KNOWN_UNVERIFIED**: remembered, no controls, VERIFY
  offered.
* Unknown schemas are reserved, never overwritten.

**Endpoint** (NVS `odd_sec`, independent of the CAMERA / LIGHT profile):
* `auth` = the authorized controller: `ctrl_id ctrl_mac cred_gen K_link`;
* `pend` = the pending replacement;
* one controller: a completed pair-mode ceremony **replaces** it.

**Never persisted:** ephemeral keys, Z, nonces, SAS, the transcript, session
keys, counters, ODD sessions and incarnations.

## Forget and revocation

* **Online:** MAO sends REVOKE in the envelope. The device answers REVOKE_ACK,
  then erases its authorization once the ACK has been transmitted. MAO erases
  its own record on the ACK, or after a bounded timeout, logging that the
  remote revocation is unconfirmed.
* **Offline:** MAO erases its local record and key at once. The device may
  keep the stale controller credential until its next completed pair-mode
  ceremony replaces it. MAO keeps no hidden key.

## Physical presence

The bench endpoint enters pair mode, accepts and rejects over its serial
console. A product needs a deliberate local action instead (button, menu or
display).
