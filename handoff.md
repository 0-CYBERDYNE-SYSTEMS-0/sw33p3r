# HANDOFF — Adding WiFi / CC1101 / BT Jamming to Room Sweep

> Fresh handoff for the next dev team, focused on the **jammer** work.
> Written 2026-10-01. Companion to (not a replacement for) the lowercase
> `handoff.md`, which covers the whole app. Read that one first for context.

---

## 0. TL;DR

Room Sweep (`room_sweep.c`, ~7400 lines) already has **two TX-capable paths**.
Jamming is mostly *wiring*, not new RF:

- **CC1101 (300–928 MHz)** jam = app-only, ~50 LOC, testable on hardware you
  already have. **Start here.**
- **WiFi (2.4 GHz)** jam = needs a **Marauder ESP32 firmware fork** (long pole).
- **BT (2.4 GHz)** jam = same ESP32 radio as WiFi, same fork.

**One thing to know before you start:** the spec *pins jamming out of scope*.
`specs/full-capability-expansion-2026-09-20.md` "Never in scope (pinned)" lists
`jam/jam-detector TX modes` explicitly, and `MISSION.md` / `CONTRIBUTING.md`
say adding it is a spec violation that gets rejected. So this is a **deliberate
spec break** — get sign-off, or update the spec in the same commit.

---

## 1. What already exists (do not reinvent)

### 1.1 Internal CC1101 continuous-carrier TX
`room_sweep.c`:
- `tx_thread` (line 2685) — the TX worker thread. Sets frequency, then
  `subghz_devices_start_async_tx(app->radio, tx_carrier_cb, app)` (line 2735).
- `tx_carrier_cb` (line 2675) — the async-TX callback. It feeds
  `level_duration_make(app->tx_level, app->tx_level ? 1000000U : 2U)` — i.e. a
  **continuous tone** (1 s high / 2 µs low ≈ CW). Stop via
  `subghz_devices_stop_async_tx(app->radio)` (line 2758).

  **This is already a jammer** — a single-frequency CW tone. To make it a real
  jammer you change *what the callback/thread does*, not the plumbing.

### 1.2 TX state machine (the template to copy)
`room_sweep.h` `TxState` enum (line ~43) + `SweepModeTx` (line 18). The
arm/disarm flow (`TxDisarmed → TxArmed → TxTransmitting`) is the safety pattern
every jam mode should reuse. See `room_sweep.c` lines ~300–360.

### 1.3 External Marauder command channel
`room_sweep.c`:
- `marauder_send(app, cmd)` (line 1287) — writes a command string over UART.
- `marauder_start_scan(app, command, clear)` (line 1373) /
  `marauder_stop_scan(app)` (line 1355) — the start/stop pair to mirror.
- Command strings live in `room_sweep.h` lines 116–122:
  ```
  #define MARAUDER_CMD_WIFI    "sniffbeacon"
  #define MARAUDER_CMD_BLE     "sniffbt"
  #define MARAUDER_CMD_STOP    "stopscan"
  #define MARAUDER_CMD_RAW     "sniffraw"
  #define MARAUDER_CMD_PROBE   "sniffprobe"
  #define MARAUDER_CMD_ESP     "sniffesp"
  #define MARAUDER_CMD_PWN     "sniffpwn"
  ```

### 1.4 CC1101 band reality (BFFB hardware)
`room_sweep.h` lines 80–81, 102:
- The BFFB board's **external** CC1101 is a **dual 400/900 MHz** path, selected
  by a physical switch: up = 900 MHz, down = 400 MHz.
  - 400 path: 387–464 MHz
  - 900 path: 779–928 MHz
- The **internal** CC1101 covers the low band (~300 MHz). `ExtBandPref`
  (`ExtBandAuto/400/900`, lines 53–56) selects the path; ~300 MHz presets
  require the internal radio (see `room_sweep.c` ~461, 1110, 1161).
- **CC1101 cannot reach 2.4 GHz.** That is the whole reason WiFi/BT jamming
  must go through the Marauder ESP32, not the CC1101.

---

## 2. Band → hardware → mechanism

| Band | Hardware | Mechanism | Effort |
|---|---|---|---|
| **CC1101 300–928 MHz** | Internal CC1101 (low) / external dual 400/900 | CW carrier (exists) → add freq **sweep** or noise preset | App-only, ~50 LOC |
| **WiFi 2.4 GHz** | Marauder ESP32 | ESP32 raw/noise TX cycling ch 1–13 (2412–2472 MHz), or deauth-flood per channel | Firmware fork + app |
| **BT 2.4 GHz** | Marauder ESP32 (same radio) | TX on the 79 BT adaptive-hopping channels; BT Classic + BLE both 2.4 GHz | Firmware fork + app |

---

## 3. Concrete changes

### 3.1 CC1101 jam (app-only — do this first)
1. `room_sweep.h`: add a jam flag or `SweepModeJam` (or extend `SweepModeTx`
   with a "jam" sub-mode). Keep the existing `TxState` machine.
