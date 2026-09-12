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
| RF | Energy above -75 dBm on 16 fixed presets | What is transmitting; only 3 CC1101 bands, with gaps |
| Wi | AP beacons: name, RSSI, channel | Device type or owner; anything on 5 GHz |
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

**Up/Down** = Survey / Sweep / Peak / **Waterfall**. **Hold Up/Down** = open
the lock/map card from any sub-mode (Up/Down returns to the map).

| Sub-mode | OK | Hold OK | Hold Right |
|----------|-----|---------|------------|
| Survey | — (continuous presets) | Lock qualified hit | Map ↔ **Lock card** |
| Sweep | Start/cancel band sweep | Lock result | **Band** step while idle on map |
| Peak | Start/cancel refine | Lock result | Map ↔ **Lock card** |
| Waterfall | — (passive history) | Lock qualified hit | Map ↔ **Lock card** |

Hold Left = analyzer (Hunt meter / Field spectrum / Radar / Big meter).

### Waterfall (RF sub-mode)

Scrolling spectrum history of the 16 presets — newest column on the right,
~2.5 snapshots/second, per-channel **peak-hold dots** on the right edge and
a dashed alert-threshold line. Purely passive: read the room's RF activity
over the last ~10 seconds at a glance.

### Wi-Fi

Marauder command: `sniffbeacon` (AP beacons only).

| Page (Hold Right) | Shows |
|-------------------|--------|
| **Detail** | One selected AP: SSID, RSSI, channel, MAC |
| **List** | Up to 5 rows RSSI + name |
| **Help** | Short control reminder |

- **Up/Down:** select AP  
- **OK:** lock/unlock the selected AP (starts the first scan when the list is empty)  
- **Hold OK:** rescan (clears the AP table and any lock)  
- **Hold Left:** analyzer for the **selected/locked** AP  
- Scan window: **Settings → ScanWin** (15/30/60 s)

**Meter truth:** the fat analyzer bar tracks **that AP’s live table RSSI**.
If beacons stop, after ~2 s the bar **fades**; by ~6 s it is **empty (LOST)**.
You are not auto-cycling every network on the fat bar—only the selection.

Beacon heard ≠ Internet, telemetry, recording, ownership, or intent.

### BLE

Same page/control pattern as Wi-Fi. Command: `sniffbt`.

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
| 1 Radio | RF path, SPI, Marauder, GPS source |
| 2 State | Record, baseline, lock target, dump, UART |
| 3 Keys | Control cheat-sheet |
| 4 Files | SD path, last report number |
| 5 Limits | Honest non-claims |

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
| Record | Start/stop session → CSV + report |
| ExtBand | External CC1101 400 / 900 / AUTO (= assumed switch path, not sensed) |
| SPI Path | CC1101 vs nRF24 (nRF forces internal Sub-GHz) |
| GPS Src | BFFB Marauder vs GPIO |
| GPS Log | Include coordinates in session |
| Baseline | Snapshot RF survey floor |
| Raw Dump | Bounded UART snapshot file |
| TXDur | Carrier length 1–10 s |
| FullSweep | Auto RF→Wi→BT→nR→GPS, save report, **SWEEP DONE** |

## Session files

Path: `/ext/apps_data/room_sweep/`

| File | Contents |
|------|----------|
| `session-N.csv` | Events; Wi/BT IDs as session ordinals |
| `report-N.txt` | Plain Room Report |
| `uart-N.txt` | Only from Raw Dump (may include raw IDs/GPS) |

Recording is size-capped; scans continue if logging stops.

## FullSweep

Settings → FullSweep → OK:

1. Hard timeouts: RF 10 s, Wi-Fi 15 s, BLE 15 s, nRF24 12 s, GPS 8 s
   (GPS may finish after 2 s when usable NMEA data exists — parsed
   position or sentences; a receiver fix without coordinates does not count)
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
