# Spec: Host dashboard + export formats

## Goal
Take session data off the SD card: a host-side Python tool that turns
`session-N.csv` / `report-N.txt` into a static HTML dashboard — timelines,
per-device history across sessions, GPS map, drone/operator map (when RID
rows exist) — plus machine-readable exports (GeoJSON, WiGLE CSV).

## Why host-side
The Flipper is a sensor head: 128×64, 6 KiB stack, main-loop budget. All
rendering/analysis beyond the Room Report belongs on the host. Purely
additive — no firmware risk, no mission exposure (read-only ingest).

## Contract
1. `tools/room_sweep_dashboard.py` (Python 3, **stdlib only** — matches the
   repo's tool style; no pip deps to babysit). CLI:
   `python3 tools/room_sweep_dashboard.py /path/to/sessions/ [-o out_dir]`.
2. Inputs: `session-N.csv` (events), `report-N.txt`, optional `watchlist.txt`
   (opt-in labels). Tolerates missing/rotated files; never modifies inputs.
3. Outputs (default `./sweep_dashboard/`):
   - `index.html` — self-contained (inline CSS/JS, Leaflet via CDN with a
     graceful no-map fallback): session list, unified timeline (RF bursts,
     Wi, BT, nR, RID, FPV in one sorted stream), per-ordinal stats, RSSI
     sparklines, coverage summary.
   - `map-N.geojson` — your GPS path + (only if GPS Log was ON) event
     points; RID rows contribute drone/operator markers keyed by
     per-session ordinals, never raw identifiers.
   - `wigle-N.csv` — WiGLE-compatible CSV (BSSID/SSID/lat/lon/RSSI) for
     optional community upload; only when coordinate tokens are present.
4. Privacy rules mirror the device: ordinal identifiers only; coordinates
   only from sessions that logged them; the dashboard never de-anonymizes
   across sessions (cross-session joins require watchlist.txt and are
   labeled as opt-in, same honesty line as the Room Report).
5. Rendering conventions follow the project's docs aesthetic (matches
   `docs/` pages); output lands outside the repo by default (user path) —
   `docs/` integration is a separate, deliberate step.

## Verification
- Host test suite for the parser (`tests/` runs C — so Python-side checks
  live in `tools/test_dashboard_fixtures.py`, run manually and in CI later
  if adopted): fixture sessions → assert timeline ordering, GeoJSON
  validity, privacy redaction, WiGLE column order.
- Round-trip check: a real session-N.csv from `.omo/evidence/` renders with
  zero stderr warnings.

## Out of scope
Live serial streaming (future), cloud upload automation (manual by user),
server components, cross-user sharing.
