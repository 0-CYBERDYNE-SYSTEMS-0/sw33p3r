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

**BLE (`sniffbt`) — redacted format fixture:**
```text
Started BLE Scan
>  RSSI: -37 Device: 02:44:55:66:77:01 RSSI: -50 Device: 02:44:55:66:77:02#stopscan
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

## On-device CLI help transcript (2026-09-20, mntm-012 test rig)

Captured from the BFFB ESP32 by a prior Room Sweep session's raw UART dump
(`#help` echo + response; ring-bounded, so ordering is start/end of the help
block). This is the command set for the tested firmware build, not the
upstream wiki's. Device identifiers and room-specific observations are not
retained in this repository:

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

## Probe-format fixture (2026-09-20, USB-UART bridge, mntm-012 test rig)

The examples below preserve the verified UART formats while redacting all
device-derived addresses, names, and room-specific SSIDs. Raw captures remain
local-only and are not part of this repository.

**Bridge fact:** with the bridge active, the Flipper's single CDC port IS the
ESP32 CLI (`stopscan` → `#stopscan\r\nStopping WiFi tran/recv\r\n> `). The
Flipper text CLI/RPC is unavailable while bridged. Exit the bridge on-device
(long BACK) to restore.

**`sniffbeacon` (format fixture):** prompt `> ` prefixes only the FIRST
line after the banner; later lines are bare:
```text
> RSSI: -54 Ch: 2 BSSID: 02:33:44:55:66:01 ESSID: LAB_NET_ALPHA
RSSI: -53 Ch: 2 BSSID: 02:33:44:55:66:01 ESSID: LAB_NET_ALPHA
```

**`scanap` (exists on this build; format DIFFERS from upstream claim):**
the capability data is a SEPARATE FOLLOW-UP LINE, not a suffix on the AP line,
and the AP line carries a TRAILING SPACE after the SSID:
```text
> RSSI: -47 Ch: 5 BSSID: 02:33:44:55:66:02 ESSID: LAB_NET_BETA␠
Beacon: 11 14 1 100040
```
The four `Beacon:` values are NOT interpreted here (no upstream source pulled
for this build); any future parser must treat them as opaque. The old
"`ESSID: Name 00 00` same-line suffix" format was NOT observed on this build.

**`list -a`:** `[0][CH:5] LAB_NET_BETA <raw byte>` — index, channel, SSID,
then a raw byte (binary, garbles UTF-8). Do not parse the byte.

**`scansta`:** requires `scanap` FIRST — without it:
`The AP list is empty. Scan APs first with scanap`. With it, stations
associate against that AP list. `list -s` prints `0 selected` when empty.
Station line format still UNOBSERVED (no client associated during capture).

**`sniffraw` (discovered — per-frame transmitter radar):** one line per
received 802.11 frame on the current channel, INCLUDING stations, not just APs:
```text
RSSI: -44 Ch: 6 BSSID: 02:22:33:44:55:01
RSSI: -32 Ch: 6 BSSID: 02:22:33:44:55:02
```
This is the command used to exercise non-AP transmitter parsing. The
"BSSID" label is the firmware's; the address is the TRANSMITTER of the frame
(AP or station).

**`sniffbt` (format fixture):** abutting-record stream confirmed, and the
trailer is misleading: `Scan complete! Found 0 devices` counts SAVED devices
(SavePCAP setting), not observed ones — the `>  RSSI: … Device: …` stream is
the payload. The stream may include the attached test rig itself; exact names
and addresses are intentionally omitted.

**`sniffbt -t airtag|flipper|flock|meta`:** all four ACCEPTED syntactically
(banner identical), but the output stream was NOT filtered — the same
heterogeneous device mix appears under every filter. **Tracker filtering is
NOT demonstrable on this
build over serial.** Any Phase 1 claim must be gated on this or pivoted
host-side.

**`sniffprobe`:** starts (`Starting Probe sniff. Stop with stopscan`).
**RESOLVED 2026-09-21 — format validated with a redacted fixture** (a 60 s
window supplied six redacted probe records; see the fixture section below).
The 2026-09-20
6 s window was simply too short: probes are bursty.

