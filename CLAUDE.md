# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Room Sweep is a Flipper Zero external app (`.fap`, appid `room_sweep`) for
receive-side RF surveying on the operator's own property: sub-GHz survey/sweep
via CC1101, WiFi/BLE scans driven over UART on a Just Call Me Koko **BFFB**
ESP32 running ESP32Marauder, passive GPS display, session recording, and a
safety-gated TX tab. Target firmware: **Momentum mntm-012, API 87.1, target 7**.

**Mission scope is a hard constraint, not prose:** receive-side surveying plus a
bounded carrier-frequency TX test only. No jamming, blocking, deauthentication,
capture/replay, or beacon/probe flooding — this is enforced by design
(`MISSION.md`, `README.md`, `USER_GUIDE.md`) and by the TX policy checks. Do
not add attack modes.

## Build, test, deploy

```sh
./init.sh          # THE gate: builds+runs all host tests with -Werror, then ufbt
ufbt               # build dist/room_sweep.fap only
ufbt launch        # upload+run on a connected Flipper (app must not already be running)
python3 _verify_api.py   # every called symbol must be in the Momentum API export table
```

Host tests are plain C, compiled per-suite with `-std=c11 -Wall -Wextra -Werror`.
Run one suite by copying its line from `init.sh`, e.g.:

```sh
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/t_nmea && /tmp/t_nmea
```

Suites: `test_nmea` (58 assertions), `test_input_state` (Back routing),
`test_rf_tx_state`, `test_wireless_state`, `test_gps_state`,
`test_record_state`, `test_report_state`, `test_scan_logic`. Tests use a
`CHECK(cond, msg)` counter macro and return non-zero on failure — no framework.

Device helpers (need a Flipper over USB, pyserial): `_smoke_test.py`
(build/upload/traverse; `FLIPPER_PORT`/`FLIPPER_BAUD` env overrides, default
`/dev/cu.usbmodemflip_Rug1k01`), `_dev_check.py` (device_info/power_info).
If `ufbt launch` says the app must be closed, send `input send back long` via
the serial CLI first.

**`./init.sh` must pass before any device deploy.** Build output is
`dist/room_sweep.fap`; `*.fap`/`dist/` are gitignored.

## Architecture

Single ViewPort app, one draw function, six tabs (`SweepMode` in
`room_sweep.h`): RF / WiFi / BLE / GPS / TX / Info. `room_sweep.c` (~4k lines)
is the intentional monolith: input routing, drawing, RF engine, TX thread,
UART parsing, feedback. Everything that can be logic-tested off-device lives in
**header-only, Flipper-header-free state files** so host suites compile with
plain `cc`:

- `room_sweep_state.h` — RF→TX signal-candidate handoff (freshness, thresholds)
  and the TX decision/refusal state machine (`room_sweep_tx_ok_decision`,
  `RoomSweepTxRefusal*`).
- `room_sweep_scan.h` — Marauder scan timeout decision (timeout from scan
  start; ERR only when zero results; BLE dedup silence is not an error).
- `room_sweep_input.h` — Back-button routing decision table.
- `room_sweep_gps_state.h`, `room_sweep_wireless.h`, `room_sweep_record_state.h`,
  `room_sweep_report.h` — GPS presentation, WiFi/BLE list state, recorder
  queue, report text assembly.

When changing behavior, put the decision logic in one of these headers and add
a host test; keep `room_sweep.c` as the wiring/rendering layer.

Other modules: `nmea.c/h` (host-tested NMEA parser: GGA/RMC/GLL/ZDA/GSV),
`session_log.c/h` (session writer; main-loop-only), `application.fam`
(ufbt manifest, stack 6 KiB, `sources=["*.c","!tests"]`).

### Threads and locking

- Main loop: input queue, draw, settings, feedback, session writer.
- `RoomSweepRF` thread (2048-byte stack): CC1101 survey/band-sweep/peak.
- `RoomSweepTX` thread (2048-byte stack): spawned per transmit, joined on cleanup.
- UART RX: async serial callback (ISR context) filling a ring buffer; parsed on
  the main loop. `marauder_confirmed` requires a real Marauder response —
  UART-handle acquisition alone does NOT prove a BFFB is attached.
- Locks: `mutex` (app state), `radio_mutex` (CC1101 — RX and TX share the
  radio, never run both; idle to yield), `record_mutex`, `gps_mutex`.

