# BFFB + Momentum + Marauder — source of truth

Authoritative hardware/UART references used by Room Sweep. Prefer these
over blog posts. Operator controls live in
[`room_sweep_control_map.html`](room_sweep_control_map.html),
[`USER_GUIDE.md`](../USER_GUIDE.md), and [`README.md`](../README.md).

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
>  RSSI: -37 Device: 02:5a:9c:11:22:33 RSSI: -50 Device: 02:5a:9c:44:55:66#stopscan
```
Also may appear as wiki form `-60 Device: name`. Updates for already-seen devices can be silent.

**Critical Room Sweep pitfalls (root cause of empty BLE list):**
1. Do **not** drop lines starting with `>` — BFFB prefixes every result with `> `.
2. Format is often `RSSI: -NN Device: …`, not bare `-NN Device:`.
3. Multiple records abut on one line with no `\n`; split on the next `RSSI:`.
4. `#stopscan` can abut the last MAC with no space.

**WiFi AP beacons — two DIFFERENT upstream formats (verified against
ESP32Marauder master `WiFiScan.cpp`, 2026-09-11):**

`scanall` → `RunAPScan` → `apSnifferCallbackFull` — prints the two raw
capability bytes after the ESSID (the `00 00`-style suffix):
```text
-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName 00 00
```

`sniffbeacon` → `RunBeaconScan(WIFI_SCAN_AP)` → `beaconSnifferCallback` —
prints **nothing after the SSID** (just the newline):
```text
-45 Ch: 6 AA:BB:CC:DD:EE:FF ESSID: NetworkName
```

Room Sweep sends **only `sniffbeacon`**, so live lines should not carry a
capability-byte suffix, and an SSID may legitimately end in short tokens
(e.g. `... ESSID: Lab AB CD`) that must be kept verbatim.

**GPS (`nmea`):** `Serial.println` of queued NMEA + `generateGXgga` / `generateGXrmc`.

## On-device CLI help transcript (2026-09-20, mntm-012 rig `Rug1k0`)

Captured verbatim from the BFFB ESP32 by a prior Room Sweep session's raw UART
dump (`#help` echo + response; ring-bounded, so ordering is start/end of the
help block). This is **this exact firmware build's** command set, not the
upstream wiki's:

```text
============ Commands ============
channel [-s <channel>]
settings [-s <setting> enable/disable>]/[-r]
clearlist -a/-c/-s
reboot
update -s/-w
ls <directory>
led -s <hex color>/-p <rainbow>
gpsdata
gps [-g] <fix/sat/lon/lat/alt/date/accuracy/text/nmea>
    [-n] <native/all/gps/glonass/galileo/navic/qzss/beidou>
         [-b = use BD vs GB for beidou]
nmea
evilportal [-c start [-w html.html]/sethtml <html.html>]
sigmon
scanap
scansta
sniffraw
sniffbeacon
sniffprobe
sniffpwn
sniffesp
ssid -r <index>
save -a/-s
load -a/-s
sniffbt
blespam -t <apple/google/samsung/windows/all>
btwardrive [-c]
sniffskim
==================================
```

Findings for the expansion spec (`specs/full-capability-expansion-2026-09-20.md`):

- **Present:** `sniffprobe` (hidden-SSID recovery), `scansta` (station/client
  list), `scanap`, `sniffraw`, `sniffesp` (other-Marauder/ESP32 beacon
  detection), `sigmon`, `sniffpwn`. Attack commands (`evilportal`, `blespam`,
  `btwardrive`, `sniffskim`) exist but are **never sent** per MISSION.md.
- **Absent from help:** `sniffdeauth` and `scanall` — Phase 10 (deauth
  detection) is an honest-skip candidate on this build; STA capture pivots to
  `scansta` + `list -s` instead of `scanall`.
- **`sniffbt` has no `-t` variant in this help** (upstream's
  `sniffbt -t airtag|flipper|flock|meta` is NOT confirmed here). Phase 1 must
  live-verify `-t` through the USB-UART bridge before building on it; if
  absent, tracker filtering moves host-side or is honestly skipped.
- **Still unverified:** serial output formats of `sniffprobe`/`scansta`/
  `sniffesp`/`sniffraw`. Raw capture requires the GPIO app's **USB-UART
  Bridge**, which refuses to start while any host session (RPC or CLI) is
  connected — start it on-device, then run the probe battery from the host.

## Live probe battery (2026-09-20, USB-UART bridge, mntm-012 rig `Rug1k0`)

Every command below was sent to the ESP32 through the GPIO app's USB-UART
bridge and the raw response captured (`.omo/evidence/marauder-probe-*`,
local-only). This is the verified format record for the expansion spec.

