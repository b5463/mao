# ODD device contract

What an ODD JOBS device must implement to join MAO safely and predictably,
and what MAO does when a device is not exactly what it expects. This is the
internal firmware contract, not a public SDK. Examples use only the two
existing bench devices, CAMERA 01 and LAMP 01.

Contract version described here: **1.1**. Wire version: **1**.
Code: `components/odd_bus` (codec and contract evaluation, shared by every
device), `components/mao_devices` (controller side). History and rationale:
`docs/m4_0_contract_audit.md`. Identity: `docs/odd_bus_identity.md`.
Security: `docs/link_security.md`.

## 1. Layers and versions

```
ESP-NOW frame (<= 250 B)
  └─ link layer (odd_link): pairing, HELLO, per-session envelope   docs/link_security.md
       └─ ODD frame, wire version 1 (<= 218 B inside an envelope)
            └─ contract major.minor, in the CAPABILITIES descriptor
                 └─ capabilities: typed records, one semantic each
```

* **Wire version** (header byte 2) covers framing only. A frame with another
  version is dropped and counted. No state changes and no reply are sent.
  It changes only if the framing does.
* **Contract version** `major.minor` covers the meaning of messages and
  capabilities. MAJOR is a breaking change. MINOR is additive: new
  capability types, new ACTION semantics, new descriptor fields.
* There are no feature flags, TLVs or profile revisions. The descriptor has
  room for them and a v1.x reader skips what it does not know.

Compatibility policy on MAO (MAO implements 1.1):

| Device reports | MAO | Controls |
|---|---|---|
| no descriptor | treated as **1.0** | all understood capabilities |
| 1.0 / 1.1 | COMPATIBLE (or LIMITED, see §4) | all understood capabilities |
| 1.4 (newer minor) | LIMITED: the known subset, MAO does not claim 1.4 | the understood subset |
| 2.0 (other major) | INCOMPATIBLE: records are never interpreted | none |

A device downgraded to an older minor is handled by the same table. Versions
are never assumed to only grow.

## 2. Messages

All integers are little-endian. 24 B header: `OD`, version 1, type, seq u16,
flags, payload length, src device_id, dst device_id (0 = any). Then the
payload and a CRC-16/CCITT-FALSE.

| Type | Direction | Purpose |
|---|---|---|
| DISCOVER 01 | controller → any | who is there (plaintext) |
| ANNOUNCE 02 | device → controller | name (1..16 printable ASCII), device type, capability count. Public discovery metadata, never authority for a paired device |
| GET_CAPS 03 / CAPABILITIES 04 | request / description | the device's contract and capability set |
| GET_STATE 05 / STATE 06 | request / snapshot or change notification | values by capability id |
| SET_VALUE 07 / ACK 08 | write one value / result | last writer wins by seq; repeating a SET converges |
| SESSION_OPEN 09 | controller incarnation | required before commands (NO_SESSION otherwise) |
| ACTION 0A / ACTION_RESULT 0B | invoke once / completion | exactly-once, see §5 |

For a **paired** device every operational message (GET_CAPS, CAPABILITIES,
GET_STATE, STATE, SET, ACK, SESSION_OPEN, ACTION, ACTION_RESULT) travels in
the authenticated link envelope. Plaintext from a paired device is at most a
hint that it is nearby. A **new** (unpaired) device may be listed from its
plaintext ANNOUNCE (name, type, NEW, PAIR) but gets no controls. Capability
authority begins with pairing.

Unknown message types, unknown header flags and malformed frames are
dropped. Neither side answers them, changes state or resets a session.

## 3. CAPABILITIES and the descriptor

```
[0xD0][len >= 2][major][minor][len-2 bytes: future descriptor fields][count][count x record]
legacy (1.0):                                                          [count][count x record]
```

`count` ≤ 8. A record is 15 B: `id` (1..255, unique in the set), `type`,
`flags` (READ 1, WRITE 2, NOTIFY 4), `min`, `max`, `step` (i32). The descriptor
comes first so a reader learns the major before it touches the records. With
another major the records are not parsed. 8 records plus the descriptor are
125 B, inside the 192 B an envelope leaves.

**Types defined by 1.x.** The type is the semantic:

