# Room Sweep — Operator Guide

Receive-side room survey on Flipper Zero + optional BFFB (Marauder / dual
CC1101 / nRF24). Firmware target: **Momentum mntm-012**, API **87.1**.

This guide matches the **current** app behavior (7 tabs, Hold-▶ pages,
analyzer age-out, FullSweep auto-save, tactile tap/hold feedback).

One-page map: [`docs/room_sweep_control_map.html`](docs/room_sweep_control_map.html).

## What each tab actually tells you

Nothing in this app identifies a device. It reports the presence of RF
activity and whatever a device advertises about itself.

| Tab | Tells you | Cannot tell you |
|-----|-----------|-----------------|
| RF | Energy above -75 dBm on 20 fixed presets | What is transmitting; only 3 CC1101 bands, with gaps |
| Wi | Source-dependent (Settings → Wi Src): AP beacons; every 802.11 transmitter on channel (RAW); client probe requests, can name hidden networks (PROBE); devices advertising like attack tooling (TOOL) | Device type or owner; anything on 5 GHz; TOOL rows never prove intent |
| BT | BLE ads: MAC, RSSI, count | Device type, intent, or owner |
| nR | 2.4 GHz channel energy (RPD hits) | Wi-Fi vs BLE vs nRF24 vs microwave leakage |
| GP | Position, speed, course, mark distance | Anything about RF sources |
| TX | Bounded 1–10 s carrier test state | Any receive-side finding |
| i | Radio path, session state, limits | — |

## Remember these controls

| Input | Action |
|-------|--------|
| Short Left / Right | Change **tab** |
| Hold Left | **Analyzer** on/off (RF, Wi-Fi, BLE, nRF24) |
| Hold Right | **Page** inside the current tab (or analyzer) |
| Up / Down | Browse list / RF sub-mode / GPS-Info pages |
| Hold Up / Down | **RF lock card** open/close (RF tab) |
| Short OK | Main action (scan, start, arm, mark; on Wi-Fi/BLE lists: **lock/unlock** selected row) |
| Hold OK | Rescan (Wi-Fi/BLE) / lock target (RF) — or **transmit** only if TX already armed. On GPS: **NMEA retry** |
| Short Back | Settings (or disarm TX first) |
| Hold Back | Exit app |

Footer lines on each page state what **L / R / OK** do there.

**Tactile feedback:** with **Settings → Vibro** ON, a short tap gives one
soft pulse and a hold gives a double pulse on release, so holds register
distinctly. Sound/Vibro state lives in **Settings → Feedback** (no glyphs on
tab headers).

## Tabs

### RF

**Up/Down** = Survey / Sweep / Peak / Waterfall / **Watch**. **Hold Up/Down**
= open the lock/map card from any sub-mode (Up/Down returns to the map).

| Sub-mode | OK | Hold OK | Hold Right |
|----------|-----|---------|------------|
| Survey | — (continuous presets) | Lock qualified hit | Map ↔ **Lock card** |
| Sweep | Start/cancel band sweep | Lock result | **Band** step while idle on map |
| Peak | Start/cancel refine | Lock result | Map ↔ **Lock card** |
| Waterfall | — (passive history) | Lock qualified hit | Map ↔ **Lock card** |
| Watch | — (passive, locked) | Lock qualified hit | Map ↔ **Lock card** |

Hold Left = analyzer (Hunt meter / Field spectrum / Radar / Big meter).

### Survey presets (RF sub-mode)

Twenty fixed presets, all inside the CC1101's three real bands. The strip
labels five of them (every fourth: `304 390 434 450 902`); the baseline CSV
rows and the band sweep name every preset individually.

Four presets were added on 2026-09-21 for the bands a room survey was
blindest to:

