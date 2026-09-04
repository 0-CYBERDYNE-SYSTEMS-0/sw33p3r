# Room Sweep — Field Guide: copy deck + design brief (single source of truth)

This document drives two deliverables: a landscape-A4 PDF
(`docs/room_sweep_field_guide.pdf`) and a promo HTML page
(`docs/room_sweep_field_guide.html`). Screenshots live in
`docs/field_guide_shots/*.png` (nearest-neighbor 8x upscales of real 128x64
device captures; display them with crisp/pixelated rendering, never smooth
interpolation).

Facts come from `USER_GUIDE.md`, `specs/`, `features.json`, and the actual
captures. Do not invent features. Tone: confident field-manual, honest limits
worn as a badge. Detect/analyze only — never imply jamming, deauth,
capture/replay, or denial.

---

## 1. Global design brief

Brand (from the published control map `docs/room_sweep_control_map.html`):

| Token | Value | Use |
|---|---|---|
| `--bg` | `#07101f` | page background (midnight blue) |
| `--box` | `#101a32` | cards/panels |
| `--head` | `#0c1730` | page header band |
| `--line` | `#2c456e` | borders/dividers |
| `--fg` | `#ffc08a` | body copy (peach-amber phosphor) |
| `--dim` | `#c4895c` | secondary copy, captions |
| `--hi` | `#ffbf70` | highlights, control chips |
| `--ivory` | `#f3ead6` | headings, big titles |

Type: monospace stack (`ui-monospace, "SF Mono", Menlo, Consolas, monospace`)
everywhere; headings uppercase with `letter-spacing: 0.06–0.08em`.

Per-section accent (thin rules, eyebrow labels, tab chips only — never body
text): RF `#62d392` · Analyzer `#4fd5ce` · Wi-Fi `#4fd5ce` · BLE `#91baff` ·
nRF24 `#7ee0a3` · GPS `#ffcf87` · TX `#ffb45b` · Info `#f3ead6` · Settings
`#c4895c`.

Screenshot treatment: the captures are dark-pixels-on-white (the inverted
`tx_transmitting` and desktop frames render white-on-dark). Present each on a
white `#f4f1ea` "LCD card" with a 1px `#2c456e` border, small corner radius,
generous padding, and a caption bar under it in `--dim` mono. Render pixels
crisp (`image-rendering: pixelated`; no smoothing). Standard display size
~448x224 CSS px (grids may go smaller, ~300x150 per cell).

Control chips: small rounded boxes, 1px dashed `--line` border, key name in
`--hi`, action in `--fg` (e.g. `OK` → `start / cancel sweep`).

Footer on every page: `ROOM SWEEP — FIELD GUIDE` left, section name center,
page number right.

Page order and content follow §2 exactly. Thirty-nine pages (P01–P39).

---

## 2. Page-by-page copy

### P01 — Cover
- shots: none required (optional decorative strip of `rf_survey.png`,
  `wifi_analyzer_p2.png`, `gp_summary.png` at small size, dimmed)
- eyebrow: `FLIPPER ZERO EXTERNAL APP · MNTM-012 · API 87.1`
- title: `ROOM SWEEP`
- subtitle: `A field guide to the receive-side room survey app`
- lede: One Flipper. Every signal in the room, mapped: Sub-GHz RF, Wi-Fi
  beacons, Bluetooth advertisements, 2.4 GHz nRF24 energy, and GPS — surveyed
  passively, logged honestly, and explained screen by screen.
- footer line: `Detect & analyze only — no jamming, no deauth, no capture/replay.`

### P02 — Mission: hear the room, touch nothing
- shot: `i_p4.png` (LIMITS 5/5: RSSI != distance / no jam / no deauth /
  beacon != telemetry / no hit != absence)
- section: Info · accent ivory
- title: `Hear the room. Touch nothing.`
- what-it-does: Room Sweep is a survey instrument, not a weapon. It listens to
  four radio fronts plus GPS and turns what it hears into meters, maps, and
  session files. The LIMITS page is printed on the device itself: RSSI is not
  distance, a beacon is not telemetry, and no hit is not proof of absence.
