# UI state handoff (M4.0 → UI / motion milestone)

> How M4.1 visualises these states, and its input grammar (INFO replaced
> by FORGET behind a hold): `m4_1_ui_motion_concept.md` section 8.

What the device layer gives the UI, and what the UI will have to visualise.
It is a list of states and events. It does not specify any visuals: layout,
motion, typography and character choices belong to the UI milestone. Current
behaviour is described as built (M4.0, `feat/mao-m4-production-odd`).

## 1. The view model (read-only, cheap, no protocol inside)

The UI asks exactly two sources. It never sees frames, versions or
capability flags.

**`mao_world_list()` / `mao_world_get(id)` → `mao_world_entry_t`,** one per
device, merged by device_id:

| Field | Question it answers |
|---|---|
| `id`, `name`, `device_type` | who is it (name and type are description, not identity) |
| `known` | is it part of my setup (else NEW) |
| `has_cred`, `link` | is it proven (VERIFY / VERIFYING / SECURE / FAILED) |
| `online` | is it reachable now (for a paired device: proven this session) |
| `auth_failed` | does it need REPAIR |
| `compat` | can MAO understand it (UNKNOWN / DESCRIBING / COMPATIBLE / LIMITED / INCOMPATIBLE / INVALID) |
| `operable` | may controls be offered now (known, proven, online, COMPATIBLE or LIMITED; never while offline) |

**`mao_devices_get()` + `mao_device_controls()` → `mao_device_controls_t`**
for an operable device:

| Field | Meaning |
|---|---|
| `primary_idx` | the primary control: CAPTURE, else LEVEL, else POWER, else none |
| `level_idx`, `toggle_idx`, `action_idx[]` / `action_sem[]` | the available controls, each semantic at most once |
| `ready_idx`, `storage_idx` | read-only facts |
| `described` (device) | every usable value is known (else show a pending value) |

Plus the action state per device, `mao_devices_action_state(id)`: IDLE,
SENDING, ACCEPTED, DONE, FAILED, BUSY, UNKNOWN.

## 2. States to visualise

The page kind is decided in one place, `mao_devpage()` in
`components/mao_app/mao_app_rel.c`.

| State (page kind) | Meaning | Controls available | Character today | Suggested UI priority |
|---|---|---|---|---|
| NEW | heard, not paired | PAIR | absent | medium: an invitation, not an alarm |
| PAIRING (ceremony) | SAS on screen, waiting for both sides | MATCH / CANCEL | absent | high while it lasts (the user is mid-ceremony) |
| VERIFY | remembered from M3.0, never secured | VERIFY, INFO | absent | medium |
| REPAIR | paired, cannot prove its identity | REPAIR, INFO | absent | medium-high: one device, never global |
| OFFLINE | known, not reachable | CONNECT (a visit), INFO; no stale controls | absent | low: quiet and descriptive |
| SECURE + DESCRIBING | proven, description pending | none yet (`...`) | absent | low: brief, must not flash controls in and out |
| SECURE + COMPATIBLE | normal | all | participates in the user's own actions only | normal |
| SECURE + LIMITED | a usable subset (maybe empty) | the subset; empty = title, CONNECT, INFO | as COMPATIBLE | normal; optionally a quiet hint |
| SECURE + INCOMPATIBLE | another contract major | INFO → FORGET | absent | low-medium: truthful, never REPAIR |
| SECURE + INVALID | malformed or missing description | INFO → FORGET | absent | low-medium |
| ACTION pending (SENDING / ACCEPTED) | the user's action is underway | the page stays operable; the same device refuses a second action | CAPTURE: the held word; others: ACK | high (the user just did something) |
| ACTION DONE | completed | — | DONE (CAPTURE: rhythm-aware, often silent) | high, short |
| ACTION FAILED / BUSY | refused or failed (unknown statuses count as FAILED) | — | FAIL / BUSY | high, short |
| ACTION UNKNOWN | it may have run; the outcome is lost | — | UNSURE | medium |

Infrastructure states (PAIRING, VERIFY, REPAIR, DESCRIBING, INCOMPATIBLE,
INVALID) have **no character participation today**. That is deliberate,
and stays so unless the UI milestone chooses otherwise.

## 3. Events (foreground vs background)

| Event | Carries | Foreground when | Background otherwise |
|---|---|---|---|
| `DEVICE_FOUND` | registry slot | the user is on DEVICES or that device's page | update the list quietly. Today it plays `mao_audio_notice` and a character glance, and also fires on background re-key blips (M3.2 F3, still to be judged by ear) |
| `DEVICE_LOST` | slot | as above | today: character DEVICE_OFF |
| `DEVICE_CHANGED` | slot | its page is open (redraw atomically) | the list only |
| `ACTION_UPDATE` | slot + state | the result belongs to the open page's device | world only; never shows on another device's page (M3.2 F2) |
| `LINK_CHANGED` | kind | pairing / revoke of the device on screen | row states |
| `REL_CHANGED` | — | a FORGET / PAIR the user started | list order and words |
| `TRANSFER_STEP` / `DEVICE_PROBED` | transfer id / slot | always (CONNECT is a user interaction) | — |

No new event types were needed (brief §136). The existing ones already are
the stable app events:

| Stable meaning | Event |
|---|---|
| device model changed (capabilities, values, compatibility, description) | `DEVICE_CHANGED`, the model published whole |
| device online changed | `DEVICE_FOUND` / `DEVICE_LOST` |
| action state changed | `ACTION_UPDATE` |
| relationship / security changed | `REL_CHANGED` / `LINK_CHANGED` |

Every compatibility change, including DESCRIBING → INVALID after the
request bound, posts `DEVICE_CHANGED`. The LVGL component (`mao_ui`) does
not even link `odd_bus` or `mao_devices`: it receives strings and flags from
`mao_app`, so it cannot parse protocol.

## 4. Stable vs asynchronous data

* **Stable within a boot:** device_id, registry slot, DEVICES order (pair
  order, then first-seen), the page's device (by identity).
* **Changes asynchronously, from the device:** online, link state, compat,
  the capability set (after a new session or a profile change), values,
  facts, name and type (authenticated only).
* **Changes because of the user:** focus, edit mode, action state, sheets.

The capability set, and with it the control list, can change under an open
page. The model is replaced whole, and focus falls back to the first control
if the focused one vanished. The UI must never keep a stale control
clickable.

## 5. Transitions worth designing (motion hooks, not implemented)

device appears · becomes SECURE · goes offline · returns · description
arrives (DESCRIBING → COMPATIBLE / LIMITED) · becomes INCOMPATIBLE / INVALID
· returns to COMPATIBLE after a firmware change · primary action starts ·
completes · fails · capability set changes under an open page · pairing
ceremony steps · FORGET.

## 6. Deferred to the UI milestone (known list)

* Physical encoder walk with two devices, screen review, audio review of
  background re-keys (M3.2 F3). The user deferred these.
* Words and visuals for INCOMPATIBLE / INVALID / LIMITED. M4.0 used plain
  words, with INVALID the least user-friendly.
* An empty LIMITED device shows only its title, CONNECT and INFO.
* After POWER (a SET) focus stays on POWER, while after an action it snaps
  back to the primary. That is the existing grammar, and the difference may
  deserve a look.
* NOT READY / STORAGE spacing, round-edge spacing, motion timing, character
  expressions (brief §119).
