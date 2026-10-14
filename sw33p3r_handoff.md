# sw33p3r handoff — launch OOM crash + finalize checklist (2026-10-11)

For the next dev team. Read `AGENTS.md` first for repo rules. This file is the
bridge: what shipped today, what is broken on device, everything we measured,
and the exact protocol to finish.

## Current status — 2026-10-13

The BFFB is now running official ESP32 Marauder v1.18.0. Before updating it,
the full 8 MiB flash was read back and digest-verified as an easy same-board
restore point (`.omo/evidence/bffb-full-flash-before-marauder-v1.18.0-2026-10-12.bin`,
SHA-256 `f8387018ba018a521fcfa191e389916f5caaeefa54cc9390a219bab434877579`).
Only app0 was written; the partition table and other flash regions were not
changed. The official `remoteid scan` / `stopscan` / `remoteid list` sequence
worked, but the 12-second scan and list were empty. No aircraft row was
observed.

Room Sweep has a strict host-tested parser for the v1.18.0 30-value `RID,`
CSV row. Fixtures are synthetic. The scan is passive; RID transmission and
the explicit `remoteid clear` command are disabled. Raw IDs are not
displayed/logged, and TX stays under the existing mission gate. Marauder
v1.18.0 resets its volatile RID table when an all-target scan starts. No real
RID row has been decoded in Room Sweep. See
`.omo/evidence/room-sweep-rid-validation-2026-10-12.md` and the live receipt
`.omo/evidence/room-sweep-rid-live-verification-2026-10-12.md`.

Current device candidate: `dist/room_sweep.fap`, 152,156 bytes, SHA-256
`1f9dd5b1b69ca01fe25b4506702f84a079e35b1b27c896e8c9ef34bc8d0f6edf`.
`./init.sh`, `_verify_api.py`, and `git diff --check` passed. The UART ring
retains 31 complete 272-byte lines across eight 1,088-byte heap allocations;
the RF worker is stopped while the UART is opened to return its stack.

`flipperctl upload` verified the candidate at
`/ext/apps/Tools/room_sweep.fap` (152,156 bytes, MD5
`0e66b4e36e301df50a2455e8209749df`). On 2026-10-13, `loader info` confirmed
Room Sweep running after launch. With no RPC screen capture, the app remained
open for 60 seconds on Wi, then returned to RF. Free heap was 5,680 bytes on
Wi (minimum 5,560) and 12,048 bytes on RF. The app is left running on RF with
TX untouched. The Wi tab transition was verified, but no Marauder response or
scan was confirmed; no eight-tab walk or live Remote ID reception was tested.

A prior build with one contiguous UART allocation reset during a Wi-tab
transition. Keep the chunked allocation and avoid RPC screen captures while
the app is open: an earlier capture attempt was followed by a Flipper reboot.
The bounded BFFB scan/list and AV page were empty, and no live `RID,` row was
seen. Keep Remote ID marked unverified until an actual aircraft broadcast
produces a Room Sweep AV row. Keep `~/.local/bin/flipperctl` as the only host
access path and require a real CLI reply or complete frame before each new
device action.

## Previously verified v5 baseline (2026-10-12)

The v5 140,492-byte FAP (`40095b308e09a120b659b7586897aebd642bd44cce803603318b410a5ff47f56`)
passed the requested host/API gates, exact upload/read-back verification, five
clean launch/exit cycles, the eight-tab walk with TX DISARMED, GPS-Log-OFF
FieldSweep refusal, and honest-empty AV pages. It survived the 131-second
hands-off check; a later relaunch remained on RF Survey across more than 25
minutes between real RPC screen frames. It was left running at the end of that
receipt; its current state is unknown while the Flipper USB CDC port is absent.
The measured low-water heap was 1,096 bytes, so memory headroom remains tight
even though the baseline run and repeated launches passed.

The exact historical 7c236dd → 5174b22 → 822eb76 → HEAD A/B sequence was not
run as one controlled series. Existing receipts show 7c236dd surviving 144.2
seconds and OOM resets for 5174b22, 822eb76, and the pre-v5 HEAD candidate; v5
is the distinct passing image. The precise allocation that triggered the old
OOM remains an inference from measured memory reductions and successful v5
runs, not a captured firmware allocation failure. The empty
`v5-wrapper-readback-2026-10-12.fap` is from a later interrupted redundant
download and is not the successful read-back artifact.

## P0 — historical defect record: FAP OOM-crashed the Flipper at runtime

Symptom (operator-confirmed 2026-10-11): the app launches and RUNS (operator
was using the RF/CC1101 tab), then the Flipper reboots with the framework
**"out of memory"** crash screen ~60–110 s into runtime. Reproduced 3x today,
serial port vanishes on death, uptime resets on reconnect.

### What we know (measured, not guessed)

