# Room Sweep — Developer Handoff

**Date:** 2026-08-01
**Version:** v3.0.1 (commit 8829c77)
**Branch:** main
**Firmware:** Momentum mntm-012, API 87.1, target 7
**Build tool:** ufbt (pyenv shim at ~/.pyenv/shims/ufbt)

---

## Current State

Fully functional 6-tab Flipper Zero app. Deployed to device and running.
Passes clean build with -Werror. 44/44 NMEA host tests pass.

| Feature | Status |
|---------|--------|
| RF Survey (16-point) | Working — live RSSI bars |
| RF Band Sweep (3 bands) | Working — needs field verification |
| RF Peak Refine | Working — needs field verification |
| WiFi AP scanner | Implemented — needs BFFB hardware test |
| BLE device scanner | Implemented — needs BFFB hardware test |
| GPS passive listener | Working — stale-fix + GLL bugs fixed |
| TX (safety-gated tab) | Implemented — needs field verification |
| Audio feedback | Implemented — continuous Geiger model |
| Vibro feedback | Implemented — heartbeat + edge + lock |
| Settings overlay | Fixed (was crashing, InputTypePress bug) |
| Info tab | Working — live state card |

---

## File Layout

```
sw33p3r/
├── room_sweep.h        # Enums, structs, constants (SweepMode, RfSubMode, TxState, etc.)
├── room_sweep.c        # Main app (~1550 lines): threads, draw, input, UART, feedback
├── nmea.c              # NMEA sentence parser (GGA, RMC, GLL, ZDA, GSV)
├── nmea.h              # NMEA types and function decls
├── application.fam     # ufbt manifest (appid=room_sweep, stack=4096)
├── tests/
│   └── test_nmea.c     # Host tests (44 checks, cc -std=c11)
├── MISSION.md          # Product contract + TX safety spec
├── USER_GUIDE.md       # End-user manual (every button/setting)
├── FLIPPER_PITFALLS.md # 14 verified API pitfalls (READ THIS FIRST)
├── DESIGN.md           # Architecture rationale from QA audit
├── fix_plan.md         # Work queue (done/next)
├── handoff.md          # This file
└── dist/               # Build output (room_sweep.fap)
```

---

## Build & Deploy

```bash
# Build (from project root)
ufbt

# Deploy workflow (Flipper must be plugged in via USB):
# 1. Close running app
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodemflip_XXXX01', 115200, timeout=2)
s.write(b'input send back long\r\n')
time.sleep(2)
s.close()
"

# 2. Upload (will error on RPC close step — ignore it, upload succeeds)
ufbt launch

# 3. Launch via CLI
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodemflip_XXXX01', 115200, timeout=3)
s.write(b'loader open /ext/apps/Tools/room_sweep.fap\r\n')
time.sleep(1)
s.close()
"
```

**Known issue:** `ufbt launch` hangs on the "Closing current app" RPC step because
the app requires manual back-press. The FAP upload completes before this error.
Workaround: close via CLI first, then `ufbt launch`, then launch via CLI if needed.

**Serial port:** `/dev/cu.usbmodemflip_XXXX01` at 115200 baud.
If "Resource busy": `lsof /dev/cu.usbmodemflip_XXXX01` → kill the PID.

---

## Architecture Notes

### Threading Model
- **Main thread:** GUI event loop (input + view_port_update)
- **RF sweep thread:** `rf_sweep_thread` — runs continuously, handles all 3 sub-modes
- **TX thread:** `tx_thread` — spawned on Long-OK, auto-exits after duration
- **UART:** callback-driven via `furi_hal_serial` (no dedicated thread)

### Key Patterns
- All shared state accessed from main thread only (threads write to app struct fields, main reads them in draw)
- `NotificationSequence` = NULL-terminated array of `const NotificationMessage*` (NOT a struct)
- Force messages (`message_force_speaker_volume_setting_1f`, `message_force_vibro_setting_on`) bypass global mute
- Only these delays exist: 1, 10, 25, 50, 100, 250, 500, 1000 ms
- Input filtering: ONLY process `InputTypeShort` and `InputTypeLong` (line 1448). Never InputTypePress.

### TX Safety State Machine
```
DISARMED --[Short OK]--> ARMED --[Long OK]--> TRANSMITTING --[timer expires]--> DISARMED
                            |                                                       ^
                            +--[Short Back]------------------------------------------+
```
TX NEVER fires on tab entry. Multiple deliberate actions required.

### Marauder Protocol (BFFB ESP32)
- WiFi scan: send `scanap\r\n`, stream results until `stopscan\r\n`
- BLE sniff: send `sniffbt\r\n`, stream results until `stopscan\r\n`
- WiFi line format: `-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName 00 00`
- BLE line format: `-60 Device: DeviceName`
- Lines starting with `#` are command echoes — skip them
- No "done" marker — scans stream indefinitely until stopscan
- RSSI is the bare negative integer at the START of the line

---

## Open Items / Next Work

| Priority | Item | Notes |
|----------|------|-------|
| HIGH | Field-test audio near WiFi router | Verify Geiger clicks accelerate |
| HIGH | Field-test TX arm→transmit→auto-disarm | Verify safety gating on hardware |
| HIGH | Capture real BFFB output | Verify parser matches actual firmware |
| MED | Band sweep field verification | Test with known in-between signal |
| MED | WiFi/BLE scan timeout detection | If scan hangs >30s, show stale indicator |
| LOW | GPS C/N0 bars from GSV | Signal quality visualization |
| LOW | Configurable sweep step/dwell | User-adjustable in settings |

---

## Critical Pitfalls (summary — see FLIPPER_PITFALLS.md for full list)

1. **NotificationSequence is an array**, not a struct with .message_count
2. **Only discrete delays exist** (1,10,25,50,100,250,500,1000) — no message_delay_150
3. **No sequence_sound_off builtin** — must define custom seq_sound_stop
4. **InputTypePress fires BEFORE Short/Long** — processing both = double-fire crash
5. **-Wdouble-promotion is -Werror** — use integer math for ms→s display
6. **CC1101 has 3 bands with real gaps** — 300-348, 387-464, 779-928 MHz
7. **RX and TX share the CC1101** — never simultaneous
8. **ufbt launch RPC close hangs** — close app via CLI first

---

## Testing

```bash
# Host NMEA tests (44 checks)
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/test_nmea && /tmp/test_nmea

# Build verification
ufbt  # Must produce: Target: 7, API: 87.1, zero warnings
```

No device-side automated tests exist. All device verification is manual via:
- Serial CLI (input simulation, loader commands)
- Visual inspection of Flipper screen
- Audio/haptic observation

---

## Handoff Rule

Three verification levels — never conflate them:
1. **Source-backed:** code path exists and compiles
2. **Host-verified:** passes host tests (only NMEA currently)
3. **Device-verified:** confirmed on physical Flipper hardware

Current device-verified: RF survey bars, settings menu, app launch/exit.
Everything else is source-backed or host-verified only.
