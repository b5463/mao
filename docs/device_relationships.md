# MAO device relationships (M3.0)

MAO tells apart a device it merely **hears** from a device the user has
**deliberately made part of MAO's setup**, and remembers the second kind across
boots, even while it is switched off.

> **Security boundary.** M3.0 pairing is a *persistent local association*. It
> does **not** authenticate the remote device or the controller, and it does
> **not** encrypt anything (the development transport is unencrypted
> ESP-NOW). The stable `device_id` is derived from the device's MAC and can be
> imitated: it is not cryptographic proof. Blocking controls for unpaired
> devices is MAO product policy, not network security: another controller could
> still send packets. Authentication, key establishment, encrypted transport and
> controller authorization are a later milestone. Until then MAO never calls a
> device TRUSTED, AUTHENTICATED or SECURE.

## Terms

| Term | Meaning | Where it lives |
|---|---|---|
| DISCOVERED | MAO can hear the device now; the user never added it | live registry only (RAM) |
| KNOWN | the user pressed PAIR; persisted, survives reboots | relationship DB (NVS) |
| ONLINE / OFFLINE | reachability right now (registry liveness timeout) | live registry |
| PAIR | "create a persistent known-device relationship" | user act |
| FORGET | "remove that relationship" | user act, confirmed |

Relationship and reachability are independent: a KNOWN device can be OFFLINE;
a DISCOVERED one is shown only while ONLINE.

## Identity model: seven separate things

| Concept | What it is | Lifetime | Owner |
|---|---|---|---|
| DEVICE ID | stable hardware identity (0x0DD0 + MAC) | the device's life | device firmware |
| RELATIONSHIP | persistent local user choice (KNOWN) | until FORGET | `mao_relationships` (NVS) |
| REACHABILITY | live online / offline | seconds | `mao_devices` registry |
| INCARNATION ID | MAO's boot epoch (random u64) | one MAO boot | `mao_devices` |
| ODD SESSION | runtime command relationship per (controller, incarnation) | until either side reboots | ODD BUS, both sides |
| TRANSFER ID | one visual CONNECT / visit interaction | one transfer | `mao_app_transfer.c` |
| ACTION SEQUENCE | one command identity within an incarnation | one action | ODD BUS |

None stands in for another. PAIR never opens a session, a session never
implies a relationship, and CONNECT never pairs. Dropping only the device's
ODD session re-establishes it on the next command, with no PAIR prompt.

## Architecture

```
mao_radio -> odd_bus -> mao_devices  (live registry: transient, RAM)
                            \
                             mao_relationships  (KNOWN devices: NVS "mao_rel")
                            /
            mao_world (merged view, by device_id) -> mao_app -> mao_ui
```

* `components/mao_relationships/mao_rel_table.c`: the model and persistence
  rules, backend-agnostic (host-tested).
* `mao_rel.c`: the NVS backend, locking, `MAO_EVENT_REL_CHANGED`, dev commands.
* `mao_world.c`: the one merged list DEVICES shows. It reads RAM only.
* `components/mao_app/mao_app_rel.c`: page kinds (NEW / OFFLINE / CONTROL),
  PAIR, FORGET and the confirmation sheet.

The UI never touches NVS, the ODD BUS knows nothing of pairing, and the
character knows nothing of relationships (no character source changed in M3.0).

## Persistence

* NVS namespace `mao_rel`, one blob per slot (`r0` .. `r7`). No other namespace
  is ever written or erased.
* **Schema 1**, 33 bytes:

  | Bytes | Field |
  |---|---|
  | 1 | schema |
  | 1 | reserved |
  | 2 | last known type |
  | 4 | pair order |
  | 8 | device_id |
  | 17 | last known name (NUL-padded) |

* **Maximum 8** KNOWN devices (= `MAO_DEVICES_MAX`). A full list refuses PAIR
  (the page says FULL); nothing is ever evicted.
* **Not persisted:** online state, READY, STORAGE, LEVEL, POWER, capability
  tables or values, RSSI, incarnation, sessions, actions, transfer ids.
  Capabilities always come live from the device.
* **Writes happen only** on PAIR, FORGET, and a real change of a KNOWN device's
  name or type (reflash as another profile, rename). A normal session with
  captures writes nothing (measured: 0 writes after 10 boots with captures).
