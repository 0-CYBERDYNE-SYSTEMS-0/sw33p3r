# Room Sweep — Developer Handoff

**Current as of 2026-09-21 (evening session).** This file carries (1) the
live state of the capability rollout and what remains, (2) the device status
that gates the owed verification, and (3) the older 2026-08-01 v3.0.1
archive snapshot at the bottom (kept for history — do not copy tab counts,
line counts, or file lists from it).

## Read these first (in this order)

1. [`AGENTS.md`](AGENTS.md) — repo conventions, hard rules, architecture map.
   [`CLAUDE.md`](CLAUDE.md) mirrors it and the two must stay in sync.
2. [`MISSION.md`](MISSION.md) — detect/analyze **only**. No attack modes, ever.
3. [`FLIPPER_PITFALLS.md`](FLIPPER_PITFALLS.md) — before touching notifications,
   input, UART, or radio code.
4. [`docs/BFFB_MOMENTUM.md`](docs/BFFB_MOMENTUM.md) — source of truth for every
   Marauder UART command and output format. Pinned by live probe transcripts,
   not wiki text.
5. [`specs/full-capability-expansion-2026-09-20.md`](specs/full-capability-expansion-2026-09-20.md)
   — the master spec for the program currently in flight (phases, contracts,
   verification gates, "never in scope" list).
6. [`progress.log`](progress.log) — dated session log (most detailed recent
   history). [`fix_plan.md`](fix_plan.md) — session-by-session work queue.
   [`features.json`](features.json) — QA checklist with per-feature
   `passes` flags. [`specs/sigint-truth-audit-2026-09-11.md`](specs/sigint-truth-audit-2026-09-11.md)
   — the truth-contract standard every string must survive.

---

## Where the work stands — MERGED TO `main` (was `feat/capability-rollout-2026-09`)

**2026-09-21: the rollout is merged to trunk** (`d55e5a1`, merge commit; the
feature branch is kept for history). Rollback: tag
`restore/pre-capability-merge-2026-09-21`. The merged build was deployed and
smoke-tested on the rig (launch → run → clean exit, console alive).
`./init.sh` ALL PASS (25 suites) and `_verify_api.py` CLEAN on `main`.

The in-flight program is **"identify every emitter"** — an 11-phase,
receive-side-only capability rollout specced in
`specs/full-capability-expansion-2026-09-20.md`. One phase per iteration,
each phase = its own host test suite + commit + receipt. Phase 0 probed the
real BFFB firmware over serial (`tools/marauder_probe.py`, transcripts in
`.omo/evidence/` — gitignored, local only) and pinned actual output formats,
which redirected later phases: `sniffdeauth` and `scanall` are **absent from
this build**, so the deauth-detection phase pivoted to hostile-tooling
detection.

Phase status:

| Phase | Feature | Status |
|-------|---------|--------|
| 0 | Firmware probe / capability receipt | **COMPLETE** — formats pinned in `docs/BFFB_MOMENTUM.md` |
| 1 | OUI vendor lookup + device-class hints (`room_sweep_oui.h`, `room_sweep_classify.h`) | **LANDED + device-verified** (PRT? tag on real printer; branch `feat/phase1-identification-2026-09` merged, rollback tag `restore/pre-phase1-2026-09-20`) |
| — | BLE random-static fix (0xC0 top bits now detected; curated OUI wins) | **LANDED** on this branch |
| 6 | Duplicate-SSID rogue/evil-twin correlation (`room_sweep_rogue.h`, `!` marker, `SAME NAME ON n BSSIDS`) | **LANDED**; on-device not exercisable — this room has no duplicate SSIDs (no cloned-SSID line in any report, no `rogue=` token). Host-verified only |
| 7 | Per-device RSSI min/max/avg stats (`room_sweep_stats.h`) | **LANDED + DEVICE-VERIFIED** (ranges in `report-16/17/18.txt`; `min=/max=/avg=` on 1588/620/700+ rows) |
| 8 | RF burst watch mode (`room_sweep_watch.h`, `RfSubWatch`, BURSTS/LAST/DUTY page) | **LANDED + DEVICE-VERIFIED** — 8 then 32 `observation submode=watch` rows with `duty=/bursts=`; the WATCH page pixels are not visually confirmed (no screenshots) |
| 9 | Opt-in cross-session watchlist (`room_sweep_watchlist.h`) | **LANDED + DEVICE-VERIFIED** end-to-end: flag → `watchlist.txt` (redacted synthetic entry) → 174 `watch=1` rows → report `Watchlist matches: 1`. Badge pixels not visually confirmed |
| 2, 3 | Absorbed into Phase 1's ident layer (OUI + name hints) | **LANDED** |
| 4 | Client/station visibility — pivoted to `sniffraw` transmitter radar + `sniffprobe`/`sniffesp`/`sniffpwn` sources (commit `937d7d`) | **LANDED + DEVICE-VERIFIED** for RAW: 1955 `submode=raw` rows with `STA-NN` ordinals in `session-20.csv`. Measured limit: ~55 rows/s in a busy room outran the recorder and dropped 271 rows (11.7 %, counted and reported) |
| 5 | Hidden-SSID passive recovery (`room_sweep_probe.h`) | **LANDED + FORMAT UN-GATED** (`6c83d28`): redacted fixture pins the format, 6/6 records parse, 10 validation `submode=probe` rows covered with `PR-01..PR-06` |
| 10 | Hostile-tooling detection (`sniffesp`/`sniffpwn`, Wi Src TOOL) | **Code landed, parser still FORMAT-GATED** — the 2026-09-21 TOOL window heard nothing (no hostile-tooling emitter in the room) |
| 11 | Capstone: RF presets 16→20, full-sweep probe pass, Info Caps card, docs refresh | **LANDED** (`c2e6089`, `2d365d2`); 11a and 11b **DEVICE-VERIFIED** (20/20 baseline rows incl. the four new presets; every sweep pass measured at its ceiling, Wi-probe 8.1 s). 11c's Caps page is text-only — no pixels (no screenshots on this build) |
| — | **OOM-on-launch crash fix** (app state 20644→14500 B, NULL-checked allocation) | **LANDED + device-verified** (`6d5b028`) — this was found on the first device deploy of phases 6-9 |

