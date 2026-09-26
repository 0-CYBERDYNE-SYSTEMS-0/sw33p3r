# Changelog

Keep-a-Changelog-inspired, condensed per release series. The full per-fix
history lives in `fix_plan.md` (session-by-session) and `progress.log`
(dated session log).

## Unreleased — device walk results: phases 7/8/9/11a/11b verified (2026-09-21)

- **The device walk landed the remaining receipts** (artifact-level; screenshots
  are unavailable on this build — the RPC screen stream cannot allocate
  alongside the running app, reproduced three times including on the fixed
  build). Detail with artifact names: `.omo/evidence/phase-receipts-2026-09-21.md`.
- **Phase 11b (Wi-probe pass) is device-verified**: a complete FullSweep session
  whose every pass stopped at its documented ceiling — RF +10.1 s, Wi-Fi
  15.1 s, BLE 15.0 s, Wi-raw 6.0 s, **Wi-probe 8.1 s**, nRF24 honest skip, GPS
  early exit at 2.0 s — ending in `full_sweep_done partial` and a report saying
  the sweep finished. That also verifies the effective-source routing fix: the
  three Wi passes appear as distinct ordinal fingerprints (AP-01/AP-12/AP-14).
- **Phase 11a** re-confirmed: a baseline snapshot wrote 20/20 rows including
  the four new presets at channels 14-19.
- **Client radar (RAW source) measured, not just shipped**: 1955 rows with
  `STA-NN` ordinals and stats tokens, and an honest limit — a busy 2.4 GHz room
  made it enqueue ~55 rows/s, outrunning the 8-slot recorder queue for a loss
  of 271 rows (11.7 %), counted and reported. Follow-up candidate recorded in
  the handoff: log per MAC per interval instead of per frame.
- **Phase 6 premise checked against the wire**: the live SSID list (five APs,
  each with exactly one BSSID) and four sessions with 10-11 APs and no
  cloned-SSID line confirm no duplicate SSIDs exist here to correlate.
- Still owed, and only because the room can't supply it: `sniffesp`/`sniffpwn`
  and `scansta` formats (no emitter, no associated client) and phase 6's
  correlation (no duplicate SSID).

## Unreleased — sniffprobe format validated with a redacted fixture (2026-09-21)

