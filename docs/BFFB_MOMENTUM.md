# BFFB + Momentum + Marauder — source of truth

Authoritative references used by Room Sweep. Prefer these over blog posts.

## Hardware: Just Call Me Koko BFFB

Wiki: https://github.com/justcallmekoko/ESP32Marauder/wiki/BFFB

| Fact | Implication for Room Sweep |
|------|----------------------------|
| ESP32 runs **Marauder Dev Board Pro** firmware (`_marauder_dev_board_pro.bin`) | CLI command set = current ESP32Marauder `CommandLine.h` |
| **GPS is wired to the ESP32 only**, not Flipper GPIO | Stock Flipper GPS apps will **not** see BFFB GPS. Must use Marauder CLI (`nmea`, `gps -g …`) over UART |
| Bottom SPI switch silkscreen **nRF24 ↔ CC1101** (no “ESP32” label; wiki “NRF24 vs ESP32” is wrong) | **SPI mux only.** Operator-verified: **up = CC1101**, **down = nRF24**. ESP32 is on **UART 13/14**, not SPI — Marauder WiFi/BLE/GPS work in **both** positions |
| Top switch **400 ↔ 900** | **up = 900 MHz**, **down = 400 MHz** external CC1101. Irrelevant for BLE/WiFi |
| Dual CC1101 + nRF24 on Flipper **SPI** | Room Sweep RF prefers Momentum **`cc1101_ext`**. External CC1101 needs bottom **up (CC1101)**. Falls back to internal if not detected (e.g. nRF24 selected) |
| Official Flipper UI | [Marauder Companion](https://github.com/0xchocolate/flipperzero-wifi-marauder) |

## GPS paths

1. **GPIO LPUART (primary):** Flipper pins **15/16** @ **9600** (then 115200). Momentum: MNTM → Protocols → GPIO Pins → **NMEA GPS UART = Extra 15,16**.
2. **Marauder `nmea` (fallback):** USART 13/14 if GPIO silent.

## UART path (Momentum FAP)

Matches companion `wifi_marauder_uart.c` / `wifi_marauder_app.c`:

1. `expansion_disable()` so expansion protocol does not own USART
2. `furi_hal_serial_control_acquire(FuriHalSerialIdUsart)`
3. `furi_hal_serial_init(handle, 115200)`
4. `furi_hal_serial_async_rx_start(...)`
5. On exit: stop RX, deinit, release, `expansion_enable()`

**Baud:** 115200 (companion `#define BAUDRATE (115200)`).  
Not GPS module baud — that is internal ESP32↔module (Marauder probes 9600→115200 on Serial2).

**Line ending:** Marauder `Serial.readStringUntil('\n')` + `trim()`. Companion TX is `command + "\n"`. Room Sweep sends `"\n"`.

## Marauder CLI commands we use

From `esp32_marauder/CommandLine.h` + companion menu (current main):

| App action | CLI | Notes |
|------------|-----|--------|
| WiFi AP meter | `sniffbeacon` | `SNIFF_BEACON_CMD` → `WIFI_SCAN_AP` — Serial prints `-RSSI Ch: n MAC ESSID:` (what Room Sweep parses). `scanall` is AP+STA; legacy `scanap` removed from CLI. |
| BLE sniff | `sniffbt` | `BT_SNIFF_CMD` → `BT_SCAN_ALL`. Variants: `sniffbt -t airtag|flipper|flock|meta` |
| Stop | `stopscan` | Companion Back. Force: `stopscan -f` |
| GPS stream | `nmea` | Companion “NMEA Stream” → `WIFI_SCAN_GPS_NMEA` → `RunGPSNmea()` ~1 Hz |
| GPS one-shot | `gps -g nmea` | Emits synthetic GGA+RMC via `sendSentence` |
| GPS status | `gps -g fix\|sat\|lat\|lon\|…` | Human text, not required for our NMEA parser |

### Output formats (WiFiScan.cpp)

**BLE (`sniffbt`) — live BFFB capture (uart-4.txt):**
```text
Started BLE Scan
>  RSSI: -37 Device: f4:0c:6d:d1:90:07 RSSI: -50 Device: 4e:81:fa:17:67:1a#stopscan
```
Also may appear as wiki form `-60 Device: name`. Updates for already-seen devices can be silent.

**Critical Room Sweep pitfalls (root cause of empty BLE list):**
1. Do **not** drop lines starting with `>` — BFFB prefixes every result with `> `.
2. Format is often `RSSI: -NN Device: …`, not bare `-NN Device:`.
3. Multiple records abut on one line with no `\n`; split on the next `RSSI:`.
4. `#stopscan` can abut the last MAC with no space.

**WiFi AP beacon path (`scanall` / `sniffbeacon`):**
```text
-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName 00 00
```

**GPS (`nmea`):** `Serial.println` of queued NMEA + `generateGXgga` / `generateGXrmc`.

## Momentum firmware (this project)

- Target: **Momentum mntm-012**, **API 87.1**, **target 7**
- Build: `ufbt` against Momentum SDK (`~/.ufbt`)
- RF: internal CC1101 bands 300–348 / 387–464 / 779–928 MHz (see FLIPPER_PITFALLS.md)
- Notifications: Momentum `NotificationSequence` = NULL-terminated message pointer array; force volume/vibro messages required

## What not to claim

- Do not claim Flipper GPIO GPS works on BFFB (wiki explicitly says it does not).
- Do not send legacy `scanap` on modern Marauder (command absent).
- Do not set Flipper USART to 9600 for BFFB GPS (wrong link; GPS is behind Marauder CLI).