**`sniffesp`:** starts (`Starting Espressif device sniff…`) — no Espressif
beacons in window, format unobserved. **`sniffpwn`:** `Starting Pwnagotchi
sniff…` — a hostile-tooling detector (other people's offensive WiFi gear).
**`sigmon`:** `Starting Signal Strength Scan…` — no output in window.
**`settings`:** dumps name/type/value blocks (ForcePMKID, ForceProbe,
SavePCAP, EnableLED — the tested fixture records these as `true`).
**`sniffdeauth`, `scanall`:** confirmed ABSENT from this build's CLI.

## Format pins from upstream source (2026-09-20, Phase 5/10)

Two of the three formats below were pinned from the upstream ESP32Marauder
source (github.com/justcallmekoko/ESP32Marauder) because no live emitter
appeared during the probe battery. **`sniffprobe` has since been validated
against a redacted fixture (2026-09-21), and the upstream pin is kept for
cross-reference. `sniffesp` /
`sniffpwn` remain annotated "format from upstream source; not yet observed
live on this build"** — the truth contract requires it until a live capture
confirms otherwise; the 2026-09-21 TOOL window produced zero records (no
hostile-tooling emitter in the room).

### `sniffprobe` — **FORMAT VALIDATED WITH A REDACTED FIXTURE 2026-09-21** (upstream source agrees)

**Redacted fixture** (derived from an mntm-012 test-rig capture, BFFB over
USART @115200; raw identifiers intentionally omitted):

```
#sniffprobe
Starting Probe sniff. Stop with stopscan
> RSSI: -60 Ch: 2 Client: 02:11:22:33:44:01 Requesting:
RSSI: -67 Ch: 11 Client: 02:11:22:33:44:02 Requesting:
RSSI: -91 Ch: 11 Client: 02:11:22:33:44:03 Requesting: SYNTH_CAMERA_NET
RSSI: -59 Ch: 1 Client: 02:11:22:33:44:04 Requesting:
RSSI: -50 Ch: 8 Client: 02:11:22:33:44:05 Requesting:
```

Confirmed by that redacted fixture:

- the line shape is `RSSI: -NN Ch: N Client: <mac> Requesting: <ssid>` (the
  v1.9.1 variant, with the `RSSI: ` prefix present),
- the requested SSID is the last field and may be EMPTY (a client probing for
  a hidden network) — the app maps empty to its `Hidden/unknown` label,
- the record carries NO target BSSID on this build, so the hidden-SSID repair
  cannot fire from a probe whose target is a *specific* AP; a named probe still
  carries the network NAME in cleartext,
- the `> ` prompt prefixes the first record after the command echo only, and
  the scan scaffolding (`#sniffprobe`, `Starting Probe sniff...`, `Stopping
  WiFi tran/recv`) is never mistaken for a record.
- A named synthetic fixture (`SYNTH_CAMERA_NET`) demonstrates that a client
  may announce a remembered network name in a probe request.

Pinned from `esp32_marauder/WiFiScan.cpp`, `beaconSnifferCallback`,
`WIFI_SCAN_PROBE` branch (probe requests, `payload[0] == 0x40`). The print
block, quoted verbatim:

```cpp
Serial.print(F("RSSI: "));                 // v1.9.1 only — master omits this prefix
Serial.print(snifferPacket->rx_ctrl.rssi);
Serial.print(F(" Ch: "));
Serial.print(snifferPacket->rx_ctrl.channel);
Serial.print(F(" Client: "));
char addr[] = "00:00:00:00:00:00";
getMAC(addr, snifferPacket->payload, 10);
Serial.print(addr);
Serial.print(F(" Requesting: "));
// ...then payload[25] SSID octets from payload[26+i], then Serial.println()
```

Resulting line shapes (both accepted by the parser):

```text
RSSI: -52 Ch: 6 Client: aa:bb:cc:dd:ee:ff Requesting: SomeNet   (v1.9.1)
-52 Ch: 6 Client: aa:bb:cc:dd:ee:ff Requesting: SomeNet         (master)
```

Empty SSID prints as `<hidden>` on master (`checkEmptyProbe()`) and as
nothing on v1.9.1. **The pinned lines carry NO target BSSID** — only the
client address. Consequence: the Phase 5 hidden-SSID repair contract
(`room_sweep_probe_names_hidden`) is implemented and host-tested, but on
the pinned formats it can never fire (it requires a BSSID in the line). The
parser honors an explicit `BSSID:` key so a future build that prints it
works unchanged.

### `sniffpwn` — format from upstream source; not yet observed live on this build

Pinned from `WiFiScan.cpp` `beaconSnifferCallback` →
`processPwnagotchiBeacon` (Pwnagotchi beacons carry a JSON payload; the
source MAC must match Marauder's `de:ad:be:ef:de:ad` sentinel). Verbatim:

```cpp
// v1.9.1:
Serial.print(F("Pwnagotchi Name: ")); Serial.println(name);
Serial.print(F("Pwnd Totals: "));     Serial.println(pwnd_tot);
// master:
Serial.print(F("Name: "));            Serial.println(name);
Serial.print(F("Pwnd #: "));          Serial.println(pwnd_tot);
```

Two lines per Pwnagotchi beacon: `Pwnagotchi Name: <name>` /
`Pwnd Totals: <n>` (v1.9.1) or `Name: <name>` / `Pwnd #: <n>` (master). No
RSSI, no MAC. Noise lines `JSON payload not found.` and `Not a Pwnagotchi
frame.` are printed by the same callback and must be rejected by the parser
(they carry no `Name:` key).

### `sniffesp` — banner only on every current upstream tree; historical shape only

**No per-frame serial output exists in any current upstream tree
(v1.7.2 … master):** `WiFiScan::StartScan()` has NO `WIFI_SCAN_ESPRESSIF`
branch, so `sniffesp` prints its banner
(`Starting Espressif device sniff. Stop with stopscan`, `CommandLine.cpp`)
and never streams lines. This fully explains the Phase 0 observation
("starts cleanly, zero lines").

The only format that can be cited is the historical
`espressifSnifferCallback` (commit `1a41361`, 2020-07-02 "Add detect
espressif devices", removed in `bc3038c` "Trim fat"), which printed:

```text
RSSI: <rssi> Ch: <ch> BSSID: <addr>
```

format from upstream source (historical commit only); not yet observed live
on this build — and unreachable on current upstream builds. The Room Sweep
parser accepts this documented shape; on this firmware the ESP half of a
TOOL window will honestly stay empty.

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