| Preset | Why it is there |
|--------|-----------------|
| `850` | Bottom of the cellular 850 band — **cellular uplink overlap; energy here may be a SIM tracker**, but the CC1101 cannot decode cellular: this is energy in a band, nothing more |
| `880` | GSM-900 uplink — same caveat as `850` |
| `902` | Bottom edge of the US 902–928 ISM band (LoRa/mesh/remotes) |
| `927` | Top of the US ISM band (LoRa US channel 64, utility meters) |

**Cellular-edge truth:** `850`/`880` are the closest this hardware gets to
cellular. A phone or a cellular tracker in the room can raise them; so can a
baby monitor, a wireless mic, or the noise floor. There is no identity here —
only "something is on in that band".

### Waterfall (RF sub-mode)

Scrolling spectrum history of the 20 presets — newest column on the right,
~2.5 snapshots/second, per-channel **peak-hold dots** on the right edge and
a dashed alert-threshold line. Purely passive: read the room's RF activity
over the last ~10 seconds at a glance.

### Watch (RF sub-mode)

**Burst watch** locks the receiver to ONE frequency — the last qualified
peak if fresh, otherwise the nearest preset — and stops hopping. It measures
**bursts**: stretches of energy above the −75 dBm threshold, closed after
600 ms of quiet. The page shows `BURSTS n` (closed bursts), `LAST s`
(seconds since that frequency was last above the threshold) and `DUTY n%`
(share of the watch window with energy). This is timing evidence — the
on/off pattern a duty-cycled transmitter leaves — and nothing more: it is
still just energy on one frequency. RSSI is **not distance** (a stronger
bar is not a nearer device), and no decoder is involved, so Room Sweep
cannot tell you *what* is transmitting. Each closed burst writes one
`observation` CSV row (identifier `WATCH`) with its duration, peak RSSI and
`duty=/bursts=` counts. Leaving the sub-mode or re-entering it re-locks the
frequency and restarts the counters.

### Wi-Fi

The Wi tab has ONE scan source at a time, chosen in **Settings → Wi Src**
(default **BEACON** on every launch — nothing is persisted):

| Wi Src | Marauder command | What it shows |
|--------|------------------|---------------|
| **BEACON** | `sniffbeacon` | Wi-Fi access points (beacons only) — the classic list, unchanged |
| **RAW** | `sniffraw` | **every 802.11 transmitter heard on the current channel, stations included** — laptops, phones, printers, anything sending |
| **PROBE** | `sniffprobe` | devices announcing networks they remember; can name hidden networks |
| **TOOL** | `sniffesp` + `sniffpwn` (alternating windows) | devices advertising like attack tooling — not proof of intent |

Switching the source stops the running scan first (only one Marauder scan
runs at a time). The header shows which source owns the tab: `WI SCAN` /
`WI RAW` / `WI PROBE` / `WI TOOL`. RAW/PROBE/TOOL scans keep the BEACON AP
list in place, so cloned-SSID `!` marks still work after switching back.
On RAW/PROBE/TOOL, **OK restarts the scan** — locking is a BEACON/AP
feature. TOOL alternates its two commands window by window; `sniffesp`
produces no lines on current Marauder builds (pinned from upstream source,
see `docs/BFFB_MOMENTUM.md`), so an empty ESP half is normal, not a fault.

#### Source truth lines

- **RAW:** a row is "a transmitter heard at this RSSI" — never "a person's
  phone" or "the camera". There is no name to read; only the address.
- **PROBE:** a probe request is a client *asking* for a network it
  remembers, in cleartext. The name is the strongest identity claim in the
  app — and it is still just a name.
- **TOOL:** "advertising like attack tooling" means the device matches a
  class of known offensive-WiFi gear (Marauder/ESP32 boards, Pwnagotchi
  peers). It is a classification of the *advertisement*, never proof of
  intent — a Pwnagotchi is also a toy, and an ESP32 is also a dev board.

