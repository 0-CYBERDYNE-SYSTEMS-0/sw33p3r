# MISSION CONTRACT — Room Sweep

**Owner:** 0-CYBERDYNE-SYSTEMS-0 · **Updated:** 2026-08-19
**Status:** Current FAP is 7 tabs (RF / Wi / BT / nR / GP / TX / i). v3.1
field fixes below still apply. Later work added nRF24, FullSweep, waterfall,
and the analyzer meter suite.

## What this app does

Room Sweep is a Flipper Zero application for local RF environment assessment
on the user's own property and hardware:

- **RF tab** (4 sub-modes): Sub-GHz signal discovery via CC1101
  - Survey: 16-point preset sweep (fast room check)
  - Band Sweep: coarse sweep across selectable CC1101 band with peak hold
  - Peak Refine: fine-step refinement around a detected signal
  - Waterfall: passive 16-channel history (~2.5 Hz, 24 columns)
- **Wi tab**: Marauder AP scan via BFFB ESP32 UART (`sniffbeacon`)
- **BT tab**: Marauder BLE sniff via BFFB ESP32 UART (`sniffbt`)
- **nR tab**: nRF24 RPD channel survey (detect only, no jam)
- **GP tab**: NMEA listener (GPIO LPUART primary, Marauder `nmea` fallback)
- **TX tab**: Dedicated transmit mode, safety-gated (disarm → arm → transmit)
- **i tab**: Live capability card (Radio / State / Keys / Files / Limits)

## Scope / legal

Authorized assessment on the user's own property and hardware.
TX mode is for local testing and experimentation only.
WiFi/BLE/GPS via the user's own BFFB ESP32.

## TX safety contract

TX exists on its OWN dedicated tab with multi-step guardrails:
1. TX tab defaults to DISARMED state
2. OK arms the transmitter (audible warning, inverse-video ARMED display)
3. Up/Down select frequency while armed
4. LONG-OK transmits (bounded duration, countdown display)
5. Auto-disarms after transmission completes
6. Back disarms at any point
7. Detected signal frequency from RF tab pre-loads into TX tab

TX NEVER activates on tab entry. Multiple deliberate actions required.

## Feedback contract

- Sound defaults OFF, vibro defaults OFF
- **CONTINUOUS** when enabled: heartbeat click every 2s at idle, accelerating to 60ms at extreme signal
- Sustained lock tone when signal holds above threshold for 5+ ticks
- Vibro: heartbeat pulse every 4s, detection edge pulse, sustained lock pulse every 800ms
- Enabling sound/vibro produces an immediate test pulse (observable)
- LED escalation: green → yellow → red → red blink (by peak RSSI)

## v3.1 field fixes (BLE / GPS / feedback)

- [x] BLE/WiFi instant ERR: timeout used last_data_tick==0 as expired
- [x] BLE silence after first sightings no longer forces ERR
- [x] GPS requests Marauder `nmea` stream (not passive-only)
- [x] GPS UI: speed, course, sat bar, mark+distance
- [x] Per-tab LED/sound/vibro (centralized feedback_tick)

## v3.0.1 fixes (post field-test 1)

- [x] Settings crash fixed: removed InputTypePress from event filter (double-fire bug)
- [x] Audio made continuous: heartbeat model (2s idle → 60ms hot), not just threshold clicks
- [x] Marauder parser rewritten: handles bare RSSI at line start, skips '#' echo lines
- [x] Removed fake "done" markers — scans stream until stopscan
- [x] User guide written (USER_GUIDE.md)
- [x] QA audit: settings + input handler verified crash-safe
- [x] Long Back exits from Settings; short Back closes Settings or disarms TX
- [x] GPS freshness only follows navigation sentences, not telemetry-only traffic
- [x] Invalid NMEA checksum characters are rejected
- [x] WiFi/BLE scans show an error and recover when no result lines arrive for 30 seconds

## Control scheme

Matches `room_sweep.c` input + `docs/room_sweep_control_map.html`.

| Button | RF | Wi / BT | nR | TX | Settings | GPS / Info |
|--------|----|---------|----|----|----------|------------|
| Short ◀ / ▶ | Tab | Tab | Tab | Tab | Group | Tab |
| Hold ◀ | Analyzer | Analyzer | Analyzer | ExtBand if DISARMED+EXT | Group | — |
| Hold ▶ | Map/lock, or band on idle Sweep | Detail/List/Help | Status/Results | ExtBand if DISARMED+EXT | Group | Next page |
| ▲ / ▼ | Sub-mode (short). Hold opens lock card | Browse rows | — | Freq preset | Item | Page |
| OK | Start/cancel Sweep or Peak | Start scan | Start/stop RPD | Arm | Toggle | Mark, or retry if no/stale NMEA |
| Hold OK | Lock RF target | Lock row | Analyzer | TRANSMIT if armed | — | NMEA retry |
| Back short | Settings | Settings | Settings | Disarm, else Settings | Close | Settings |
| Back long | Exit | Exit | Exit | Exit | Exit | Exit |

ScanWin 15/30/60 s is **Settings → ScanWin**, not Hold ▶.

## Definition of done

- [x] RF survey + band sweep + peak refinement implemented
- [x] Dedicated TX tab with multi-step safety guardrails
- [x] Sound/vibro feedback functional with force-volume messages
- [x] WiFi/BLE RSSI parser and signal meters
- [x] GPS stale-fix, navigation freshness, and GLL bounds bugs fixed (58/58 executable NMEA assertions pass)
- [x] Info tab shows live state (no stale "passive RX only" text)
- [x] Builds clean: API 87.1, target 7, zero warnings (-Werror)
- [x] Deployed to device via USB
- [x] Device traversal verified across all tabs without a crash (nR added later; field re-check pending)
- [x] Settings short-close and long-exit paths verified with valid Press→Short/Long→Release input sequences
- [x] Back-routing host checks pass (4/4 assertions)
- [ ] Field test: audio audible near WiFi router / BLE devices
- [ ] Field test: TX guardrails verified on device
- [ ] Field test: band sweep detects known in-between signal

## Recovery

Restore: `git log --oneline` → `git checkout <sha>`.
