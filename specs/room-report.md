# Spec: Plain-English Room Report

## Goal
`report-N.txt` must be readable by a non-engineer. It is a **Room Report**,
not only a machine coverage dump.

## Required sections (in order)
1. Title: `ROOM REPORT`
2. Status + coverage (COMPLETE/INCOMPLETE, FULL/PARTIAL)
3. What we checked (sensors confirmed / unavailable / not run)
4. Plain summary line (activity: quiet / some activity / busy)
5. Per-sensor findings (RF, Wi-Fi, BLE, nRF24, GPS)
6. TX outcome (if any)
7. Files + record stats
8. Limitations (existing legal/privacy language)

## Rules
- No claim of ownership, intent, distance from RSSI, or absence proof.
- nRF24 is receive/RPD only in language: "activity on 2.4 GHz channels".
- Host-tested via `room_sweep_report_format` / findings helpers.