| Page (Hold Right) | Shows |
|-------------------|--------|
| **Detail** | One selected AP: SSID, `Vendor:` (curated OUI label, `unlisted`, or `randomized` for a self-assigned MAC), RSSI + observed range (`-38dBm -72..-38 n14`), MAC, a `HINT: x? (name guess)` line when the SSID matches a device-class pattern, a `SAME NAME ON n BSSIDS` badge when two or more APs share that exact name, or, on the opt-in watchlist, `ON WATCHLIST` / `flagged: session N` |
| **List** | Up to 5 rows RSSI + name + right-aligned `?` hint tag; duped-SSID rows carry a `!` before the tag column; watchlisted rows carry a `WATCH` badge at the right edge |
| **Help** | Short control reminder |

- **Up/Down:** select AP
- **OK:** lock/unlock the selected AP (starts the first scan when the list is empty)
- **Hold OK:** rescan (clears the AP table and any lock)
- **Hold Up:** with the watchlist ON, flag the selected AP (detail page)
- **Hold Left:** analyzer for the **selected/locked** AP
- Scan window: **Settings → ScanWin** (15/30/60 s)

**Vendor truth:** `Vendor:` names the company that registered the MAC's
3-byte OUI prefix — a short curated, IEEE-verified table, not an exhaustive
database, so real devices read `unlisted`. `randomized` means the first
octet marks the address as locally administered (privacy phones and spoofed
MACs) — it says nothing about the vendor. **HINT truth:** the hint is a
pattern match on the *advertised name* — a guess with a `?` on purpose; the
placeholder `Hidden/unknown` never produces one.

**Stats truth:** the detail line `-38dBm -72..-38 n14` is the strongest,
weakest, and rounded-average signal heard from *that row this session*, and
`n14` is the beacon count. RSSI is not distance; a steady number is not a
promise.

**`SAME NAME ON n BSSIDS` / `!` truth:** two or more access points
advertising the exact same name (byte-for-byte) is a *lead* — an evil twin,
a cloned hotspot, or simply enterprise mesh / roaming, which legitimately
shares one name across many radios. It is never, by itself, an attack.

**`WATCH` truth:** the badge means *you* flagged this address in an earlier
session (Settings → Watchlist, off by default). It reads "this address was
here before" — addresses can be spoofed or re-randomized, so a match is a
reminder, not an identification. See the watchlist section above.

**Meter truth:** the fat analyzer bar tracks **that AP's live table RSSI**.
If beacons stop, after ~2 s the bar **fades**; by ~6 s it is **empty (LOST)**.
You are not auto-cycling every network on the fat bar—only the selection.

Beacon heard ≠ Internet, telemetry, recording, ownership, or intent.

### BLE

Same page/control pattern as Wi-Fi. Command: `sniffbt`. Detail rows carry
the same `Vendor:`, `HINT:`, and RSSI-range lines; tracker names (`tile`,
`smarttag`, `trackr`, `airtag`) show `TRK?` — most AirTags advertise no name
at all, so no hint is not evidence of absence. With the watchlist ON,
Hold UP on the detail page flags the selected device's address.

**BLE randomized truth:** BLE addresses randomize differently from Wi-Fi.
Besides the locally-administered bit, BLE **random-static** addresses (top
two bits `11`, e.g. `c3:…`, `f0:…`) now also read `randomized` — unless a
real curated OUI names the vendor first (several genuine prefixes share that
bit pattern). BLE **resolvable / non-resolvable private** subtypes (top bits
`01` / `00`) still cannot be detected from the address alone and read
`unlisted` or a vendor label.

### nRF24 (nR tab)

**2.4 GHz energy detection** — the nRF24's RPD bit only says "power above
~-64 dBm in this channel", whatever the emitter. **RX only** (no jam /
mousejack in this app). Hits are channel energy counts — **no packets, no
addresses, no device IDs**, and no dBm readings (analyzer numbers here are
**ACT** = activity units, arbitrary scale).

| Page | Shows |
|------|--------|
| **Status** | SPI path, phase, module/switch hints |
| **Results** | Active channels + top hit channels |

