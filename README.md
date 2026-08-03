# Room Sweep

Room Sweep is a Flipper Zero external app for receive-side RF surveying,
WiFi/BLE scan control through an attached UART device, passive GPS display, and
a safety-gated TX control tab.

## Current controls

- Six tabs: RF Survey/Sweep/Peak, WiFi, BLE, GPS, TX, and Info.
- Short Left/Right changes tabs. Up/Down browses the current tab: RF modes,
  WiFi/BLE results, GPS/Info pages, or armed TX presets.
- Short OK performs the ordinary action; Long OK locks a qualified result or
  confirms an already armed TX request.
- Short Back opens Settings, closes Settings, or disarms TX.
- Long Back exits the app from every context.
- Sound and vibration are Settings items only.
- TX requires short-OK arming followed by long-OK confirmation. Do not transmit
  unless you have authorization and intend to do so.

## Session files

Settings → Record creates a numbered session across every tab. Settings → Raw
Dump separately captures a bounded raw UART snapshot. On the SD card, files are
stored at:

```text
/ext/apps_data/room_sweep/session-N.csv
/ext/apps_data/room_sweep/report-N.txt
/ext/apps_data/room_sweep/uart-N.txt
```

Exact GPS coordinates are omitted unless `GPS in Log` is explicitly enabled.
Persistent WiFi/BLE identifiers are replaced with per-session ordinals; the
explicit raw UART dump may contain raw identifiers and coordinates. See
[`USER_GUIDE.md`](USER_GUIDE.md) for the complete field list and evidence limits.

## Build and host checks

```sh
ufbt
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/room_sweep_test_nmea
/tmp/room_sweep_test_nmea
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c -o /tmp/room_sweep_test_input_state
/tmp/room_sweep_test_input_state
python3 _verify_api.py
```

Deploy the built FAP with `ufbt launch` when a Flipper is connected.

Live BFFB result parsing, measured audio/haptic output, known-signal behavior,
and deliberate RF field transmission require separate hardware/field checks.
Passive WiFi/BLE observations cannot prove Internet telemetry, silent/offline
recording, ownership, or intent. The TX handoff is a bounded carrier-frequency
test, not capture/replay or blocking.
