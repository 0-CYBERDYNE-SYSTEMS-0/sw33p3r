# Room Sweep recorder review — Spec 06 / integrated implementation

Read-only review of the current worktree. No source, build, device, or TX
operations were performed.

## Evidence commands

The following commands were run from `/Users/scrimwiggins/sw33p3r`:

```sh
rg -n "storage_common_fs_info|storage_file_open|storage_file_get_error|storage_file_sync" session_log.c
rg -n "record_enqueue|record_drain|uart_line_drops|gps_byte_drops|dump_push_line" room_sweep.c
sed -n '1,35p' /Users/scrimwiggins/.ufbt/current/sdk_headers/f7_sdk/applications/services/storage/storage.h
sed -n '76,105p;430,445p;570,610p' /Users/scrimwiggins/.ufbt/current/sdk_headers/f7_sdk/applications/services/storage/storage.h
```

Binary/source observables are the current line-numbered contents of
`session_log.c`, `room_sweep.c`, `room_sweep_record_state.h`,
`room_sweep_report.h`, and the SDK header cited above.

## Findings

1. **HIGH — wrong filesystem preflight.** `session_log_begin()` writes under
   `APP_DATA_PATH("room_sweep")`, which the SDK defines as `/data/room_sweep`,
   but calls `storage_common_fs_info()` on `STORAGE_EXT_PATH_PREFIX` (`/ext`)
   (`session_log.c:154-179`; SDK `storage.h:15-24`). The check can reject a
   usable `/data` filesystem or budget against unrelated storage. There is no
   periodic `/data` free-space or aggregate-cap monitor.

2. **HIGH — failed-open cleanup violates the SDK contract.** The SDK warns that
   callers must call `storage_file_close()` even when `storage_file_open()` fails
   (`storage.h:78-86`). The begin, raw-dump, and report paths free an unopened
   file without closing (`session_log.c:193-202`, `462-465`, `396-403`). No
   `storage_file_get_error()` is queried before close and close results are
   ignored (`session_log.c:401`, `427`, `482`).

3. **HIGH — CSV contract is incomplete.** The emitted header and rows contain
   13 fields (`session_log.c:204-206`, `247-281`), while Spec 06 requires the
   fixed fields `has_pos`, `fix_quality`, `sats_used`, `sats_view`,
   `speed_kts`, `course_deg`, `count`, `state`, and `error_code` (and a
   submode). GPS quality/state is only text-packed into `detail`; consumers
   cannot parse the required fields.

4. **HIGH — legacy migration is absent.** `session_log_begin()` only creates
   directories and selects a new ordinal (`session_log.c:154-173`). It never
   detects `session.csv`/`bffb_dump.txt`, creates `legacy-*` targets, renames
   after collision checks, or records migration failures required by Spec 06.

5. **MEDIUM — lifecycle/buffering policy is weaker than Spec 06.** Begin and
   TX records are not synchronised immediately; session sync is only every eight
   sequence values (`session_log.c:317-320`). `record_drain()` drains an
   unbounded `while` loop rather than a bounded per-tick batch
   (`room_sweep.c:412-445`). The pure 24 KiB/512-record cap applies to session
   writes, but raw dumps have no explicit storage-budget/free-space accounting.

6. **MEDIUM — drop/error accounting is incomplete.** The report receives record
   queue/serializer drops, but not UART ISR drops (`room_sweep.c:566-590`), GPS
   byte-ring drops, dump-ring overwrites (`room_sweep.c:345-351`), malformed
   parser lines/table-full counts, or raw-dump/report I/O failures. The raw dump
   header says only “explicit user action” and does not warn that identifiers or
   coordinates may be present (`session_log.c:467-479`).

7. **MEDIUM — report failure is not persisted.** `session_log_end()` finalizes
   the pure state, then writes the report; a report failure only latches the
   in-memory record state after report text generation (`session_log.c:429-435`).
   No report artifact can communicate that failure when report creation itself
   failed.

8. **MEDIUM — producer state races/lost tail events.** `record_enqueue()` reads
   non-locked `session_log_on`, `gps_log_coordinates`, and `mode` while RF/TX
   workers and the main loop can change them (`room_sweep.c:365-408`). Disabling
   logging can race a worker that passed the initial flag check, leaving a queue
   entry after the main-loop drain/end. `record_queue_drops` is also read by the
   draw path without `record_mutex` (`room_sweep.c:2646-2650`).

## Passing seams

- Runtime producers enqueue; current `session_log_event()` storage I/O is only
  called by main-loop `record_drain()` (`room_sweep.c:412-445`).
- Ordinal collision checks plus `FSOM_CREATE_NEW` avoid normal overwrite races
  (`session_log.c:45-64`, `193-198`, `453-465`).
- Identifier output is per-session AP/BLE/RF/GPS ordinals; raw values are not
  retained by `room_sweep_record_state.h`.
- GPS coordinates default off and queue snapshot fields gate `has_pos`
  (`room_sweep.c:379-381`, `2888`); the settings control is explicit.
- Raw UART snapshot uses the newest bounded 24-line ring with 127-character
  parser lines and appends line terminators (`room_sweep.c:345-351`,
  `session_log.c:467-483`).

## Conclusion

**Not a PASS for Spec 06.** The single-consumer queue, privacy ordinals,
default GPS omission, and non-overwriting ordinary naming are present, but the
filesystem preflight, SDK cleanup contract, schema, migration, accounting, and
producer lifecycle issues require correction before acceptance.