### Device status — read this first (2026-09-21)

**The device walk is done for phases 7, 8, 9, 11a, 11b and for the phase 5
parser.** Phases 7/8/9 came from session artifacts, 11a from a 20-row baseline
snapshot, 11b from a full sweep whose every pass hit its documented ceiling,
and phase 5's format gate is satisfied by a live `sniffprobe` capture. Two
device-found defects were fixed and re-verified on the way (identity-map
saturation; the baseline's 20-row burst). Full detail with artifact names:
`.omo/evidence/phase-receipts-2026-09-21.md`.

**The rig's console is flaky, and that is normal here:** it has gone silent
mid-run four times. The app is not at fault — `session-16.csv` shows it
scanning and logging 4.5 minutes after the console stopped answering, and
closing cleanly. It never recovers on its own (polled for minutes; baud change
and DTR/RTS toggles do nothing; `ufbt` can't reach it either) and needs a
power cycle. So: one connection per run, short runs, and **harvest artifacts
before driving the app** — the scripts in `.omo/` are built that way
(`session1_harvest.py`, `session2_full_sweep.py`, `ssid_capture.py`).

**Also measured this session, and it changes how receipts are taken:**
screenshots via RPC no longer work. With the app running, the largest
contiguous heap block is **7128 B** and the device-side screen stream needs
more than that — `rpc_gui_snapshot_screen()` reboots the Flipper with "out of
memory" (reproduced twice). The 2026-09-20 session could still do it; phases
4-10 grew the app past the point where it fits. Receipts must come from the
session artifacts (`session-N.csv`, `report-N.txt`, `watchlist.txt`) and CLI
transcripts until the app's runtime headroom grows again.

**What the walk has since established (2026-09-21, later):** the console death
is host-side, not an app hang — `session-16.csv` shows the app scanning and
logging for 4.5 more minutes and closing cleanly after the console went
silent. Two defects the walk found are fixed and re-verified on device
(identity-map saturation redacting rows; the baseline's 20-row burst losing
rows). Phases 7, 8, 9 and 11a are device-verified from artifacts; phase 6 is
environment-blocked (no duplicate SSIDs in this room); 11b/11c and the
probe/esp/pwn parser captures are still owed. Full detail:
`.omo/evidence/phase-receipts-2026-09-21.md`.

**It happened twice more (runs B/C), so treat it as the rig's normal
behaviour, not a one-off:**
- One connection per run, short runs, and **harvest artifacts first** — read
  the session files before driving the app.
- It does not recover on its own: polled for minutes, and a baud change plus
  DTR/RTS toggles do nothing. `ufbt` cannot talk to it either. Only a power
  cycle clears it.

**First command when the rig is back** (harvest what the dead session left):

```sh
python3 .omo/session1_harvest.py     # uart-19/20 + session-19/20 + reports
python3 .omo/session2_full_sweep.py  # FullSweep walk + SSID capture
```

### The format-gated parsers (Phases 4/5/10) — the key open item

`sniffraw`, `sniffprobe`, `sniffesp`, and `sniffpwn` are implemented and
host-tested, but their parsers are **gated**: Phase 0's live capture saw no
lines from `sniffprobe`/`sniffesp`/`sniffpwn` (nothing transmitting in the
room), so the `sniffprobe` (hidden-SSID recovery), `sniffesp`, and `sniffpwn`
output formats are **unpinned** — annotated in `docs/BFFB_MOMENTUM.md` as
"not yet observed live", derived from upstream ESP32Marauder source only.
To finish these phases:

1. Capture a **longer live window** (probes are bursty; the BFFB build already
   has `ForceProbe: true`) using `tools/marauder_probe.py` against the BFFB
   ESP32's own USB port (it refuses while a host session is connected; the
   on-device USB-UART bridge is the workaround — see Phase 0 notes in the
   spec). Save the transcript to `.omo/evidence/marauder-probe-*`.
2. Confirm/adjust the pinned grammar in `docs/BFFB_MOMENTUM.md`, then un-gate
   the parsers in `room_sweep_probe.h` / `room_sweep_sta.h` /
   `room_sweep_tool.h` and extend their host suites with real fixture lines.
3. `scansta` requires `scanap` to run first on this build and its station-line
   format is still unobserved — same treatment.
4. Also still open from Phase 1's device session: BLE resolvable/private
   (0x01/0x00) addresses are undetectable as randomized — documented, follow-up
   candidate.

### Immediate next steps (in order)

1. **Phase 10's tooling parsers need a live emitter, not a code change.** The
   TOOL window on this rig produced zero records (no Marauder/ESP32/Pwnagotchi
   in the room). Capture a window where one is present, then pin
   `sniffesp`/`sniffpwn` in `docs/BFFB_MOMENTUM.md` the way `sniffprobe` was
   just pinned, and add the fixture lines to `tests/test_tool.c`. Until then
   they stay annotated "not yet observed live" — that annotation is doing its
   job.
2. **`scansta`'s station-line format** is still unobserved (needs `scanap`
   first plus an associated client). Same treatment; the command stays unsent.