### RF and TX

- Radio via `subghz_devices`: prefers BFFB external dual CC1101
  (`SUBGHZ_DEVICE_CC1101_EXT_NAME`, Momentum `cc1101_ext`), falls back to
  internal. Preset `FuriHalSubGhzPresetOok650Async`. CC1101 bands are
  300–348 / 387–464 / 779–928 MHz — gaps are real hardware limits.
- External radio has a physical 400/900 path switch → `ExtBandPref`
  (Auto/400/900) filters survey channels and TX presets (`rf_channel_allowed`).
- TX contract: `TxDisarmed → TxArmed` (short OK) `→ TxStarting → TxTransmitting`
  (long OK). Duration 1–10 s (`TX_MAX_DURATION_S`), auto-disarm after.
  Preflight enforces `subghz_devices_is_frequency_valid`,
  `furi_hal_region_is_frequency_allowed` when a region is provisioned, and the
  ExtBand policy. The carrier is a keyed ~1 s on / 2 µs OOK burst
  (`tx_carrier_cb`) — a bounded frequency test, never replay or blocking.
  Detected RF candidates pre-fill the TX frequency via `room_sweep_state.h`.

### BFFB / Marauder UART and GPS

- USART pins 13/14 @ 115200, **after `expansion_disable()`** (restore with
  `expansion_enable()` on exit). Line ending is `\n` only (Marauder
  `readStringUntil('\n')` + trim — not CRLF).
- Commands sent are fixed and scan-only: `sniffbeacon` (WiFi AP), `sniffbt`
  (BLE), `stopscan`, `nmea`, `gps -g nmea`, `help`. Modern Marauder has no
  `scanap`. WiFi lines: `-RSSI Ch: n MAC ESSID: …`; BLE: `-RSSI Device: name|mac`.
- GPS primary path is Flipper GPIO **LPUART 15/16 @ 9600** (Momentum setting:
  NMEA GPS UART = Extra 15,16); Marauder `nmea` stream over USART is the
  fallback when GPIO is silent. BFFB's GPS module is wired to the ESP32 only.
- `docs/BFFB_MOMENTUM.md` is the source of truth for BFFB/Marauder/Momentum
  facts; check it before changing any UART command or baud assumption.

### Session evidence

Settings → Record opens session N; artifacts go to
`/ext/apps_data/room_sweep/`: `session-N.csv` (events via `record_enqueue` →
`session_log_write_event`), `report-N.txt`, `uart-N.txt` (explicit bounded Raw
Dump only). Privacy model: WiFi/BLE identifiers are per-session ordinals and
GPS coordinates are omitted unless `GPS in Log` is ON; the raw dump may contain
both. Every TX attempt (start/refuse/end) is recorded as an event.

## Hard rules learned the hard way

Read `FLIPPER_PITFALLS.md` before touching notifications, input, UART, or
radio code — it is a maintained log of verified API symbols and breakages:

- Momentum `NotificationSequence` is a NULL-terminated array typedef (not a
  struct); custom sequences MUST be `static const` (async playback → stack
  sequences are use-after-free); prepend `message_force_speaker_volume_setting_1f`
  / `message_force_vibro_setting_on` or user mute settings win. Delays exist
  only for 1/10/25/50/100/250/500/1000 ms.
- `-Werror` includes `-Wdouble-promotion` — use integer math or explicit casts.
- Thread stack-local buffers stay < 64 bytes (2 KiB thread stacks).
- Every API call must exist in the export table — run `python3 _verify_api.py`
  (or grep `~/.ufbt/current/.../api_symbols.csv`) before using a new symbol;
  unlisted symbols hard-fault at `loader open`, not at link.
- Display is 128×64; FontKeyboard is the smallest font (~25 chars/line).

## Repo conventions

- Fix-loop workflow lives in `PROMPT.md` / `fix_plan.md` / `progress.log`;
  per-fix specs are in `specs/`. One fix-plan item per iteration, commit after
  each verified item; no mock data; QA receipts accumulate in `.omo/evidence/`.
- Branches: `main` trunk; `feat/*` feature work; `restore/*` are pre-overhaul
  snapshots kept for recovery (`git log --oneline` → checkout sha).
- Commits: no Claude/Anthropic co-author trailers, ever.
