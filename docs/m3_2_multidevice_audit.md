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

## Fixes (as built)

* **F1** (`9405cef`): `action_tx_t` is a member of each registry `entry_t`;
  `mao_devices_invoke_action` refuses only when that device has one open;
  `mao_devices_action_state(id)` is per device. Exactly-once rules unchanged.
* **F2** (`0f18f00`): `on_action_update` drops page and character feedback
  for a result whose slot is not the open DEVICE page's device (the world
  still refreshes). Per-slot `s_act_sem` / `s_act_via_centre`.
* **Diagnostics** (`065cb9f`): `mao link status` prints each peer's bound
  radio MAC and whether its ESP-NOW entry is encrypted, plus peer counts.
  No key material.
* F3 and F4 unchanged (observation / documented edge).

## Hardware validation (two physical endpoints)

Bench: MAO (98:88:e0:d4:d0:90); CAMERA 01 on a LOLIN C3 Mini
(60:55:f9:23:53:24, id 0dd06055f9235324); LAMP 01 on an ESP32-C3 v0.3
(a0:76:4e:1d:86:d4, id 0dd0a0764e1d86d4). Both endpoints at the 8.5 dBm TX cap.
Distinct credentials and sessions throughout; `link status` shows two
encrypted ESP-NOW entries bound to the right MACs (3 peers incl. broadcast,
max 10).

| Case | Result |
|---|---|
| Pair both, one after the other | PASS: distinct fp, the first undisturbed |
| CAMERA 20 CAPTURE / 5 IDENTIFY / 5 SYNC / 3 CONNECT with LAMP online | PASS, LAMP counters unchanged |
| LIGHT 20 LEVEL / 10 POWER / 5 IDENTIFY / 3 CONNECT with CAMERA online | PASS, CAMERA unchanged |
| F1: LIGHT IDENTIFY while a CAMERA action is open | PASS (was "still working") |
| F2: CAMERA UNKNOWN while the LAMP page is open | PASS: no feedback on LAMP |
| Runtime re-key ×5 each | PASS: the other session unchanged |
| Endpoint reboot ×5 each, simultaneous ×3 | PASS: same credential, fresh session, 2 rows |
| MAO reboot ×10 | PASS: SEEN→SECURE median 10 / 11.5 ms, boot→SECURE 481–503 ms, 0 writes |
| Online FORGET / re-pair ×3 each | PASS: moves to the end, the other untouched |
| Offline forget / re-pair ×2 each | PASS: revoke NOT confirmed, commit-last replace |
| REPAIR ×3 each while the other is SECURE | PASS: order kept, the other controllable |
| Old credential after re-repair | PASS: refused, the other unaffected |
| Forget row 0 with row 1's page open (§86/87) | PASS: page and selection follow identity |
| Row 0 offline with row 1's page open (§88) | PASS: selection kept, row 1 action once, rejoin with order kept, 0 writes |
| 20 mixed leak cycles (re-key, SET, reboot) | PASS: heap 151,128 B at every checkpoint, 0 resets, 0 writes |
| 34 min dual soak (SET traffic on both, status every minute) | PASS: 0 resets / offline / hello fail / env drops; heap 151,128 → 150,904 → 150,864 B (two one-time steps, then flat), min 146,992 |
| CONNECT to LAMP while CAMERA reboots and re-keys (§58) | PASS: transfer stays AWAY in LAMP, returns on press; LAMP session untouched |
| CONNECT to CAMERA during LAMP SETs and reboot (§59) | PASS: CAMERA session untouched |
| Fairness: 5 CAPTURE baseline, 5 during a 50 Hz LAMP flood (1250 frames) | PASS: each exactly once; radio→ACK 5.7–9.1 ms (flood) vs 5.9–12.2 ms; DONE ≈ 0.7 s both |
| NVS writes over the whole run above (≈ 49 min, 34 HELLOs) | 0 |
| Profile swap B LIGHT → CAMERA → LIGHT while A runs (§53) | PASS: same id and credential, 1 write per swap (B only), B's caps refresh with no ghost controls, A's session untouched |
| Two devices named "CAMERA 01" (§54/§55) | PASS: one CAPTURE per row, each executed only by its own board |
| Same ACTION seq (60001) and cap on both devices (§81) | PASS: each executed once |
| Both endpoints silent (§129) | PASS: MAO TX ≈ 0.33 frames/s, no HELLO storm; CAMERA released first recovers without waiting for LAMP |
| CAMERA wrong credential, LAMP valid (§130) | PASS: CAMERA FAILED / NOT VERIFIED / REPAIR, retries ≈ 2 per 30 s, its ESP-NOW entry removed; LAMP SECURE, IDENTIFY and POWER work; heap flat; REPAIR → fp 92d9eea1, 1 write |
| Recovery after CAMERA reboot, LAMP idle vs flooding (§97) | endpoint boot → SECURE 4.3 / 5.3 / 4.7 s vs 5.1 / 3.9 / 4.4 s: no difference |
| Feedback collision: 6 s CAPTURE, LAMP page + IDENTIFY meanwhile (§77) | PASS: no BUSY/DONE bleed either way, each executed once |
| Single device (§138): CAMERA alone, LAMP alone | PASS: actions, re-key, MAO reboot (SECURE 494 ms) |
| Endpoint reboot ×6 after the F5 fix | 0 plaintext refusals on either endpoint |