- how-to-use: Read the LIMITS page (Info tab, page 5/5) before you trust any
  reading. Every screen in this guide keeps those honesty rules.
- chips: `L / R` → change tab · `Back` → settings · `Hold Back` → exit

### P03 — Quickstart: useful in 30 seconds
- shots: `00_launch.png` + `i_p2.png` (2-up; captions "Launch lands on RF
  Survey, already listening" / "KEYS page — the whole control map, on device")
- section: Info · accent ivory
- title: `Launch. Listen. Sweep left to right.`
- what-it-does: The app opens straight onto the RF Survey screen with the radio
  already running — no menus to dig through. Seven tabs ride one Right-press
  apart: RF → Wi → BT → nR → GP → TX → i.
- how-to-use: Short Left/Right changes tab. Hold Left opens the analyzer.
  Hold Right pages inside a mode. OK performs the main action, Back opens
  settings, Hold Back exits. Footers on every screen remind you what L / R / OK
  do right there.

### P04 — RF · Survey
- shot: `rf_survey.png`
- section: RF · accent `#62d392`
- title: `Survey — the room's RF pulse, live`
- what-it-does: Continuous listen across the CC1101 preset bank (300–348 / 387–464 /
  779–928 MHz hardware bands). Each bar is one preset; the ruler underneath marks
  304 / 390 / 434 / 450. The header tracks the hottest preset's dBm and raises a
  `SIGNAL!` badge when energy crosses the dashed alert line — a fixed −75 dBm
  threshold. Baseline (Settings → Session → Baseline) is a separate snapshot of
  the room's RF floor that the survey bars are drawn against.
- how-to-use: Leave it running and watch which bars breathe. Up/Down switches RF
  sub-mode (Survey / Sweep / Peak / Waterfall). Hold Up/Down opens the lock card
  from anywhere; Hold Left drops into the analyzer.
- chips: `Up/Down` → sub-mode · `Hold Up/Down` → lock card · `Hold Left` → analyzer

### P05 — RF · Sweep
- shot: `rf_sweep.png`
- section: RF
- title: `Sweep — walk a whole band, mechanically`
- what-it-does: `[SWEEP EXT]` steps the external CC1101 across the selected band
  (here 300–348 MHz), measuring channel by channel instead of preset hops. It is
  the slow, thorough cousin of Survey — the way to find a lone device that never
  sits on a preset frequency.
- how-to-use: OK starts or cancels the sweep. While idle on the map, Hold Right
  steps the band. Results can be locked with Hold OK.
- chips: `OK` → start / cancel · `Hold Right` → step band · `Hold OK` → lock result

### P06 — RF · Peak
- shot: `rf_peak.png`
- section: RF
- title: `Peak — close in on one carrier`
- what-it-does: `[PEAK EXT]` refines around the strongest carrier and reports its
  center frequency — here 329.999 MHz. Sweep finds the neighborhood; Peak
  narrows to the doorstep.
- how-to-use: OK starts or cancels a refine pass. Up/Down changes RF sub-mode;
  the result can be locked from the lock card.
- chips: `OK` → start / cancel refine · `Hold OK` → lock result

### P07 — RF · Waterfall
- shot: `rf_waterfall.png`
- section: RF
- title: `Waterfall — ten seconds of RF history`
- what-it-does: A scrolling spectrum of the 16 presets: newest column on the
  right, roughly 2.5 snapshots per second, with per-channel peak-hold dots down
  the right edge and the dashed alert-threshold line across the chart. Purely
  passive — bursty traffic that Survey's eye might miss leaves a trail here.
- how-to-use: Nothing to drive; read the room's last ~10 seconds at a glance.
  Hold Up/Down opens the lock card, Hold Left opens the analyzer.
- chips: `passive` → just watch · `Hold Up/Down` → lock card · `Hold Left` → analyzer

### P08 — RF · Lock card
- shot: `rf_lockcard.png`
- section: RF
- title: `Lock card — name your target`
- what-it-does: `CANDIDATE 330.000 MHz · -65 dBm · fresh`. From any RF sub-mode,
  Hold Up/Down slides the card over the map: the strongest candidate, its exact
  frequency, level, and how fresh the hit is. Hold OK pins it as the lock target
  that the analyzer then hunts.
- how-to-use: Hold Up/Down opens/closes (press again to return to the map).
  Hold OK locks the candidate; Up/Down backs out.
- chips: `Hold Up/Down` → open / close · `Hold OK` → lock target

### P09 — Analyzer · one suite, four radios
- section: Analyzer · accent `#4fd5ce`
- shots: `rf_analyzer_p0.png`, `rf_analyzer_p1.png`, `rf_analyzer_p2.png`,
  `rf_analyzer_p3.png` as a labeled 2x2 grid (cells: HUNT / FIELD / RADAR / METER)
- title: `Hold Left: the four-page analyzer`
- what-it-does: On RF, Wi-Fi, BLE, and nRF24 alike, Hold Left opens a four-page
  analyzer tuned to the selected (or locked) source. HUNT: one fat proximity bar
  with CLOSER / FARTHER / STABLE / STALE / LOST verdicts. FIELD: spectrum/peer bars for
  context. RADAR: polar view where rings are RSSI (labeled on screen — not
  meters) and a locked target blinks as a diamond. METER: a big-number dBm read
  with peak hold and trend.
- how-to-use: Hold Right turns the analyzer's pages. Fresh samples move the bar;
  silence ages it out — on Wi-Fi/BLE the bar fades after ~2 s and reads LOST by
  ~6 s. The scan stays alive while the analyzer is open.
- chips: `Hold Left` → analyzer on/off · `Hold Right` → analyzer page

### P10 — Analyzer · Wi-Fi
- shots: `wifi_analyzer_p0.png`, `wifi_analyzer_p1.png`, `wifi_analyzer_p2.png`,
  `wifi_analyzer_p3.png` 2x2 grid (HUNT / FIELD / RADAR / METER)
- section: Analyzer
- title: `Wi-Fi analyzer — walk an AP to its door`
- lede: Hunting `REDACTED-SSID_3TFXQX` at -37 dBm, CLOSER 91%.
- what-it-does: The Hunt bar tracks that one AP's live table RSSI from the
  Marauder beacon stream. If beacons stop, the bar fades to LOST rather than
  pretending — the meter tells the truth about silence.
- how-to-use: From the Wi-Fi tab, select the AP with Up/Down, then Hold Left.
  Hold Right pages Hunt → Field → Radar → Meter.

### P11 — Analyzer · Bluetooth
- shots: `bt_analyzer_p0.png`, `bt_analyzer_p1.png`, `bt_analyzer_p2.png`,
  `bt_analyzer_p3.png` 2x2 grid (HUNT / FIELD / RADAR / METER)
- section: Analyzer
- title: `BLE analyzer — chase an advertisement`
- lede: One device, `00:11:22:33:44:5a`, from -72 dBm, CLOSER at 47%.
- what-it-does: Same four pages, aimed at a BLE advertiser. The radar's sweep
  line and blinking diamond mark your locked target; the meter's PK value keeps
  the strongest sighting while the trend arrow says whether you're gaining.
- how-to-use: Select the device in the BT tab, Hold Left to open, Hold Right to
  page. The analyzer feeds from `sniffbt` and keeps it running.

### P12 — Analyzer · nRF24
- shots: `nr_analyzer_p0.png`, `nr_analyzer_p1.png`, `nr_analyzer_p2.png`,
  `nr_analyzer_p3.png` 2x2 grid (HUNT / FIELD / RADAR / METER)
- section: Analyzer
- title: `nRF24 analyzer — 2.4 GHz, the honest zero`
- lede: `2.4G RPD` with nothing to hear reads LOST 0% — and says so.
- what-it-does: The analyzer works over the nRF24 RPD (receive power detector)
  energy survey. Empty spectrum shows an empty instrument, not a fake signal:
  -127 dBm, LOST 0%.
- how-to-use: Open from the nR tab with Hold Left. Requires SPI Path = nRF24 and
  the BFFB bottom switch DOWN.

### P13 — Wi-Fi · Listening
- shot: `wifi_p0.png`
- section: Wi-Fi · accent `#4fd5ce`
- title: `Beacon listening, on your marks`
- what-it-does: `Wi scan 0/0 · Listening… · beacon only`. The Wi-Fi tab drives a
  fixed Marauder scan (`sniffbeacon`) — access-point beacons only. The window
  length comes from Settings → Wireless → ScanWin (15 / 30 / 60 s).
- how-to-use: OK restarts the scan; the `Wi scan a/b` counter is your selection
  position within the APs heard (`0/0` = none yet, `1/2` = first of two). Hold
  Right pages Detail / List / Help; Hold Left opens the analyzer.
- chips: `OK` → start / restart scan · `Hold Right` → page · `Hold Left` → analyzer

### P14 — Wi-Fi · Detail
- shot: `01_after_right.png`
- section: Wi-Fi
- title: `Detail — one AP, four facts`
- lede: `GIII Mechanical · -49 dBm · Ch2 · 00:11:22:33:44:5b`
- what-it-does: The Detail page shows the selected AP's name, live RSSI, channel,
  and MAC, plus a status line that reads `beacon` when unlocked and `LOCK on`
  once you lock the AP. This is the record you lock for the analyzer.
- how-to-use: Up/Down selects; Hold OK locks/unlocks the AP for follow; Hold Left
  analyzes the selected or locked AP.
- chips: `Up/Down` → select AP · `Hold OK` → lock / unlock · `Hold Left` → analyzer

### P15 — Wi-Fi · List + Help
- shots: `wifi_p1.png` + `wifi_p2.png` (2-up; captions "LIST — strongest first,
  inverted row is your selection" / "HELP — the page's controls, on screen")
- section: Wi-Fi
- title: `List the room, then read the help`
- what-it-does: The List page stacks the heard APs, strongest first, with your
  selection inverted. The Help page prints the controls so the app teaches
  itself on a device with no manual.
- how-to-use: Up/Down moves the selection; the footer always states what L / R /
  OK do here.
- chips: `Up/Down` → browse · `R` → next page · `ScanWin` → window in Settings

### P16 — BLE · Detail
- shot: `bt_p0.png`
- section: BLE · accent `#91baff`
- title: `Bluetooth — same discipline, different traffic`
- lede: `00:11:22:33:44:5a · -73 dBm · n1 · adv`
- what-it-does: The BT tab runs Marauder's `sniffbt` and shows advertisements:
  MAC, level, count, and advertisement type. Everything the Wi-Fi tab does, it
  does for BLE.
- how-to-use: OK restarts the scan, Hold OK locks the device, Hold Left opens
  its analyzer, Hold Right pages Detail / List / Help.
- chips: `OK` → scan · `Hold OK` → lock · `Hold Left` → analyzer

### P17 — BLE · List + Help
- shots: `bt_p1.png` + `bt_p2.png` (2-up; captions "LIST — strongest first;
  the inverted row is your selection" / "HELP — ScanWin window applies here too")
- section: BLE
- title: `Lock a tracer, keep your place`
- what-it-does: Locking a device tags the page header with ` L` and flips the
  Detail line to `LOCK on`; newer advertisements keep arriving around your
  selection. The help page mirrors the BT controls.
- how-to-use: Up/Down browse, Hold OK lock/unlock, R page.
- chips: `Hold OK` → lock / unlock · `R` → next page

### P18 — nRF24 · Status
- shot: `nr_p0.png`
- section: nRF24 · accent `#7ee0a3`
- title: `nRF24 — the 2.4 GHz energy survey`
- what-it-does: `nR STATUS · SPI CC1101 · phase idle`. This tab surveys raw
  2.4 GHz channel energy with the nRF24 in RPD mode — receive only, by design.
  The status page names your wiring truth: it needs SPI Path = nRF24 and the
  BFFB bottom switch DOWN, and while nRF24 is selected the Sub-GHz radio falls
  back to the Flipper's internal CC1101.
- how-to-use: OK starts/stops an RPD pass; Hold Left or Hold OK opens the
  analyzer; Right shows results.
- chips: `OK` → start / stop pass · `Hold Left` → analyzer · `R` → results

### P19 — nRF24 · Results
- shot: `nr_p1.png`
- section: nRF24
- title: `Results — active channels, top hitters`
- lede: `active 0 · hits 0 · no RPD energy yet` — an honest empty.
- what-it-does: After a pass, Results lists how many 2.4 GHz channels showed
  energy and which channels hit hardest. Cordless phones, keyboards, drones,
  Wi-Fi overlap — anything pulsing 2.4 GHz leaves a count.
- how-to-use: OK rescan; Hold Left opens the analyzer on the results.
- chips: `OK` → rescan · `Hold Left` / `Hold OK` → analyzer

### P20 — GPS · Summary
- shot: `gp_summary.png`
- section: GPS · accent `#ffcf87`
- title: `GPS — the room gets coordinates`
- what-it-does: The Summary page carries time, a satellite bar (here 0/0, no
  fix), position, and speed/course once a fix lands. OK drops a mark when the
  NMEA stream is fresh; if the fix is stale or silent, the same press retries
  the source.
- how-to-use: Hold OK always retries the source — reinit the stream, swap the
  GPIO baud 9600 ↔ 115200, or fall back to Marauder's `nmea`. Up/Down pages
  Summary → Detail → Radar.
- chips: `OK` → set mark / retry · `Hold OK` → retry source · `Up/Down` → page

### P21 — GPS · Detail
- shot: `gp_detail.png`
- section: GPS
- title: `Detail — trust, but count the sentences`
- what-it-does: `Src:BFFB Marauder · NMEA:12 nav:5 bytes:5 · Age:0s drops:0`.
  The Detail page shows which GPS source is live, the date, NMEA/nav counters,
  the age of the last fix, and dropped sentences — so a confident-looking
  position can be checked against its plumbing.
- how-to-use: Read Age before trusting the Radar. OK still sets a mark from
  here; Up/Down or Right pages on.
- chips: `OK` → mark · `Hold OK` → retry source · `U/D | R` → page

### P22 — GPS · Radar (walk to target)
- shot: `gp_radar.png`
- section: GPS
- title: `Radar — real meters, walk to the mark`
- lede: `Radar needs a mark · OK=mark, then walk`
- what-it-does: Set a mark and the radar centers on it, north-up, in real
  meters: your live position is the blip at true bearing and true haversine
  distance, rings auto-scale from 2 m to 1 km, and your last ~8 fixes draw the
  approach trail. The Flipper has no compass — face the blip toward the top of
  the screen and walk straight at it. The trail lives in RAM only and is never
  logged.
- how-to-use: OK sets the mark (needs a fresh fix), then walk. The cross at
  center is the mark; distance to it shows in the header.
- chips: `OK` → set mark · `north-up` → steer blip to screen-top

### P23 — TX · Disarmed by default
- shot: `tx_default_300.png`
- section: TX · accent `#ffb45b`
- title: `TX — a bounded carrier tool, born disarmed`
- what-it-does: `DISARMED · 300MHz: INT radio on`. The TX tab is a test
  transmitter, not an attack mode: it keys a plain OOK carrier on a preset —
  never replay, never blocking, never continuous. It wakes up DISARMED every
  single launch, and preflight checks frequency validity, regional rules, and
  the external-band policy before anything radiates.
- how-to-use: Short OK arms. Up/Down steps presets (300 / 400 / 900 families).
  Back disarms at any moment.
- chips: `OK` → arm · `Up/Down` → preset · `Back` → disarm

### P24 — TX · Armed
- shot: `tx_armed.png`
- section: TX
- title: `ARMED — the radio knows its limit`
- what-it-does: `!! ARMED !! · Preset carrier only · 434.420 MHz 3s max`. The
  banner is impossible to miss, the screen names the ceiling, and the length
  comes from Settings → Radio → TXDur (1–10 s, never more).
- how-to-use: Hold OK transmits the bounded carrier and the countdown starts.
  Nothing here latches: the arm state exists only to make a deliberate press
  required.
- chips: `Hold OK` → transmit (bounded) · `U/D` → change preset first

### P25 — TX · Transmitting
- shot: `tx_transmitting.png`
- section: TX
- title: `TX API ACTIVE — seconds, not minutes`
- what-it-does: `434.419 MHz · 2.1s remaining · Back=stop`. A live carrier shows
  its own countdown and its own kill switch. At zero the radio disarms itself;
  Back ends it early; every attempt is written to the session log as an event.
- how-to-use: Back stops immediately. That is the whole interaction — a keyed
  burst with a visible fuse.
- chips: `Back` → stop now · `auto` → disarm at 0 s

### P26 — TX · After disarm + switches
- shot: `tx_after_disarm.png`
- section: TX
- title: `Back to DISARMED — hardware tells the truth too`
- what-it-does: After auto-disarm the tab returns to `DISARMED` and states its
  guardrails in plain text: `Carrier only; no replay`. `sw:400` reads the
  physical BFFB top switch so the screen always agrees with the hardware band
  you actually selected.
- how-to-use: While DISARMED on the external CC1101, Hold Left/Right steps
  ExtBand (AUTO / 400 / 900). Flip the top switch to match.
- chips: `Hold L/R` → ExtBand (disarmed, ext radio) · `top switch` → 400 / 900

### P27 — TX · Presets
- shot: `tx_preset_down2.png`
- section: TX
- title: `Presets — 300, 434, 450, and a wall of rules`
- lede: `450.000 · 3s · sw:400`
- what-it-does: Preset stepping walks twelve stops across the 300 / 400 / 900 MHz
  families and back; the CC1101's real band gaps (300–348 / 387–464 / 779–928 MHz)
  are enforced in hardware, not politely suggested. Regional allow-lists and the ExtBand policy gate every
  attempt in preflight.
- how-to-use: Up/Down cycles presets while armed or disarmed; the footer repeats
  the active preset and duration.
- chips: `Up/Down` → step preset · `TXDur` → 1–10 s in Settings

### P28 — Info · Radio & State
- shots: `i_p0.png` + `i_p1.png` (2-up; captions "1/5 RADIO — path, SPI,
  Marauder, GPS source" / "2/5 STATE — record, baseline, lock, UART")
- section: Info · accent ivory
- title: `The Info tab: what the app believes, published`
- what-it-does: Page 1/5 prints the live radio path (`RF:EXT SPI:CC1101`,
  `Marauder:confirmed` — confirmed means a real Marauder response, not just a
  cable). Page 2/5 prints session state: record on/off and drops, baseline,
  lock target, dump lines, UART counters.
- how-to-use: Hold Right pages through Info; everything here is also in the
  session files.
- chips: `Hold Right` → next page

### P29 — Info · Keys, Files, Limits
- shots: `i_p2.png` + `i_p3.png` + `i_p4.png` (3-up; captions KEYS / FILES /
  LIMITS)
- section: Info
- title: `A cheat sheet, a paper trail, and a conscience`
- what-it-does: Pages 3–5: KEYS (the full control map), FILES (SD path and the
  last report number), LIMITS (the honesty page — the same rules this guide
  opened with). The app carries its own manual and its own disclaimers.
- how-to-use: Everything on these pages is on-device — no website required in
  the field.
- chips: `Hold Right` → next page

### P30 — Settings · Feedback
- shots: `set_feedback_sound.png` + `set_feedback_vibro.png` (2-up; captions
  "Sound — off by default" / "Vibro — off by default")
- section: Settings · accent `#c4895c`
- title: `Settings: feedback you can feel, silence you can keep`
- what-it-does: Back from any tab opens Settings. Feedback groups Sound and
  Vibro, both defaulting off. With Vibro on, a short tap gives one soft pulse
  and a hold a double pulse on release — holds register distinctly without
  looking at the screen. Every toggle confirms with the same tick.
- how-to-use: Hold Left/Right (or short) changes group; Up/Down moves inside the
  group; OK toggles.
- chips: `Back` → open settings · `L / R` → group · `OK` → toggle

### P31 — Settings · Wireless
- shots: `set_wireless_rescan.png` + `set_wireless_scanwin.png` (2-up; captions
  "Rescan — auto-restart the Wi/BT window" / "ScanWin — 15 / 30 / 60 s")
- section: Settings
- title: `Wireless: keep the windows open`
- what-it-does: Rescan ON restarts the Wi-Fi/BLE scan window automatically so a
  busy room keeps refreshing instead of freezing on one pass. ScanWin sets that
  window: 15, 30, or 60 seconds.
- how-to-use: Defaults are sensible; shorten ScanWin for snappier lists, lengthen
  it for quiet rooms.
- chips: `OK` → toggle / cycle

### P32 — Settings · Radio
- shots: `set_radio_extband.png` + `set_radio_spipath.png` +
  `set_radio_txdur.png` (3-up; captions "ExtBand" / "SPI Path" / "TXDur")
- section: Settings
- title: `Radio: one SPI bus, two front-ends, one fuse`
- what-it-does: ExtBand picks the external CC1101 band policy (AUTO / 400 /
  900). SPI Path switches the BFFB's bottom SPI switch role between CC1101 and
  nRF24 — and choosing nRF24 forces Sub-GHz onto the internal CC1101 so the two
  radios can never fight. TXDur caps every carrier at 1–10 s.
- how-to-use: Set SPI Path to nRF24 only with the bottom switch DOWN; the nR tab
  status page repeats the requirement.
- chips: `ExtBand` → AUTO/400/900 · `SPI Path` → CC1101/nRF24 · `TXDur` → 1–10 s

### P33 — Settings · GPS
- shots: `set_gps_src.png` + `set_gps_log.png` (2-up; captions "GPS Src — BFFB
  stream or GPIO LPUART" / "GPS Log — coordinates on/off")
- section: Settings
- title: `GPS: pick the pipe, gate the privacy`
- what-it-does: GPS Src chooses between the BFFB Marauder NMEA stream and the
  Flipper GPIO LPUART (15/16 @ 9600). GPS Log decides whether coordinates are
  written to session files at all — off, and the logs stay coordinate-free by
  design.
- how-to-use: If the source goes quiet, Hold OK on any GPS page retries it —
  including a GPIO baud swap 9600 ↔ 115200.
- chips: `GPS Src` → BFFB / GPIO · `GPS Log` → on / off

### P34 — Settings · Session
- shots: `set_session_record.png` + `set_session_baseline.png` +
  `set_session_dump.png` + `set_session_fullsweep.png` (4-up; captions RECORD /
  BASELINE / RAW DUMP / FULLSWEEP)
- section: Settings
- title: `Session: evidence, not noise`
- what-it-does: Record opens session N and writes `session-N.csv` plus a Room
  Report. Baseline snapshots the RF survey floor so `SIGNAL!` means something.
  Raw Dump captures a bounded UART snapshot (`uart-N.txt`) — the only file that
  can contain raw identifiers. FullSweep launches the whole-room sequence.
- how-to-use: Recording is size-capped; if logging stops, scanning doesn't.
- chips: `OK` → start / stop · `Baseline` → snapshot floor

### P35 — FullSweep
- shot: `set_session_fullsweep.png`
- section: Settings
- title: `FullSweep — one press, the whole room, a report`
- what-it-does: FullSweep sequences RF (10 s) → Wi-Fi (15 s) → BLE (15 s) →
  nRF24 (12 s) → GPS (8 s) with hard timeouts, skipping nRF24 immediately if
  the SPI path isn't nRF24. It closes the session, writes `report-N.txt`, and
  prints an inverted `SWEEP DONE · saved report-N` banner. Progress rides the
  top strip (`FULL RF 10s`) while tab footers stay visible.
- how-to-use: Settings → Session → FullSweep → OK, pocket the Flipper, walk the
  room, come back to a finished report.
- chips: `OK` → start sequence · `top strip` → live phase

### P36 — Session files
- shot: `i_p3.png`
- section: Info
- title: `The paper trail: three files, no surprises`
- what-it-does: Everything lands in `/ext/apps_data/room_sweep/`:
  `session-N.csv` (events, with Wi-Fi/BLE identifiers as session ordinals, not
  raw names), `report-N.txt` (the plain-text Room Report), and `uart-N.txt`
  (only if you asked for Raw Dump). Every TX attempt is recorded as an event.
- how-to-use: Copy the three files off SD after a job; the Info → FILES page
  shows the path and last report number on device.
- chips: `csv` → events · `txt` → report · `uart` → only on request

### P37 — Built for the BFFB (and without it)
- shot: `i_p0.png`
- section: Info
- title: `Plays with the big board, works with the little one`
- what-it-does: With a BFFB attached, Room Sweep drives the dual CC1101 and the
  nRF24 over the Flipper SPI bus and talks to the Marauder ESP32 over USART
  13/14 @ 115200 — the Marauder stream carries the Wi-Fi/BLE scans, while GPS
  prefers the Flipper GPIO LPUART (15/16 @ 9600) with Marauder `nmea` as the
  fallback. Without it, the app falls back to the Flipper's internal CC1101 —
  RF survey and TX still work, honestly labeled `INT`.
- how-to-use: The Info → RADIO page tells you which world you're in: EXT vs
  INT, SPI path, `Marauder:confirmed`.
- chips: `BFFB` → optional · `internal CC1101` → fallback

### P38 — Clean exit
- shot: `99_desktop_after_exit.png`
- section: Info
- title: `Leaves as quietly as it listens`
- what-it-does: Hold Back exits; the radio closes, the UART expansion port is
  restored, and the Momentum desktop comes back untouched. Nothing keeps
  running after the app is gone.
- how-to-use: Build from source with `./init.sh` (host tests + firmware build),
  deploy with `ufbt launch`. Targets Momentum mntm-012, API 87.1.
- chips: `Hold Back` → exit · `./init.sh` → build gate

### P39 — Back cover
- shots: none (typographic page)
- title: `ROOM SWEEP`
- lede: Detect & analyze only. No jamming, no deauth, no capture/replay, no
  denial. RSSI is not distance; a beacon is not telemetry; no hit is not proof
  of absence.
- lines: Companion files: `room_sweep_control_map.html` (one-page control map)
  · this guide as PDF · `USER_GUIDE.md` in the repo. Firmware: Momentum
  mntm-012 · API 87.1 · appid `room_sweep`.

---

## 3. Notes for builders

- 39 pages total, in §2 order. PDF: one `@page` per entry (P39 is the last;
  P01 cover, P39 back cover). HTML: one `<section>` per entry with the sticky
  tab nav grouping (RF P04–08, Analyzer P09–12, Wi-Fi P13–15, BLE P16–17,
  nRF24 P18–19, GPS P20–22, TX P23–27, Info P28–29 + P36–38, Settings
  P30–35, plus intro P01–03).
- Image paths are relative: `field_guide_shots/<file>`.
- Never upscale screenshots with smoothing; crisp pixels only.
- Do not add feature claims beyond this document.
