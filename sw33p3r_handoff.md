# sw33p3r handoff — launch OOM crash + finalize checklist (2026-10-11)

For the next dev team. Read `AGENTS.md` first for repo rules. This file is the
bridge: what shipped today, what is broken on device, everything we measured,
and the exact protocol to finish.

## P0 — the open defect: FAP OOM-crashes the Flipper at runtime

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

### Protocol to finish (use skill `flipper-device-harness` — it encodes all of this)

1. Clean state: unplug/replug (done), CLI alive (`uptime` answers).
2. Upload with generous timeout + **verify on-SD size** matches `ls -la` of
   the built `.fap` (upload.py + rpcvenv in `~/.hermes/cache/scratch/`; kill
   running apps first — RPC conflicts).
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

## Shipped today (all committed, host gates green)

| Commit | Contents |
|---|---|
| 5174b22 | AV tab (8th) + ASTM F3411 RID parsing (`room_sweep_rid.h`/`_rid_state.h`, DR-nn ordinals, rate-limited events) — send path GATED OFF (`ROOM_SWEEP_RID_SEND_ENABLED=0`): `remoteid` is NOT compiled into the current BFFB build (probe receipt 2026-10-11 in `docs/BFFB_MOMENTUM.md`) |
| 1d7fec1 + c499988 + 80f99af | Host dashboard `tools/room_sweep_dashboard.py` + GeoJSON/WiGLE exports + premium design + WCAG/palette pass (13/13 fixture tests, vision-verified 8/10) |
| 822eb76 | FieldSweep (GPS-stamped outdoor loop; refuses without GPS Log; field report section; RID report-coverage) |
| 282ab38 | BFFB live probe receipt (full CLI command set pinned, scanap 2-line format pinned, sniffdeauth build-drift note) |
| 03a8383 / ea2796e / 26156cd | Specs (drone-rid, field-sweep, esp32-c5 [HARDWARE-SHELVED], rx5808 [HARDWARE-SHELVED], host-dashboard), features.json QA checklist, AGENTS/CLAUDE/MISSION 8-tab sync |

Host state: `./init.sh` ALL PASS incl. `ufbt` build zero warnings
(Target 7 / API 87.1); `_verify_api.py` CLEAN. The FAP builds correctly —
the bug is runtime memory on device.

## Owed / not done

- P0 crash fix (above).
- features.json: drone-rid + field-sweep entries marked passes:true are
  host-verified; the on-device receipt walks are still owed (crash blocks).
- `docs/room_sweep_control_map.html` + field guide: not updated for AV tab.
- MISSION.md definition-of-done field tests (audio near router, TX guardrails,
  in-between signal) — still open.
- esp32-c5 / rx5808 specs: implement only if that hardware arrives.
- If BFFB firmware gains `remoteid` (verify via `help`), pin the format with
  `tools/marauder_probe.py`, then flip `ROOM_SWEEP_RID_SEND_ENABLED` (one
  line, `room_sweep.h`) and update docs.

## Tooling & evidence map

- Harness scripts: `~/.hermes/cache/scratch/` — `ab_launch.py` (launch-only
  survival), `ab_test.py`/`ab_test2.py` (upload+watch), `runC.py` (heap
  trajectory), `upload.py` + `rpcvenv/` (RPC file upload).
- Device port: `/dev/cu.usbmodemflip_Rug1k01`. CLI-dead recovery: 10 rapid
  open/close DTR cycles (reboots device). Momentum `top` swallows CLI — never
  embed it in a harness.
- Evidence: `.omo/evidence/` (launch-oom-fix-2026-10-11.md, receipt-walk-shots/,
  launch-oom-shots/, marauder-probe-2026-10-11-165200.txt) — gitignored, local.
- Worker transcripts with raw telemetry: `~/.hermes/cache/delegation/live/`.
