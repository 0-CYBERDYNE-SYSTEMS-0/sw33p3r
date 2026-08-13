# Mission Contract Graph — Meter Suite: radar / waterfall / big-meter (2026-08-13)

Orchestrator: main agent. Workers: parallel general-purpose agents.
Restore point: tag `restore/pre-meter-suite-2026-08-13` @ 0397d5d.
Branch: `feat/meter-suite-2026-08-13`.

## Verified platform facts (checked against SDK + export table)

- `canvas_draw_circle`, `canvas_draw_disc`, `canvas_draw_triangle`,
  `FontBigNumbers` exist in canvas.h AND are status `+` in api_symbols.csv.
- `canvas_draw_arc` does not exist.
- **libm is NOT exported**: sinf/cosf/atan2f/sqrtf/fabsf all absent from
  api_symbols.csv → all trig must be fixed-point LUT math in Flipper-free
  headers (host-testable with plain cc). Logged as Pitfall #19.
- `-Wdouble-promotion`: float math must use `f`-suffixed literals and explicit
  casts, or stay integer. Radar headers are integer-only.

## Nodes

### A1 — `room_sweep_radar.h` (new) + `tests/test_radar.c`  [agent A]
Integer polar math, Flipper-free. Exact API (orchestrator compiles against):
- `#define ROOM_SWEEP_RADAR_FLOOR_DBM (-127)` / `ROOM_SWEEP_RADAR_CEIL_DBM (-20)`
- `int16_t room_sweep_radar_sin128(int16_t deg)` — deg normalized 0..359,
  returns sin*128. 1°-step LUT, `static const int16_t[360]`.
- `int16_t room_sweep_radar_cos128(int16_t deg)` — via sin128(deg+90).
- `uint16_t room_sweep_radar_atan2_deg(int32_t y, int32_t x)` — 0..359,
  fixed-point (octant fold + atan LUT or ratio poly); accuracy ±2° at the
  8 compass points and diagonals (host-tested).
- `uint32_t room_sweep_radar_bearing_deg(int32_t lat1_e6, int32_t lon1_e6, int32_t lat2_e6, int32_t lon2_e6)` — initial true bearing 0..359, integer math using sin128/cos128 for the longitude convergence; ±2° on the tested cases.
- `uint8_t room_sweep_radar_rssi_radius(int8_t rssi, uint8_t max_r)` — linear rssi→radius clamp.
- `uint16_t room_sweep_radar_angle_for_channel(uint8_t ch, uint8_t ch_count)` — (ch*360)/count normalized.
- Ring scale: `uint16_t room_sweep_radar_ring_unit_m(uint32_t dist_m)` returning
  the per-ring meter spacing from {2,5,10,20,50,100,250,500,1000} such that
  dist ≤ ~3.2 rings (host-tested); `const char* room_sweep_radar_ring_label(uint16_t unit_m)`.
- `void room_sweep_radar_blip_xy(int16_t deg, uint8_t radius, int16_t cx, int16_t cy, int16_t* out_x, int16_t* out_y)` — via sin128/cos128.

### A2 — `room_sweep_waterfall.h` (new) + `tests/test_waterfall.c`  [agent B]
Scrolling spectrum snapshot buffer:
- `#define ROOM_SWEEP_WATERFALL_COLS 24`, `ROOM_SWEEP_WATERFALL_CHANNELS 16`
- `typedef struct { int8_t col[24][16]; uint8_t newest; uint8_t count; int8_t peak[16]; } RoomSweepWaterfallState;`
- `room_sweep_waterfall_init` (all -127), `room_sweep_waterfall_push(state, const int8_t* snapshot16)` (NULL → silence snapshot; updates per-channel peak from live samples), `room_sweep_waterfall_col_at(state, age)` (age 0 = newest), `room_sweep_waterfall_channel_at(state, age, ch)`.

