# fix_plan.md — Room Sweep (graph-engineering)

## DONE (prior sessions)
[x] v3.0 / v3.0.1 tabs, TX guards, NMEA fixes, settings crash, Back routing
[x] Marauder scan timeout, GPS stream, GPS UI, per-tab feedback (2026-08-01)
[x] Full-sweep expansion, Room Report, nRF24 RPD survey (2026-08-09)
[x] UI/UX spread: collision fixes, layout constants, tap/hold feedback (2026-08-13)

## DONE (this mission — 2026-08-13, Meter Suite)

[x] Integer trig radar math + host tests (room_sweep_radar.h)
[x] Scrolling RF waterfall history + host tests (room_sweep_waterfall.h)
[x] GPS trail ring + Radar page (room_sweep_gps_state.h)
[x] Analyzer 4 pages: Hunt / Field / Radar / Big-number Meter
[x] RF sub-mode 4: Waterfall (2.5 Hz, peak-hold edge, threshold line)
[x] GPS walk-to radar: mark-centered, true bearing, auto meter rings, trail
[x] Pitfall #19 (no libm in export table)
[x] Gate: ./init.sh + ufbt + _verify_api green; deployed via ufbt launch

Plan: `specs/meter-suite-2026-08-13.md` (mission contract graph).
Restore: tag `restore/pre-meter-suite-2026-08-13` @ 0397d5d.

## DONE (2026-09-05 — per-tab feedback parity)

[x] WiFi/BLE Geiger: identity-aware aged peak (locked target > selected
    row > strongest), 2s→6s fade to silence — no more frozen
    strongest-of-table (room_sweep_feedback.h)
[x] nRF24 Geiger: real RPD activity integrator replaces the channel-counter
    synthetic (room_sweep_nrf24_state.h activity_score, ~4s idle decay)
[x] Vibro graded ladder 150/300/600/1200/2500/5000 ms (GPS/TX exempt);
    sound ladders centralized in room_sweep_feedback_sound_interval_ms
[x] feedback_tick ~40 ms throttle + unconditional top-of-main-loop call
    (held buttons no longer starve feedback)
[x] Gates: ./init.sh ALL PASS (17 suites), ufbt Target 7 API 87.1,
    _verify_api.py CLEAN, header purity -pedantic OK

Plan: `specs/per-tab-feedback.md`.

## PENDING (user)

[ ] Visual QA on device: radar sweep/blips, waterfall scroll, big meter,
    GPS radar walk-to (needs outdoor fix), honest labels
[ ] Field-test TX radiate (user consent)
[ ] Capture live BFFB line dumps if parser still mismatches
