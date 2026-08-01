# fix_plan.md — Room Sweep v3.0 work queue

## DONE (v3.0 — 2026-08-01)

[x] Restructured to 6 tabs: RF / WiFi / BLE / GPS / TX / Info
[x] TX moved to dedicated tab with DISARMED→ARMED→TRANSMITTING state machine
[x] TX guardrails: OK=arm, Long-OK=transmit, auto-disarm, freq pre-load from RF peak
[x] Sound feedback: Geiger clicks (rate ∝ RSSI), lock tone, test beep on enable
[x] Vibro feedback: edge pulses, sustained pulse, test buzz on enable
[x] Force-volume and force-vibro messages (bypasses global mute)
[x] RF sub-modes: Survey (16pt) / Band Sweep (3 bands, coarse step) / Peak Refine (fine)
[x] WiFi parser: RSSI + SSID + channel + BSSID extraction, signal meter, freshness
[x] BLE parser: RSSI + name + MAC extraction, signal meter, freshness
[x] Auto-rescan WiFi/BLE every 5s (configurable in Settings)
[x] GPS: fixed stale-fix (clear on no-fix GGA/RMC/GLL)
[x] GPS: fixed GLL fields[5] OOB (guard changed to nf >= 7)
[x] Info tab: live capability card (version, UART state, feedback state, TX state)
[x] Settings overlay: Sound, Vibro, Auto-Rescan, TX Duration
[x] NMEA host tests: 44/44 pass
[x] Build: clean with -Werror, API 87.1, target 7
[x] Deployed to device via USB

## DONE (v3.0.1 — field-test fix round)

[x] Settings crash: removed InputTypePress from event filter (was double-firing)
[x] Audio continuous: heartbeat 2s idle → 60ms hot (not just threshold clicks)
[x] Marauder parser: bare RSSI at line start, skip '#' echoes, no fake done markers
[x] User guide: USER_GUIDE.md (every button/tab/mode/setting documented)
[x] QA audit: settings + input handler verified crash-safe (no bounds/null/div issues)
[x] MISSION.md updated to v3.0.1

## NEXT (field-test 2 driven)

[ ] Verify audio is audible on device (user test near WiFi router)
[ ] Verify TX guardrails work end-to-end on device
[ ] Verify band sweep detects known signals between presets
[ ] Capture actual BFFB Marauder output lines → adjust parser if needed
[ ] Add WiFi/BLE scan timeout detection (if scan hangs > 30s, show stale)
[ ] Consider: C/N0 bars from GSV for GPS signal quality display