- Settings **SPI Path = nRF24** and BFFB bottom switch **down**  
- Sub-GHz then uses **internal** CC1101  
- **OK:** start/stop an energy pass · **Hold Left** or **Hold OK:** analyzer

Why no packet decoding: reading an nRF24 packet requires knowing its 40-bit
address in advance, and discovering unknown addresses passively requires
mousejack-style attack techniques this app bans (see `MISSION.md`).

### GPS

| Page | Content |
|------|---------|
| Summary | Time, sats bar, position, speed/course, mark distance |
| Detail | Date, NMEA counters, age, drops |
| **Radar** | Mark-centered, north-up **real meters** — you walk toward the center |

**OK** = set mark when NMEA is fresh. If there is no sentence or the fix is
stale, **OK** retries the source (same path as Hold OK). **Hold OK** always
retries (reinit stream / GPIO baud swap 9600 ↔ 115200, or Marauder `nmea`).
Coordinates in logs only if **GPS Log** ON.

**GPS Radar (walk-to-target):** set a mark (OK), then walk. The radar is
centered on the **mark**; your live position is the blip at true bearing +
real haversine distance; rings are auto-scaled meters (2m…1km); the trail of
your last ~8 fixes draws your approach path; the cross at the center is the
mark itself. Bearing is **true north** (map bearing, north-up) — the Flipper
has no compass, so while walking, face the blip toward the top of the screen
to walk straight at it. Position history is in-RAM only and is never written
to the session log.

