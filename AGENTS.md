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
- `DESIGN.md`, `USER_GUIDE.md`, `progress.log`, `specs/`.

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

## Architecture

Single ViewPort app, one draw function, **7 tabs** (`SweepMode` in
`room_sweep.h`): RF / Wi / BT / nR / GP / TX / i.

- `room_sweep.c` (~5790 lines) is an intentional monolith: input
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
  `room_sweep_ui_layout.h`.
- Other modules: `nmea.c/h` (host-tested NMEA parser),
  `session_log.c/h` (session writer, main-loop-only), `nrf24_survey.c/h`.
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

### BFFB / Marauder UART & GPS

- USART pins 13/14 @ 115200 **after `expansion_disable()`** (restore on exit).
  Line ending is `\n` only (not CRLF).
- Fixed scan-only commands: `sniffbeacon`, `sniffbt`, `stopscan`, `nmea`,
  `gps -g nmea`, `help` (no `scanap` on modern Marauder).
- GPS primary: Flipper GPIO **LPUART 15/16 @ 9600**; Marauder `nmea` stream is
  the fallback. Verify against `docs/BFFB_MOMENTUM.md`.

### Session evidence

Settings → Record opens session N; artifacts in
`/ext/apps_data/room_sweep/`: `session-N.csv` (events), `report-N.txt` (Room
Report), `uart-N.txt` (only if Raw Dump). Privacy: WiFi/BLE identifiers are
per-session ordinals; GPS coordinates omitted unless GPS Log ON. Every TX
attempt recorded as an event.

## Hard rules (from FLIPPER_PITFALLS.md)

- Momentum `NotificationSequence` is a NULL-terminated array typedef (not a
  struct); custom sequences MUST be `static const`; prepend
  `message_force_speaker_volume_setting_1f` /
  `message_force_vibro_setting_on` or user mute settings win. Delay values only
  1/10/25/50/100/250/500/1000 ms.
- `-Werror` enables `-Wdouble-promotion` — use integer math or explicit casts.
- Every API call must be in the export table — `python3 _verify_api.py`; an
  unlisted symbol hard-faults at `loader open`, not link.
- Display is 128×64; FontKeyboard is the smallest font (~25 chars/line).

## Repo conventions

- Workflow: `PROMPT.md`, `fix_plan.md`, `progress.log`, `specs/`. One item per
  iteration, commit after each verified item, no mock data. QA receipts go in
  `.omo/evidence/`.
- Branches: `main` trunk; `feat/*` feature work; `restore/*` are pre-overhaul
  snapshots for recovery (`git log --oneline` → checkout sha).
- Commits: **no Anthropic/Claude co-author trailers, ever.**