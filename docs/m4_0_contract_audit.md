# M4.0 ODD contract audit (engineering note)

The ODD BUS contract as it is implemented at M3.2 (`1ee3fdf`), before any
M4.0 change: `components/odd_bus` (codec, shared by MAO and the bench
endpoint), `components/mao_devices` (controller side), `mao_device_view.c`
(capability → control mapping), `mao_world` (the merged device model).

## 1. Current contract

**Wire.** 24 B little-endian header: magic `OD`, version `1`, type, seq (u16,
serial arithmetic), flags (only `INCARNATION`, other bits rejected), payload
length, src id, dst id (0 = any). Payload, then CRC-16/CCITT-FALSE. Frame ≤ 250
B. Inside the M3.1 envelope (32 B overhead) an ODD frame is ≤ 218 B, so the
payload is ≤ 192 B.

**Version.** The header `version` byte is the only version there is. A frame
with a different version is rejected (`BAD_VERSION`, counted, rate-limited
log). `odd_device_info_t.protocol_version` is a copy of that header byte.
There is no contract, profile or feature version.

**Messages** (unknown types 0x0C+ are rejected as malformed, on both sides):

| Type | Dir | Payload | Notes |
|---|---|---|---|
| DISCOVER 01 | C→any | device_type u16, cap_count, name_len, name | "who is there" |
| ANNOUNCE 02 | D→C | same | discovery metadata; plaintext for strangers |
| GET_CAPS 03 | C→D | empty | operational (secure link for paired peers) |
| CAPABILITIES 04 | D→C | count ≤ 8, count × 15 B records | exact length |
| GET_STATE 05 | C→D | empty | |
| STATE 06 | D→C | in_reply_to u16, count ≤ 8, count × (cap_id, i32) | snapshot or notification; unknown cap ids skipped |
| SET_VALUE 07 | C→D | [incarnation], cap_id, i32 | |
| ACK 08 | D→C | [incarnation], acked_seq, status, cap_id, i32 applied | |
| SESSION_OPEN 09 | C→D | incarnation (mandatory) | |
| ACTION 0A | C→D | incarnation, cap_id | |
| ACTION_RESULT 0B | D→C | incarnation, cap_id, action_seq, result | result must be 1 or 2 |

**Capability record** (15 B): id (1..255, device-local), type, flags (READ 1,
WRITE 2, NOTIFY 4), min, max, step (i32). The *type is the semantic*:
POWER 1, LEVEL 2, ACTION 3 (semantic in min == max: IDENTIFY 1, CAPTURE 2,
SYNC_TEST 3), READY 4, STORAGE 5. There is no separate semantic field.

**Codec validation of CAPABILITIES** (any failure rejects the whole frame):
id ≠ 0, min ≤ max, step > 0 for every record whatever its type; ACTION
canonical (min == max > 0, step 1); READY / STORAGE never WRITE and canonical
ranges (0..1 / 0..100, step 1). Duplicate ids, POWER ranges and unknown types
are not checked.

**ACK statuses:** OK, CLAMPED, STALE, UNKNOWN_CAP, READ_ONLY, NO_SESSION,
STALE_SESSION, ACCEPTED, BUSY. **ACTION results:** DONE, FAILED.

**Identity / exactly-once:** a state-changing command is (src device_id,
controller incarnation, semantic, seq). Only the current incarnation
executes; ACTION retries reuse the identity and the device replays the cached
outcome (M2.3). SET is last-writer-wins by seq (STALE for older). Unchanged in
M3.x.

**Names:** 1..16 printable ASCII, not NUL-terminated on the wire.
**device_id:** 0x0DD0 + factory MAC, never changes. **Device types:** UNKNOWN,
CONTROLLER, LIGHT, DISPLAY, CLOCK, SPEAKER, CAMERA (description only).

**Controller side.** ANNOUNCE from a *paired* device counts only when it
arrives through the secure envelope (plaintext is a hint to the link layer).
A changed cap_count or device_type in an ANNOUNCE ("reshaped") clears the
capability view and re-describes. CAPABILITIES replaces the whole set under
one lock (M2.4). A device counts as `described` once every declared
capability has a STATE value. The page is built by `mao_device_controls()`
from types, flags and ranges; the primary action is the first CAPTURE.

**Session guards.** For a paired device every operational frame is enveloped:
a frame of an earlier link session (endpoint reboot, profile change, re-key)
fails as WRONG_SESSION or replays as REPLAY before ODD sees it. ACK / RESULT
from another controller incarnation are ignored. So a delayed CAPABILITIES of
an old profile cannot land in the new session.

