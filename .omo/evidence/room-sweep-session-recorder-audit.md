# Room Sweep session recorder audit (read-only)

## Observed implementation

- `session_log.c` appends to `/data/room_sweep/session.csv` via `APP_DATA_PATH`, despite the header comment saying `/ext/apps_data`.
- `session.csv` has no session boundary and uses `FSOM_OPEN_ALWAYS`; `bffb_dump.txt` uses `FSOM_CREATE_ALWAYS`, so every dump overwrites the prior dump.
- Current rows are `tick_ms,lat,lon,kind,id,rssi,freq_hz`; writes/sync results are ignored, `kind` is not sanitized, and hit writes can occur from both the RF worker and main UART parser without a recorder mutex/queue.
- The UART dump is a newest-only ring of 16 strings capped at 63 characters. The upstream pending UART ring is 24 lines capped at 127 characters and silently drops on full.

## Proposed bounded v2 contract

Create one non-overwriting `session-N.csv` per run under `/data/room_sweep/`, optional explicit `uart-N.txt`, and a plain-English `report-N.txt`. Use `storage_get_next_filename` followed by `FSOM_CREATE_NEW`; never append to the old session file or use `FSOM_CREATE_ALWAYS` for new artifacts.

Use a sparse fixed CSV schema:

`schema,seq,tick_ms,event,source,mode,submode,id_ref,rssi_dbm,freq_hz,channel,lat_deg,lon_deg,has_pos,fix_quality,sats_used,sats_view,speed_kts,course_deg,count,state,error_code`

Events cover begin/end, mode/config, RF survey/sweep/peak, WiFi/BLE observations and snapshots, GPS snapshots, baseline, target lock, TX arm/start/end/abort, UART/system, and recorder errors. `seq` and a clean end marker distinguish complete versus interrupted sessions.

Use per-session ordinals (`AP-01`, `BLE-01`) instead of persisting raw SSID/name/MAC by default. Exact GPS coordinates require an explicit opt-in; otherwise emit fix/time/satellite/freshness status only. Raw UART/NMEA is never continuous; a user-triggered dump contains only the bounded newest ring and warns that it may contain identifiers/coordinates.

Snapshots are main-loop copies of calculated state, not raw streams. RF survey snapshots contain the latest 16 channel averages (each is eight `radio_rssi` samples); coarse/fine sweeps emit terminal aggregate/configuration rows. WiFi/BLE rows represent accepted parser lines/tables (BLE RSSI updates may be silent). GPS rows represent checksum/semantic-valid `GpsFix` state and freshness. No raw IQ, calibrated RSSI, TX field power, modulation, GPS accuracy/HDOP/altitude, or unparsed packet metadata is available in the current producers and must not be claimed.

## Lifecycle and failure policy

Queue compact recorder records (for example 32 x 64 bytes) from workers/ISR; only the main loop performs storage I/O. Drain a bounded number each tick into a 512-byte serializer buffer. Flush at 1 second or 75% full, sync every roughly 4 KiB/5 seconds, and sync immediately for begin/TX/end. Check every write/sync result and query `storage_file_get_error` before close. Preflight and periodically monitor `/data` free space with `storage_common_fs_info`; enforce a per-session/event cap and an app-data aggregate cap. On short write, sync failure, or low space, stop writes, retain counters, and let the live app continue. Queue, UART, malformed-line, migration, and storage drops/errors appear in the final report. Missing end marker means incomplete.

On shutdown, stop scans, join TX/RF workers, drain records, emit final snapshots and TX-abort status if needed, append end, flush/sync, and close. Failed opens must still call `storage_file_close` as required by the SDK.

## Migration

On first v2 begin, detect `session.csv` and `bffb_dump.txt`. Allocate unique `legacy-session-N.csv` and `legacy-bffb-N.txt` targets with `storage_get_next_filename`, then rename only after the target is known free. Preserve old bytes because the append-only CSV has unknown run boundaries. If migration fails, leave the legacy file untouched and report the error. Start a new v2 session file regardless only when it can be created safely.

## Producer map and cadence

- RF: `rf_channels`, `rssi[]`, `peak_*`, sweep/peak progress and band constants in `room_sweep.h`/`room_sweep.c`.
- WiFi/BLE: parsed `WifiAp`/`BleDev` tables, strongest/count/last-seen fields and accepted Marauder lines.
- GPS: all fields of `GpsFix`, `gps_last_valid_tick`, GPIO/Marauder source and baud/open state.
- TX: `tx_state`, `tx_active`, `tx_freq_hz`, duration, remaining time, `tx_started`, and async-start result.
- System/config: mode/submode, radio path/ext band, Marauder state, UART availability/line/drop counters, baseline, target lock, marks, settings, recorder counters, and free space.

The current 1.5-second global hit throttle bounds hit rows to at most 0.667/s (roughly 24-80 B/s depending on GPS columns), so an unbounded hourly log would grow to roughly 85-290 KiB. A 24 KiB event cap plus 8 KiB optional dump cap is intentionally conservative; actual free space must be queried at runtime. RF snapshots at 5 seconds and GPS snapshots at 5 seconds keep storage bounded.

## Verification coverage

Existing host checks are `tests/test_nmea.c` (GPS parsing/checksums/bounds/freshness), `tests/test_scan_logic.c` (Marauder timeout and haversine), and `tests/test_input_state.c` (Back routing). Add pure host tests for CSV escaping/bounds/redaction, event-ring overflow/sequence, snapshot cadence/throttle, non-overwrite filename/migration behavior, short-write/sync/free-space failures, clean/incomplete markers, and report claims/privacy. Keep the Flipper storage adapter thin.

## SDK evidence used

The current Momentum SDK declares `storage_file_write` may write fewer bytes, `storage_common_fs_info` for total/free space, `storage_file_get_error` before close, `storage_get_next_filename` for collision-free names, and `FSOM_CREATE_NEW` versus destructive `FSOM_CREATE_ALWAYS` in `/Users/scrimwiggins/.ufbt/current/sdk_headers/f7_sdk/applications/services/storage/`.
