# Spec: GPS full capability (BFFB / Marauder)

## Problem
GPS tab stuck on `Waiting for GPS...`. App never requests NMEA from the BFFB; it only passively listens.

## Hardware / firmware facts (BFFB wiki + Marauder source)
- [BFFB wiki](https://github.com/justcallmekoko/ESP32Marauder/wiki/BFFB): GPS connected to **ESP32 only** — Flipper GPIO GPS apps will not work.
- BFFB firmware target: Marauder **Dev Board Pro**.
- Flipper ↔ ESP32 UART is **115200** Marauder CLI (companion `BAUDRATE 115200`), not raw GPS baud.
- GPS module is ESP32 Serial2 (Marauder probes 9600→115200 internally).
- Continuous NMEA: CLI **`nmea`** → `WIFI_SCAN_GPS_NMEA` / `RunGPSNmea()` (companion “NMEA Stream”).
- One-shot: `gps -g nmea` emits synthetic GGA+RMC.
- Status: `gps -g fix|sat|lat|lon|...` (human text; optional).

## Contract
1. On GPS tab enter: send `nmea` to start streaming; clear/reinit fix state.
2. On GPS tab leave: `stopscan` (existing tab-change path).
3. If no nav sentences for 5s while streaming: re-send `nmea` or poll `gps -g nmea` (bounded retry).
4. UI must use already-parsed fields: time, date, lat/lon, sats used/view, **speed**, **course**.
5. **Mark + distance**: Short OK sets/clears a mark at current fix; UI shows distance (m/km) via haversine.
6. Status labels: Waiting / Streaming / FIX / NO FIX / STALE — never claim fix without fresh nav data.
7. GPS drives feedback when on GPS tab (sat quality → LED; fix acquire edge → optional sound/vibro if enabled).

## Out of scope
- Writing GPX files to SD (future).
- Changing Flipper UART baud for raw GPS (wrong architecture for BFFB).

## Verification
- Host tests: haversine known distances; mark state pure logic if extracted.
- Device: enter GPS tab → leaves "Waiting" when BFFB has GPS; shows speed/course when fix present.
