# Room Sweep connected-device QA receipt — 2026-08-02

## Bound identity

- Source commit: `1432a5f3a5d27604a98fac5b77008efb8c40ee77`
- Artifact: `dist/room_sweep.fap`, 73,028 bytes
- SHA-256: `ff5c4cf15819d938c90e58a28f0e75d59020660720db85704fe93730a451f7e4`
- Local/device MD5: `574597ad81298b4c467e837c118bf544`
- APPCHK: Target 7, API 87.1
- Device firmware: Momentum `mntm-012`, commit `e1784e74`, API 87.1
- Installed path: `/ext/apps/Tools/room_sweep.fap`

## Exact-artifact interaction

The installed artifact was launched over USB-C. Complementary
press/short/release input events were used and the following paths were
exercised while recording session 4:

- Settings Record start and stop.
- RF, Wi-Fi, BLE, GPS, TX, Info, then wrap back to RF.
- Up and Down in every tab.
- Short OK scan/retry actions in Wi-Fi, BLE, and GPS.
- Explicit Raw Dump while the session was active.
- Long Back exit.

No OK event was sent in the TX tab, TX remained disarmed, and no RF
transmission was performed.

## Device evidence

- `/ext/apps_data/room_sweep/session-4.csv`: 5,251 bytes, 28-column schema,
  clean begin/end, mode rows for every tab, two Wi-Fi scan windows, two BLE
  scan windows, raw-dump event, and no TX event.
- `/ext/apps_data/room_sweep/uart-4.txt`: 766 bytes.
- `/ext/apps_data/room_sweep/report-4.txt`: 725 bytes; `Status: COMPLETE`,
  `Coverage: PARTIAL`, `TX: none`, `Dropped events: 0`, `Storage: ok`, and
  `GPS: omitted`.
- CSV end marker and report both state 46 records and zero drops.
- The pre-existing nested `session.csv` (703 bytes) and `bffb_dump.txt`
  (924 bytes) remained in place and unchanged.
- Earlier numbered session artifacts also remained in place.

Wi-Fi/BLE/GPS were not confirmed by attached sensor data during this indoor
pass. That is reported as partial coverage, not as evidence that devices,
telemetry, or recording were absent.

## Host verification

- `git diff --check`: PASS.
- `_verify_api.py`: 58/58 call-like symbols resolved against API 87.1.
- `./init.sh`: all host suites PASS; uFBT APPCHK PASS.
- Three independent max-effort final audits reported no remaining blocker.

