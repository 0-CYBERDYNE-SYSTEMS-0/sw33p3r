# AGENTS.md — Room Sweep (Flipper Zero external FAP)

Receive-side RF / wireless **room survey** app for Flipper Zero, written in C.
External `.fap` (appid `room_sweep`) targeting **Momentum mntm-012, API 87.1,
target 7**. Mission = **detect/analyze only**; no jamming, blocking, deauth,
capture/replay, or flood modes. Do not add attack modes — this is a hard
requirement enforced by `MISSION.md`.

## Read these first

- `MISSION.md` — scope / legal / TX safety contract (never violate).
- `PROMPT.md` — fix-loop workflow (stack order, one-feature-per-iteration).
- `FLIPPER_PITFALLS.md` — **before touching** notifications, input, UART, or
  radio code. Maintained log of verified API symbols and breakages.
- `docs/BFFB_MOMENTUM.md` — source of truth for BFFB/Marauder/Momentum facts;
  check before changing any UART command or baud.
- `CONTRIBUTING.md` — mission constraint (non-negotiable) + the gate contract.
- `DESIGN.md`, `USER_GUIDE.md`, `progress.log`, `specs/` (one spec per
  feature), `features.json` (QA feature checklist).
- `docs/` — GitHub Pages landing: control map (`index.html` /
  `room_sweep_control_map.html` + `.jpg` screenshots) and hardware guides
  (`build-your-own-standalone.html`, `hardware-alternatives.html`).

## Build / test / deploy

```sh
./init.sh            # THE gate: all host tests (-Werror) + ufbt build
ufbt                 # build only -> dist/room_sweep.fap
ufbt launch          # upload + run (app must NOT already be running)
python3 _verify_api.py   # every called symbol must exist in the Momentum export table
```

Host tests are plain C with a `CHECK(cond,msg)` macro, no framework. Run one
suite by copying its `cc` line from `init.sh`, e.g.:

```sh
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/t_nmea && /tmp/t_nmea
```

**`./init.sh` must pass before any device deploy.** `*.fap` and `dist/` are
gitignored.

- CI (`.github/workflows/ci.yml`) runs `./init.sh` on push/PR to `main`.
  `python3 _verify_api.py` is deliberately NOT in CI: it needs a locally
  initialized ufbt SDK (`~/.ufbt/.../api_symbols.csv`, override via
  `UFBT_API_SYMBOLS`) and stays a local pre-deploy step.
- `scripts/verify.sh` — generic success bar (git fsck + clean-tree warn; no C
  tests). `./init.sh` is still the gate.
- On-device helpers (need a connected Flipper; `FLIPPER_PORT` env):
  `_smoke_test.py` / `_dev_check.py` (serial CLI checks) and
  `tools/marauder_probe.py` (BFFB UART probe battery, receive-only).

## Architecture

Single ViewPort app, one draw function, **8 tabs** (`SweepMode` in
`room_sweep.h`): RF / Wi / BT / nR / GP / TX / i / AV (air vehicles — RID
receive, detect-only). The RF tab cycles **5 sub-modes** (`RfSubMode`):
Survey / Sweep / Peak / Waterfall / Watch.

- `room_sweep.c` (~8.2k lines) is an intentional monolith: input
  routing, drawing, RF engine, TX thread, UART parsing, feedback.
- **All logic that can be host-tested lives in header-only, Flipper-header-free
  state files** so suites compile with plain `cc`. When changing behavior, put
  the decision logic in the relevant header and add a host test; keep
  `room_sweep.c` as wiring/rendering only. State headers:
  `room_sweep_state.h` (RF→TX handoff + TX refusal state machine),
  `room_sweep_scan.h`, `room_sweep_marauder.h`, `room_sweep_input.h`,
  `room_sweep_gps_state.h`, `room_sweep_wireless.h`,
  `room_sweep_record_state.h`, `room_sweep_report.h`,
  `room_sweep_full_sweep.h`, `room_sweep_radio_path.h`,
  `room_sweep_nrf24_state.h`, `room_sweep_settings.h`,
  `room_sweep_analyzer.h`, `room_sweep_radar.h`, `room_sweep_waterfall.h`,
  `room_sweep_ui_layout.h`, `room_sweep_rf_presets.h` (20-preset survey table;
  host-tested invariants — all three parallel arrays in sync, every frequency
  inside a real CC1101 band), plus the identity/watch layer, each with its own
  `tests/test_*.c` suite: `room_sweep_probe.h` (sniffprobe parse + hidden-SSID
  repair), `room_sweep_sta.h` (sniffraw transmitter table),
  `room_sweep_tool.h` (hostile-tooling source), `room_sweep_oui.h` (OUI vendor
  lookup), `room_sweep_classify.h` (name-pattern class hints),
  `room_sweep_rogue.h` (duplicate-SSID rogue correlation),
  `room_sweep_stats.h` (per-device RSSI evidence stats),
  `room_sweep_watch.h` (RF burst watch), `room_sweep_watchlist.h`
  (cross-session watchlist), `room_sweep_feedback.h` (feedback cadence curves),
  `room_sweep_rid.h` (ASTM F3411 RID text parse), `room_sweep_rid_state.h`
  (8-drone track list, staleness, alert edges), `room_sweep_field.h`
  (FieldSweep loop/rate-limit/coordinate-eligibility policy).
- Other modules: `nmea.c/h` (host-tested NMEA parser),
  `session_log.c/h` (session writer, main-loop-only), `nrf24_survey.c/h`.
  The nRF24 driver test builds against stub headers in `tests/nrf24_stubs/`.
- `application.fam` — ufbt manifest: stack 6 KiB, `sources=["*.c","!tests"]`.