- **Phase 5's format gate is satisfied.** `sniffprobe`'s serial format was the
  last unpinned piece of phases 4/5/10 ("format from upstream source; not yet
  observed live on this build"). A six-record redacted fixture derived from
  the probe window parses with the shipped grammar:
  `RSSI: -60 Ch: 2 Client: 02:11:22:33:44:01 Requesting:` and
  `RSSI: -91 Ch: 11 Client: 02:11:22:33:44:03 Requesting: SYNTH_CAMERA_NET`.
- What the redacted fixture confirms: the v1.9.1 shape with the `RSSI: ` prefix;
  the requested SSID is last and may be empty (mapped to the app's
  `Hidden/unknown`); no target BSSID is printed on this build, so the
  hidden-SSID repair can only fire on a build that prints one (unchanged and
  still documented); and the scan scaffolding around the records never
  false-positives.
- The validation fixture contains 10 redacted probe observations
  (`submode=probe`, ordinals `PR-01..PR-06`) — so the source, parser, ordinal
  registry and Raw Dump evidence path are covered together without retaining
  room-specific identifiers.
- `tests/test_probe.c` now carries six synthetic records as fixtures plus the
  scaffolding lines that must not parse. Docs updated: `docs/BFFB_MOMENTUM.md`
  (live pin replaces the upstream-only annotation), `AGENTS.md`/`CLAUDE.md`.
- Still gated, honestly: `sniffesp`/`sniffpwn` (the 2026-09-21 TOOL window
  heard nothing — no hostile-tooling emitter in the room) and the `scansta`
  station-line format.

## Unreleased — fix: identity map saturation + baseline burst loss (2026-09-21, device-found)

- **Two defects the device walk found, both invisible to the host suite.**
- **Baseline snapshot lost rows.** Taking a baseline writes one event per preset
  in a single tick — 20 events into a queue that holds 8 — so on device 9 of 20
  rows were dropped before storage (`record_queue_full`, honestly counted, but
  the baseline was incomplete). The settings handler now drains in small groups
  as it enqueues, so all 20 rows reach storage. Verified on device: 20/20
  baseline rows, RF-01..RF-20, including the four phase-11 presets at
  channels 14-19.
- **Identifier map saturated, redacting real rows.** The per-session
  fingerprint map held 32 identities; a normal room already needs 20 RF preset
  labels (a baseline alone) plus up to 12 APs plus 12 BLE devices plus the
  system kinds. On device the map filled and later rows came out with
  `id=REDACTED` — safe (nothing leaked) but the evidence lost its correlation,
  and the phase 8 burst rows lost the identifier the spec names. The map is now
  64 entries, and the burst watch writes a **literal** `WATCH` id that spends no
  map slot and cannot be coerced from anything else: a row mistagged as literal
  is refused whole rather than truncated into validity, so a MAC can never reach
  the id column that way. Verified on device: 32 WATCH rows all carrying
  `id=WATCH`, zero `REDACTED` rows across 10 APs + 13 BLE devices + 20 presets.
- New `tests/test_record_state.c` cases (9) pin the literal-vs-identity split,
  the refusal path, and that the map is sized for a full baseline plus both
  wireless tables.

## Unreleased — capability expansion, Phase 11b/11c — sweep probe pass, capability card, docs (2026-09-21)

- **FullSweep gains a Wi-probe pass** (`sniffprobe`, 8 s) after the Wi-raw
  radar pass, with its own completion bit and the same honest-skip rule: no
  Marauder attached means the bit stays unset and the sweep continues. The
  window is longer than the raw pass on purpose — probes are bursty, so a
  quiet room returning zero probes is normal, not a fault.
- **Fixed a real sequencer defect found while wiring it:** a full-sweep Wi
  pass now routes its UART lines through *its own* source instead of the
  Settings choice. Before this, the Wi-raw radar pass collected nothing
  unless the operator had already selected RAW as the Wi source — the pass
  ran, the data went nowhere. `wifi_effective_source()` now drives the UART
  routing, the table count, and the Wi header tag, so the header shows what
  is actually scanning.
- Documented that **the Wi-raw pass *is* the client/station view on this
  firmware** (`sniffraw` reports one line per 802.11 frame, stations
  included). A dedicated `scanap`+`scansta` pass stays unused until the
  station-line format is pinned live — the format gate still applies.
- **Info tab gains a Caps page** ("what this build can find": vendors +
  hints, hidden-SSID repair, clients via Wi-raw, rogue/watch flags). The page
  count is one constant now (`INFO_PAGE_COUNT`) instead of three hardcoded
  5s, and LIMITS is an explicit page rather than the fallback branch.
- **README "What it can now find" table** — one row per thing a room can
  contain, with the honest "what it still cannot say" column, plus the
  hardware-blind list (silent recorders, 5 GHz, LTE mid-band, Bluetooth
  Classic audio, the CC1101 gaps, wired devices). USER_GUIDE, the control map
  and the field guide follow the new sweep order and preset count.
- Tests: `tests/test_full_sweep_state.c` extended for the new phase
  (transition chain, bit, honest skip, limit, label, window ordering).
- Gates: `./init.sh` ALL PASS (25 suites), `_verify_api.py` CLEAN, ufbt zero
  warnings. Not device-verified: the rig went unresponsive this session
  (`.omo/evidence/device-unresponsive-2026-09-21.md`) — the capstone's device
  walk is owed, and `features.json` carries it as an open `passes: false`
  verification entry.

## Unreleased — capability expansion, Phase 11a — 20-preset RF survey (2026-09-21)

- **RF presets 16 → 20.** Four presets added where the survey was blindest:
  `850` and `880` (the cellular uplink edge — the honest line is
  "cellular uplink overlap; energy here may be a SIM tracker", with the
  standard caveat that this hardware cannot decode cellular and cannot
  identify anything), `902` (bottom of the US 902–928 ISM band) and `927`
  (top of it, LoRa US channel 64 / utility meters).
- The preset tables moved to a new Flipper-free header
  `room_sweep_rf_presets.h` so a host suite can pin what used to drift
  silently: one entry per preset in all three parallel arrays (frequencies,
  labels, EXT band filter — the band column is indexed directly, so a short
  table meant tuning an arbitrary frequency), every frequency inside a real
  CC1101 band, the band column matching the frequency, no duplicates, and no
  label wider than the 4-character strip budget. New suite
  `tests/test_rf_presets.c` (registered in `init.sh`).
- Survey strip layout is now derived from the preset count instead of
  hand-tuned: `room_sweep_ui_layout.h` gains `rf_bar_stride`/`rf_bar_width`/
  `rf_bar_x`/`rf_label_step`, pinned by `tests/test_ui_layout.c` at both 16
  and 20 presets (16 keeps the historical 8 px stride, 20 gets 6 px; five
  labels at 0/24/48/72/96 px, all inside the text margin). The waterfall's
  vertical channel spacing is derived the same way, and its channel count
  follows the presets.
- Fixed a latent buffer overflow the expansion would have hit: the analyzer
  radar drew one blip per RF preset into a fixed `blips[16]`.
- Cost, stated plainly: a survey pass takes ~800 ms instead of ~640 ms
  (20 presets × 8 samples × 5 ms), the baseline CSV gains four rows, and the
  waterfall is 20 rows in the same 40 px band (denser, still readable).
- Not yet device-verified: the device was unresponsive this session (see
  `.omo/evidence/device-unresponsive-2026-09-21.md`); strip geometry is
  pinned by host tests instead of a screenshot.

## Unreleased — fix: out-of-memory reboot on launch (2026-09-21)

- **Crash fix (device-found).** Launching the app could reboot the whole
  Flipper with the framework's `out of memory` screen instead of opening.
  Cause: app state is a **single contiguous 20644-byte allocation** taken at
  entry, and the largest free block at that moment was only ~7 KB larger —
  so a slightly more fragmented heap made the allocation fail and the
  framework's OOM handler rebooted the device.
- The record queue dominated that size (**8 KiB = 40% of app state**), so it
  now holds 8 events instead of 32: app state is 14500 bytes, the entry
  margin roughly doubles, and runtime contiguous headroom after startup goes
  from ~4 KB to ~10 KB. A shorter queue still buffers ~3 s at the throttled
  producer rate, and overflow stays counted in `record_queue_drops` and
  reported as session drop evidence — no silent loss, no format change.
- The allocation is now NULL-checked: if it ever cannot be satisfied the app
  logs, sounds an error, and exits, rather than letting the framework reboot
  the Flipper out from under the operator.
- Verified in the on-device regression run: 10/10 launch→exit cycles clean with a
  stable heap across cycles; entry free/max-block 38784/28832 against a
  14500-byte need. Receipt: `.omo/evidence/oom-regression-2026-09-21.md`.

## Unreleased — capability expansion, Phase 9 — opt-in cross-session watchlist (2026-09-20)

- New **Settings → Watchlist: OFF/ON** — the app's **only
  persistent-identity feature**, and it is strictly opt-in. The toggle is
  never persisted, so OFF is the factory state on every launch: while OFF
  the feature does nothing — zero file reads, zero writes, zero RAM.
- When ON, `watchlist.txt` (`/ext/apps_data/room_sweep/`) is read once per
  enable; flagging appends one `aa:bb:cc:dd:ee:ff,label` line (created on
  first flag, 16-entry RAM ceiling with a `WATCH FULL` notice). The file
  tolerates `\r`, surrounding spaces, blank lines and `#` comments so it
  stays hand-editable; garbage and overlong lines are refused whole, never
  truncated into validity. Raw MACs live ONLY here — session CSVs keep
  per-session ordinals exactly as before.
- Flagging is manual: **Hold UP on the Wi-Fi or BLE detail page** labels
  the selected row's address with its SSID/name (23 chars) or `flagged`;
  the existing hold-confirm pulse acknowledges. Matching happens on the
  Wi/BLE table upserts: future sightings show a right-edge `WATCH` badge
  on list + detail rows (coexisting with `?` hint tags and the Phase 6 `!`
  mark via an extended, tested ui_layout chain), `ON WATCHLIST` /
  `flagged: session N` on the detail page, and an all-or-nothing
  `watch=1` token in the 48-byte CSV observation detail.
- The Room Report gains `Watchlist matches: N` (distinct flagged addresses
  heard; only printed for sessions recorded while ON) plus the honesty
  line naming it the app's only persistent-identity feature and the
  caveat that a matching address is not proof of the same physical device.
- New Flipper-free header `room_sweep_watchlist.h` (parse/match/list
  logic, integer math only) with host suite `tests/test_watchlist.c`
  registered in `init.sh`; `room_sweep_report.h`, `room_sweep_stats.h`,
  `room_sweep_settings.h`, `room_sweep_ui_layout.h`, and the session
  writer extended with tests.

## Unreleased — capability expansion, Phase 8 — RF burst watch (2026-09-20)

- New RF sub-mode **Watch** (Up/Down cycles it after Waterfall):
  lock-and-log on ONE frequency — the last qualified peak's tuned
  frequency if fresh, else the nearest ExtBand-legal preset — fed by the
  existing RF thread at the survey cadence (5 ms). No hopping, no new
  thread.
- Bursts: energy above the shared −75 dBm gate opens on a rising edge and
  closes after 600 ms of silence. The page shows `BURSTS n`, `LAST s`
  (seconds since the watched frequency was last above the gate) and
  `DUTY n%` (integer share of the watch window with energy) — honest
  units, no distance/direction claims. A closed burst gets the existing
  soft press-pulse (respects Sound/Vibro; no new notification sequence).
- Session evidence: one `observation` CSV row per closed burst
  (source RF, identifier `WATCH`, submode `watch`) with the burst's
  ACTIVE duration in `count` (first-to-last above-threshold sample; the
  600 ms gap is detection latency, not TX time), peak RSSI in `rssi` and
  `duty=N% bursts=M` in the detail.
- New Flipper-free header `room_sweep_watch.h` (edge/gap/duty state
  machine + label/detail formatters, integer math only) with host suite
  `tests/test_watch.c` (edge math, gap boundary, max tracking, duty
  overflow guard, long-run and tick-wrap safety). `room_sweep_ui_layout.h`
  gains the watch footer hint.
- Gates: `./init.sh` ALL PASS, `_verify_api.py` CLEAN, `ufbt` zero
  warnings (Target 7, API 87.1). No device deploy in this phase.

## Unreleased — capability expansion, Phases 4/5/10 — Wi capture sources (2026-09-20)

- Three new Wi capture sources behind **Settings → Wi Src** (default BEACON
  every launch; switching stopscans first — only one Marauder scan runs at
  a time):
  - **RAW** (`sniffraw`, Phase 4): every 802.11 transmitter heard on the
    current channel, stations included — live-verified format. Rows carry
    vendor evidence, per-row RSSI min/max/avg, and CSV ordinals `STA-NN`
    (submode `raw`). The AP table is kept, so cloned-SSID marks survive.
  - **PROBE** (`sniffprobe`, Phase 5): devices announcing networks they
    remember; can name hidden networks. Includes the hidden-SSID repair
    contract (`room_sweep_probe_names_hidden`, one-shot `hidden_resolved`
    CSV event, history-preserving rename). Honest caveat: the pinned
    upstream probe lines carry no target BSSID, so repair cannot fire
    until a build prints one — the parser honors `BSSID:` if a future
    firmware does. Format pinned from upstream source, not yet observed
    live.
  - **TOOL** (`sniffesp` + `sniffpwn`, Phase 10, alternating windows):
    devices advertising like attack tooling — not proof of intent. Pwn
    names pinned from upstream source (v1.9.1 + master shapes); `sniffesp`
    provably streams nothing on current upstream builds (mode unrouted
    since "Trim fat"), so its half of the window honestly stays empty.
- Full sweep gains a 6 s Wi-raw radar pass after BLE (honest skip without
  a Marauder).
- CSV: new identifier kinds `STA-NN` / `PR-NN` / `TL-NN`; submodes
  `raw` / `probe` / `tool` on WIFI observation rows.
- Host suites: `tests/test_sta.c`, `tests/test_probe.c`, `tests/test_tool.c`
  (new); `test_settings_state.c`, `test_ui_layout.c`,
  `test_full_sweep_state.c`, `test_record_state.c` consumers extended via
  headers. Gates: `./init.sh` ALL PASS, `_verify_api.py` CLEAN, `ufbt`
  zero warnings.
- Docs: `docs/BFFB_MOMENTUM.md` gains upstream format pins with mandatory
  "not yet observed live" annotations.

## Unreleased — capability expansion, Phases 6–7 + BLE fix (2026-09-20)

- BLE randomized detection fixed (found during on-device verification): BLE
  random-STATIC addresses (top bits `11`) now read `randomized` alongside the
  locally-administered bit, while a curated OUI hit still wins — `f4:f5:e8`
  keeps naming Google instead of being mislabeled. Wi-Fi detection is
  unchanged. Resolvable/non-resolvable private BLE subtypes remain
  undetectable from the address alone (documented).
- Duplicate-SSID / possible-rogue correlation (Phase 6): two or more APs
  sharing one exact SSID get a `!` in the Wi list, a `SAME NAME ON n BSSIDS`
  badge on the detail page, a `rogue=<n>` CSV token, and a
  `Possible cloned SSIDs` report line — always labeled a lead, never a
  verdict (mesh/roaming legitimately shares names).
- Per-device RSSI evidence stats (Phase 7): Wi-Fi/BLE rows keep min/max/avg
  of every heard signal. Detail pages show `-38dBm -72..-38 n14` (the
  channel left the stats line — it stays in the CSV), CSV observation
  details carry `min=/max=/avg=` tokens, and the report's strongest-device
  line gains `(range a..b)`. Sum uses integer offsets so it cannot overflow;
  a cut CSV value is never fabricated (tokens drop whole).
- Host suites: `tests/test_rogue.c`, `tests/test_stats.c` (new);
  `test_oui.c`, `test_ui_layout.c`, `test_report_state.c` extended.
  Gates: `./init.sh` ALL PASS (23 suites + ufbt), `_verify_api.py` CLEAN.

## Unreleased — capability expansion, Phase 1 (2026-09-20)

- Identification layer, backend-side only (no new UART commands, no setup):
  Wi-Fi/BLE rows now carry a curated OUI vendor label, `randomized` for
  locally administered MACs, and `?`-suffixed name-pattern hints.
- `room_sweep_oui.h`: 62-prefix vendor table, every entry verified against
  the IEEE registry on 2026-09-20; unlisted MACs print `unlisted` — never a
  guess. Host suite `tests/test_oui.c`.
- `room_sweep_classify.h`: camera/printer/hotspot/IoT/drone/devboard hints
  (tracker names on BLE) with boundary guards (`cameron` never hints
  camera); `Hidden/unknown` is neutral. Host suite `tests/test_classify.c`.
- Wi/BT list rows show the hint tag; detail pages add `Vendor:` and
  `HINT: x? (name guess)` lines; CSV observation details carry
  `oui=…`/`hints=…` tokens; the Room Report gains `Vendors seen (curated
  OUI, not exhaustive)` and `Hints (name-pattern guesses only)` lines.

## v3.2 — 2026-09-11 (truth audit)

- RF tab: the SIGNAL badge now states its real meaning — energy above the
  -75 dBm gate; Peak reports ~100 kHz resolution with the 650 kHz
  measurement bandwidth noted.
- Analyzer: CLOSER/FARTHER renamed STRONGER/WEAKER (energy rose or fell, not
  distance); the radar page is now ENERGY MAP with an explicit NO DIRECTION
  (rings = RSSI, angles = channel wheel).
- ExtBand AUTO is labeled as the assumed switch path — the physical switch
  is not sensed.
- nR tab retitled 2.4 GHz energy detection; all synthetic dBm displays now
  read ACT (arbitrary activity units).
- Session CSV marks analyzer rows `rssi=0` and records real hit totals; the
  Room Report prints the real totals.
- SSID truncation fix: the trailing two-character strip was removed
  (verified against the upstream parser).
- GPS: FIX now requires a parsed position (NO POS status otherwise), with
  trail and mark gated on it; the FullSweep GPS gate also uses parsed
  position, not mere sentence presence.
- Docs and specs synced to the new wording; stale on-device screenshots
  flagged for re-capture.

## v3.1.0 — 2026-09

**Feedback overhaul (2026-09-05)**

- Per-tab proximity parity: the WiFi/BLE Geiger is now identity-aware and ages
  out (locked target > selected row > strongest, fading to silence over ~2–6 s)
  — walking out of range no longer freezes the click rate.
- Real nRF24 activity: the nR Geiger is driven by an actual RPD activity
  integrator (~4 s idle decay) instead of a synthetic channel counter.
- Graded vibro ladder (150/300/600/1200/2500/5000 ms); GPS fix heartbeat and TX
  cadences preserved; sound ladders centralized in one host-tested helper.
- `feedback_tick` runs at the top of every main-loop pass with a ~40 ms
  throttle — held buttons no longer starve sound/vibro/LED.

**dB-linear curves, hunting keep-alive, OK=lock (2026-09-05)**

- Step ladders replaced with continuous dB-linear curves: sound 2000→60 ms
  (~24 ms/dB), vibro 5000→150 ms (~61 ms/dB), plus a new GPS curve driven by
  satellite count. Monotonic by construction, pure integer math.
- Hunting keep-alive: with the analyzer open or a WiFi/BLE target locked, scan
  windows restart within ~250 ms so live meters and feedback never starve;
  windows still stop and log `scan_end` normally.
- Wi/BT controls: short OK locks/unlocks the selected row (empty list starts
  the first scan); Hold OK forces a manual rescan. RF/nR/GPS/TX unchanged.

**Scan & GPS reliability (2026-08-01, v3.1 / v3.1.1)**

- Fixed the BLE/WiFi instant-ERR bug: timeout is measured from scan start and
  only fires when zero results arrived for the full window; BLE first-sighting
  silence is normal, not an error.
- GPS now requests the Marauder `nmea` stream on tab enter (passive listening
  was wrong for BFFB); the GP tab shows speed, course, satellite bar, and mark
  distance. `stopscan` is always sent on GPS tab leave and app exit.

**On-device UI QA (2026-09-04)**

- Headless walk of 65 UI states over USB: fixed text collisions, clipped
  hints, and footer/radar geometry across Wi/BT help, GPS summary, analyzer,
  meter, and settings pages.

**Platform pin**

- Firmware: Momentum mntm-012, API 87.1, target 7 (ufbt). Release gates:
  17 host test suites under `-Werror`, ufbt build, and `_verify_api.py`
  export-table check all green.

## v3.0.1 — 2026-08-01

Post-field-test hardening:

- Settings crash fixed (input event double-fire); Back routing cleaned up —
  long Back exits the app, short Back closes Settings or disarms TX.
- Audio made continuous: heartbeat model (2 s idle accelerating to 60 ms at
  strong signal) instead of threshold clicks only.
- Marauder parser rewritten: bare-RSSI lines at line start handled, `#` echo
  lines skipped; fake "done" markers removed — scans stream until `stopscan`.
- WiFi/BLE scans show an error and recover when no result lines arrive for
  30 s; GPS freshness follows navigation sentences only; invalid NMEA checksum
  characters rejected.
- User guide (`USER_GUIDE.md`) written; settings and input handler QA audit.

## v3.0 series — 2026-08

**v3.0 (2026-08-01) — full restructure**

- Tabbed single-viewport app (6 tabs at launch, 7 once nR landed): RF / Wi /
  BT / nR / GP / TX / i.
- Dedicated safety-gated TX tab: DISARMED → ARMED → bounded carrier with
  countdown → auto-disarm; Back disarms at any point; RF-detected frequencies
  preload into TX.
- RF tab sweep modes: 16-point preset Survey, Band Sweep with peak hold, and
  fine-step Peak Refine.
- Wi-Fi (`sniffbeacon`) and BLE (`sniffbt`) scans via the BFFB/Marauder UART;
  RF uses the BFFB external CC1101 via the Momentum `cc1101_ext` device.
- GPS tab, upgraded to GPIO LPUART 15/16 primary with Marauder `nmea`
  fallback; streaming LED/Geiger/vibro feedback from live RSSI.

**Later v3.0-series additions (unversioned, 2026-08-09 – 2026-08-15)**

- nRF24 tab: receive-only RPD 2.4 GHz channel survey (2026-08-09); SPI Path
  policy forces the internal CC1101 whenever nRF24 is selected.
- FullSweep sequencer (RF → Wi-Fi → BLE → nRF24 → GPS) with hard timeouts, plus
  the plain-English Room Report and session evidence files (`session-N.csv`,
  `report-N.txt`, optional `uart-N.txt`) with privacy ordinals (2026-08-09).
- UI/UX spread: on-page collision fixes, shared layout constants, tap/hold
  tactile feedback, RF hold-to-lock card, GPS Hold-OK retry (2026-08-13).
- Meter suite: four-page analyzer (Hunt / Field / Radar / Meter), RF Waterfall
  sub-mode (~2.5 Hz scroll with peak hold), integer-trig polar RSSI radar, and
  a mark-centered GPS walk-to radar with real-meter rings and fix trail
  (2026-08-13).
- Marauder line parsing extracted into a host-tested header (2026-08-15).

## Earlier

- v1.0 baseline (crash-free, deployed) and v2.0 streaming feedback work,
  2026-08-01 — see `git log` for the full trail.
