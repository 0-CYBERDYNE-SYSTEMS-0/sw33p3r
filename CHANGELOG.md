# Changelog

Keep-a-Changelog-inspired, condensed per release series. The full per-fix
history lives in `fix_plan.md` (session-by-session) and `progress.log`
(dated session log).

## v3.2 — 2026-09-11 (truth audit)

- RF tab: the SIGNAL badge now states its real meaning — energy above the
  -75 dBm gate; Peak reports ~100 kHz resolution with the 650 kHz
  measurement bandwidth noted.
- Analyzer: CLOSER/FARTHER renamed STRONGER/WEAKER (energy rose or fell, not
  distance); the radar page is now ENERGY MAP with an explicit NO DIRECTION
  (rings = RSSI, angles = channel wheel).
- ExtBand AUTO is labeled as the assumed switch path — the physical switch
  is not sensed.
- nR tab retitled 2.4 GHz energy detection; all synthetic dBm displays now
  read ACT (arbitrary activity units).
- Session CSV marks analyzer rows `rssi=0` and records real hit totals; the
  Room Report prints the real totals.
- SSID truncation fix: the trailing two-character strip was removed
  (verified against the upstream parser).
- GPS: FIX now requires a parsed position (NO POS status otherwise), with
  trail and mark gated on it; the FullSweep GPS gate also uses parsed
  position, not mere sentence presence.
- Docs and specs synced to the new wording; stale on-device screenshots
  flagged for re-capture.

## v3.1.0 — 2026-09

**Feedback overhaul (2026-09-05)**

- Per-tab proximity parity: the WiFi/BLE Geiger is now identity-aware and ages
  out (locked target > selected row > strongest, fading to silence over ~2–6 s)
  — walking out of range no longer freezes the click rate.
- Real nRF24 activity: the nR Geiger is driven by an actual RPD activity
  integrator (~4 s idle decay) instead of a synthetic channel counter.
- Graded vibro ladder (150/300/600/1200/2500/5000 ms); GPS fix heartbeat and TX
  cadences preserved; sound ladders centralized in one host-tested helper.
- `feedback_tick` runs at the top of every main-loop pass with a ~40 ms
  throttle — held buttons no longer starve sound/vibro/LED.

**dB-linear curves, hunting keep-alive, OK=lock (2026-09-05)**

- Step ladders replaced with continuous dB-linear curves: sound 2000→60 ms
  (~24 ms/dB), vibro 5000→150 ms (~61 ms/dB), plus a new GPS curve driven by
  satellite count. Monotonic by construction, pure integer math.
- Hunting keep-alive: with the analyzer open or a WiFi/BLE target locked, scan
  windows restart within ~250 ms so live meters and feedback never starve;
  windows still stop and log `scan_end` normally.
- Wi/BT controls: short OK locks/unlocks the selected row (empty list starts
  the first scan); Hold OK forces a manual rescan. RF/nR/GPS/TX unchanged.

**Scan & GPS reliability (2026-08-01, v3.1 / v3.1.1)**

- Fixed the BLE/WiFi instant-ERR bug: timeout is measured from scan start and
  only fires when zero results arrived for the full window; BLE first-sighting
  silence is normal, not an error.
- GPS now requests the Marauder `nmea` stream on tab enter (passive listening
  was wrong for BFFB); the GP tab shows speed, course, satellite bar, and mark
  distance. `stopscan` is always sent on GPS tab leave and app exit.

**On-device UI QA (2026-09-04)**

- Headless walk of 65 UI states over USB: fixed text collisions, clipped
  hints, and footer/radar geometry across Wi/BT help, GPS summary, analyzer,
  meter, and settings pages.

**Platform pin**

- Firmware: Momentum mntm-012, API 87.1, target 7 (ufbt). Release gates:
  17 host test suites under `-Werror`, ufbt build, and `_verify_api.py`
  export-table check all green.

## v3.0.1 — 2026-08-01

Post-field-test hardening:

- Settings crash fixed (input event double-fire); Back routing cleaned up —
  long Back exits the app, short Back closes Settings or disarms TX.
- Audio made continuous: heartbeat model (2 s idle accelerating to 60 ms at
  strong signal) instead of threshold clicks only.
- Marauder parser rewritten: bare-RSSI lines at line start handled, `#` echo
  lines skipped; fake "done" markers removed — scans stream until `stopscan`.
- WiFi/BLE scans show an error and recover when no result lines arrive for
  30 s; GPS freshness follows navigation sentences only; invalid NMEA checksum
  characters rejected.
- User guide (`USER_GUIDE.md`) written; settings and input handler QA audit.

## v3.0 series — 2026-08

**v3.0 (2026-08-01) — full restructure**

- Tabbed single-viewport app (6 tabs at launch, 7 once nR landed): RF / Wi /
  BT / nR / GP / TX / i.
- Dedicated safety-gated TX tab: DISARMED → ARMED → bounded carrier with
  countdown → auto-disarm; Back disarms at any point; RF-detected frequencies
  preload into TX.
- RF tab sweep modes: 16-point preset Survey, Band Sweep with peak hold, and
  fine-step Peak Refine.
- Wi-Fi (`sniffbeacon`) and BLE (`sniffbt`) scans via the BFFB/Marauder UART;
  RF uses the BFFB external CC1101 via the Momentum `cc1101_ext` device.
- GPS tab, upgraded to GPIO LPUART 15/16 primary with Marauder `nmea`
  fallback; streaming LED/Geiger/vibro feedback from live RSSI.

**Later v3.0-series additions (unversioned, 2026-08-09 – 2026-08-15)**

- nRF24 tab: receive-only RPD 2.4 GHz channel survey (2026-08-09); SPI Path
  policy forces the internal CC1101 whenever nRF24 is selected.
- FullSweep sequencer (RF → Wi-Fi → BLE → nRF24 → GPS) with hard timeouts, plus
  the plain-English Room Report and session evidence files (`session-N.csv`,
  `report-N.txt`, optional `uart-N.txt`) with privacy ordinals (2026-08-09).
- UI/UX spread: on-page collision fixes, shared layout constants, tap/hold
  tactile feedback, RF hold-to-lock card, GPS Hold-OK retry (2026-08-13).
- Meter suite: four-page analyzer (Hunt / Field / Radar / Meter), RF Waterfall
  sub-mode (~2.5 Hz scroll with peak hold), integer-trig polar RSSI radar, and
  a mark-centered GPS walk-to radar with real-meter rings and fix trail
  (2026-08-13).
- Marauder line parsing extracted into a host-tested header (2026-08-15).

## Earlier

- v1.0 baseline (crash-free, deployed) and v2.0 streaming feedback work,
  2026-08-01 — see `git log` for the full trail.