## 2. Gaps against a production contract

| # | Gap | Effect |
|---|---|---|
| G1 | An *unknown* writable capability with a 0..1 range is taken as the POWER toggle (`type == POWER \|\| binary`) | unknown capability read as a known one |
| G2 | Two CAPTUREs (or two LEVELs / POWERs / READYs) resolve by packet order | MAO guesses |
| G3 | An ACTION with an unknown semantic becomes a control | fake control |
| G4 | `described` waits for a STATE value of *every* declared capability | one unknown capability without a value keeps the device "describing" forever |
| G5 | Range and step checks apply to unknown types too | one additive unknown capability rejects the whole set |
| G6 | Duplicate capability ids accepted; lookup takes the first | ambiguous addressing |
| G7 | POWER ranges not validated; an ACTION without WRITE is silently hidden | contradictory metadata tolerated |
| G8 | ACK with an unknown status for an action: ignored | action hangs until the 8 s deadline, no log |
| G9 | ACTION_RESULT with an unknown result rejects the frame | same |
| G10 | No contract version: any semantic break would need a new wire version | old and new cannot even talk |
| G11 | No compatibility state: a malformed or missing description is silent and retried forever | UI cannot tell "loading" from "broken" |

Not gaps (checked): unknown message types, flags and wire versions are
refused without state change; unknown STATE cap ids are skipped; a paired
peer's plaintext never touches its registry entry; stale old-session
responses are stopped by the link session; STATE is a snapshot or a
per-capability notification, SET converges (last writer wins).

## 3. Smallest model (proposal)

* **Wire version stays 1.** No ODD v2.
* **Contract version = major.minor, carried in an optional descriptor at the
  front of CAPABILITIES** (authenticated for paired peers):
  `[0xD0][len ≥ 2][major][minor][len − 2 bytes, skipped][count][records]`.
  A legacy payload starts with `count ≤ 8`, so it is unambiguous and means
  contract **1.0**. The descriptor comes first so a v1 decoder can read the
  major without understanding the records. If the major is not 1, the
  records are not parsed and the device is INCOMPATIBLE.
* No feature bits, TLVs or profile revision yet: nothing needs them. The
  descriptor length leaves room for them later, and v1 skips the extra bytes.
* **Primary control** by well-defined semantic priority, not a wire field:
  CAPTURE, else LEVEL, else POWER.
* **Codec = structure** (lengths, ids, duplicate ids, canonical known types
  incl. POWER 0..1 and ACTION WRITE). **Contract evaluation = meaning**
  (`odd_contract.c`, pure C): which records MAO understands, ambiguity,
  compatibility state. Unknown types only need id ≠ 0 (G5).
* **Compatibility state per device** (a fourth axis next to relationship,
  reachability, authentication): UNKNOWN, DESCRIBING, COMPATIBLE, LIMITED
  (a usable subset, including zero understood controls), INCOMPATIBLE
  (another major), INVALID (malformed authenticated description, or none
  within the bound). **Policy:** a set is evaluated whole before it is
  published, and it never partially applies. An authenticated description
  is the device's current truth, so an invalid or incompatible one replaces
  the view with *no controls*. The old set is not kept, because it described
  firmware that is no longer there. A frame the codec rejects changes
  nothing except the INVALID bookkeeping.
* Unknown ACK status or ACTION result: FAILED, numeric value logged (G8, G9).
* Existing CAMERA 01 / LAMP 01 firmware stays unchanged and proves the legacy
  1.0 path.

## 4. As built (`2f74347`)

| Gap | Fix |
|---|---|
| G1, G3 | the view uses only usable capabilities; POWER by type only; an unknown ACTION semantic is not usable |
| G2 | a semantic that repeats is dropped as a whole (`odd_contract.c`), whatever the order |
| G4 | `described` counts usable capabilities only |
| G5 | the codec checks ranges of known types only |
| G6, G7 | the codec rejects duplicate ids, POWER outside 0..1, ACTION without WRITE |
| G8, G9 | unknown ACK status / ACTION result: FAILED, value logged; results ≠ 0 decode |
| G10 | descriptor `[D0][len][major][minor]...` at the front of CAPABILITIES; none = 1.0 |
| G11 | per-device `compat`; bounded description (5) → INVALID; authenticated malformed CAPABILITIES → INVALID (`odd_bus_set_malformed_handler`) |

One existing ODD selftest rule changed on purpose: "unknown result rejected"
became "unknown result decodes" (G9). Existing endpoint firmware is
unchanged and reads as contract 1.0 COMPATIBLE.
