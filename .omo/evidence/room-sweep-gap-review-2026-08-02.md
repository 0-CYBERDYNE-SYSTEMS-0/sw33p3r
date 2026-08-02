# Room Sweep overhaul gap review (read-only)

Date: 2026-08-02
Scope: source/docs review only; no build, device, or network actions.

## Invocation and binary observables

- `rg -n "MARAUDER_CMD_WIFI|last_signal_freq|session_log|gps_last_valid|MarauderDone|LongOK|scanall|passive|date|Info" room_sweep.c room_sweep.h session_log.c nmea.c USER_GUIDE.md MISSION.md README.md specs`
  - showed `MARAUDER_CMD_WIFI "sniffbeacon"` in `room_sweep.h`, while `USER_GUIDE.md` claims `scanall`.
  - showed `last_signal_freq` is a single global handoff value; Peak refinement writes it without an RSSI threshold.
  - showed `MarauderDone` is rendered but never assigned.
- `nl -ba room_sweep.c | sed -n '300,318p;800,827p;1038,1173p;1611,1773p;1776,1865p;2189,2213p;2278,2317p'`
  - confirms source line locations for unqualified TX handoff, UART overclaim, stale-row UI, GPS display, scan state, and logging toggles.
- `nl -ba session_log.c`
  - confirms durable `session.csv`/`bffb_dump.txt`, `CREATE_ALWAYS` dump overwrite, and CSV ID sanitization that omits quote escaping.
- Upstream check: ESP32Marauder's current public CLI page lists `scanap`, `scansta`, `sniffbeacon`, `sniffbt`, and `stopscan`, with 115200 UART and `help` as the connection check; it does not list `scanall`. The local source's `sniffbeacon` is therefore plausible, while the `USER_GUIDE.md`/spec `scanall` claim needs version pinning or correction: <https://github.com/justcallmekoko/ESP32Marauder/wiki/cli>.
- Upstream BFFB page (edited 2026-02-02) says GPS is directly on the ESP32, not Flipper GPIO, while dual CC1101 modules are Flipper SPI. The local docs/code's GPIO-first GPS path is therefore a high-risk architecture contradiction: <https://github.com/justcallmekoko/ESP32Marauder/wiki/BFFB>.

## Prioritized findings

### Must fix

1. Expire and qualify RF-to-TX candidates; validate radio/path/band and show source/age before arm.
2. Serialize GPS parser/state updates from GPIO ISR and UART main loop; add dual-source stress coverage.
3. Protect session logging against RF/main concurrency and close races; report I/O/drop errors.
4. Add explicit privacy warning, retention/delete/path affordance, CSV quoting, and dump metadata.
5. Distinguish UART handle/open from firmware handshake and RF unavailable from quiet; prevent false “UART ok”/`[SURVEY INT]` claims.
6. Make WiFi/BLE rows time-bounded and identity-safe; expose overflow/drops and preserve stable MAC/BSSID identities.
7. Reconcile command/state/docs (`sniffbeacon` vs `scanall`, unreachable Done, GPS active stream vs “fully passive”).
8. Block TX with no radio or unsupported external-band frequency while retaining bounded carrier and two deliberate actions.
9. Scope target locks to their source tab (or visibly indicate a global lock); `feedback_tick` currently applies any lock before mode dispatch, so a WiFi/BLE lock can drive RF/GPS feedback after tab changes.

### Should fix

- Make Info tab truthful (current implementation omits version/API/TX/audio/legal data and advertises nonexistent LongOK behavior); show GPS date if documented.
- Label RSSI as observation, not telemetry/occupancy/identity; show RF sweep coverage and missed-signal limitations.
- Add baseline age/clear/quality and physical external-band indication; add explicit scan stop and truthful auto-rescan semantics.
- Require selectable/confirmed target identity; current strongest-by-name/SSID lock can choose the wrong device.

### Later

- Add session manifest (UTC origin, app/FAP hash, radio path, band/step/dwell, firmware, parser drops, GPS uncertainty), paginated/exportable full rows, persistence settings, HDOP/CN0.

### Reject

- Automated jamming/blocking/deauth, arbitrary replay, broad transmit, or claims that passive RSSI proves telemetry, location, ownership, or transmitter identity.
