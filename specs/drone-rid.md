# Spec: Drone / air-vehicle tab (AV) + Remote ID reception

## Goal
Detect and identify surveillance-relevant air vehicles around the user's
property: receive ASTM F3411-22a Remote ID (OpenDroneID) broadcasts via the
BFFB Marauder, show drone identity/position and operator position, and layer
2.4 GHz RC/drone-link energy on the same tab. Everything receive-side.

## Why it is in mission
Remote ID is standardized for PUBLIC reception (FAA 14 CFR §89, ASTM
F3411-22a): a compliant drone broadcasts Basic ID (serial/registration),
Location/Vector (lat/lon/alt/speed/heading) and System (operator location) over
BLE 4/5 and Wi-Fi Beacon/NaN on 2.4 + 5 GHz. Receiving it is the detect/analyze
mission exactly. No RID transmission, no spoofing, no injection — ever.

## Hardware / firmware facts (2026-10-11 web research)
- ESP32Marauder has a `remoteid` command family: `scan`, `list`, `track`,
  `clear` (upstream status: **development**). Upstream wiki states Marauder
  "can passively receive ASTM/OpenDroneID broadcasts... does not transmit".
- RID arrives on the SAME Marauder UART as every other scan (115200, `\n`
  line endings, `expansion_disable()` path — unchanged).
- Marauder is linear: one task at a time. `remoteid` mutually excludes
  Wi/BT sniffing while active (existing stopscan-before-switch pattern).
- nRF24 layer is RPD energy ONLY. Per `specs/nrf24-survey.md`, mousejack-class
  address recovery is banned — drone RC links (XN297/nRF24-class toy and
  camera drones) appear as 2.4 GHz energy hits in the existing nR survey and
  burst watch, nothing more. Do not spec promiscuous sniffing here.

## Contract
1. New tab **AV** (`SweepModeAv`, 8th tab; MISSION.md + control map updated at
   implementation time). Sub-modes: RID list / Track / 2.4 energy.
2. UART send set gains `remoteid scan` and `remoteid clear` ONLY after a
   phase-0 probe (`tools/marauder_probe.py` step) pins the exact output format
   on a live BFFB. Until then the parser is annotated "not yet observed live"
   in `docs/BFFB_MOMENTUM.md`, exactly like sniffesp/sniffpwn.
3. Parser: new Flipper-free `room_sweep_rid.h` (Basic ID / Location-Vector /
   System message fields; tolerant of unknown message types — skip, never
   guess). Host tests with redacted/synthetic fixtures, `tests/test_rid.c`.
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
- Device: phase-0 probe receipt first; then a field check against a compliant
  RID broadcast (or a second phone running an OpenDroneID beacon app).

## Out of scope
RID transmission, drone control, RC packet decoding/injection, nRF24
promiscuous sniffing, operator identity lookup (FAA-only — report says so).
