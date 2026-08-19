# Room Sweep — Developer Handoff

**This file is a historical snapshot from 2026-08-01 (v3.0.1).**
It is not the current operator or architecture description.

Current sources:

- Operator: [`README.md`](README.md), [`USER_GUIDE.md`](USER_GUIDE.md)
- Control map: [`docs/room_sweep_control_map.html`](docs/room_sweep_control_map.html)
- Scope: [`MISSION.md`](MISSION.md)
- Agent brief: [`AGENTS.md`](AGENTS.md)

The live FAP has **7 tabs** (`RF Wi BT nR GP TX i`), stack 6 KiB, and
`room_sweep.c` is ~5790 lines. Do not copy tab counts or file lists from
the archive below.

---

**Archive date:** 2026-08-01
**Archive version:** v3.0.1 (HEAD d6182a1 plus verified working-tree fixes)
**Branch:** main
**Firmware:** Momentum mntm-012, API 87.1, target 7
**Build tool:** ufbt (pyenv shim at ~/.pyenv/shims/ufbt)
**Archive FAP SHA-256:** `7994ddea5961dc2889a0f767831fc3428b32ae6d1df9f048ec2f0a2fe91e853c`

---

## Archive state (2026-08-01)

Six-tab Flipper Zero app (nR / Waterfall / analyzer suite did not exist yet). The current working tree builds clean with -Werror,
passes 58/58 executable NMEA assertions plus 4/4 Back-state assertions, and has been
installed and traversed on the connected Flipper without a crash. Device proof
uses valid Press→Short/Long→Release input sequences; iPhone Mirroring visually
checked RF Survey, Settings, WiFi, BLE, GPS, TX, and Info after the final UI
spacing fixes.

| Feature | Status |
|---------|--------|
| RF Survey (16-point) | Working — live RSSI bars |
| RF Band Sweep (3 bands) | Device traversal verified — signal detection needs field verification |
| RF Peak Refine | Device traversal verified — signal detection needs field verification |
| WiFi AP scanner | Implemented — needs BFFB hardware test |
| BLE device scanner | Implemented — needs BFFB hardware test |
| GPS passive listener | Working — stale-fix + GLL bugs fixed |
| TX (safety-gated tab) | Arm/disarm/navigation verified — deliberate RF transmission needs field verification |
| Audio feedback | Implemented — continuous Geiger model |
| Vibro feedback | Implemented — heartbeat + edge + lock |
| Settings overlay | Fixed (was crashing, InputTypePress bug) |
| Info tab | Working — live state card |

---

## File Layout

```
sw33p3r/
├── room_sweep.h        # Enums, structs, constants (SweepMode, RfSubMode, TxState, etc.)
├── room_sweep.c        # Main app: threads, draw, input, UART, feedback
├── nmea.c              # NMEA sentence parser (GGA, RMC, GLL, ZDA, GSV)
├── nmea.h              # NMEA types and function decls
├── application.fam     # ufbt manifest (appid=room_sweep, stack=4096)
├── tests/
│   ├── test_nmea.c     # Host NMEA tests (58 executable assertions, cc -std=c11)
│   └── test_input_state.c # Host Back-routing tests (4 assertions)
├── room_sweep_input.h  # Back-routing decision table
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
# 1. Close a running app with a complete input sequence
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodemflip_XXXX01', 115200, timeout=2)
s.write(b'input send back press\r\n')
time.sleep(0.1)
s.write(b'input send back long\r\n')
time.sleep(0.1)
s.write(b'input send back release\r\n')
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

If `ufbt launch` reports that the current fullscreen app must be closed manually,
send the complete input sequence above, then rerun `ufbt launch`.

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
- UART ISR only enqueues bounded lines; the main loop parses them. RF/TX radio state is serialized with a mutex; GUI draws app state.
- `NotificationSequence` = NULL-terminated array of `const NotificationMessage*` (NOT a struct)
- Force messages (`message_force_speaker_volume_setting_1f`, `message_force_vibro_setting_on`) bypass global mute
- Only these delays exist: 1, 10, 25, 50, 100, 250, 500, 1000 ms
- Input filtering: ONLY process `InputTypeShort` and `InputTypeLong`. Never InputTypePress.

### TX Safety State Machine
```
DISARMED --[Short OK]--> ARMED --[Long OK]--> TRANSMITTING --[timer expires]--> DISARMED
                            |                                                       ^
                            +--[Short Back]------------------------------------------+
```
TX NEVER fires on tab entry. Multiple deliberate actions required.

### Marauder Protocol (JCMK BFFB = Dev Board Pro)
See `docs/BFFB_MOMENTUM.md` for wiki + source citations.
- UART: USART1 @ **115200**, `expansion_disable`, TX ends with **`\\n`** (companion style)
- WiFi: **`sniffbeacon`** (AP beacon path; not legacy `scanap`. `scanall` is AP+STA and not used here)
- BLE: **`sniffbt`** → `-60 Device: NameOrMac`
- GPS: **`nmea`** stream; GPS is on ESP32 only (BFFB wiki — not Flipper GPIO)
- Stop: **`stopscan`** (companion also uses `stopscan -f`)
- WiFi line: `-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName 00 00`
- No "done" marker; BLE RSSI updates for known devices are silent

---

## Open Items / Next Work

| Priority | Item | Notes |
|----------|------|-------|
| HIGH | Field-test audio near WiFi router | Verify Geiger clicks accelerate |
| HIGH | Field-test TX arm→transmit→auto-disarm | Verify safety gating on hardware |
| HIGH | Capture real BFFB output | Verify parser matches actual firmware |
| MED | Band sweep field verification | Test with known in-between signal |
| DONE | WiFi/BLE scan timeout detection | Implemented: no result lines for 30s shows ERR and permits recovery |
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
# Host NMEA tests (58 executable assertions)
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/test_nmea && /tmp/test_nmea

# Host Back-routing tests (4 assertions)
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c -o /tmp/room_sweep_input_test && /tmp/room_sweep_input_test

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
2. **Host-verified:** 58/58 executable NMEA assertions and 4/4 Back-routing assertions pass
3. **Device-verified:** confirmed on physical Flipper hardware

Archive device-verified (2026-08-01, six tabs then): no-crash traversal, settings controls,
Settings short-close, long-exit, TX disarm navigation, and app launch/exit.
Actual BFFB result parsing, audio/haptic observation, and deliberate RF
transmission remain user field tests because the required hardware/consent was
not available during this audit.