3. **RAW source fidelity (measured, not fixed).** The client radar logs one row
   per frame and in a busy room outruns the recorder: 271 dropped rows (11.7 %)
   in `session-20.csv`, counted and reported but lossy. Follow-up candidate:
   log per MAC per interval (the stats tokens already aggregate min/max/avg and
   the observation count), which would cut CSV volume and eliminate the drops.
   Not done because it changes evidence granularity and needs its own device
   run — and note the record queue is only 8 slots deep by design (RAM), so a
   bigger queue would not fix a sustained rate mismatch anyway.
4. **Phase 6 stays environment-blocked** until a room with two APs sharing an
   exact SSID shows up: no duplicate SSIDs in this one (checked against the
   wire, `uart-23.txt`), so the correlation has nothing to correlate. Host
   tests cover the logic.
5. **Screenshots remain unavailable** on this build (the RPC screen stream
   needs more contiguous heap than the app leaves — reproduced three times,
   including on the fixed build). Anything pixel-level — the survey strip
   geometry, the `WATCH`/`!`/`?` badge layout, the Info Caps page — is
   host-pinned in `test_ui_layout.c` and has never been seen on the device.
   Say that plainly rather than implying a visual check.
6. **Optional, cheap:** merge the branch to `main` once you are happy with the
   receipts — every phase is either device-verified or blocked on an emitter
   or a different room.

### Working rules you must not break

- **Verification gate:** `./init.sh` (all host suites, `-Werror`) +
  `python3 _verify_api.py` (every called symbol must exist in the Momentum
  export table — an unlisted symbol hard-faults at loader open, not link) +
  `ufbt` zero warnings. `./init.sh` must pass before any device deploy.
- **Truth contract:** every new string passes the sigint-truth-audit standard.
  "name suggests", "possible cloned SSID" — never "is a camera"/"is an
  attacker". Energy is never identity; a MAC is not an owner. Heuristics carry
  `?`.
- **Privacy:** session CSVs keep per-session ordinals; raw MACs only in the
  opt-in watchlist file (`watchlist.txt`, OFF by default, never persisted) and
  Raw Dump. GPS coordinates omitted unless GPS Log ON.
- **Never in scope (pinned):** `attack*`, beacon spam, karma/evil portal,
  `sniffpmkid`, deauth TX, nRF24 packet-address recovery, jam modes, anything
  the CC1101 cannot tune (5 GHz, cellular RX). Adding these violates
  MISSION.md.
