# Room Sweep v3.2 — Operator Guide

Room Sweep is designed so a non-specialist can collect useful, honest evidence
without pretending that RSSI identifies a device's purpose or that passive scans
prove what a device is doing.

## The controls to remember

- **Short Left / Right:** previous or next tab.
- **Up / Down:** browse or change the thing shown in the current tab.
- **Short OK:** the normal action shown at the bottom of the screen.
- **Long OK:** lock a target, or confirm TX only after TX has been armed.
- **Short Back:** open/close Settings; on an armed TX screen, disarm first.
- **Long Back:** exit from anywhere.

Sound and vibration are changed only in Settings. Up and Down are no longer
wasted on those toggles.

## RF: Survey, Sweep, and Peak

Use **Up/Down** to choose one of three receive modes:

- **Survey:** continuously checks 16 useful preset frequencies. Long OK locks a
  qualified signal so the relative-strength feedback can follow it.
- **Sweep:** checks an entire 300–348, 387–464, or 779–928 MHz band. Long
  Left/Right changes the band while idle. Short OK starts or cancels. Long OK
  locks the completed qualified result.
- **Peak:** refines the most recent qualified Survey/Sweep result in 25 kHz
  steps. Short OK starts or cancels; Long OK locks the completed refined result.

A candidate must be stronger than -75 dBm, come from a completed operation,
have a valid actually tuned frequency, and be less than 30 seconds old. RSSI is
relative signal strength—not distance, identity, ownership, or intent.

## Wi-Fi

The BFFB Marauder connection passively listens for AP beacon observations.

- **Up/Down:** browse every stored AP row.
- **Long Left/Right:** scan window 15 / 30 / 60 seconds.
- **Short OK:** start a new scan window.
- **Long OK:** lock/unlock the selected row for relative-strength feedback.

Each row shows its display name, redacted/present hardware identity on disk,
RSSI, channel, age, and observation count. Hidden devices are shown as
`Hidden/unknown`; the app does not invent names. Observations with neither a
name nor hardware identity are explicitly grouped and are not a device count.

Important: an AP beacon proves only that a beacon was heard. It does not prove
Internet connectivity, telemetry upload, recording, ownership, or intent. A
window with no observation does not prove a device is absent or inactive.

## BLE

The BFFB Marauder connection performs active BLE scanning (`sniffbt` over UART — not Flipper native BLE).

- **Up/Down:** browse every stored BLE row.
- **Long Left/Right:** scan window 15 / 30 / 60 seconds.
- **Short OK:** start a new scan window.
- **Long OK:** lock/unlock the selected row.

The app records advertisements/scan responses, RSSI, age, and observation count.
Observations with no name or hardware identity are grouped rather than treated
as individually identified devices.
It cannot prove Internet telemetry or detect a silent/offline recorder merely
because nothing advertised during the bounded window.

## GPS

The default source is **BFFB Marauder**. Settings can select an optional external
GPIO NMEA receiver instead.

- **Up/Down:** switch between Summary and Detail pages.
- **Short OK:** retry when data is absent/stale; with a fresh position, set or
  clear a distance mark.
- **Long OK:** intentionally does nothing.

The pages expose source, link/fix state, UTC/date, latitude/longitude, fix
quality, satellites used/in view, speed/course, valid sentence count, navigation
sentence count, bytes received, age, and dropped-byte count.

## TX: a bounded frequency-only test

TX is intentionally harder to activate because it radiates RF energy.

1. Entering TX automatically preloads a fresh qualified RF candidate when one
   exists; otherwise it uses the selected safe preset.
2. **Up/Down** chooses among 12 presets spanning ~300 / ~400 / ~900 MHz.
   On BFFB external radio, ExtBand **auto-follows** 400 vs 900 presets.
   **Flip the board top switch** to match the on-screen `sw:400` / `sw:900`.
   ~300 MHz presets need the **internal** radio (not the dual external path).
3. **Short OK** performs preflight and arms. Arming emits no RF.
4. **Long OK** confirms a bounded 1–10 second carrier test.
5. **Back** disarms/stops; leaving the tab also stops and disarms.
   Refusal reasons (band, expired candidate, real region table) stay on DISARMED.
   Flipper region `--` (unprovisioned) is **not** a full TX ban; radio + ExtBand
   still apply. A provisioned region that forbids a frequency still blocks it.

