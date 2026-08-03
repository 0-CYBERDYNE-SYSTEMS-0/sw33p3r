# Room Sweep final code-quality review

Date: 2026-08-02
Scope: current uncommitted diff relative to `8e0a34e`; read-only source and host-build review.

## Verdict

- **codeQualityStatus:** BLOCK
- **recommendation:** REQUEST_CHANGES
- **blockers:** Correct the Wi-Fi/BLE table-full parser/result contract before release. A valid observation received after the fixed UI table fills is currently logged as a different, previously stored device.

## Verification performed

- `git diff --check` passed.
- `PYTHONDONTWRITEBYTECODE=1 python3 _verify_api.py` passed: 61/61 referenced API symbols resolve.
- `./init.sh` passed: host NMEA/input/RF-TX/wireless/GPS/recorder/report suites and the Target 7/API 87.1 FAP build.
- The required `remove-ai-slops` and `programming` skills were not available in the provided skill catalog, so their criteria were applied directly. The new host-helper tests are mostly behavior-oriented, but the wording-only evidence-string assertions in `tests/test_wireless_state.c` are brittle implementation/presentation mirrors and provide limited regression value. This is LOW, not a release blocker.

## Findings

### CRITICAL

None.

### HIGH

1. **A full Wi-Fi/BLE table causes false audit records.**
   `parse_wifi_line()` and `parse_ble_line()` return `true` after incrementing only their `*_table_full` counters; neither updates `*_last_updated` in that path (`room_sweep.c:1242-1243`, `room_sweep.c:1328-1329`). `process_uart_lines()` treats every `true` return as an accepted observation and logs `wifi_aps[wifi_last_updated]` / `ble_devs[ble_last_updated]` (`room_sweep.c:1396-1408`, `room_sweep.c:1440-1452`). Once the table fills, each distinct subsequent device is therefore recorded using stale identity, RSSI, channel, and count from a prior row. The report and CSV can claim evidence for the wrong transmitter, defeating the stated truthful/comprehensive dump goal.

   Required fix: return a result that distinguishes `updated/added` from `table-full` (or make table-full return false and record only an explicit loss event). Only dereference `*_last_updated` for an actual stored update.

### MEDIUM

1. **Reported records/bytes can overstate durable data after a write error.** `checked_write()` advances `records_written` and `bytes_written` before `storage_file_write()` succeeds (`session_log.c:154-165`); `write_report()` then presents those attempted values as `Records` and `bytes` (`session_log.c:519-526`). The incomplete status helps, but the numeric summary is inaccurate exactly when an operator needs it for recovery.

2. **Starting a new session migrates and removes old fixed-name logs without an operator action.** `migrate_legacy_files()` copies `/data/room_sweep/session.csv` and `bffb_dump.txt`, then deletes the originals (`session_log.c:114-152`). The contents are copied first, but this still changes/removes existing user-visible files as a side effect of enabling Record. New numbered files already avoid collisions, so this destructive migration needs explicit consent or should retain the originals.

### LOW

1. **Wireless helper tests are presentation-coupled.** `tests/test_wireless_state.c:41-58` asserts the first character of UI strings rather than a behavior boundary. This violates the programming/remove-ai-slops perspective on brittle implementation-mirroring tests, though it does not affect runtime correctness.

2. **No host test exercises the parser-to-recorder table-full branch.** The pure wireless tests cover identity comparison only; they cannot catch the HIGH finding because they do not exercise `parse_*_line()` plus the queued record creation. A focused regression test or a small pure parse-result seam should cover that transition.

## Scope conclusion

The button routing, bounded frequency-only TX lifecycle, explicit GPS-coordinate opt-in, sensor limitations, numbered artifacts, and main-loop storage writer are substantively present. The host gates demonstrate that this source builds, but they do not establish truthful logging after the bounded wireless tables fill. Do not install this revision as the promised comprehensive-recording version until the HIGH finding is fixed and verified.