**Bridge fact:** with the bridge active, the Flipper's single CDC port IS the
ESP32 CLI (`stopscan` → `#stopscan\r\nStopping WiFi tran/recv\r\n> `). The
Flipper text CLI/RPC is unavailable while bridged. Exit the bridge on-device
(long BACK) to restore.

**`sniffbeacon` (live, re-verified):** prompt `> ` prefixes only the FIRST
line after the banner; later lines are bare:
```text
> RSSI: -54 Ch: 2 BSSID: c8:4f:86:db:66:9d ESSID: Gill Mechanical
RSSI: -53 Ch: 2 BSSID: c8:4f:86:db:66:9d ESSID: Gill Mechanical
```

**`scanap` (exists on this build; format DIFFERS from upstream claim):**
the capability data is a SEPARATE FOLLOW-UP LINE, not a suffix on the AP line,
and the AP line carries a TRAILING SPACE after the SSID:
```text
> RSSI: -47 Ch: 5 BSSID: ac:91:9b:d3:2b:fe ESSID: Verizon_3TFXQX␠
Beacon: 11 14 1 100040
```
The four `Beacon:` values are NOT interpreted here (no upstream source pulled
for this build); any future parser must treat them as opaque. The old
"`ESSID: Name 00 00` same-line suffix" format was NOT observed on this build.

**`list -a`:** `[0][CH:5] Verizon_3TFXQX <raw byte>` — index, channel, SSID,
then a raw byte (binary, garbles UTF-8). Do not parse the byte.

**`scansta`:** requires `scanap` FIRST — without it:
`The AP list is empty. Scan APs first with scanap`. With it, stations
associate against that AP list. `list -s` prints `0 selected` when empty.
Station line format still UNOBSERVED (no client associated during capture).

**`sniffraw` (discovered — per-frame transmitter radar):** one line per
received 802.11 frame on the current channel, INCLUDING stations, not just APs:
```text
RSSI: -44 Ch: 6 BSSID: 86:9a:c8:b0:3c:47
RSSI: -32 Ch: 6 BSSID: ea:5b:25:d1:62:14
```
This is the only command observed that surfaces non-AP transmitters live.

**`sniffbt` (live, re-verified):** abutting-record stream confirmed, and the
trailer is misleading: `Scan complete! Found 0 devices` counts SAVED devices
(SavePCAP setting), not observed ones — the `>  RSSI: … Device: …` stream is
the real payload. The Flipper's own BLE name (`Rug1k0`) appears in the stream:
the ESP32 hears the Flipper itself.

**`sniffbt -t airtag|flipper|flock|meta`:** all four ACCEPTED syntactically
(banner identical), but the output stream was NOT filtered — the same
heterogeneous device mix (Bose headphone, random MACs, the Flipper itself)
appears under every filter. **Tracker filtering is NOT demonstrable on this
build over serial.** Any Phase 1 claim must be gated on this or pivoted
host-side.

**`sniffprobe`:** starts (`Starting Probe sniff. Stop with stopscan`) but
produced ZERO lines in a 6 s window (no client probed during capture).
Command exists; line format remains UNPINNED — needs a longer window or a
deliberately probing device before a parser is written against it.

**`sniffesp`:** starts (`Starting Espressif device sniff…`) — no Espressif
beacons in window, format unobserved. **`sniffpwn`:** `Starting Pwnagotchi
sniff…` — a hostile-tooling detector (other people's offensive WiFi gear).
**`sigmon`:** `Starting Signal Strength Scan…` — no output in window.
**`settings`:** dumps name/type/value blocks (ForcePMKID, ForceProbe,
SavePCAP, EnableLED — all `true` on this rig).
**`sniffdeauth`, `scanall`:** confirmed ABSENT from this build's CLI.

## Momentum firmware (this project)

- Target: **Momentum mntm-012**, **API 87.1**, **target 7**
- Build: `ufbt` against Momentum SDK (`~/.ufbt`)
- RF: internal CC1101 bands 300–348 / 387–464 / 779–928 MHz (see FLIPPER_PITFALLS.md)
- Notifications: Momentum `NotificationSequence` = NULL-terminated message pointer array; force volume/vibro messages required

## What not to claim

- Do not claim Flipper GPIO GPS works on BFFB (wiki explicitly says it does not).
- ~~Do not send legacy `scanap` on modern Marauder (command absent).~~
  **Superseded 2026-09-20:** the on-device help transcript shows `scanap` (and
  `scansta`) DO exist on this build. The rule stays for a different reason:
  Room Sweep sends only `sniffbeacon` by policy — beacon-only capture needs no
  active scan, and `scanap` output carries the capability-byte suffix that
  already bit the parser once (see the superseded invariant in
  `specs/marauder-parser-2026-08-15.md`).
- Do not set Flipper USART to 9600 for BFFB GPS (wrong link; GPS is behind Marauder CLI).