The handoff copies frequency only. It does not capture or replay modulation,
decode a protocol, clone a remote, measure antenna output, or identify what the
signal controls. There is no jammer, blocker, deauthentication, or arbitrary
replay mode. Transmission is restricted by the radio, installed region table
(when present), selected external band, and two-step confirmation. Use only
where you are authorized.

## Info

**Up/Down** switches between live status and a plain-language glossary. Status
shows radio path, Marauder/GPS evidence, recording number/errors/drops, baseline,
lock, UART lines, and UART drops.

## Settings

Open Settings with Short Back. **Long Left/Right** changes group; **Up/Down**
moves within the group; **OK** changes the value:

**Feedback**
- **Sound:** Geiger-style audio feedback.
- **Vibro:** haptic feedback.

**Wireless**
- **Rescan:** automatic Wi-Fi/BLE scan windows.
- **ScanWin:** 15 / 30 / 60 second window (also Long L/R on Wi-Fi/BLE tabs).

**Radio**
- **ExtBand:** Auto, explicit 400 MHz, or explicit 900 MHz external-radio path.
  External TX requires 400 or 900 (not Auto).
- **TXDur:** bounded carrier duration, 1–10 seconds.

**GPS**
- **GPS Src:** BFFB Marauder or optional external GPIO NMEA.
- **GPS Log:** exact coordinates are omitted by default; this is a separate
  explicit opt-in.

**Session**
- **Record:** start/finish a numbered cross-tab session (`session-N.csv` +
  `report-N.txt`). Status also shows on the Info tab.
- **Baseline:** save the current RSSI value for all 16 RF Survey channels.
- **Raw Dump:** save the newest bounded full UART lines. This explicit file may
  contain raw identifiers and GPS coordinates.

## What gets dumped

The main **Record** switch creates one session spanning every tab; you do not
need to start a separate dump in each tab. It writes:

- `session-N.csv`: a versioned, 28-column machine-readable event stream.
- `report-N.txt`: a plain-English completion/coverage summary, strongest
  observations, scan-window counts, TX outcome, privacy state, dropped data,
  storage state, file paths, and limitations.
- `uart-N.txt`: created only by **Raw Dump**; the newest 24 complete 127-character
  UART lines, oldest first, with queue-drop and ring-overwrite counts.

The CSV includes sequence/tick, event, source, tab/submode, per-session redacted
identifier, RSSI, tuned frequency, channel, optional coordinates, GPS fix
quality/satellites/speed/course/UTC/date/NMEA counters, repeated-observation
count, state, error code, and detail. It records begin/end, tab/config changes,
RF results/baselines/locks, accepted Wi-Fi/BLE observations and scan windows,
GPS snapshots/marks, TX intent/result/refusal/abort, and data loss.

Recording is bounded to 2,048 records and at most 256 KiB per session, reduced
automatically when SD free space is low. The app refuses to start below 1 MiB
free, syncs periodically, never overwrites an earlier numbered file, and marks
sessions incomplete when data was dropped or storage failed. Live scanning
continues if recording stops.

### Where the files are

On the SD card:

```text
/ext/apps_data/room_sweep/session-N.csv
/ext/apps_data/room_sweep/report-N.txt
/ext/apps_data/room_sweep/uart-N.txt
```

In qFlipper or the Flipper Files browser, open `apps_data` → `room_sweep`.
Older fixed files remain untouched at
`/ext/apps_data/room_sweep/room_sweep/session.csv` and
`/ext/apps_data/room_sweep/room_sweep/bffb_dump.txt`.

## Hardware and evidence boundaries

| Capability | Required path | What a result means |
|---|---|---|
| RF Survey/Sweep/Peak | internal CC1101 or supported BFFB external CC1101 | energy was measured near a tuned frequency |
| Wi-Fi | BFFB Marauder UART, `sniffbeacon` | an AP beacon was observed |
| BLE | BFFB Marauder UART, `sniffbt` | an advertisement/scan response was observed |
| GPS | BFFB Marauder NMEA or optional GPIO NMEA | checksummed navigation sentences were parsed |
| TX | supported radio plus region/band approval | software accepted a bounded carrier request; antenna output is not measured |

If the app says `not confirmed`, `unavailable`, `stale`, `partial`, or
`incomplete`, treat that wording literally. It is not evidence of absence.