### Threads & locking

- Main loop: input, draw, settings, feedback, session writer.
- `RoomSweepRF` and `RoomSweepTX` threads: **2 KiB stacks** → thread
  stack-locals must stay **< 64 bytes**.
- UART RX is an async serial ISR callback filling a ring buffer, parsed on the
  main loop. `marauder_confirmed` requires a real Marauder response — owning the
  UART handle alone does NOT prove a BFFB is attached.
- Locks: `mutex` (app state), `radio_mutex` (CC1101 — RX and TX share the
  radio, never run both), `record_mutex`, `gps_mutex`.

### RF & TX

- Radio via `subghz_devices`: prefers BFFB external dual CC1101
  (`SUBGHZ_DEVICE_CC1101_EXT_NAME`), falls back to internal. CC1101 bands are
  300–348 / 387–464 / 779–928 MHz — gaps are real hardware limits.
- TX contract: `TxDisarmed → TxArmed → TxStarting → TxTransmitting` (1–10 s
  bounded carrier, auto-disarm). Preflight enforces frequency
  validity + regional allow + ExtBand policy. Carrier is a keyed OOK burst,
  never replay/blocking.
- Bottom SPI switch: **up = CC1101, down = nRF24**; setting **SPI Path** forces
  internal CC1101 when nRF24 is selected.
- nRF24 wiring (verified on the 3-in-1 AIO board): CE = `gpio_ext_pb2`
  (pin 6), CS = PA4 (pin 4) — PC3 is NOT CE on this board. nRF24 is
  register-verified before external CC1101 startup because Momentum's
  `cc1101_ext_is_connect()` also accepts the nRF24 STATUS 0x0e as a part
  number.

### BFFB / Marauder UART & GPS

- USART pins 13/14 @ 115200 **after `expansion_disable()`** (restore on exit).
  Line ending is `\n` only (not CRLF).
- Fixed scan-only commands: `sniffbeacon`, `sniffbt`, `stopscan`, `nmea`,
  `gps -g nmea`, `help`, plus the Phase 4/5/10 Wi capture sources
  (2026-09-20 expansion): `sniffraw` (transmitter radar, Wi Src RAW),
  `sniffprobe` (client probes, Wi Src PROBE — also sent by the FullSweep
  Wi-probe pass, phase 11), `sniffesp`/`sniffpwn`
  (hostile-tooling, Wi Src TOOL, alternating windows). `scanap`/`scansta`
  exist on the current BFFB build but stay unsent; `sniffprobe`'s format is
  validated against a redacted fixture (2026-09-21 — `RSSI: -NN Ch: N Client:
  <mac> Requesting: <ssid>`), while esp/pwn remain upstream-pinned only,
  annotated "not yet observed live" (see `docs/BFFB_MOMENTUM.md`).
- `remoteid` (AV tab RID receive) is parsed but its send path is gated OFF
  (`ROOM_SWEEP_RID_SEND_ENABLED` in `room_sweep.h`): the command is NOT
  compiled into the current BFFB build (2026-10-11 probe receipt — see
  `docs/BFFB_MOMENTUM.md`). Flip the gate only on a build whose `help` lists
  `remoteid`, backed by a fresh probe receipt pinning the output format.
- GPS primary: Flipper GPIO **LPUART 15/16 @ 9600**; Marauder `nmea` stream is
  the fallback. Verify against `docs/BFFB_MOMENTUM.md`.

### Session evidence

Settings → Record opens session N; artifacts in
`/ext/apps_data/room_sweep/`: `session-N.csv` (events), `report-N.txt` (Room
Report), `uart-N.txt` (only if Raw Dump). Privacy: WiFi/BLE identifiers are
per-session ordinals; GPS coordinates omitted unless GPS Log ON. Every TX
attempt recorded as an event. RID rows use per-session `DR-nn` ordinals;
FieldSweep rows carry `pos=` coordinate tokens only when GPS Log is ON.

## Hard rules (from FLIPPER_PITFALLS.md)

- Momentum `NotificationSequence` is a NULL-terminated array typedef (not a
  struct); custom sequences MUST be `static const`; prepend
  `message_force_speaker_volume_setting_1f` /
  `message_force_vibro_setting_on` or user mute settings win. Delay values only
  1/10/25/50/100/250/500/1000 ms.
- `-Werror` enables `-Wdouble-promotion` — use integer math or explicit casts.
- Every API call must be in the export table — `python3 _verify_api.py`; an
  unlisted symbol hard-faults at `loader open`, not link.
- Momentum does NOT init the external SPI handle at boot: call
  `furi_hal_spi_bus_handle_init()` before the first external SPI acquire
  (`furi_hal_spi_bus_handle_deinit()` on completion/failed start), or
  cold-start probing reports an attached nRF24 as absent.
- Display is 128×64; FontKeyboard is the smallest font (~25 chars/line).

## Repo conventions

- Workflow: `PROMPT.md`, `fix_plan.md`, `progress.log`, `specs/`. One item per
  iteration, commit after each verified item, no mock data. QA receipts go in
  `.omo/evidence/` — **local only**: `.omo/`, `.codex/`, and
  `.debug-journal.md` are gitignored and must never be committed.
- Branches: `main` trunk; `feat/*` feature work; `restore/*` are pre-overhaul
  snapshots for recovery (`git log --oneline` → checkout sha).
- Commits: short `fix:` / `feat:` / `docs:` / `chore:`-prefixed subjects;
  **no Anthropic/Claude co-author trailers, ever.**
- `CLAUDE.md` mirrors these instructions for Claude Code sessions; keep the
  two in sync when changing scope, workflow, or hard rules.
