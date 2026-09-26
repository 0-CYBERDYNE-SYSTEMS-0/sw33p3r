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

## DONE (2026-09-05 — continuous proximity feedback + OK=lock)

[x] Continuous dB-linear feedback curves replace the step ladders: sound
    2000→60 ms (~24 ms/dB, ~80 distinct speeds), vibro 5000→150 ms
    (~61 ms/dB), new GPS curve 2000→200 ms over [-100,-60] via sats proxy
    (room_sweep_feedback.h); TX 120 ms fixed cadence + GPS/TX legacy vibro
    preserved; monotonic non-increasing by construction
[x] Hunting keep-alive: analyzer open OR WiFi/BLE target locked restarts
    scan windows with a 250 ms gap (was 5 s dead gap > 2 s meter stale
    time); windows still stop + log scan_end, restarts keep table/lock
[x] WiFi/BLE controls: short OK = lock/unlock selected row (populated
    list) or start first scan (empty); Hold OK = manual rescan (clears
    table + lock); footer hints updated; RF/nR/GPS/TX handlers untouched
[x] Gates: ./init.sh ALL PASS, ufbt Target 7 API 87.1, _verify_api.py
    CLEAN, room_sweep_feedback.h -pedantic -fsyntax-only clean

Plan: `specs/per-tab-feedback.md`.

## DONE (2026-09-20/21 — capability rollout "identify every emitter")

Phases 0-11 per `specs/full-capability-expansion-2026-09-20.md`. Receipts:
`.omo/evidence/phase-receipts-2026-09-21.md`; per-phase detail in
`progress.log`.

[x] Phase 0 fingerprint probe; phases 1-3 ident layer (device-verified)
[x] Phases 4/5/10 Wi capture sources; phase 5's sniffprobe grammar un-gated
    with a redacted fixture (2026-09-21)
[x] Phases 6-9 rogue / stats / burst watch / opt-in watchlist;
    7/8/9 device-verified from session artifacts
[x] Phase 11 capstone: 20 presets, Wi-probe sweep pass, Info Caps page;
    11a/11b device-verified
[x] Device-found fixes: OOM-on-launch (app state 20644→14500 B + NULL guard),
    identity-map saturation, baseline burst loss
[x] Gates throughout: ./init.sh ALL PASS (25 suites), _verify_api.py CLEAN,
    ufbt zero warnings

## PENDING (user)

[ ] Visual QA on device: radar sweep/blips, waterfall scroll, big meter,
    GPS radar walk-to (needs outdoor fix), honest labels — **still open, and
    now the only way to see pixels**: the RPC screen stream cannot allocate
    next to this app (reproduced three times), so nothing pixel-level
    (survey strip, badges, Info Caps page) has been seen on hardware
[ ] Field-test TX radiate (user consent)
[ ] Capture a live window with a hostile-tooling emitter → pin and un-gate
    sniffesp/sniffpwn (phase 10); same for scansta's station-line format
[ ] Optional: RAW source per-MAC interval logging (measured 11.7% drop rate
    under heavy traffic, session-20.csv)
