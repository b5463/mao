# M3.2 multi-peer architecture audit (engineering note)

Question: can MAO hold more than one authenticated peer without credentials,
sessions, UI state, capability state, retries or identity bleeding between
them? Answered from the M3.1 code (`d4a0960`), before any M3.2 change.

## Answers (§12)

| # | Question | Finding |
|---|---|---|
| 1 | Relationship records | `MAO_REL_MAX` = 8 records, NVS keys `r0`–`r7`, one per slot; a write touches only the affected slot. |
| 2 | Live registry slots | `MAO_DEVICES_MAX` = 8 `entry_t`, looked up by device_id. Slots are assigned first-heard and never reused within a boot, so events carrying a slot index stay unambiguous. A ninth distinct device heard in one boot is ignored ("registry full"). |
| 3 | Secure peer slots | `PEERS_MAX` = 8 `peer_t` in `mao_link`, one per credential. |
| 4 | ESP-NOW peers | `mao_radio_set_peer_key(mac, lmk)`: add or modify the encrypted peer for that MAC; `NULL` deletes that MAC only. Unknown devices are reached by broadcast, never by an ESP-NOW peer entry. |
| 5 | One session or per device | **Per device.** Each `peer_t` owns its key, bound MAC, `odl_session_t` (session id, LMK, K_c2d / K_d2c, TX counter, RX window), HELLO nonce and buffer, tries, probe and retry timestamps. RX resolves the peer by source MAC, TX by destination MAC, HELLO_ACK by dev_id. |
| 6 | Global retry timers | Link layer: none; `hello_tick` walks every peer's own fields. **ODD layer: the ACTION transaction (`s_act`) is one global state machine**, including its retry and deadline times (finding F1). Per-capability SET retries and ODD session retries are per `entry_t`. |
| 7 | Replay counters | Per peer (`p->sess.rx_hi / rx_bits`), advanced only by authentic frames of that session. |
| 8 | Sequence / session bleed | ODD session state (`session_ok`, `session_seq`, retries) is per `entry_t`; NO_SESSION marks only the answering entry stale. SET command seqs are per capability per entry. ACK and RESULT matching for the action requires `src == s_act.dev`, so results cannot be misattributed. Envelope frames must claim the session's own peer id (`s_auth_mismatch`). |
| 9 | Capability caches | Per `entry_t`; `cap_index(e, cap_id)` is always inside one device, so equal cap ids on two devices cannot collide. |
| 10 | UI active device vs world | The open page is `mao_state()->device_id` (identity) and the list selection is `s_sel_dev` (identity, re-resolved on every refresh), so background list changes cannot move the selection or hijack the page. **Exception: action feedback (finding F2).** |
| 11 | Simultaneous HELLOs | Each peer has its own nonce and HELLO buffer. HELLO building and HELLO_ACK verification run under the `mao_link` mutex; RX processing is serialised on the devices task. `s_rx_auth` / `s_rx_peer` are file-scope, but set and consumed per frame on that single task, so they are safe. |
| 12 | 10 encrypted slots vs 8 relationships | At most one encrypted ESP-NOW peer per secure session, and at most 8 sessions, so 8 ≤ 10 with 2 spare. |

**Deliberately global** (by protocol or UI definition): one pairing ceremony
at a time (`s_pair`), one revocation at a time (`s_revoke`), the controller
incarnation, the discovery schedule, the transfer (one CONNECT interaction,
guarded by transfer id and `s_tr.dev`), and diagnostic counters.

**§13 verdict:** no link-security state (LMK, session id, envelope keys, TX
counter, RX window, HELLO nonce, bound radio, HELLO retry) is global. The only
global *runtime* state for "the active device" is the ODD action transaction.

## Findings

**F1: one ACTION in flight for all of MAO.** `mao_devices_invoke_action`
refuses any new action while `s_act` is SENDING or ACCEPTED, whichever device
owns it. A CAMERA CAPTURE (≈0.7 s), or one that ends UNKNOWN (up to the 8 s
deadline), therefore makes a LIGHT IDENTIFY answer "still working". Nothing
bleeds: results are matched by device, cap and seq. But it is the "global busy
lock" §35 and §77 rule out. SET_VALUE (LEVEL, POWER) is per device and
unaffected. Origin: the M2.3 single-device design ("the one in-flight action
transaction").

**F2: action feedback is not scoped to its device.** `on_action_update` shows
the page's DONE / BUSY / FAILED / REST feedback whenever any DEVICE page is
open (`tool = … && view == MAO_VIEW_DEVICE`) and never compares the action's
device (the event carries its registry slot) with the open page's device. With
two devices, a CAMERA result that arrives after the user moved to LIGHT (easy
with UNKNOWN, 8 s later) would appear on LIGHT's page. It is a real multi-peer
bug (§76, §77); with one device it cannot happen.

**F3 (observation):** a background peer's re-key is a brief "device gone →
available" blip, and every device appearance plays `mao_audio_notice()` and a
character glance (M2.x behaviour). To be checked on hardware against §113
(no unexpected sounds from background re-keys).

**F4 (edge, documented):** a FORGET started while another device's
revocation is still in flight (≤ 1.6 s) falls back to a local-only forget,
because revocation is single. The FORGETTING sheet blocks input, so this needs
two pages within 1.6 s; left as is.

## Smallest per-device model (proposal)

* **F1:** move `action_tx_t` from the file-scope `s_act` into `entry_t`, so
  each device has its own transaction. `invoke_action(id, cap)` refuses only
  when *that* device has one in flight. `service_action` walks the entries.
  ACK / RESULT matching uses the entry the frame came from. Events keep the
  slot. `mao_devices_action_state()` takes a device. The exactly-once rules
  (same identity on retry, never re-invoke after ACCEPTED, UNKNOWN on
  deadline) stay exactly as they are, just per device.
* **F2:** `on_action_update` applies page feedback only when the event's slot
  is the open page's device. The character keeps reacting to the user's own
  action result (it is the user's interaction), unchanged.
* No change to link security, the pairing protocol, the envelope, the UI
  layout or the character.