## F5 (found on hardware, fixed)

An endpoint's boot-time ANNOUNCE (dst ANY) reached the *other* endpoint,
which counted it as `plaintext refused rx` — the counter meant for
operational plaintext from a controller. The frame was already dropped (no
security effect), but ordinary two-device use raised a security counter.
Fixed in the bench endpoint (`78ed99b`): an endpoint ignores ANNOUNCE (it
only ever sends it). MAO and `odd_link` unchanged.

## Jobs, buffers, timers (§124–§127)

* ESP-NOW RX callback copies each frame by value into the devices inbox; no
  shared scratch buffer, no crypto or UI work in the callback.
* HELLO nonce, HELLO frame buffer, tries, probe / retry / offline deadlines:
  per `peer_t` (link) or per `entry_t` (ODD). `hello_tick` walks all peers.
* Link jobs: pairing jobs belong to the single ceremony (`s_pair`, carries
  its device id); revocation is single (`s_revoke`, carries its id);
  dev-hook jobs carry their argument. No "current peer" target.
* Action deadline / recovery: per device (F1). Transfer: one interaction,
  guarded by transfer id and `s_tr.dev`; probe answers and device loss are
  matched to that device.
* Events carry identity: DEVICE_* and ACTION_UPDATE carry the registry
  slot; secure / probe callbacks carry device id + MAC; LINK_CHANGED is
  resolved through the ceremony / revocation state, which carries its id.
* Pairing-result feedback is shown on the open page without comparing
  devices, but the ceremony page captures all input until the result
  (BACK cancels in place), so it cannot land elsewhere. Edge, like F4.

## Resources

| | 0 peers | 1 peer | 2 peers |
|---|---|---|---|
| free heap, idle | 151,692 | 151,428 | 150,864–151,128 |
| ESP-NOW peers (encrypted) | 1 (0) | 2 (1) | 3 (2), max 10 |
| mao_devices stack free | 2,452 | 1,236 | 1,172 |
| mao_link stack free | 5,488 | 5,488 | 3,504 (2,912 after selftest) |

≈ 260–400 B heap per secure peer. Image: DEV 1,312,256 B (+1,280 vs M3.1,
28.5 % free); release 1,234,784 B (+320, 32.7 % free).

## Known gaps

* Hardware validation covers two simultaneous secure peers, not eight.
* Physical encoder walk, screen review and audio (F3) deferred to the UI
  milestone (user decision).
* No hook for an ESP-NOW peer-install failure (§69): not tested.
* After an endpoint reboot MAO re-keys when its offline detection fires
  (≈ 5 s with a DEVICE page open, longer at HOME): single-device behaviour,
  unchanged.

Memory idle (free heap) with 0 / 1 / 2 secure peers: 151,536 / 151,344 /
150,880 B.

Host suites: character 36/36 identical; relationships 122 (84 + 38 M3.2);
link 180 + 73 (220 + 33 M3.2). On target: link selftest 47/47, ODD 64 (final firmware `b0b218b`).