* **Atomicity:** write + commit first, RAM second. A failed PAIR stays
  DISCOVERED (page: NOT SAVED); a failed FORGET stays KNOWN. RAM always equals
  what the next boot loads.
* **Load:** once at boot, before the radio starts (about 1 ms for one record).
  Malformed records are skipped and logged; a duplicate device_id keeps the
  first; a record of an unknown (newer) schema reserves its slot and is never
  overwritten. An unusable namespace means "no known devices this boot",
  never a failed boot or an erase.
* RAM: 375 B (table and state).

## DEVICES list

The list is one merge by `device_id` (never name, type or row):

1. KNOWN devices in stable pair order. Offline ones stay, dimmed, `OFFLINE`.
2. DISCOVERED devices that are online, in this session's first-seen order,
   quieter (70 % presence), `NEW`.

Nothing is re-sorted by RSSI, reachability or activity. A known device that
comes online merges into its existing row; there is never a second row.
DISCOVERED devices are never persisted and leave when they go offline.

## DEVICE pages

> M4.1 changed the page grammar and removed INFO (FORGET is reached by
> holding): see `m4_1_ui_motion_concept.md` section 8. The states and the
> rules below are unchanged.

| Page | Shown for | Content | Controls |
|---|---|---|---|
| NEW | DISCOVERED | name, `NEW`, **PAIR** (focused) | none: no CAPTURE / LEVEL / POWER / CONNECT |
| CONTROL | KNOWN + online | the normal M2.6 page, plus a quiet **INFO** below CONNECT | as approved; CAMERA opens on CAPTURE, LIGHT on LEVEL |
| OFFLINE | KNOWN + offline | name, `OFFLINE`, CONNECT, INFO | none: memory is not connectivity |

The frozen grammar applies everywhere: the dial moves focus, a single press
activates, a long press goes BACK, a double press does nothing, and the 350 ms
entry guard holds. Pages change in place: PAIR turns NEW into CONTROL (focus on
the primary control); a device returning turns OFFLINE into CONTROL; FORGET
turns CONTROL into NEW.

### PAIR

Press PAIR, then:

1. the word dips (tool feedback) and the record is committed (about 1.1 ms);
2. the same page becomes the device's normal page.

It is not a cutscene, and there is no character state. PAIR works as long as
MAO has the device's metadata; if the device drops out during the write it
becomes KNOWN + OFFLINE. Nothing is ever paired automatically: not by
discovery, first control, CONNECT, IDENTIFY or name.

### FORGET

The path is INFO, then the details sheet (`PAIRED` / type / FORGET), then
FORGET, then `FORGET / <NAME>?` with `NO  YES`:

* NO is focused. YES needs a deliberate turn and a press.
* NO or a long press goes back one level.
* The sheet is plain typography over the receding page: no card, icon or red.

After YES:

* an online device's page becomes NEW in place (MAO still hears it);
* an offline device leaves DEVICES and stays gone after reboot.

Layout A (FORGET directly after CONNECT) exists as the dev switch
`mao rellayout a|b` for review. On the 240 × 240 panel it puts a destructive
word on the control page and meets the round edge, so B is the default.

### CONNECT to an offline known device

This stays a deliberate user attempt. It fails honestly (the existing failed
escape) and never runs by itself because a known device is absent. Boot with a
known device switched off shows it OFFLINE, with no failure reaction.

## Development commands

These are for DEV builds only:

```
mao rel list                     known devices, online state, writes since boot
mao rel dump                     raw slots
mao rel pair <registry slot>     pair from the registry (same metadata as the PAIR word)
mao rel forget <index|id>
mao rel inject <hex> <type> <name>  synthetic TEST record
mao rel clear-test               removes only synthetic records (multicast OUI 0xFF;
                                 no real device can have one). Real relationships stay.
mao rel failnext write|erase     one-shot storage failure injection
mao rellayout a|b                FORGET placement for the physical review
```

## Tests

* `tests/relationships/check.sh`: 55 host checks with synthetic ids. These are
  persistence and model tests only:
  * 0 / 1 / 2 / 8 records, full;
  * duplicate id, rename / profile change;
  * forget the middle record, re-pair;
  * write / erase failure;
  * malformed and unknown-schema records.
* Hardware walks with the one physical endpoint: see the M3.0 report.
  Simultaneous multi-radio DEVICES is not physically proven yet.