| Type | Meaning | Rules (else the whole set is refused) |
|---|---|---|
| POWER 1 | on/off | min 0, max 1, step 1 |
| LEVEL 2 | bounded level | min ≤ max, step > 0. min == max is not a control |
| ACTION 3 | a discrete operation, run once | min == max == semantic > 0, step 1, WRITE |
| READY 4 | fact: can do its primary operation now | 0..1 step 1, never WRITE |
| STORAGE 5 | fact: remaining capacity % | 0..100 step 1, never WRITE |

ACTION semantics: IDENTIFY 1, CAPTURE 2, SYNC_TEST 3. They are generic and
never product-specific. Any device type may declare any of them, and the
device type never gates a control.

**Rules for the set:**

* Structure (lengths, ids ≠ 0, duplicate ids, the canonical rules above) is
  checked by the codec. A violation refuses the *whole* set. Nothing applies
  partially.
* A **type** or **ACTION semantic** this build does not know is carried and
  never interpreted. Its range fields are not judged. It never becomes a
  control and never stands in for a known one. The device is then LIMITED,
  not broken.
* A semantic may occur **once**. Two CAPTUREs, two LEVELs, two POWERs or two
  READYs make that semantic ambiguous. MAO offers none of them, and never
  picks by packet order. The rest of the set stays usable.
* Flag bits 1.1 does not define are ignored (a newer minor may add some).
  Contradictory known flags refuse the set: an ACTION without WRITE, or a
  READY / STORAGE with WRITE.
* Everything else is optional. A CAMERA without SYNC_TEST, or a LIGHT
  without IDENTIFY, is fully usable with what it has.

**Primary control** comes from semantic priority, never from packet order
or device type: CAPTURE, else LEVEL, else POWER, else none.

## 4. Compatibility states (per device)

A separate axis from relationship (NEW / KNOWN), reachability (OFFLINE /
ONLINE) and authentication (VERIFY / SECURE / FAILED):

| State | Meaning | Controls |
|---|---|---|
| UNKNOWN | not described this boot or this session | none yet |
| DESCRIBING | description requested, none valid yet | none yet (the page shows `...`) |
| COMPATIBLE | same major, not newer, every declared capability understood and unambiguous | all |
| LIMITED | same major, a usable subset (possibly empty): unknown or ambiguous capabilities, or a newer minor | the usable subset |
| INCOMPATIBLE | another major | none; INFO / FORGET only |
| INVALID | an authenticated description that is malformed, or none after 5 requests | none; INFO / FORGET only |

* A new authenticated description replaces the view as a whole, after
  evaluation. The UI sees the old model and then the new one, never a
  mixture. An INCOMPATIBLE or INVALID description removes every control.
* A device is described again on every new secure session (reboot, re-key,
  profile or firmware change) and when its ANNOUNCE shows a different shape.
* INVALID keeps being retried at the normal cadence, so a device that is
  still booting recovers by itself.
* Incompatibility is not a security problem. It never shows REPAIR, never
  asks for a re-pair and never touches the credential.
* The state is per device. One incompatible device never affects another.
* The live capability set, the state and the compatibility result are never
  written to flash. The relationship record keeps name and device type,
  written only when they change.

## 5. Statuses, exactly-once, sessions

**ACK statuses:** OK, CLAMPED (applied, adjusted), STALE (older than an
applied command), UNKNOWN_CAP, READ_ONLY, NO_SESSION, STALE_SESSION, ACCEPTED
(an ACTION was taken), BUSY (it cannot start now; covers "not ready").
**ACTION results:** DONE, FAILED. A value MAO does not know, in either,
counts as **FAILED** and is logged with its number. No new statuses in 1.1:
compatibility is decided from the descriptor, and authentication by the link
layer.

**Exactly-once** (unchanged since M2.3): an ACTION is identified by
(controller device_id, controller incarnation, capability, seq). Retries
re-send the *same* identity, and the device answers from its cache. Only the
current incarnation executes. After ACCEPTED, MAO never re-invokes; an
unrecoverable outcome is UNKNOWN.

**Stale answers** cannot cross sessions. A CAPABILITIES or STATE from before
a reboot, re-key or profile change belongs to an earlier link session and
fails as WRONG_SESSION or REPLAY before ODD sees it (host test:
`tests/link_security`, "a delayed description of an earlier profile").

## 6. Lifecycle

