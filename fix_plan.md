# fix_plan.md — Room Sweep field-test 3 (graph-engineering)

## DONE (prior sessions)
[x] v3.0 / v3.0.1 tabs, TX guards, NMEA fixes, settings crash, Back routing

## DONE (this mission — 2026-08-01)

[x] P0: Fix Marauder scan timeout — never treat last_data_tick==0 as expired; ERR only after full timeout with zero results (BLE dedup silence is OK)
[x] P0: GPS request stream — send `nmea` on GPS tab enter; stopscan on leave; poll `gps -g nmea` if silent
[x] P1: GPS UI full use — speed, course, mark+haversine distance, sat quality, honest status labels
[x] P1: Per-tab feedback — centralize LED/sound/vibro by mode (RF/WiFi/BLE/GPS/TX); remove RF-thread LED monopoly
[x] P2: Host tests for timeout decision + haversine; init.sh includes them
[x] P2: Build + deploy to Flipper USB; device smoke (BLE stay-alive 8s, GPS tab, feedback toggles, no crash)

## DEFERRED
[ ] Field-test TX radiate (user consent)
[ ] Capture live BFFB line dumps if parser still mismatches after timeout fix
[ ] Visual confirm BLE list populates with BFFB attached + nearby devices
[ ] Visual confirm outdoor GPS FIX with module