### A3 — extend `room_sweep_gps_state.h` + `tests/test_gps_state.c`  [agent C]
- Add `RoomSweepGpsPageRadar` (page count 3); update next/prev/page_text ("Radar").
- GPS trail ring (Flipper-free): `#define ROOM_SWEEP_GPS_TRAIL_MAX 8`
  `typedef struct { int32_t lat_e6; int32_t lon_e6; uint32_t tick; } RoomSweepGpsTrailPoint;`
  `typedef struct { RoomSweepGpsTrailPoint pts[8]; uint8_t head; uint8_t count; } RoomSweepGpsTrail;`
  `room_sweep_gps_trail_push(trail, lat_e6, lon_e6, tick)` — dedupe <2 m from last point;
  `room_sweep_gps_trail_point_at(trail, age)`; `room_sweep_gps_trail_clear`.
- Keep every existing function + test green.

### B — orchestrator wiring (`room_sweep.h` + `room_sweep.c`)
- `RfSubWaterfall` (RfSubCount 4) — passive scrolling spectrum of the 16 presets.
- Analyzer pages 2→4: 0 HUNT, 1 FIELD, 2 RADAR, 3 METER (HoldR cycles %4).
  RADAR: polar rings + animated sweep + per-mode blips (RF freq wheel /
  WiFi channel wheel / BLE ordinal wheel / nRF24 channel wheel), locked
  target = blinking diamond; honest label "RSSI ring, not meters".
  METER: FontBigNumbers dBm + peak hold + trend arrow.
- GPS tab page 3 RADAR: real meters — center = live position, rings from
  ring_unit_m, mark blip at bearing/radius, trail of last ≤8 fixes
  (mark-centered, north-up), bearing + distance + closing arrow
  (dist delta over trail). No fix → honest fallback text.
- Waterfall push every ~400 ms from app->rssi (RF thread data); trail push
  every ≥2 s when a fresh fix moved ≥2 m.
- Footer/hint strings updated everywhere; no new Flipper API beyond the
  verified canvas symbols.

### C — gates & deploy
- init.sh + two new suites; `_verify_api.py`; `ufbt`; `ufbt launch`.

### D — docs
- Pitfall #19 (no libm in export table; canvas shapes verified).
- USER_GUIDE.md, README.md, progress.log, features.json, fix_plan.md, spec log.

## Edges
A1,A2,A3 parallel → B (after all land) → C → D.
Conflict avoidance: each agent owns distinct files; orchestrator owns
room_sweep.c/h, init.sh, docs, git.

## Execution log (2026-08-13)

- A1 delivered `room_sweep_radar.h` + `tests/test_radar.c`: 360-entry sin128
  LUT, octant-fold atan2 (≤1.8° err), integer true bearing, rssi→radius,
  channel→angle, ring scale {5..1000m, 0→2}, blip_xy. ALL PASS.
- A2 delivered `room_sweep_waterfall.h` + `tests/test_waterfall.c` (24 checks PASS).
- A3 delivered gps_state Radar page + trail ring (dedupe <2 m) + extended
  tests (34 checks PASS; two stale 2-page assertions updated in place, disclosed).
- Orchestrator post-agent fixes: `#include <stddef.h>` in waterfall/gps_state
  headers (NULL dependency); meter page fill_px (0..118) not percent.
- Wired: RF Waterfall sub-mode (RfSubCount 4), analyzer 4 pages
  (Hunt/Field/Radar/Meter), GPS Radar page (mark-centered true-bearing,
  auto-scaled meter rings, 8-point trail, north tick), waterfall push 2.5 Hz,
  trail push ≥2 s, honest labels ("RSSI ring, not meters", "true bearing").
- Pitfall #19 logged (no libm in export table; canvas circle/disc/triangle +
  FontBigNumbers verified exported).
- V1 gate: ./init.sh ALL PASS (16 suites) + ufbt + _verify_api 64/64 CLEAN.
- V2: ufbt launch OK on flip_XXXX0 (app closed via CLI first).
- Field verify pending: user visual QA of radar/waterfall/meter/GPS-radar.
