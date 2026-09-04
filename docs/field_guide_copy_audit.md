# Audit — `docs/field_guide_copy.md` (field guide copy deck)

Auditor: independent fact-check pass. Date: 2026-09-04.
Sources checked: `USER_GUIDE.md`, `MISSION.md`, `docs/BFFB_MOMENTUM.md`,
`specs/` (meter-suite-2026-08-13, full-sweep, room-report, nrf24-survey,
ui-ux-2026-08-13, per-tab-feedback), `AGENTS.md`, source code
(`room_sweep.h`, `room_sweep.c`, `room_sweep_analyzer.h`,
`room_sweep_state.h`), and every PNG referenced by P01–P39 in
`docs/field_guide_shots/` (read image-by-image; quoted dBm values, SSIDs,
MACs, headers, and footer hints compared against the pixels).

Result: 29 of 39 entries PASS clean. 10 entries carry 11 required
corrections. No entry implies jamming, deauth, replay, or denial; the
bounded keyed OOK 1–10 s TX framing is intact everywhere; the
honest-limits lines are not weakened.

## Verdict table

| Entry | Verdict | Reason |
|---|---|---|
| P01 | PASS | All three decorative shots exist (`rf_survey.png`, `wifi_analyzer_p2.png`, `gp_summary.png`); eyebrow facts (mntm-012, API 87.1) match. |
| P02 | PASS | `i_p4.png` shows exactly "LIMITS 5/5 / RSSI != distance / no jam / no deauth / beacon != telemetry / no hit != absence"; chips match USER_GUIDE. |
| P03 | PASS | `00_launch.png` shows RF Survey already reporting (-65dBm, SIGNAL!); `i_p2.png` is the KEYS 3/5 control map; tab order RF→Wi→BT→nR→GP→TX→i correct. |
| P04 | ISSUE | Equates the dashed SIGNAL! threshold with the Settings Baseline; threshold is a fixed −75 dBm constant, Baseline is a separate floor snapshot (correction 1). Ruler marks 304/390/434/450 and SIGNAL! badge do match `rf_survey.png`. |
| P05 | PASS | `rf_sweep.png` shows "[SWEEP EXT]" and "Band: 300–348 MHz"; OK start/cancel, Hold Right band step idle, Hold OK lock match USER_GUIDE. |
| P06 | PASS | `rf_peak.png` shows "[PEAK EXT]" and "Center: 329.999 MHz" exactly as quoted. |
| P07 | PASS | Waterfall facts (16 presets, ~2.5 snapshots/s, peak-hold dots right edge, dashed threshold, ~10 s) match `rf_waterfall.png` and specs. |
| P08 | PASS | `rf_lockcard.png` shows "CANDIDATE / 330.000 MHz / -65 dBm fresh", "U/D back HoldOK lock"; open/close and lock semantics match USER_GUIDE. |
| P09 | ISSUE | Verdict list omits STABLE, which is on-screen in `rf_analyzer_p0.png` ("= STABLE 61%") and in `room_sweep_analyzer.h` (correction 2). Grid layout, page roles, ~2 s fade / ~6 s LOST, scan-kept-alive all verified. |
| P10 | PASS | `wifi_analyzer_p0.png`: "REDACTED-SSID_3TFXQX", "-37dBm", "CLOSER 91%" — lede matches exactly. FIELD/RADAR/METER cells verified. |
| P11 | PASS | `bt_analyzer_p0.png`: "00:11:22:33:44:5a", "-72dBm" — matches lede. PK/trend claims match `bt_analyzer_p3.png` ("PK -72"). (Minor prose nit, see notes.) |
| P12 | ISSUE | "RPD (radio-path detection)" mis-expands RPD; spec says "receive power detector" (correction 3). Ledes "-127 dBm, LOST 0%", "2.4G RPD", SPI Path/switch requirements all match captures and specs. |
| P13 | ISSUE (x2) | Title word "capture" conflicts with the detect/analyze wording contract (correction 4); "counter shows windows completed" is wrong — `Wi scan a/b` is selection position over APs heard (correction 5). Quoted screen text "Wi scan 0/0 · Listening… · beacon only" matches `wifi_p0.png`. |
| P14 | ISSUE | "security/BEACON line" — no security is displayed; the status line reads `beacon` / `LOCK on` (correction 6). Lede "GIII Mechanical · -49 dBm · Ch2 · 00:11:22:33:44:5b" matches `01_after_right.png` exactly. |
| P15 | PASS | `wifi_p1.png` list strongest-first with inverted selection row; `wifi_p2.png` help page; ScanWin chip correct. |
| P16 | PASS | `bt_p0.png`: "00:11:22:33:44:5a / -73dBm n1 / adv" — lede matches exactly; sniffbt controls match. |
| P17 | ISSUE | "a locked device stays marked (`LOCKED`)" and the LIST caption are not supported: the lock appears as a header ` L` tag and `LOCK on` on Detail, rows invert only for selection; `bt_p1.png` shows no LOCKED tag (correction 7). HELP caption is fine. |
| P18 | PASS | `nr_p0.png` matches quoted "nR STATUS · SPI CC1101 · phase idle" plus "need SPI=nRF24 / BFFB bottom DOWN"; internal-CC1101 fallback matches nrf24-survey spec. |
| P19 | ISSUE | "Left opens the analyzer" should be "Hold Left" (USER_GUIDE; the page's own chip and `nr_p1.png` footer "L=AN" say Hold) (correction 8). Lede "active 0 · hits 0 · no RPD energy yet" matches exactly. |
| P20 | PASS | `gp_summary.png`: time, empty sat bar "0/0", "No position yet", "spd/crs --"; OK mark/retry and Hold OK retry (incl. 9600↔115200 swap) match USER_GUIDE. |
| P21 | PASS | `gp_detail.png`: "Src:BFFB Marauder", "NMEA:12 nav:5 bytes:5", "Age:0s drops:0" — quote matches exactly; chips match footer "U/D|R page OK=mark". |
| P22 | PASS | `gp_radar.png`: "Radar needs a mark / OK=mark, then walk" — lede matches exactly; 2 m…1 km rings, ~8-fix RAM-only trail, north-up, no-compass caveat all match USER_GUIDE. |
| P23 | ISSUE | "300 / 434 / 900 families" — the preset families are 300 / 400 / 900 (correction 9). DISARMED-by-default, "300MHz: INT radio on" quote, preflight claims, and arm/back controls match `tx_default_300.png` and USER_GUIDE. |
| P24 | PASS | `tx_armed.png`: "!! ARMED !! / Preset carrier only / 434.420 MHz 3s max" — quote exact; TXDur 1–10 s in Settings → Radio confirmed by `set_radio_txdur.png`. |
| P25 | PASS | `tx_transmitting.png`: "TX API ACTIVE / 434.419 MHz / 2.1s remaining / Back=stop" — quote exact; auto-disarm and per-attempt event logging match. |
| P26 | PASS | `tx_after_disarm.png`: "DISARMED", "Carrier only; no repl…", "sw:400", "Flip TOP switch to 40…" — all quoted elements present; Hold L/R ExtBand while disarmed+ext matches USER_GUIDE. |
| P27 | ISSUE | "walks 300 → 434 → 450 MHz and back" understates the 12-preset walk across the 300/400/900 families (correction 10). Lede "450.000 · 3s · sw:400" matches `tx_preset_down2.png` exactly; band-gap numbers correct. |
| P28 | PASS | `i_p0.png` "RF:EXT SPI:CC1101", "Marauder:confirmed"; `i_p1.png` Rec/Base/Lock/Target/dump/UART — quotes and page roles match; marauder_confirmed meaning stated correctly. |
| P29 | PASS | `i_p2.png` KEYS 3/5, `i_p3.png` FILES 4/5, `i_p4.png` LIMITS 5/5 — captions and content match. |
| P30 | PASS | Both feedback shots show Sound off / Vibro off; tap-vs-hold vibro semantics match USER_GUIDE; group/toggle controls match. |
| P31 | PASS | Rescan/ScanWin shots match captions; 15/30/60 s values correct; `set_wireless_scanwin.png` shows 30 s. |
| P32 | PASS | ExtBand AUTO/400/900, SPI Path forces internal Sub-GHz on nRF24, TXDur 1–10 s — all match shots, USER_GUIDE, and nrf24-survey spec. |
| P33 | PASS | GPS Src BFFB/GPIO LPUART 15/16 @ 9600, GPS Log gating, Hold OK retry with baud swap — match shots, BFFB doc, USER_GUIDE. |
| P34 | PASS | Record → session-N.csv + Room Report; Baseline; Raw Dump uart-N.txt as the only raw-identifier file; FullSweep — all match USER_GUIDE and the four Session shots. |
| P35 | PASS | Timeouts RF 10 / Wi-Fi 15 / BLE 15 / nRF24 12 / GPS 8 s, nRF24 skip on wrong SPI path, session close + report-N.txt, top-strip progress, inverted SWEEP DONE banner — all match USER_GUIDE. |
| P36 | PASS | Path `/ext/apps_data/room_sweep/`, three file names, ordinals privacy, uart-N on request, TX attempts as events, FILES page role — all match `i_p3.png`, USER_GUIDE, AGENTS.md. |
| P37 | ISSUE | Says the dual CC1101 and nRF24 are driven "over USART 13/14 @ 115200" (they are on SPI; USART 13/14 is the Marauder/ESP32 link) and that Marauder is "preferred" for NMEA (GPIO LPUART is primary, Marauder `nmea` is fallback) (correction 11). INT-fallback claims and Info → RADIO references are correct. |
| P38 | PASS | `99_desktop_after_exit.png` shows the untouched Momentum desktop; expansion restore, `./init.sh` gate, `ufbt launch`, mntm-012/API 87.1 all match. |
| P39 | PASS | Scope lines intact; appid `room_sweep`; companion file names correct. |

## Required corrections

1. **P04 — SIGNAL! threshold is not the Baseline.**
   Current: "The header tracks the hottest preset's dBm and raises a `SIGNAL!` badge when energy crosses the dashed threshold — the baseline you snapshot in Settings → Session → Baseline."
   Replace with: "The header tracks the hottest preset's dBm and raises a `SIGNAL!` badge when energy crosses the dashed alert line — a fixed −75 dBm threshold. Baseline (Settings → Session → Baseline) is a separate snapshot of the room's RF floor that the survey bars are drawn against."
   Source: `room_sweep_state.h` (`ROOM_SWEEP_SIGNAL_THRESHOLD_DBM (-75.0f)`, dashed line drawn at `RF_ALERT_THRESHOLD`, `room_sweep.c:3241`); Baseline is stored separately (`baseline_rssi[]`, bar fill "above/below baseline", `room_sweep.c:3256-3267`) — USER_GUIDE: "Baseline: Snapshot RF survey floor".

2. **P09 — verdict list must include STABLE.**
   Current: "HUNT: one fat proximity bar with CLOSER / FARTHER / STALE / LOST verdicts."
   Replace with: "HUNT: one fat proximity bar with CLOSER / FARTHER / STABLE / STALE / LOST verdicts."
   Source: `rf_analyzer_p0.png` shows "= STABLE 61%"; `room_sweep_analyzer.h:142-147` returns "CLOSER"/"FARTHER"/"STABLE"; `room_sweep.c:2436-2449` draws "LOST 0%"/"STALE fade".

3. **P12 — RPD expansion.**
   Current: "The analyzer works over the nRF24 RPD (radio-path detection) energy survey."
   Replace with: "The analyzer works over the nRF24 RPD (receive power detector) energy survey."
   Source: `specs/nrf24-survey.md` — "via nRF24 RPD (receive power detector)". ("Radio path" is the name of a state header, not the RPD expansion.)

4. **P13 — title wording (scope safety).**
   Current: "- title: `Beacon capture, on your marks`"
   Replace with: "- title: `Beacon listening, on your marks`"
   Reason: the deck's own contract bans implying capture capability; the capture/`sniffbeacon` framing is also redundant with the body. The screen itself says "Listening…". (Any of "Beacon listening/watch" is acceptable; the point is dropping "capture".)

5. **P13 — what the scan counter means.**
   Current: "OK restarts the scan; the counter shows windows completed."
   Replace with: "OK restarts the scan; the `Wi scan a/b` counter is your selection position within the APs heard (`0/0` = none yet, `1/2` = first of two)."
   Source: `room_sweep.c:3474` (`"Wi %s %u/%u%s"` prints `selected+1` / `count`); captures agree — `wifi_p0.png` "0/0", `01_after_right.png` "1/1", `wifi_p1.png` "1/2".

6. **P14 — the status line shows no security.**
   Current: "The Detail page shows the selected AP's name, live RSSI, channel, and MAC, plus the security/BEACON line."
   Replace with: "The Detail page shows the selected AP's name, live RSSI, channel, and MAC, plus a status line that reads `beacon` when unlocked and `LOCK on` once you lock the AP."
   Source: `room_sweep.c:3563` (`locked ? "LOCK on  HoldOK=unlock" : "beacon  HoldOK=lock"`); `01_after_right.png` shows "beacon HoldOK=lock". No security/WPA string is rendered anywhere on this page.

7. **P17 — locked-device marking is a header tag, not a LOCKED row tag.**
   Current caption: "LIST — a locked device stays inverted and tagged"
   Replace caption with: "LIST — strongest first; the inverted row is your selection"
   Current body: "In the list, a locked device stays marked (`LOCKED`) while newer advertisements arrive around it."
   Replace body with: "Locking a device tags the page header with ` L` and flips the Detail line to `LOCK on`; newer advertisements keep arriving around your selection."
   Source: `room_sweep.c:3609` (header suffix `" L"`), `room_sweep.c:3691` (`"LOCK on  HoldOK=unlock"`), `room_sweep.c:3656-3659` (rows invert only for the scroll selection); `bt_p1.png` shows the inverted first row and no LOCKED tag.

8. **P19 — analyzer needs Hold Left.**
   Current: "OK rescan; Left opens the analyzer on the results."
   Replace with: "OK rescan; Hold Left opens the analyzer on the results."
   Source: USER_GUIDE ("Hold Left or Hold OK: analyzer"); the entry's own chip and `nr_p1.png` footer "L=AN".

9. **P23 — preset family names.**
   Current: "Up/Down steps presets (300 / 434 / 900 families)."
   Replace with: "Up/Down steps presets (300 / 400 / 900 families)."
   Source: `room_sweep.c:402-424` — preset table grouped "~300 MHz path / ~400 MHz path / ~900 MHz path"; `tx_default_300.png` hint "U/D for 400/900 prese[t]". 434.420 is a member of the 400 family, not a family of its own.

10. **P27 — the preset walk has twelve stops.**
    Current: "Preset stepping walks 300 → 434 → 450 MHz and back; the CC1101's real band gaps (300–348 / 387–464 / 779–928 MHz) are enforced in hardware, not politely suggested."
    Replace with: "Preset stepping walks twelve stops across the 300 / 400 / 900 MHz families and back; the CC1101's real band gaps (300–348 / 387–464 / 779–928 MHz) are enforced in hardware, not politely suggested."
    Source: `room_sweep.c:407` (`TX_FREQ_PRESET_COUNT 12`) and the preset table at `room_sweep.c:408-424` (390, 418, 433.42, 433.92, 434.42, 450, 868.35, 915, 925, 303.875, 315, 345 MHz).

11. **P37 — wrong bus for the radios, wrong GPS preference.**
    Current: "With a BFFB attached, Room Sweep drives the dual CC1101 and the nRF24 over USART 13/14 @ 115200 and prefers the Marauder stream for both Wi-Fi/BLE scans and NMEA."
    Replace with: "With a BFFB attached, Room Sweep drives the dual CC1101 and the nRF24 over the Flipper SPI bus and talks to the Marauder ESP32 over USART 13/14 @ 115200 — the Marauder stream carries the Wi-Fi/BLE scans, while GPS prefers the Flipper GPIO LPUART (15/16 @ 9600) with Marauder `nmea` as the fallback."
    Source: `docs/BFFB_MOMENTUM.md` — "Dual CC1101 + nRF24 on Flipper SPI", "ESP32 is on UART 13/14", GPS paths "GPIO LPUART (primary) … Marauder `nmea` (fallback)"; USER_GUIDE hardware notes; AGENTS.md BFFB/Marauder section.

## Minor notes (non-blocking)

- §1 says "the captures are dark-pixels-on-white" — true for the app screens, but `tx_transmitting.png` and `99_desktop_after_exit.png` are inverted (white-on-dark). Consider appending "(the inverted TX-active and desktop frames render white-on-dark)" so builders are not surprised.
- P11 lede "from -72 dBm toward zero bars": the quoted values match `bt_analyzer_p0.png` (-72 dBm, CLOSER 47%), but the bar is about half full, so "toward zero bars" reads oddly. Optional polish: "from -72 dBm, CLOSER at 47%."
- P08: `rf_lockcard.png` header also offers "R=map" as an exit; the copy's "press again [Hold Up/Down] to return to the map" matches USER_GUIDE and is not wrong — both exits exist.
- §3 groupings, page count (39), and per-section accents in §1 were cross-checked against §2 and are internally consistent.

## Approved as accurate

- **Intro/mission group (P01–P03):** clean, including the on-device LIMITS page quote and launch state.
- **RF sub-modes (P05–P08):** every quoted screen string ("[SWEEP EXT]", "Band: 300–348 MHz", "[PEAK EXT]", "Center: 329.999 MHz", "CANDIDATE 330.000 MHz · -65 dBm · fresh") matches its capture pixel-for-pixel; controls match USER_GUIDE.
- **Analyzer captures (P10–P12 ledes and grids):** all SSIDs, MACs, dBm values, and verdict/percent strings match the PNGs exactly; fade/LOST timing and "RSSI ring, not meters" honesty labels verified.
- **Wi-Fi/BLE/nRF24 tab pages (P15, P16, P18):** clean, including sniffbeacon/sniffbt command names, ScanWin values, and the nRF24 wiring requirements.
- **GPS (P20–P22):** all three page quotes match exactly; privacy claim (trail RAM-only, never logged) intact.
- **TX (P24–P26):** clean; bounded keyed OOK carrier framing, DISARMED-on-launch, visible countdown, Back=stop, auto-disarm, and ExtBand/switch semantics all verified — no wording weakens the safety contract.
- **Settings and session evidence (P30–P36):** clean, including group memberships, defaults (Sound/Vibro off), file names, path, ordinals privacy, GPS-Log gating, and FullSweep timeouts/banner.
- **Scope safety overall:** no jamming, deauth, capture/replay, or denial capability is claimed anywhere; honest-limits lines are quoted, not paraphrased away. The only scope-wording fix needed is the P13 title (correction 4).
