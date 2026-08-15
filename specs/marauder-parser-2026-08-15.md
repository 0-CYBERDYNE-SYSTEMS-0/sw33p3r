# Spec: Marauder line parser deepening (2026-08-15)

Orchestrator: main agent. Single focused refactor — no parallel agents.
Restore point: current HEAD `d82376e` (main). Work branch: `feat/marauder-parser-2026-08-15`.

## Problem (architecture review candidate #1)

~370 lines of Marauder line-parsing decisions live inside `room_sweep.c` with no
host test. The framing state machine they feed (`room_sweep_scan.h`) is already
host-tested, but the seam stops one step short of where the bugs are: RSSI /
ESSID / BSSID / channel / device-name extraction and the "Hidden/unknown"
identification. This parser has real bug history (v3.0.1 "Marauder parser
rewritten") and a pending fix-plan item — *"capture live BFFB line dumps if
parser still mismatches"* — that turns directly into host fixtures the day the
parser crosses a seam.

## Goal

Deepen the parser into a **header-only, Flipper-header-free** module: a line in,
a parsed record out. `room_sweep.c` keeps UART ring plumbing, table mutation,
and canvas drawing. Pure in-process work — no Furi headers, plain `cc` tests.

## Scope

**Phase 1 (this iteration) — pure tokenization only.** Move the line → record
parsing into a new header. Table mutation (identity match, update-or-insert,
full counters) and all side effects stay in `room_sweep.c`.

**Phase 2 (follow-up, separate spec) — table upsert.** Relocate `WifiAp` /
`BleDev` (currently in `room_sweep.h`, which pulls in `<furi.h>`) and the
update-or-insert / dedup / full-counter logic into a Flipper-free header so the
whole ingest path is host-testable.

## Contract (exact API)

New file `room_sweep_marauder.h` — Flipper-header-free, compiles with
`cc -std=c11 -Wall -Wextra -Werror -pedantic -I.`. Includes `room_sweep_scan.h`
for the existing framing helpers (`room_sweep_uart_strip_prompt`,
`room_sweep_uart_find_ble_record`, `room_sweep_uart_ble_record_end`).

```c
typedef struct {
    int8_t rssi;
    uint8_t channel;
    char ssid[33];
    char bssid[18];
    bool valid;
} RoomSweepWifiRecord;

typedef struct {
    int8_t rssi;
    char name[33];
    char mac[18];
    bool valid;
} RoomSweepBleRecord;

/* True iff `line` is a valid WiFi AP line; fills *out. */
bool room_sweep_marauder_parse_wifi(const char* line, RoomSweepWifiRecord* out);

/* True iff `line` is a valid single BLE observation record; fills *out. */
bool room_sweep_marauder_parse_ble_record(const char* line, RoomSweepBleRecord* out);

/* Frame `line` (may hold several abutting records) and parse up to `max`;
   returns the number of records parsed. */
uint8_t room_sweep_marauder_parse_ble(const char* line, RoomSweepBleRecord* out, uint8_t max);
```

Internal helpers moved verbatim from `room_sweep.c` (become `static inline`):
`parse_int_after`, `parse_str_after`, `parse_essid`, `is_hex_digit`,
`copy_mac`, `parse_device_name`.

The pure parser is deterministic given the line — **no `furi_get_tick()`, no
`App*`, no mutex** — which is what makes captured dumps testable.

## Preserved invariants (behavior must not change)

These are the load-bearing semantics; golden tests assert them.

### `room_sweep_marauder_parse_wifi`
1. Strip prompt `"> "` via `room_sweep_uart_strip_prompt`; reject NULL / empty / `#`.
2. RSSI: leading `-NN` in `[-120, 0]` followed by space/NUL, else `RSSI:` / `rssi:`
   key; reject `rssi > 0` or `rssi < -120`.
3. Require `ESSID` / `essid` / `Ch:` / `BSSID`; reject if `Device:` present (BLE).
4. SSID: `parse_essid` (everything after `"ESSID: "` to end, strip trailing
   `" XX XX"` capability bytes); fallback `parse_str_after "ESSID"`; fallback
   `"Hidden/unknown"`.
5. Channel: `"Ch:"` then `"Channel"`.
6. BSSID: `copy_mac` (17-char `XX:XX:XX:XX:XX:XX`, not adjacent to a hex digit).
7. Return true iff a valid WiFi AP line; set `out->valid = true`.

### `room_sweep_marauder_parse_ble_record`
1. Strip prompt; reject empty / `#`.
2. Reject if `ESSID` / `BSSID` / `Ch:` present (WiFi).
3. RSSI: same rule as WiFi.
4. Require `Device` / `Name` / `name` or a MAC.
5. Name: `parse_device_name` (after `Device:` / `Name:`, trim trailing
   `" MAC:"` / `" RSSI"` / spaces / quotes); fallback `parse_str_after` on
   `Device` then `Name`; truncate at leaked `" RSSI"` / `"RSSI:"`; fallback
   `mac_early`; fallback `"Hidden/unknown"`.
6. MAC: `parse_str_after "MAC"`; fallback `copy_mac(line)`; fallback `copy_mac(name)`.
7. Return true iff a valid BLE record; set `out->valid = true`.

### `room_sweep_marauder_parse_ble`
- Frame via `find_ble_record` / `ble_record_end`; trim trailing spaces/tabs;
  stop at `#`; return the count parsed (bounded by `max`).

## What stays in `room_sweep.c` (Phase 1)

- `process_uart_lines` routing: ring drain, GPS feed, Marauder confirmation,
  error detection, per-mode routing.
- Table upsert: `room_sweep_wireless_identity_matches` → update-or-insert,
  `wifi_table_full` / `ble_table_full` / `*_window_table_full` counters,
  strongest-RSSI recompute, `last_scan_tick`, `marauder_state` transitions.
- `record_enqueue_ex` side effects and `target_rssi` matching.
- The `.c` wrappers (`parse_wifi_line`, `parse_ble_one_record`, `parse_ble_line`)
  become thin: call the pure parser, then upsert. Their outer return semantics
  are preserved — `parse_wifi_line` / `parse_ble_one_record` still return true
  for "handled" (including table-full), `parse_ble_line` still returns the count.

## Files

- **New:** `room_sweep_marauder.h`, `tests/test_marauder_parse.c`
- **Edit:** `room_sweep.c` (replace inline parse bodies with calls to the header),
  `init.sh` (add the new suite's `cc` line)
- **No change:** `application.fam` (`sources=["*.c","!tests"]` already covers
  header-only modules via inclusion)

## Verification

- `tests/test_marauder_parse.c` — repo `CHECK(cond,msg)` style. Fixtures:
  - Captured BFFB lines (WiFi `-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: Name 00 00`,
    `> RSSI: -38 Ch: 5 BSSID: aa:… ESSID: Name`, BLE `-60 Device: AirPods`,
    `RSSI: -37 Device: 00:11:22:33:44:55`).
  - Edge cases: bare leading RSSI, `#` echo, `"Hidden/unknown"` fallback,
    MAC-only BLE, abutting records + `#stopscan`, trailing capability bytes,
    leaked `" RSSI"` token in the name, `rssi` out of range, prompt `"> "`.
  - Golden equivalence: for every fixture, the pure parser's output must equal
    what the current inline parser produces (assert field-by-field).
- `./init.sh` ALL PASS (existing 16 suites + the new suite).
- `python3 _verify_api.py` CLEAN — the parser adds no new Furi symbols.
- `ufbt` — Target 7, API 87.1, zero warnings (`-Werror`).

## Out of scope

- Table upsert relocation (Phase 2, above).
- GPS freshness consolidation (candidate #3) and TX gate consolidation
  (candidate #2) — separate specs.

## Definition of done

- [ ] `room_sweep_marauder.h` compiles standalone with plain `cc -Werror`.
- [ ] `tests/test_marauder_parse.c` passes; golden fixtures match current behavior.
- [ ] `./init.sh` ALL PASS; `_verify_api.py` CLEAN; `ufbt` builds.
- [ ] `room_sweep.c` parse functions are thin wrappers; no behavior change on device.
- [ ] Committed on `feat/marauder-parser-2026-08-15` (no co-author trailers).