**Heading reality check (verified against the SDK):** a GPS receiver cannot
sense a stationary device's facing, and a FAP has no IMU/magnetometer access
(Pitfall #20), so there is no stationary compass. But the app already parses
GPS **course-over-ground** (`spd/crs` on Summary) — the true-north direction
of movement while walking ≥ ~0.5 m/s. A planned course-up mode will rotate
this radar so screen-top = your walking direction, turning the blip into pure
left/right steering guidance. Until then: north-up map + true bearing.

### TX (safety-gated)

1. Defaults **DISARMED**  
2. Short OK → **ARMED**  
3. Up/Down preset  
4. **Hold OK** → bounded carrier (1–10 s, Settings TXDur)  
5. Auto-disarm; Back disarms anytime  

No jam, replay, or continuous denial TX. Hold ▶ does **not** open pages here.
While **DISARMED** on external CC1101, Hold ◀/▶ steps ExtBand (AUTO / 400 / 900).

### Info

| Page | Content |
|------|---------|
| 1 Radio | RF path, SPI, Marauder, GPS source, ident (curated OUI) |
| 2 State | Record, baseline, lock target, dump, UART |
| 3 Keys | Control cheat-sheet |
| 4 Files | SD path, last report number |
| 5 Caps | What this build can find (vendors + hints, hidden-SSID repair, clients, flags) |
| 6 Limits | Honest non-claims |

The **Caps** page is the short version of what shipped: curated-OUI vendors and
name hints (both carrying `?`), hidden-SSID repair from client probes, clients
via the Wi-raw transmitter radar, and the rogue/watch flags. It is a map of the
app's own features, not a claim about any device in the room.

## Analyzer (Hold Left on RF / Wi / BT / nR)

| Page (Hold Right) | Role |
|-------------------|------|
| **Hunt** | Fat continuous bar + STRONGER/WEAKER/STALE/LOST |
| **Field** | Peer/spectrum bars for context |
| **Radar** | **ENERGY MAP**: rings = RSSI, angle = channel wheel, **NO DIRECTION** |
| **Meter** | FontBigNumbers dBm + peak hold + trend |

- Hunt meters **one** selected (or locked) source.  
- Fresh samples move the bar; silence ages out to zero.  
- On the nR tab the meter value is **ACT** (RPD activity, arbitrary units) — never dBm.
- While the Wi/BT analyzer is open **or a Wi/BT target is locked**, scans
  restart ~250 ms after each window ends, so meters and feedback never
  starve (no freeze-then-fade gap).  
- The radar ring scale is **RSSI, not meters** — the display says so, and
  blip angles are a channel/index wheel, not a direction. Real meters and
  real bearings exist only on the GPS tab's Radar page (with a real fix).

**Hunting with the trend:** lock one target and hold a consistent
orientation. Walk slowly and follow a rising trend (STRONGER), never the
absolute number. Expect nulls near metal and reflectors — the trend can lie
locally — so trust it over seconds of walking. Never compare two devices'
bars for distance; on nR the value is ACT units, meaningless as dBm.

## Settings (Back)

Groups: Feedback · Wireless · Radio · GPS · Session  

◀/▶ (short or hold) change group. ▲/▼ move inside the group.
Every value toggle confirms with a soft tick/beep (respects Sound/Vibro).

| Item | Role |
|------|------|
| Sound / Vibro | Feedback (defaults off) |
| Rescan | Auto Wi/BT window restart |
| ScanWin | 15 / 30 / 60 s |
| Wi Src | Wi capture source: BEACON / RAW / PROBE / TOOL (BEACON each launch) |
| Record | Start/stop session → CSV + report |
| ExtBand | External CC1101 400 / 900 / AUTO (= assumed switch path, not sensed) |
| SPI Path | CC1101 vs nRF24 (nRF forces internal Sub-GHz) |
| GPS Src | BFFB Marauder vs GPIO |
| GPS Log | Include coordinates in session |
| Baseline | Snapshot RF survey floor |
| Raw Dump | Bounded UART snapshot file |
| TXDur | Carrier length 1–10 s |
| FullSweep | Auto RF→Wi→BT→raw→probe→nR→GPS, save report, **SWEEP DONE** |
| Watchlist | Cross-session flags, **off** every launch (opt-in) |

## Watchlist (opt-in — the app's only persistent-identity feature)

**Watchlist: OFF** is the factory state on every launch — the app keeps no
settings, so nothing about it persists. While it is OFF the feature does
nothing at all: no file is read, none is written, nothing is remembered.

Turning it **ON** is the only way the app ever reads
`/ext/apps_data/room_sweep/watchlist.txt`, and flagging is the only way the
app ever writes it. This is deliberate: the watchlist is where raw MAC
addresses live — the same privacy class as Raw Dump. Session CSVs never
change: they keep per-session ordinals exactly as before.

- **Flag:** with the watchlist ON, **Hold UP on a Wi-Fi or BLE detail page**
  adds the selected row's address. The label is the current SSID/name
  (up to 23 chars) or `flagged`. At 16 entries the page shows
  **WATCH FULL** and nothing more is written. The file format is one
  `aa:bb:cc:dd:ee:ff,label` per line; `#` comments and blank lines are
  fine, so you can edit it by hand on the SD card.
- **Match:** every future sighting of a flagged address shows a right-edge
  `WATCH` badge on the Wi/BT list and detail pages (it can share a row
  with a `?` hint tag and the `!` cloned-SSID mark), and the session CSV
  observation detail gains `watch=1`.
- **Report:** sessions recorded while the watchlist is ON add
  `Watchlist matches: N` (distinct flagged addresses heard) plus the
  honesty line stating this is the app's only persistent-identity feature.

**Watchlist truth:** a match says "this address was here before" — nothing
more. Addresses can be spoofed and randomize daily; a matching address is
not proof it is the same physical device, and a non-match is not proof of
absence. Flagging is always a manual act; the app never adds entries by
itself.

## Session files

Path: `/ext/apps_data/room_sweep/`

| File | Contents |
|------|----------|
| `session-N.csv` | Events; Wi/BT IDs as session ordinals; identified Wi/BT observation details carry `oui=<label\|unlisted\|randomized>`, `min=<a> max=<b> avg=<c>` signal stats, `rogue=<n>` when the SSID is shared across BSSIDs, `watch=1` on rows whose address is on the opt-in watchlist, and, when the name matched a pattern, `hints=<tag>` (details are bounded; when space is tight the trailing evidence phrase yields first and a value is never cut) |
| `report-N.txt` | Plain Room Report; adds a `Vendors seen` list (when curated labels matched), a `Hints (name-pattern guesses only)` count, a `Possible cloned SSIDs` line with the mesh caveat (when duplicate-SSID groups existed), a `Watchlist matches: N` section with the persistent-identity honesty line (only when the watchlist was ON), and the strongest device's observed range |
| `uart-N.txt` | Only from Raw Dump (may include raw IDs/GPS) |
| `watchlist.txt` | Only from the opt-in watchlist: one `mac,label` line per address you flagged by hand — the only file where raw identifiers persist |

Recording is size-capped; scans continue if logging stops.

## FullSweep

Settings → FullSweep → OK:

1. Hard timeouts: RF 10 s, Wi-Fi 15 s, BLE 15 s, Wi-raw 6 s, Wi-probe 8 s,
   nRF24 12 s, GPS 8 s
   (GPS may finish after 2 s when usable NMEA data exists — parsed
   position or sentences; a receiver fix without coordinates does not count.
   The Wi-raw pass is a short `sniffraw` transmitter-radar window after BLE,
   and **it is the client view on this firmware**: `sniffraw` reports one
   line per 802.11 frame with stations included, which is what a separate
   `scanap`+`scansta` pass would otherwise provide — that pair stays unused
   until its station-line format is pinned on hardware. The Wi-probe pass
   that follows runs `sniffprobe` for hidden-SSID recovery; probes are
   bursty, so its window is longer than the raw pass. Either Wi pass is
   skipped honestly — bit unset, sweep continues — when no Marauder is
   attached, and a quiet room with zero probes is normal, not a fault.)
2. nRF skipped immediately if SPI path is not nRF24  
3. Session closed; `report-N.txt` written  
4. Progress shows in the **top strip** (`FULL RF 10s`) — tab footers stay visible  
5. Inverted **SWEEP DONE** banner after auto-save (`saved report-N`)

## Hardware notes (BFFB)

- USART 13/14 @ 115200 after expansion disable  
- GPS preferred on LPUART 15/16 @ 9600 when Momentum GPS UART = Extra 15,16  
- Bottom switch: up CC1101 / down nRF24  
- Top switch: 400 / 900 MHz external CC1101  

## Honest limits

- RSSI ≠ distance  
- Beacon/ad ≠ telemetry or intent  
- `Vendor:` is a curated prefix registrant, not the device; `unlisted` is normal; `randomized` is a self-assigned address, not a vendor; on BLE, resolvable/non-resolvable private address subtypes still cannot all be detected from the address alone
- `HINT:` tags are name-pattern guesses — always keep their `?`  
- `SAME NAME ON n BSSIDS` / `rogue=` is a cloned-SSID *lead*; mesh/roaming shares names legitimately
- `TOOL` is a class match on the advertisement (Marauder/ESP32, Pwnagotchi) — never proof of intent; a Pwnagotchi is also a toy, an ESP32 also a dev board. `sniffesp` streams nothing on current Marauder builds (pinned from upstream source)
- `WATCH` means you flagged that address earlier — a "here before" reminder; addresses can be spoofed or re-randomized, so a match is not an identification
- Coverage: 16 RF presets, 3 CC1101 bands (348–387 and 464–779 MHz invisible), no 5 GHz
- Misses: non-transmitting recorders, burst TX between samples, out-of-band, wired
- No hit ≠ proof of absence  
- Bounded TX ≠ replay or jam  
- Analyzer needs ongoing reports; empty when the source goes silent  

## Build

```sh
./init.sh
ufbt launch
```
