# ODD BUS identity model

Four identities exist in the ODD JOBS ecosystem. They are different things,
solve different problems, and must never be conflated.

```
DEVICE ID          stable hardware identity
                   0x0DD0 marker + factory MAC, 64 bit
                   survives everything; the registry key; never shown raw

INCARNATION ID     random per controller boot
                   64 bit, non-zero, from the hardware RNG, never persisted
                   "this particular running instance of the controller"

SEQUENCE           command ordering within one incarnation
                   16 bit, serial-number arithmetic, wraps

TRANSFER ID        one visual connect/transfer interaction on MAO
                   MAO-internal only; it never appears on the wire
```

A concrete life:

```
MAO (device_id A)
  boot #1  -> incarnation X   seq 300, 301, 302 ... (wraps at 65535 -> 0)
  boot #2  -> incarnation Y   seq 41000, 41001 ...  (random start; may be
                                                     numerically "older" - that
                                                     is fine, Y is not X)
```

## Wire form (ODD BUS v1, append-only extension)

- Header flag `ODD_FRAME_F_INCARNATION = 0x01` (the only defined flag bit;
  frames with unknown flag bits are rejected as malformed).
- When set, the payload begins with the 64-bit incarnation, little-endian,
  before the message's normal payload. Covered by the ordinary frame CRC.
- Only `SESSION_OPEN` (0x09), `SET_VALUE` and `ACK` may carry the flag;
  `SESSION_OPEN` requires it (its payload is exactly the prefix).
- Sizes: SET_VALUE 31 B legacy / 39 B flagged; ACK 34 B / 42 B;
  SESSION_OPEN 34 B. ESP-NOW budget is 250 B; the largest ODD frame remains
  CAPABILITIES at 147 B.
- New ACK statuses: `NO_SESSION` (5) and `STALE_SESSION` (6). Generic on
  purpose: they serve every controller/device pair, not any one product.

## Session model

A device tracks, per controller (per `source device_id`):

- **current incarnation** — the only one whose commands execute;
- **previous incarnation** — remembered specifically to be refused;
- per-capability sequence history **with each applied command's result**.

Rules:

- `SESSION_OPEN(current)` → idempotent: re-ACK OK, history preserved
  (covers a lost SESSION_OPEN ACK).
- `SESSION_OPEN(previous)` → `STALE_SESSION`. A delayed old open can never
  reclaim the session.
- `SESSION_OPEN(new)` → previous = current, current = new, sequence history
  reset, ACK OK. A controller reboot is a normal event, logged as one.
- Command with current incarnation → normal dedupe: same seq = duplicate
  (answer with the **original result**, never re-execute); older seq = STALE;
  newer = apply and remember the result.
- Command with previous incarnation → `STALE_SESSION`, **not executed**.
- Command with any other incarnation → `NO_SESSION`, **not executed**
  (the controller opens a session and re-sends).

The controller (MAO) opens sessions lazily: the first state-changing command
for a device sends `SESSION_OPEN` first (about one extra ~6 ms round trip,
once per boot per device). Discovery never generates session traffic. ACKs
whose incarnation is not the current boot's are ignored — a stale ACK from an
earlier boot can never confirm a new command. `NO_SESSION` answers (the device
rebooted and forgot) flip the session down, reopen it and re-send the command
automatically; the user never reconnects anything by hand.

Nothing is persisted on either side, deliberately: a reboot anywhere heals
through `NO_SESSION` → `SESSION_OPEN` on the next command.

## Safety invariants

> A state-changing command is uniquely identified by its controller
> device_id, controller incarnation, command semantic (capability or, later,
> action id), and sequence number.

> A command from a non-current controller incarnation must never execute.

These are the invariants one-shot ACTIONs (CAPTURE, IDENTIFY, ...) will rely
on: a retried ACTION (same incarnation + seq) is answered with the original
result and executes once; after a controller reboot the same seq under the
new incarnation is a new intentional act; a delayed command from the old
incarnation is refused.

## Actions (one-shot operations)

`ODD_CAP_ACTION` (type 3) advertises a discrete operation; its semantic is
generic (`ODD_ACTION_IDENTIFY = 1`), carried as `min == max` of the ordinary
capability record (`step == 1`), so the v1 CAPABILITIES layout is unchanged.
An `ODD_MSG_ACTION` (0x0A, 35 B) invokes it; the incarnation flag is
MANDATORY - no legacy unflagged ACTION exists. The lifecycle:

```
ACTION (source, incarnation, cap, seq)
  -> ACK  ACCEPTED            command taken; execution may be asynchronous
       |  BUSY                refused now; the refusal IS this identity's outcome
       |  UNKNOWN_CAP         unsupported
       |  NO_SESSION /        not executed; the session gate refused it
       |  STALE_SESSION
  -> remote work
  -> ACTION_RESULT (0x0B, 38 B): DONE | FAILED, carrying the ORIGINAL
     (incarnation, cap, seq)
```

Retrying the SAME identity is **status recovery, never a new execution**:
the device replays the cached acceptance and, when complete, the cached
result. Losing the ACK, the result, or both cannot execute anything twice.
A controller that never gets an answer resolves the outcome as UNKNOWN -
the action may have run - and never invents a new identity by itself; only
a new explicit user intention creates a new sequence. A device that reboots
mid-action loses its cache: the recovery knock is answered NO_SESSION and
the controller resolves UNKNOWN (after acceptance) or safely re-sends the
same identity (before acceptance, where nothing can have executed).

Action invariants (CAPTURE-critical):

> A duplicate ACTION with the same (source_id, incarnation_id, action_id,
> sequence) must never execute the action more than once.

> Loss of ACK or ACTION_RESULT must never cause the controller to generate
> a new action identity automatically.

> Only a new explicit user intention may create a new action sequence after
> a completed, refused or unknown action.

## State facts and camera semantics

The capability TYPE carries the semantic (the v1 record has no spare field,
and a variable-length record would break every existing decoder):

| Type | Value | Canonical record | Meaning |
|---|---|---|---|
| `ODD_CAP_READY`   | 4 | min 0, max 1, step 1, READ only | able to perform the primary operation now |
| `ODD_CAP_STORAGE` | 5 | min 0, max 100, step 1, READ only | remaining logical capacity, percent |

A writable fact, or one with a non-canonical range, is malformed. Facts are
displayed, never offered as controls (`odd_cap_is_status()`). READY is known
BEFORE a press; ACK BUSY answers the race after it - both are needed.

Action semantics: `IDENTIFY = 1`, `CAPTURE = 2` ("perform your primary image
capture once" - the device decides how), `SYNC_TEST = 3`. Device type
`ODD_DEVICE_CAMERA = 6` is broad classification only; controllers render from
capabilities. The primary action of a page is chosen by semantic priority
(CAPTURE first), never by device type or name.

A refusal (BUSY, not ready, storage empty) is an ACK outcome: no
ACTION_RESULT follows, because nothing was accepted.

## Threat model and limits

This is **not** authentication or pairing; ESP-NOW remains development-plain.
The model protects against retransmission, queue delay, reorder, reboot on
either side, sequence wrap and late ACKs. It does not order incarnations:
"previous" is one deep, so a packet delayed across **two or more** controller
reboots would look genuinely new — beyond the transport's realistic delays
(no queue on either side holds a frame for multiple boot cycles). If a future
transport can delay frames that long, add a persistent boot counter then.

The old "backward sequence jump > 4096 means restart" heuristic survives only
in the device's **legacy** (unflagged) path for old controllers, clearly
labelled; it cannot influence session-aware commands and must never carry
one-shot semantics.
