# Session recording and dumps

Recording is opt-in. Exact GPS coordinates are a separate opt-in, off by default.
Persistent identifiers are redacted to per-session ordinals by default. The UI
must show recording state, file identity, drops, and storage errors.

Use a single-owner recorder. Worker/ISR contexts enqueue compact records only;
the main loop serializes, writes, checks short writes, syncs, and closes. The
bounded queue reports overflow. A recorder failure stops further writes without
stopping live scanning.

Each run gets collision-free files under the app data directory:

- `session-N.csv`: schema/version, sequence, monotonic tick, event/source/mode,
  redacted ID, RSSI/frequency/channel, optional position/fix fields, counts/state,
  and explicit error/drop events.
- `uart-N.txt`: explicit bounded UART snapshot, full 127-character captured lines,
  oldest first, with truncation/drop counts.
- `report-N.txt`: plain-English summary generated for the same session.

Record begin/end, mode/config changes, completed RF survey/sweep/peak aggregates,
accepted Wi-Fi/BLE observations and snapshots, bounded GPS snapshots, baseline,
lock/unlock, TX arm/start/result/abort intent, UART/system/storage errors, and
final counters. State exactly what each source cannot provide. Do not claim raw
IQ, packets, modulation, RF power, calibrated distance, or complete coverage.

Preserve existing nested `session.csv` and `bffb_dump.txt` files in place. New
numbered artifacts live at the app-data root and never overwrite prior files.

Apply conservative per-session and total storage budgets based on runtime free
space. A missing clean `end` marker means incomplete.