- **Evidence receipts** go in `.omo/evidence/` — local only. `.omo/`,
  `.codex/`, `.debug-journal.md` are gitignored and must never be committed.
  `*.fap` and `dist/` are gitignored.
- **Testable logic lives in header-only, Flipper-header-free state headers**
  (`room_sweep_*.h`) so host suites compile with plain `cc`; `room_sweep.c`
  (~5790 lines, intentional monolith) stays wiring/rendering only. When
  changing behavior: put the decision logic in the header, add a host test.
- RF/TX thread stack is 2 KiB → thread stack-locals < 64 bytes. Integer math
  only (`-Wdouble-promotion` is `-Werror`). `NotificationSequence` is a
  NULL-terminated array typedef; custom sequences `static const` with the
  force-volume prepended messages; delays only 1/10/25/50/100/250/500/1000 ms.
- Branches: `main` trunk; `feat/*` work; `restore/*` pre-overhaul snapshots.
  Merge each phase to `main` after receipts. **No Anthropic/Claude co-author
  trailers, ever.**

---

## Build / test / deploy

```sh
./init.sh            # THE gate: all host tests (-Werror) + ufbt build
ufbt                 # build only -> dist/room_sweep.fap
ufbt launch          # upload + run (app must NOT already be running)
python3 _verify_api.py   # every called symbol must exist in the export table
```

Run one host suite by copying its `cc` line from `init.sh`, e.g.:

```sh
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/t_nmea && /tmp/t_nmea
```

Host suites (25 as of phase 11): `test_nmea`, `test_input_state`,
`test_settings_state`, `test_ui_layout`, `test_report_state`,
`test_full_sweep_state`, `test_oui`, `test_classify`, `test_rogue`,
`test_stats`, `test_watch`, `test_watchlist`, `test_probe`, `test_sta`,
`test_tool`, `test_rf_presets` (new in 11a), and the older per-module suites —
see `init.sh` for the full list and exact compile lines.

Deploy workflow (Flipper via USB, close the running app with a complete
Back press/long/release input sequence first, then `ufbt launch`): **the rig
was unresponsive at the end of the 2026-09-21 session — power-cycle before
trusting a silent CLI**; see the
archive section below for the serial commands — they still work.
Serial port: `/dev/cu.usbmodemflip_XXXX001` @ 115200; if "Resource busy",
`lsof` → kill the PID. BFFB ESP32 has its own USB port (`cu.usbserial*`) for
Marauder probing; the on-device bridge refuses while a host session is
connected.

---

## Handoff Rule (unchanged)

Three verification levels — never conflate them:

1. **Source-backed:** code path exists and compiles.
2. **Host-verified:** `./init.sh` suites pass.
3. **Device-verified:** confirmed on physical hardware with a receipt in
   `.omo/evidence/`.

