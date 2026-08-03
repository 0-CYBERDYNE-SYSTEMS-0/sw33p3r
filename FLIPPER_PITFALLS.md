# Flipper Zero Implementation Pitfall Log

**Project:** sw33p3r (Room Sweep)
**API:** Momentum mntm-012, API 87.1, Target 7
**Maintained by:** graph-engineering skill sessions

## How to use this file

Before using ANY Flipper API call in new code, verify it exists in this log's
"Verified API Symbols" section or in the SDK's `api_symbols.csv`. If you
discover a new pitfall, add it here immediately with the date and context.

**BFFB / Marauder:** see `docs/BFFB_MOMENTUM.md` (JCMK wiki + companion + CommandLine.h).

## Pitfall #17: BFFB switch map — ESP32 is NOT switched

**Date:** 2026-08-02  
**Source:** operator, physical board (supersedes wiki phrasing)

- **Top switch:** up = 900 MHz CC1101 path, down = 400 MHz CC1101 path.
- **Bottom switch:** up = CC1101 pair on Flipper SPI, down = nRF24 on SPI.
- The ESP32 is **always available** on UART 13/14 — it is not behind the
  switch. Marauder WiFi/BLE/GPS work in both bottom-switch positions.
- Consequence: bottom switch down (nRF24) costs only the external CC1101
  (`cc1101_ext` connect fails → app falls back to internal CC1101); WiFi,
  BLE, and GPS tabs are unaffected. The old wiki claim ("bottom switch
  selects NRF24 vs ESP32") is wrong for this board.

## Pitfall #15: BFFB GPS is not on Flipper USART

**Date:** 2026-08-01  
**Source:** https://github.com/justcallmekoko/ESP32Marauder/wiki/BFFB  

GPS is wired to the ESP32. Passive Flipper NMEA @ 9600 on GPIO will never work.
Must send Marauder CLI `nmea` / `gps -g nmea` at **115200** after `expansion_disable`.

## Pitfall #16: Modern Marauder has no `scanap`

**Date:** 2026-08-01  
**Source:** `esp32_marauder/CommandLine.h` (main), companion Scan menu  

Use **`sniffbeacon`** for AP beacons (Room Sweep). `scanall` is AP+STA and is not what this app sends. Companion TX terminator is **`\\n`**, not CRLF.

---

## Pitfall #1: NotificationSequence is an array typedef, NOT a struct

**Date:** 2026-08-01
**Severity:** Build-breaking

On Momentum firmware (API 87.1), `NotificationSequence` is defined as:
```c
typedef const NotificationMessage* NotificationSequence[];
```
It is NOT a struct with `.message_count` / `.message` fields (that's mainline).
Custom sequences are NULL-terminated arrays:
```c
static const NotificationSequence seq_example = {
    &message_force_speaker_volume_setting_1f,
    &message_note_c6,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
```

## Pitfall #2: Global mute overrides notification sequences

**Date:** 2026-08-01
**Severity:** Feature-breaking (silent app)

The user's Settings (speaker volume, vibro on/off) are applied to EVERY
sequence. `sequence_single_vibro` does NOTHING if user disabled vibro.
Sound is silent at volume 0.

**Fix:** Prepend force messages to custom sequences:
- `&message_force_speaker_volume_setting_1f` — forces max volume
- `&message_force_vibro_setting_on` — forces vibro on

## Pitfall #3: Available delay values are discrete

**Date:** 2026-08-01
**Severity:** Build-breaking

Only these delays exist: 1, 10, 25, 50, 100, 250, 500, 1000 ms.
There is NO `message_delay_150`, `message_delay_200`, etc.

## Pitfall #4: notification_message is async — sequences must be static

**Date:** 2026-08-01
**Severity:** Crash (use-after-free)

`notification_message()` queues the sequence and returns immediately.
If the sequence is on the stack, it's freed before playback completes.
ALL custom sequences MUST be `static const` (file-scope or static local).
Reference: flipperzero-firmware issue #2313.

## Pitfall #5: CC1101 has THREE separate bands, not continuous coverage

**Date:** 2026-08-01
**Severity:** Design constraint

Operating bands:
- 300–348 MHz
- 387–464 MHz
- 779–928 MHz

Gaps between bands are REAL. Cannot sweep 348–387 or 464–779 MHz.
Synthesizer resolution is ~400 Hz but useful RX bandwidth is much wider
(650 kHz with the OOK preset). Don't promise "full spectrum" scanning.

## Pitfall #6: RX and TX share the CC1101 — never run simultaneously

**Date:** 2026-08-01
**Severity:** Hardware fault

`furi_hal_subghz_rx()` and `furi_hal_subghz_start_async_tx()` use the same
radio. Running both causes undefined hardware state. The RX thread MUST
check `tx_active` and call `furi_hal_subghz_idle()` to yield.

## Pitfall #7: USART ownership ≠ BFFB connection

**Date:** 2026-08-01
**Severity:** UX/misleading state

`furi_hal_serial_control_acquire()` returning non-NULL only proves the
Flipper claimed the UART handle. It does NOT prove a BFFB ESP32 is attached.
No handshake exists by default. Label state as "UART acquired" not "BFFB
connected" until a handshake/response is verified.

## Pitfall #8: expansion_disable required before UART use

**Date:** 2026-08-01
**Severity:** Build/runtime

Must call `expansion_disable()` before acquiring USART, and
`expansion_enable()` after releasing. Otherwise the expansion module
service fights for the bus.

## Pitfall #9: All API calls must be in the export table

**Date:** 2026-08-01
**Severity:** Build-breaking (link error)

External apps (.fap) can only call functions listed in `api_symbols.csv`.
Before using any `furi_hal_*` or system function, verify:
```bash
grep "function_name" ~/.ufbt/current/api_symbols.csv
```
Unlisted functions cause link errors at build time.

## Pitfall #10: -Werror is on — implicit float→double promotion fails

**Date:** 2026-08-01
**Severity:** Build-breaking

`-Wdouble-promotion` is treated as error. Avoid mixing float and double
in expressions. Use integer math or explicit casts for display formatting.
Example: `(unsigned long)(ms / 1000)` instead of `(double)ms / 1000.0`.

## Pitfall #11: Display is 128x64, FontKeyboard is smallest

**Date:** 2026-08-01
**Severity:** Design constraint

Available fonts: FontPrimary (bold ~12px), FontSecondary (~11px),
FontKeyboard (~7px), FontBigNumbers (large digits only).
At FontKeyboard, ~25 chars fit per line, ~9 rows total.
Every pixel matters. Tab strips, status indicators, and content compete.

## Pitfall #12: Thread stack sizes are critical

**Date:** 2026-08-01
**Severity:** Crash (stack overflow)

RF sweep thread uses 2048 bytes. `snprintf` with large buffers on the
stack in a thread can overflow. Keep stack-local buffers small (< 64 bytes)
in threads. The main app stack is 4096 (set in application.fam).

## Pitfall #13: ufbt setup and build

**Date:** 2026-08-01
**Severity:** Onboarding

```bash
pip3 install ufbt
cd /path/to/app   # must contain application.fam
ufbt              # builds dist/room_sweep.fap
ufbt launch       # uploads + launches (app must not be running)
```
First run downloads SDK + toolchain (~200MB). SDK goes to `~/.ufbt/current/`.

## Pitfall #14: App must be closed before `ufbt launch`

**Date:** 2026-08-01
**Severity:** Deploy failure

If the app is already running, `ufbt launch` fails with "has to be closed
manually." Fix: send `input send back long` via serial CLI first:
```python
import serial
s = serial.Serial('/dev/cu.usbmodemflip_XXX', 115200, timeout=2)
s.write(b'input send back long\r')
```

---

## Verified API Symbols (used in this project)

All verified present in api_symbols.csv for API 87.1:

| Symbol | Purpose |
|--------|---------|
| furi_hal_subghz_reset | Reset CC1101 |
| furi_hal_subghz_load_custom_preset | Load OOK 650kHz preset |
| furi_hal_subghz_set_frequency_and_path | Tune + set RX/TX path |
| furi_hal_subghz_rx | Enter RX mode |
| furi_hal_subghz_get_rssi | Read RSSI (float, dBm) |
| furi_hal_subghz_idle | Enter idle (yield radio) |
| furi_hal_subghz_sleep | Low-power sleep |
| furi_hal_subghz_start_async_tx | Start async TX with callback |
| furi_hal_subghz_stop_async_tx | Stop async TX |
| furi_hal_serial_control_acquire | Acquire UART handle |
| furi_hal_serial_control_release | Release UART handle |
| furi_hal_serial_init | Init UART at baud |
| furi_hal_serial_deinit | Deinit UART |
| furi_hal_serial_async_rx_start | Start async RX with callback |
| furi_hal_serial_async_rx_stop | Stop async RX |
| furi_hal_serial_async_rx | Read one byte (ISR context) |
| furi_hal_serial_tx | Transmit bytes |
| furi_hal_subghz_is_frequency_valid | Check freq in range |
| notification_message | Send async notification |
| notification_message_block | Send blocking notification |
| message_force_speaker_volume_setting_1f | Force max volume |
| message_force_vibro_setting_on | Force vibro on |
| message_vibro_on / message_vibro_off | Vibro control |
| message_sound_off | Stop sound |
| message_delay_{1,10,25,50,100,250,500,1000} | Delays |
| message_note_{c0..b8} | Chromatic notes |
| expansion_disable / expansion_enable | UART bus control |
| furi_thread_alloc_ex / start / join / free | Thread management |
| furi_mutex_alloc / acquire / release / free | Mutex |
| furi_message_queue_alloc / put / get / free | Message queue |
| view_port_alloc / free | Viewport lifecycle |
| gui_add_view_port / gui_remove_view_port | GUI registration |
| furi_record_open / close | Service records |
| level_duration_make | TX callback return value |
