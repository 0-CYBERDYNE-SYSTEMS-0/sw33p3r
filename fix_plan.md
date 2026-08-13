# fix_plan.md — Room Sweep (graph-engineering)

## DONE (prior sessions)
[x] v3.0 / v3.0.1 tabs, TX guards, NMEA fixes, settings crash, Back routing
[x] Marauder scan timeout, GPS stream, GPS UI, per-tab feedback (2026-08-01)
[x] Full-sweep expansion, Room Report, nRF24 RPD survey (2026-08-09)

## DONE (this mission — 2026-08-13, UI/UX spread)

[x] Layout constants header + host test (`room_sweep_ui_layout.h`)
[x] Input seam: browse-phase classifier + tap/hold touch tracker (`room_sweep_input.h`, host tests)
[x] Collision fixes: RF Survey header/hint/SIGNAL, S/V header glyphs removed,
    FullSweep banner → top strip, Info 5 pages, WiFi/BLE list pitch 8px
[x] Input leverage: tap tick / hold-confirm pulses, RF Hold U/D lock card,
    GPS Hold OK = NMEA retry, uniform settings toggle confirmation
[x] Pitfall #18 (no swipe InputTypes on mntm-012)
[x] Gate: ./init.sh + ufbt + _verify_api.py all green; deployed via ufbt launch

Plan: `specs/ui-ux-2026-08-13.md` (mission contract graph).
Restore: tag `restore/pre-ui-ux-2026-08-13` @ aaef4b1.

## PENDING (user)

[ ] Visual QA on device: header collisions gone, RF lock card via Hold U/D,
    tap/hold pulses, FullSweep strip banner, Info 5 pages
[ ] Field-test TX radiate (user consent)
[ ] Capture live BFFB line dumps if parser still mismatches