- The 2026-09-21 fix (CHANGELOG "Unreleased — fix: out-of-memory reboot on
  launch") documents the failure class: **app state is ONE contiguous
  allocation** (~14,500 bytes after the fix; 20,644 before) taken at entry.
  Entry margin depends on the largest free block at that moment.
- 2026-09-21 receipt: entry free / max-block = **38,784 / 28,832**.
- TODAY on this rig: free heap falls to **~10,528 within seconds of launch**
  (measured via CLI `free` right after `loader open`) — the app is running with
  roughly the documented ~10 KB contiguous headroom, far tighter than 09-21.
- Today's features add ~1 KB to app state (`RoomSweepFieldState field` +
  `uint32_t field_accept_tick[5][2]` + `RoomSweepRidState rid` + scalars —
  see the struct diff in `git diff 7c236dd..HEAD -- room_sweep.c`).
- A crash screen photo from the receipt walk: `.omo/evidence/receipt-walk-shots/`
  (t_state_after_portdrop.png family). Full telemetry: `.omo/evidence/launch-oom-fix-2026-10-11.md`
  (checkpoints C0–C5) + worker transcripts under
  `~/.hermes/cache/delegation/live/deleg_*/task-0.log`.
- CONFOUND: a timed-out FAP upload mid-afternoon may have left a truncated
  FAP on the SD card; one baseline (7c236dd) launch also died. A corrupt FAP
  also crashes on open — **re-upload cleanly before blaming any build**.

### Ranked hypotheses

1. Heap exhaustion at entry/runtime: app-state growth (~1 KB) on top of a
   rig whose heap is tighter than on 09-21 (entry free 10.5 KB vs 38.8 KB).
   Something later needs a contiguous block and the OOM handler reboots.
2. Corrupt FAP on SD from the timed-out upload (confound only — does not
   explain the first crash of the day on a freshly uploaded HEAD build).
3. A runtime allocation (storage file buffers, notification, SPI handle init
   from the Oct-9 nRF24 work) failing in the ~10 KB headroom.

### Original protocol (historical audit trail; see current status above)

1. Clean state: require an actual CLI `uptime`/`loader info` reply or a screen
   frame. Port enumeration and a startup greeting alone do not prove the
   device or app is responsive. If both CLI and RPC are silent, use the
   recovery note at the end of this file; do not keep changing baud rates or
   claim the app is running.
2. Exit any running app first. Upload with a generous timeout, then verify the
   on-SD file size and MD5 against the exact built `.fap`. The helper
   `~/.hermes/cache/scratch/upload.py` uses ufbt `FlipperStorage` over the text
   CLI; `rpcvenv` supplies Python dependencies but this upload is not binary
   RPC. A timed-out upload can leave a partial file.
3. A/B: `git worktree` builds of `7c236dd` (pre-feature) → `5174b22` (AV tab)
   → `822eb76` (FieldSweep) → HEAD. Per build: `loader open
   "/ext/apps/Tools/room_sweep.fap"` (BY PATH), watch 2 min hands-off.
   Death = port vanishes / boot banner / uptime reset. A silent CLI is NOT
   death (the app mutes the CLI while fullscreen).
4. Root-cause the winner: measure `sizeof(RoomSweepApp)` growth across the
   pinned commit; audit runtime allocations in the offending diff; consider
   the 09-21 lesson directly — one monolithic malloc is fragile. Candidates
   in order: split/shrink app state (RID track list 8→4 drones,
   `ROOM_SWEEP_RECORD_MAX_IDENTIFIER_MAP` 64→32), move cold state out of the
   monolith, or allocate sub-structs separately so one failure can't take
   the app down.
5. Verify: 5x launch→exit clean, 8-tab walk (CLI `input send`, TX stays
   DISARMED), FieldSweep GPS-Log-OFF refusal message, AV tab honest-empty
   pages. Then `./init.sh` + `python3 _verify_api.py` + commit.

## Shipped on 2026-10-11 (historical state; current status above supersedes it)

| Commit | Contents |
|---|---|
| 5174b22 | AV tab (8th) + initial ASTM F3411 RID parser/state — at that time the installed BFFB v1.0.0 lacked `remoteid` (superseded by the 2026-10-12 v1.18.0 update below) |
| 1d7fec1 + c499988 + 80f99af | Host dashboard `tools/room_sweep_dashboard.py` + GeoJSON/WiGLE exports + premium design + WCAG/palette pass (13/13 fixture tests, vision-verified 8/10) |
| 822eb76 | FieldSweep (GPS-stamped outdoor loop; refuses without GPS Log; field report section; RID report-coverage) |
| 282ab38 | BFFB live probe receipt (full CLI command set pinned, scanap 2-line format pinned, sniffdeauth build-drift note) |
| 03a8383 / ea2796e / 26156cd | Specs (drone-rid, field-sweep, esp32-c5 [HARDWARE-SHELVED], rx5808 [HARDWARE-SHELVED], host-dashboard), features.json QA checklist, AGENTS/CLAUDE/MISSION 8-tab sync |

