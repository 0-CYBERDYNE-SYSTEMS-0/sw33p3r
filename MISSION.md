# MISSION CONTRACT — Room Sweep v3.0

**Owner:** 0-CYBERDYNE-SYSTEMS-0 · **Date:** 2026-08-01
**Status:** DEPLOYED FOR FIELD TEST

## What this app does

Room Sweep is a Flipper Zero application for local RF environment assessment
on the user's own property and hardware:

- **RF tab** (3 sub-modes): Sub-GHz signal discovery via CC1101
  - Survey: 16-point preset sweep (fast room check)
  - Band Sweep: coarse sweep across selectable CC1101 band with peak hold
  - Peak Refine: fine-step refinement around a detected signal
- **WiFi tab**: Marauder AP scan via BFFB ESP32 UART (RSSI meter + AP list)
- **BLE tab**: Marauder BLE sniff via BFFB ESP32 UART (RSSI meter + device list)
- **GPS tab**: Passive NMEA listener (fix health, satellites, position)
- **TX tab**: Dedicated transmit mode, safety-gated (disarm → arm → transmit)
- **Info tab**: Live capability card (version, connection state, settings)

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
- When enabled: Geiger-style clicks proportional to signal strength
- Sustained lock tone when signal holds above threshold
- Vibro pulses on detection edges and sustained lock
- Enabling sound/vibro produces an immediate test pulse (observable)
- LED escalation: green → yellow → red → red blink (by peak RSSI)

## Control scheme

| Button | RF tab | WiFi/BLE | TX tab | Other |
|--------|--------|----------|--------|-------|
| ◀ / ▶ | Cycle tabs | Cycle tabs | Cycle tabs | Cycle tabs |
| ▲ / ▼ | Sub-mode cycle | Sound/Vibro | Freq select | Sound/Vibro |
| OK | Start sweep/scan | Start scan | Arm TX | — |
| Long OK | — | — | TRANSMIT | — |
| Back (short) | Settings | Settings | Disarm/Settings | Settings |
| Back (long) | Exit | Exit | Exit | Exit |

## Definition of done

- [x] RF survey + band sweep + peak refinement implemented
- [x] Dedicated TX tab with multi-step safety guardrails
- [x] Sound/vibro feedback functional with force-volume messages
- [x] WiFi/BLE RSSI parser and signal meters
- [x] GPS stale-fix and GLL bounds bugs fixed (44/44 host tests pass)
- [x] Info tab shows live state (no stale "passive RX only" text)
- [x] Builds clean: API 87.1, target 7, zero warnings (-Werror)
- [x] Deployed to device via USB
- [ ] Field test: audio audible near WiFi router / BLE devices
- [ ] Field test: TX guardrails verified on device
- [ ] Field test: band sweep detects known in-between signal

## Recovery

Restore: `git log --oneline` → `git checkout <sha>`.
