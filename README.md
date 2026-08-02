# Room Sweep

Room Sweep is a Flipper Zero external app for receive-side RF surveying,
WiFi/BLE scan control through an attached UART device, passive GPS display, and
a safety-gated TX control tab.

## Current controls

- Six tabs: RF Survey/Sweep/Peak, WiFi, BLE, GPS, TX, and Info.
- Short Back opens Settings, closes Settings, or disarms TX.
- Long Back exits the app from every context.
- TX requires short-OK arming followed by long-OK confirmation. Do not transmit
  unless you have authorization and intend to do so.

## Build and host checks

```sh
ufbt
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/room_sweep_test_nmea
/tmp/room_sweep_test_nmea
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c -o /tmp/room_sweep_test_input_state
/tmp/room_sweep_test_input_state
python3 _verify_api.py
```

Deploy the built FAP with `ufbt launch` when a Flipper is connected. The
current verified artifact and device receipt are recorded in
`.omo/evidence/room-sweep-device-qa-final-2026-08-01.txt`.

Live BFFB result parsing, measured audio/haptic output, known-signal behavior,
and deliberate RF field transmission require separate hardware/field checks.
