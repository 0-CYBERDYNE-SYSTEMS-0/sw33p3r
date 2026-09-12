# Spec / receipt: SIGINT truth audit (2026-09-11)

Three-agent audit of every receive-side claim in the app (UI, reports, session
CSV, docs) so nothing implies identification, direction, distance, or measured
signal strength beyond what the hardware actually produces. Agents 1/2 landed
`7fc8172` (Sub-GHz / Marauder / GPS); Agent 3 (this spec) covered nRF24, the
remaining screens, the session/report wiring, and all docs.

## The 12 issues — classification and outcome

| # | Issue | Class | Fix | Owner | Verified by |
|---|-------|-------|-----|-------|-------------|
| 1 | RF Survey `SIGNAL!` badge read as "signal detected" | UI overclaim | `>-75dBm` energy-gate tag | Agent 1 (7fc8172) | device strings + docs |
| 2 | Peak refine showed exact-looking frequency | precision overclaim | `~N.NN MHz` + 650 kHz BW note | Agent 1 (7fc8172) | test_rf_tx_state |
| 3 | Hunt trend CLOSER/FARTHER implied direction/distance | inference overclaim | STRONGER/WEAKER (RSSI delta only) | Agent 1 (7fc8172) | analyzer header |
| 4 | Analyzer radar implied physical direction | UI overclaim | ENERGY MAP + NO DIRECTION footer | Agent 1 (7fc8172) + Agent 3 (docs) | device strings + docs |
| 5 | ExtBand AUTO implied the switch is sensed | state overclaim | "AUTO: assumed path" wording | Agent 1 (7fc8172) + Agent 3 (USER_GUIDE) | docs |
| 6 | Sub-GHz report line implied device ID | report overclaim | "(energy only, no ID)" qualifier | Agent 3 | test_report_state |
| 7 | nR tab implied nRF24 protocol detection | hardware overclaim | RPD is a 1-bit energy detector (-64 dBm, spec-verified); mode retitled "2.4 GHz energy detection"; packet ID ruled out (needs 40-bit address a priori or banned mousejack-class attacks) | Agent 3 | test_nrf24_state + docs |
| 8 | nRF24 hit counts rendered/logged as dBm-like values | synthetic-as-measured | analyzer nR numbers labelled ACT (arbitrary units); CSV `rssi` column 0 for NRF24 rows, hit totals in dedicated fields; report prints real hit totals + "no packets or device IDs" | Agent 3 | test_nrf24_state + test_report_state |
| 9 | Parser stripped 2 trailing chars from SSIDs (scanall-only artifact treated as sniffbeacon format) | wrong-format handling | SSID kept verbatim (7fc8172); BFFB doc now shows both upstream formats (sniffbeacon prints nothing after SSID; capability bytes are scanall-only, verified in ESP32Marauder master WiFiScan.cpp); parser spec annotated superseded | Agent 2 + Agent 3 (docs) | test_marauder_parse |
| 10 | GPS "FIX" shown without parsed position | state overclaim | FIX requires has_pos; NO POS state (7fc8172); FullSweep GPS early-exit now keys on parsed position or sentences, never bare fix | Agent 2 + Agent 3 (full_sweep gate) | test_gps_state + test_full_sweep_state |
| 11 | Wi/BT results implied identified "devices found" | inference overclaim | "AP beacons heard" / "advertisements heard" wording in report + docs | Agent 2 (conventions) + Agent 3 (report/docs) | test_report_state |
| 12 | TX tab "Detected RX candidate" implied device detection | UI overclaim | "RX energy candidate" | Agent 3 | device string |

## Key nRF24 facts (verified against the Nordic nRF24L01+ product spec)

- RPD = register 0x09 bit 0; set when received power in the current channel is
  above about -64 dBm; continuous energy snapshot in RX mode. No packet, no
  address, no RSSI value.
- Passive discovery of unknown nRF24 addresses requires attack techniques this
  app bans; reading packets needs an address known a priori. Packet
  identification is therefore ruled out for this receive-only survey mission
  (documented in `room_sweep_nrf24_state.h`, `nrf24_survey.h/.c`, and
  `specs/nrf24-survey.md`).

## Screenshot debt (requires re-capture on device)

`docs/field_guide_shots/*` showing `SIGNAL!`, `CLOSER`, `RADAR` (analyzer), or
a dBm unit on nR analyzer pages predate this audit. HTML alt texts and
captions carry "stale screenshot — requires re-capture on device" notes where
the on-screen text changed.