2. `room_sweep.c` `tx_thread`: instead of holding one frequency, **hop**:
   set freq → `start_async_tx` → wait N ms → next freq, looping across the
   selected band (reuse `ExtBandPref` 400/900 ranges). This turns a tone into a
   sweeping jam.
   - Simplest first cut: keep CW but let the user pick the band and sweep
     387→464 or 779→928 in steps.
   - Stronger: feed a noise/random pattern from `tx_carrier_cb` instead of a
     fixed level (needs a preset that supports it — verify against the Flipper
     SDK `subghz` API before promising noise).
3. UI: a **JAM** button on the TX tab + band picker. Reuse the arm/disarm
   confirm dialog from the existing TX tab.

### 3.2 WiFi + BT jam (Marauder firmware + app)
**Marauder ESP32 firmware (the long pole):**
- Add a `jam` command handler. Sub-modes: `jam wifi`, `jam bt`, `jam both`,
  `stopjam`.
  - `jam wifi`: ESP32 WiFi driver in raw TX, cycling channels 1–13 with a noise
    or deauth-frame payload.
  - `jam bt`: TX on the 79 BT channels (adaptive hopping).
- This is a **fork of the Marauder firmware**, not the app. Budget real time
  here; it is the gating dependency for both 2.4 GHz bands.

**App side (`room_sweep.h` / `room_sweep.c`):**
- Add to the command block (lines 116–122):
  ```
  #define MARAUDER_CMD_JAM_WIFI   "jam wifi"
  #define MARAUDER_CMD_JAM_BT     "jam bt"
  #define MARAUDER_CMD_JAM_BOTH   "jam both"
  #define MARAUDER_CMD_STOP_JAM   "stopjam"
  ```
- Add `marauder_start_jam(app, band)` / `marauder_stop_jam(app)` mirroring
  `marauder_start_scan` / `marauder_stop_scan` (lines 1355–1399).
- UI: JAM button on the WiFi/BLE tabs, armed/disarmed state in the header.

### 3.3 Shared plumbing (all three bands)
- `App` struct: `jam_armed`, `jam_band` (enum: CC1101 / WiFi / BT / All),
  `jam_start_tick`.
- **Safety (jamming is destructive):**
  - Confirm dialog before arming.
  - **Auto-stop after N seconds** (configurable) so a dropped device doesn't
    keep jamming.
  - Persistent **"JAM ACTIVE"** banner in the status bar.
- Follow the existing `tx_armed` / `tx_state` pattern (lines 300–360) exactly.

---

## 4. Verification gate (from the spec — every phase)

`./init.sh` ALL PASS (with the new suite) → `python3 _verify_api.py` CLEAN →
`ufbt` zero warnings → device deploy + on-device receipt (`.omo/evidence/`) →
CSV inspected for new rows → docs touched in the same commit → commit on the
phase branch, merge to `main`. **No co-author trailers.**

Note the spec's DoD line: *"No TX-path file changes outside `MARAUDER_CMD_*`
additions."* Jamming **will** change TX-path files, so this line must be
re-scoped in the spec alongside the "never in scope" edit.

---

## 5. Key file/line index

| What | Where |
|---|---|
| Marauder command strings | `room_sweep.h:116-122` |
| `marauder_send` | `room_sweep.c:1287` |
| `marauder_start_scan` / `stop_scan` | `room_sweep.c:1373` / `1355` |
| TX worker thread | `room_sweep.c:2685` (`tx_thread`) |
| Continuous-carrier callback | `room_sweep.c:2675` (`tx_carrier_cb`) |
| `start_async_tx` / `stop_async_tx` | `room_sweep.c:2735` / `2758` |
| TX state machine (template) | `room_sweep.c:300-360`, `room_sweep.h:43` |
| CC1101 band ranges | `room_sweep.h:80-81,102` |
| `ExtBandPref` (400/900 select) | `room_sweep.h:53-56` |
| `SweepMode` enum | `room_sweep.h:13-21` |
| "Never in scope" pin | `specs/full-capability-expansion-2026-09-20.md` (bottom) |
| Whole-app context | `handoff.md` (lowercase) |

---

## 6. Suggested order of work

1. **Spec sign-off** — edit the "never in scope" pin + the TX-path DoD line, or
   get explicit buy-in. (Blocks everything; do it first.)
2. **CC1101 sweep-jam** — app-only, shippable, testable on current hardware.
   Proves the safety UI + state machine.
3. **Marauder firmware fork** — `jam wifi` / `jam bt` / `stopjam`. Long pole;
   start early, it gates both 2.4 GHz bands.
4. **App wiring for 2.4 GHz** — new `MARAUDER_CMD_JAM_*` + start/stop + UI,
   once the firmware responds.
5. **All-band "JAM" mode** — CC1101 + WiFi + BT together, shared safety banner.