| Event | Identity | Credential | MAO |
|---|---|---|---|
| profile change (LIGHT → CAMERA) | same device_id | same | new session, new description, metadata update (1 write); no re-pair |
| firmware update, same major | same | same | as a profile change |
| firmware update, other major | same | same | relationship and security stay; INCOMPATIBLE, no controls; FORGET possible |
| downgrade | same | same | known subset or INCOMPATIBLE; the credential is never deleted |
| endpoint factory reset | same | lost on the endpoint | MAO keeps the relationship, shows REPAIR; needs explicit pair mode on the device; never auto-trust |
| MAO FORGET, device online | — | revoked on both sides (authenticated) | row becomes NEW |
| MAO FORGET, device offline | — | erased on MAO; the device may keep a stale one until its next pairing | row becomes NEW when heard |
| device leaves the household | — | — | FORGET is enough; no cloud revocation |

## 7. Pair mode (product contract)

The protocol never assumes a serial console. That is only the bench
stand-in. A product endpoint must provide, with whatever physical control it
has (button, screen, menu):

* entering pair mode only by a **deliberate local action**, time-limited (the
  bench uses 90 s), visible to the person at the device;
* showing or confirming the 6-digit code, with **explicit local approval**,
  plus reject and cancel;
* replacing an existing controller only through that same deliberate local
  path. The old one stays authorized until the new ceremony completes;
* the credential stored independently of the product profile.

The endpoint security API this maps to (`devices/lamp_01_test/main/lamp_link.c`,
driven by `odl_paird_*`): enter pair mode (`odl_paird_mode`), approve
(`odl_paird_accept`), reject (`odl_paird_reject`), cancel (pair mode 0), and
status (the ceremony state).

## 8. Limits

| Item | Limit |
|---|---|
| frame | 250 B (ESP-NOW); ODD inside an envelope 218 B |
| name | 1..16 printable ASCII |
| capabilities per device | 8 (with descriptor: 125 B payload) |
| STATE values per message | 8 |
| descriptor bytes after the length | 2..16 |
| ACTION payload | the capability id only (no parameters, no RPC) |
| relationships / live devices | 8 / 8 |
| description requests before INVALID | 5 (retries continue) |

Everything is fixed-size. There is no heap allocation per description.

## 9. Endpoint author checklist

- [ ] device_id = 0x0DD0 + factory MAC; never changes with profile or firmware.
- [ ] Persistent security store (active + pending credential), independent of the product profile.
- [ ] Pair mode by deliberate local action, time-limited, with local approve / reject / cancel (§7).
- [ ] Secure HELLO answered only for the stored controller and radio; all operational traffic in the envelope.
- [ ] Answers DISCOVER with ANNOUNCE; the name is 1..16 printable ASCII.
- [ ] CAPABILITIES with a descriptor (major 1, minor = what it implements), unique ids, one record per semantic, canonical known types.
- [ ] STATE answers GET_STATE with a snapshot; NOTIFY capabilities report their own changes.
- [ ] SET: clamp to min/max/step (CLAMPED), refuse READ-only (READ_ONLY), unknown ids (UNKNOWN_CAP), older seq (STALE).
- [ ] SESSION_OPEN: track the current and previous controller incarnation; execute commands only from the current one.
- [ ] ACTION: ACCEPTED or BUSY, then exactly one ACTION_RESULT; repeats of the same identity replay the cached outcome.
- [ ] Unknown message types, flags and malformed frames: drop silently, no state change.
- [ ] Another controller's frames and other devices' ANNOUNCEs: ignore.
- [ ] Reboot: forget sessions (NO_SESSION), keep credentials; the controller re-keys and re-describes.
- [ ] Profile or firmware change: same identity, same credential, new description.

## 10. Worked example (existing devices)

LAMP 01 today sends the legacy set `[3][POWER 1][LEVEL 2][ACTION 3 = IDENTIFY]`.
MAO reads it as contract 1.0: COMPATIBLE, primary LEVEL, toggle POWER,
action IDENTIFY. CAMERA 01 sends `[5][CAPTURE][IDENTIFY][SYNC_TEST][READY][STORAGE]`:
COMPATIBLE, primary CAPTURE, facts READY and STORAGE. With a descriptor the
same sets start `D0 02 01 01`.