Device-verified: Phase 0 probing + Phase 1 identification (2026-09-20); the
OOM-on-launch fix; phases 7, 8, 9 and 11a (2026-09-21, from session artifacts
— receipts in `.omo/evidence/phase-receipts-2026-09-21.md`). Host-verified
only: phase 6 (environment can't exercise it), 11b, 11c, and the phase 5/10
parsers, which are still format-gated. Screenshots are not available on this
build, so anything purely pixel-level (badges, strip geometry, the Caps page)
is host-pinned, not device-seen — say so rather than implying a visual check.
Never report a lower level as a higher one.

---

---

**Everything below this line is the 2026-08-01 v3.0.1 archive snapshot.**
Tab counts, line counts, file lists, and statuses above the double rule
supersede it. Kept because the serial deploy commands and pitfall summaries
are still accurate.

---

**Archive date:** 2026-08-01
**Archive version:** v3.0.1 (HEAD d6182a1 plus verified working-tree fixes)
**Branch:** main
**Firmware:** Momentum mntm-012, API 87.1, target 7
**Build tool:** ufbt (pyenv shim at ~/.pyenv/shims/ufbt)

---

## Archive state (2026-08-01)

Six-tab Flipper app (nR / Waterfall / analyzer suite did not exist yet). The current working tree builds clean with -Werror,
passes 58/58 executable NMEA assertions plus 4/4 Back-state assertions, and has been
installed and traversed on the connected Flipper without a crash. Device proof
uses valid Press→Short/Long→Release input sequences; iPhone Mirroring visually
checked RF Survey, Settings, WiFi, BLE, GPS, TX, and Info after the final UI
spacing fixes.

| Feature | Status |
|---------|--------|
| RF Survey (16-point) | Working — live RSSI bars |
| RF Band Sweep (3 bands) | Device traversal verified — signal detection needs field verification |
| RF Peak Refine | Device traversal verified — signal detection needs field verification |
| WiFi AP scanner | Implemented — needs BFFB hardware test |
| BLE device scanner | Implemented — needs BFFB hardware test |
| GPS passive listener | Working — stale-fix + GLL bugs fixed |
| TX (safety-gated tab) | Arm/disarm/navigation verified — deliberate RF transmission needs field verification |
| Audio feedback | Implemented — continuous Geiger model |
| Vibro feedback | Implemented — heartbeat + edge + lock |
| Settings overlay | Fixed (was crashing, InputTypePress bug) |
| Info tab | Working — live state card |

---

## Build & Deploy (archive)

```bash
# Deploy workflow (Flipper must be plugged in via USB):
# 1. Close a running app with a complete input sequence
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodemflip_XXXX001', 115200, timeout=2)
s.write(b'input send back press\r\n')
time.sleep(0.1)
s.write(b'input send back long\r\n')
time.sleep(0.1)
s.write(b'input send back release\r\n')
time.sleep(2)
s.close()
"

# 2. Upload (will error on RPC close step — ignore it, upload succeeds)
ufbt launch

# 3. Launch via CLI
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodemflip_XXXX001', 115200, timeout=3)
s.write(b'loader open /ext/apps/Tools/room_sweep.fap\r\n')
time.sleep(1)
s.close()
"
```

If `ufbt launch` reports that the current fullscreen app must be closed manually,
send the complete input sequence above, then rerun `ufbt launch`.

**Serial port:** `/dev/cu.usbmodemflip_XXXX001` at 115200 baud.
If "Resource busy": `lsof /dev/cu.usbmodemflip_XXXX001` → kill the PID.

---

## Architecture Notes (archive)

### Threading Model
- **Main thread:** GUI event loop (input + view_port_update)
- **RF sweep thread:** `rf_sweep_thread` — runs continuously, handles all 3 sub-modes
- **TX thread:** `tx_thread` — spawned on Long-OK, auto-exits after duration
- **UART:** callback-driven via `furi_hal_serial` (no dedicated thread)

### Key Patterns
- UART ISR only enqueues bounded lines; the main loop parses them. RF/TX radio state is serialized with a mutex; GUI draws app state.
- `NotificationSequence` = NULL-terminated array of `const NotificationMessage*` (NOT a struct)
- Force messages (`message_force_speaker_volume_setting_1f`, `message_force_vibro_setting_on`) bypass global mute
- Only these delays exist: 1, 10, 25, 50, 100, 250, 500, 1000 ms
- Input filtering: ONLY process `InputTypeShort` and `InputTypeLong`. Never InputTypePress.

### TX Safety State Machine
```
DISARMED --[Short OK]--> ARMED --[Long OK]--> TRANSMITTING --[timer expires]--> DISARMED
                            |                                                       ^
                            +--[Short Back]------------------------------------------+
```
TX NEVER fires on tab entry. Multiple deliberate actions required.

### Marauder Protocol (JCMK BFFB = Dev Board Pro)
See `docs/BFFB_MOMENTUM.md` for wiki + source citations.
- UART: USART1 @ **115200**, `expansion_disable`, TX ends with **`\\n`** (companion style)
- WiFi: **`sniffbeacon`** (AP beacon path; not legacy `scanap`. `scanall` is AP+STA and not used here)
- BLE: **`sniffbt`** → `-60 Device: NameOrMac`
- GPS: **`nmea`** stream; GPS is on ESP32 only (BFFB wiki — not Flipper GPIO)
- Stop: **`stopscan`** (companion also uses `stopscan -f`)
- WiFi line: `-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName 00 00`
- No "done" marker; BLE RSSI updates for known devices are silent

(Note: the current app now uses GPS on Flipper GPIO LPUART 15/16 @ 9600 as
primary with the Marauder `nmea` stream as fallback — see AGENTS.md. The
archive line above reflects the pre-phase state.)

---

## Critical Pitfalls (summary — see FLIPPER_PITFALLS.md for full list)

1. **NotificationSequence is an array**, not a struct with .message_count
2. **Only discrete delays exist** (1,10,25,50,100,250,500,1000) — no message_delay_150
3. **No sequence_sound_off builtin** — must define custom seq_sound_stop
4. **InputTypePress fires BEFORE Short/Long** — processing both = double-fire crash
5. **-Wdouble-promotion is -Werror** — use integer math for ms→s display
6. **CC1101 has 3 bands with real gaps** — 300-348, 387-464, 779-928 MHz
7. **RX and TX share the CC1101** — never simultaneous
8. **ufbt launch RPC close hangs** — close app via CLI first
