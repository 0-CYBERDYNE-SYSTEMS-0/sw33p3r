# Room Sweep — Operator Guide

Receive-side room survey on Flipper Zero + optional BFFB (Marauder / dual
CC1101 / nRF24). Firmware target: **Momentum mntm-012**, API **87.1**.

This guide matches the **current** app behavior (7 tabs, Hold-R pages,
analyzer age-out, FullSweep auto-save, tactile tap/hold feedback).

## Remember these controls

| Input | Action |
|-------|--------|
| Short Left / Right | Change **tab** |
| Hold Left | **Analyzer** on/off (RF, Wi-Fi, BLE, nRF24) |
| Hold Right | **Page** inside the current tab (or analyzer) |
| Up / Down | Browse list / RF sub-mode / GPS-Info pages |
| Hold Up / Down | **RF lock card** open/close (RF tab) |
| Short OK | Main action (scan, start, arm, mark, …) |
| Hold OK | Lock target — or **transmit** only if TX already armed. On GPS: **NMEA retry** |
| Short Back | Settings (or disarm TX first) |
| Hold Back | Exit app |

Footer lines on each page state what **L / R / OK** do there.

**Tactile feedback:** with **Settings → Vibro** ON, a short tap gives one
soft pulse and a hold gives a double pulse on release, so holds register
distinctly. Sound/Vibro state lives in **Settings → Feedback** (no glyphs on
tab headers).

## Tabs

### RF

**Up/Down** = Survey / Sweep / Peak. **Hold Up/Down** = open the lock/map
card from any sub-mode (Up/Down returns to the map).

| Sub-mode | OK | Hold OK | Hold Right |
|----------|-----|---------|------------|
| Survey | — (continuous presets) | Lock qualified hit | Map ↔ **Lock card** |
| Sweep | Start/cancel band sweep | Lock result | **Band** step while idle on map |
| Peak | Start/cancel refine | Lock result | Map ↔ **Lock card** |

Hold Left = analyzer (Hunt meter / Field spectrum).

### Wi-Fi

Marauder command: `sniffbeacon` (AP beacons only).

| Page (Hold Right) | Shows |
|-------------------|--------|
| **Detail** | One selected AP: SSID, RSSI, channel, MAC |
| **List** | Up to 5 rows RSSI + name |
| **Help** | Short control reminder |

- **Up/Down:** select AP  
- **OK:** start/restart scan  
- **Hold OK:** lock/unlock that AP for follow/analyzer  
- **Hold Left:** analyzer for the **selected/locked** AP  
- Scan window: **Settings → ScanWin** (15/30/60 s)

**Meter truth:** the fat analyzer bar tracks **that AP’s live table RSSI**.
If beacons stop, after ~2 s the bar **fades**; by ~6 s it is **empty (LOST)**.
You are not auto-cycling every network on the fat bar—only the selection.

Beacon heard ≠ Internet, telemetry, recording, ownership, or intent.

### BLE

Same page/control pattern as Wi-Fi. Command: `sniffbt`.

### nRF24

2.4 GHz RPD channel activity. **RX only** (no jam / mousejack in this app).

| Page | Shows |
|------|--------|
| **Status** | SPI path, phase, module/switch hints |
| **Results** | Active channels + top hit channels |

- Settings **SPI Path = nRF24** and BFFB bottom switch **down**  
- Sub-GHz then uses **internal** CC1101  
- **OK:** start/stop RPD pass · **Hold Left:** analyzer  

### GPS

| Page | Content |
|------|---------|
| Summary | Time, sats bar, position, speed/course, mark distance |
| Detail | Date, NMEA counters, age, drops |

**OK** = set mark. **Hold OK** = NMEA retry (reinit stream / baud swap).
Coordinates in logs only if **GPS Log** ON.

### TX (safety-gated)

1. Defaults **DISARMED**  
2. Short OK → **ARMED**  
3. Up/Down preset  
4. **Hold OK** → bounded carrier (1–10 s, Settings TXDur)  
5. Auto-disarm; Back disarms anytime  

No jam, replay, or continuous denial TX. Hold-R pages are **not** used here.

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
| **Hunt** | Fat continuous bar + CLOSER/FARTHER/STALE/LOST |
| **Field** | Peer/spectrum bars for context |

- Hunt meters **one** selected (or locked) source.  
- Fresh samples move the bar; silence ages out to zero.  
- While Wi/BT analyzer is open, scan is **kept alive** for samples.

## Settings (Back)

Groups: Feedback · Wireless · Radio · GPS · Session  

Every value toggle confirms with a soft tick/beep (respects Sound/Vibro).

| Item | Role |
|------|------|
| Sound / Vibro | Feedback (defaults off) |
| Rescan | Auto Wi/BT window restart |
| ScanWin | 15 / 30 / 60 s |
| Record | Start/stop session → CSV + report |
| ExtBand | External CC1101 400 / 900 / AUTO |
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

1. RF, Wi-Fi, BLE, nRF24, GPS each with **hard timeouts** (GPS ≤ 8 s)  
2. nRF skipped immediately if SPI path is not nRF24  
3. Session closed; `report-N.txt` written  
4. Progress shows in the **top strip** (`FULL RF 10s`) — tab footers stay visible  
5. Inverted **SWEEP DONE** banner after auto-save

## Hardware notes (BFFB)

- USART 13/14 @ 115200 after expansion disable  
- GPS preferred on LPUART 15/16 @ 9600 when Momentum GPS UART = Extra 15,16  
- Bottom switch: up CC1101 / down nRF24  
- Top switch: 400 / 900 MHz external CC1101  

## Honest limits

- RSSI ≠ distance  
- Beacon/ad ≠ telemetry or intent  
- No hit ≠ proof of absence  
- Bounded TX ≠ replay or jam  
- Analyzer needs ongoing reports; empty when the source goes silent  

## Build

```sh
./init.sh
ufbt launch
```