Host state: `./init.sh` ALL PASS incl. `ufbt` build zero warnings
(Target 7 / API 87.1); `_verify_api.py` CLEAN. The FAP builds correctly —
the bug is runtime memory on device.

## Owed / not done

- Formal historical A/B sequence as one controlled series (above). It is not
  required before user testing of the passing v5 image; run only if stronger
  root-cause attribution is needed.
- features.json: FieldSweep device walks and AV's real-row device check remain
  open; the current RID feature is marked `passes:false` until live reception
  is verified.
- `docs/room_sweep_control_map.html` + field guide: not updated for AV tab.
- MISSION.md definition-of-done field tests (audio near router, TX guardrails,
  in-between signal) — still open.
- esp32-c5 / rx5808 specs: implement only if that hardware arrives.
- BFFB firmware work is complete for this task: official v1.18.0 was installed
  and its full pre-update flash was backed up and verified. The next required
  RID milestone is a real row received by this FAP after the Flipper CDC port
  is available; do not flip the transmit or clear gates.

## Tooling & evidence map

- Flipper harness: `~/.local/bin/flipperctl`; commands and recovery are in
  `docs/flipper-agent-access.md`. Do not bypass the shared device lease with
  raw serial, `qFlipper`, or `ufbt launch`.
- Latest `flipperctl ports` output showed the BFFB CP2102 at
  `/dev/cu.usbserial-0001`, but no Flipper CDC port. Do not use DTR cycling or
  claim the FAP is running. If the Flipper MCU is unresponsive, use the
  documented physical LEFT + BACK five-second reboot, then require a real CLI
  reply or full screen frame.
- Evidence: `.omo/evidence/` (v5 launch receipts, BFFB restore/update receipts,
  RID scan summary, and `room-sweep-rid-validation-2026-10-12.md`) — gitignored,
  local.
- Worker transcripts with raw telemetry: `~/.hermes/cache/delegation/live/`.

## 2026-10-12 — USB silence and recovery

The leader must own the CDC port exclusively. Before opening it, inspect both
`/dev/cu.usbmodem*` and `/dev/tty.usbmodem*` with `lsof`, and stop only a
tracked stale launch process. CLI input injection must send complete events:
`PRESS`, then `SHORT` or `LONG`, then `RELEASE`; a lone `input send back long`
is incomplete.

An empty `uptime` or `loader info` is inconclusive. A greeting, enumerated
port, or an `ab_launch.py` `SURVIVED` verdict with blank polls is not proof of
app survival. Avoid `top`; it takes over the CLI. A screen snapshot must return
the full 1024-byte frame before it counts as visual evidence.

On 2026-10-12 the leader ran the documented ten rapid DTR open/close cycles
once, then received the 951-byte startup greeting but no `uptime` or
`loader info` bytes. A bounded 15-second protobuf screen snapshot timed out
before a frame arrived. No serial owner remained afterward. Do not repeat
serial probes or claim a crash screen from this evidence alone. If CLI and RPC
remain silent, the required physical recovery is to hold **LEFT + BACK
together for 5 seconds**, then release both and allow the device to reboot.
This is the official normal reboot sequence, not DFU; see the [Flipper Zero
reboot instructions](https://docs.flipper.net/zero/basics/reboot). Once it
re-enumerates, establish an actual CLI response or capture a screen frame
before continuing with upload and launch validation.

## 2026-10-12 — nRF24-only GPIO attachment check

The user identified the newly attached GPIO board as RF 2.4 GHz / nRF24-only.
Treat it as a separate attachment from the earlier BFFB: do not infer an
ESP32 Marauder, Wi-Fi, BLE, GPS, or Remote ID capability. No firmware was
flashed.

The updated candidate (`dist/room_sweep.fap`, SHA-256
`df450b2211f181f1df60e8900112b0e264be57b1d0bb221c339e0d124c518d69`) passed
`./init.sh` and `_verify_api.py`, was uploaded with remote size/MD5
verification, and was downloaded for an exact SHA-256 match. It launched and
reached the nR page. The page reported `2.4G ENERGY` / `SPI NRF24`; one RPD
pass completed and showed 11 active-channel hits. This proves a real energy
survey, not packet decoding or a drone/Remote ID sighting.

After repeated screen captures, CLI status, key input, and a later snapshot
all timed out with 0 bytes. The USB port remained enumerated with no host
owner, but there was no crash screen, so neither OOM nor two-minute survival
is established. Live free heap had been 12,736 bytes, with a 592-byte
low-water mark. Do not call the app fully device-validated or ready for
unplugging yet. Required recovery is a physical **LEFT + BACK hold for
5 seconds**, then release and wait for USB to re-enumerate. Continue only after
a real CLI reply or complete screen frame. The full receipt is
`.omo/evidence/nrf24-only-live-check-2026-10-12.md`.
