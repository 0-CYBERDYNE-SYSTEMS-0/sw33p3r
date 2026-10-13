# Spec: Drone / air-vehicle tab (AV) + Remote ID reception

## Goal
Detect and identify surveillance-relevant air vehicles around the user's
property: receive ASTM F3411-22a Remote ID (OpenDroneID) broadcasts via the
BFFB Marauder, show drone identity/position and operator position, and layer
2.4 GHz RC/drone-link energy on the same tab. Everything receive-side.

## Why it is in mission
Remote ID broadcasts can carry a UAS identifier, location/vector and operator
location. Room Sweep only receives and analyzes the fields the BFFB reports.
No RID transmission, spoofing, or injection is allowed.

## Hardware / firmware facts (verified 2026-10-12)
- The attached BFFB is an ESP32 Marauder Dev Board Pro running official
  ESP32 Marauder v1.18.0. `help` lists `remoteid scan/list/track/clear`.
- `remoteid scan` is passive. The observed sequence `remoteid scan` →
  `stopscan` → `remoteid list` started and stopped successfully; the list was
  empty and no RID CSV row arrived during the 12-second observation window.
  This verifies command behavior, not reception from an aircraft.
- v1.18.0 source pins the UART format to `RID,` plus 30 comma-separated
  values in `WiFiScanRemoteId.h::logRemoteIdRecord`. The strict parser is
  host-tested on synthetic redacted rows; it has not yet accepted a live row.
- RID uses the same Marauder UART as other scans (115200 baud, LF line ends,
  `expansion_disable()` path). Marauder runs one scan task at a time, so the AV
  scan stops/conflicts with Wi-Fi and BLE scans as before.
- This classic ESP32 board receives 2.4 GHz Wi-Fi and supported legacy BLE
  service-data broadcasts. It does not receive 5 GHz Wi-Fi or BLE 5 extended
  advertisements.
- nRF24 layer is RPD energy ONLY. Per `specs/nrf24-survey.md`, mousejack-class
  address recovery is banned — drone RC links (XN297/nRF24-class toy and
  camera drones) appear as 2.4 GHz energy hits in the existing nR survey and
  burst watch, nothing more. Do not spec promiscuous sniffing here.

## Contract
1. New tab **AV** (`SweepModeAv`, 8th tab; MISSION.md + control map updated at
   implementation time). Sub-modes: RID list / Track / 2.4 energy.
2. The app starts only the passive `remoteid scan` command. RID transmission
   and the explicit `remoteid clear` command stay disabled. Marauder v1.18.0
   clears and rebuilds its volatile RID table when an all-target scan starts;
   this upstream scan-start behavior is inherent to receive mode. The parser
   accepts the strict v1.18.0 source-pinned row shape; live acceptance remains
   an open device check.
3. Parser: Flipper-free `room_sweep_rid.h` accepts only Marauder v1.18.0's
   `RID,` row with exactly 30 values. It maps the summarized CSV fields with
   bounded integer/fixed-point parsing; it does not parse raw ASTM message
   types. Host tests use synthetic fixtures in `tests/test_rid.c`.
4. State: `room_sweep_rid_state.h` for list/track selection, staleness (a
   drone goes STALE after N s without a message), and alert edges (NEW DRONE /
   RETURNING DRONE / OPERATOR MOVED) feeding the existing feedback ladder.
   Cross-session RETURNING matches use the opt-in watchlist file — same
   honesty rules (a matching serial is not proof of the same physical drone
   session-over-session without a sighting gap model).
5. Session events: `source=RID`, per-session ordinals DR-nn (serials are
   identifiers — same privacy rule as Wi/BLE: raw serials stay out of CSVs
   unless the operator opts in; Room Report counts + plain-English lines).
   Rate-limit RID events (one row per drone per N s + state-change rows) so
   the 8-slot record queue cannot burst-drop (see session-17 baseline fix).
6. Track sub-mode: selected drone's last-known alt/speed/heading + age.
   Honest labels: "last broadcast" not "current position" when stale.

## UI
AV tab: title "AIR VEHICLES" / "RID LIST"; rows = ordinal, type/ID class,
RSSI, age. Detail page: all decoded fields, operator-position indicator.
2.4 energy page reuses the nR ACT-unit presentation (never dBm, never
distance claims). Alerts arrive through existing vibro/LED/sound feedback.

## Verification
- Host: tests/test_rid.c (fixture parse, staleness, alert edges, rate limit,
  malformed-line tolerance), tests/test_rid_state.c if the state header
  splits. Registered in `init.sh`.
- `python3 _verify_api.py` clean; ufbt zero warnings.
- Device: verify the Flipper app receives a real row from a compliant RID
  broadcast on a supported transport. The BFFB console scan alone does not
  verify app-level reception.

## Out of scope
RID transmission, drone control, RC packet decoding/injection, nRF24
promiscuous sniffing, operator identity lookup (FAA-only — report says so).
