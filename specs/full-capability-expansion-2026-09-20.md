# Spec: Full capability expansion — "identify every emitter" (2026-09-20)

Orchestrator: main agent, one phase per iteration (PROMPT.md loop). No parallel
agents inside a phase.
Restore point: tag `restore/pre-capability-expansion-2026-09-20` at current
`main` HEAD before Phase 1. Work branches: `feat/<phase-name>-2026-09` per phase.

## Problem

Room Sweep detects routers, BLE advertisers, sub-GHz energy, and raw 2.4 GHz
energy. The gap audit (2026-09-20) found the device can still — with the exact
same hardware — see client devices, recover hidden SSID names passively, name
vendors, flag cloned SSIDs, detect stalking trackers, detect someone ELSE
attacking the room, log RF bursts with durations, and remember devices across
sessions. None of that exists today.

Scope of this spec: **everything this hardware can still do, receive-side only.**
TX behavior is untouched. MISSION.md unchanged.

## Hardware status at spec time (truth note)

On 2026-09-20 no USB serial device (Flipper or BFFB ESP32) was enumerable from
the host (`/dev/cu.usb*` empty, IOUSB tree empty, no BT pairing). Therefore
**Phase 0 is mandatory and gates Phases 1, 4, 5, and 10**: no command is sent
and no output format is parsed until the probe transcript verifies it on THIS
firmware build. Everything Phase 0 confirms gets written into
`docs/BFFB_MOMENTUM.md` (the source of truth) with the raw transcript saved to
`.omo/evidence/` (local-only, never committed).

### Phase 0 — COMPLETE (2026-09-20)

Device `Rug1k0`, mntm-012/API 87.1/target 7 confirmed over RPC. The prior
sessions' raw dumps supplied this build's `help` transcript, and the USB-UART
bridge (started on-device with the host session closed — it refuses while any
host session is connected) carried the full 16-step probe battery
(`tools/marauder_probe.py`; transcripts in `.omo/evidence/marauder-probe-*`,
formats embedded in `docs/BFFB_MOMENTUM.md`). Verified verdicts:

- **Formats pinned:** `sniffbeacon` (re-verified; `> ` prefixes only the
  first line), `scanap` (AP line with TRAILING SPACE plus a separate
  `Beacon: a b c d` line — values treated as opaque; the old same-line
  `00 00` suffix did NOT appear on this build), `list -a`
  (`[n][CH:c] SSID <raw byte>`), `sniffraw` (`RSSI: -NN Ch: n BSSID: m` per
  802.11 frame, STATIONS included), `sniffbt` (abutting records confirmed;
  `Found N devices` counts SAVED devices, not observed ones).
- **`sniffbt -t airtag|flipper|flock|meta`:** accepted syntactically but NOT
  filtering over serial — identical heterogeneous stream under every filter.
- **`scansta`:** runs but requires `scanap` first; station line format still
  unobserved (no client associated during the capture). `list -s` prints
  `0 selected` when empty.
- **`sniffprobe` / `sniffesp` / `sigmon` / `sniffpwn`:** all start cleanly and
  produced no lines in short windows (nothing to hear) — their formats remain
  unpinned; parsers for them stay gated on a longer capture.
- **`sniffdeauth`, `scanall`:** confirmed ABSENT from this build.

Consequences applied below: Phase 1 cannot claim tracker filtering on this
build; Phase 4 pivots to `sniffraw` as the primary transmitter radar with
`scanap`/`scansta` for AP association; Phase 5 keeps its format gate; Phase 10
pivots to `sniffesp` + `sniffpwn` (hostile-tooling detection: other people's
Marauders/ESP32 spam and Pwnagotchis) replacing impossible deauth detection.

## Global truth contract (applies to every phase)

- Heuristics are labeled as heuristics: **"name suggests", "OUI suggests",
  "possible cloned SSID"** — never "is a camera", "is an attacker".
- Energy is never identity. RSSI is never distance. A MAC is not an owner.
- Every new CSV/detail string must survive the sigint-truth-audit standard
  (`specs/sigint-truth-audit-2026-09-11.md`): no claim the hardware can't back.
- Detect-only: every phase is RX or analysis. Nothing transmits toward a
  target. `sniffpmkid`, `attack*`, karma/rogue-portal/beacon-spam commands are
  **never sent** and are out of scope by MISSION.md (capture/replay + attack).

## Sequenced phases (one feature per iteration)

Order is value-per-cost, cheapest identification wins first. Each phase adds
its own `features.json` entry (`passes: false` at start) and its own host suite
in `init.sh`.

---

### Phase 0 — Host-side Marauder probe + firmware capability receipt

**Goal:** pin down, on the real BFFB firmware, which receive-side commands
exist and their exact serial output, so later parsers parse reality, not wiki
text.

**Deliverables**

- `tools/marauder_probe.py` (host-only, pyserial, never committed to the FAP;
  `tools/` stays out of `application.fam` sources which are `*.c` only).
  Port autodetect: `cu.usbmodem*` (Flipper) vs `cu.usbserial*`/ESP32. Requires
  the BFFB ESP32's own USB (there is no Flipper→ESP32 UART bridge from the
  host console).
- Probe sequence (each step: send, capture 3–6 s transcript, then
  `stopscan`; never leave a scan running):
  1. `stopscan` (quiesce), `version`, `help` (full command list — the receipt)
  2. `list -a`, `list -s`, `clearlist`
  3. `scanall` 4 s → `stopscan` → `list -a` → `list -s` (AP+STA formats,
     capability-byte suffixes)
  4. `scansta` 4 s → `stopscan` → `list -s`
  5. `sniffprobe` 5 s → `stopscan` (line format, incl. hidden-SSID probes)
  6. `sniffdeauth` 4 s → `stopscan` (existence + line format)
  7. `sniffesp` 4 s → `stopscan` (ESP32/other-Marauder beacon detection)
  8. `sniffbt` 3 s; then `sniffbt -t airtag`, `-t flipper`, `-t flock`,
     `-t meta` 4 s each → `stopscan` (verify variants + line format)
  9. `gps -g fix` (sanity, already-known path)
- Transcript → `.omo/evidence/marauder-probe-<date>.txt`.
- `docs/BFFB_MOMENTUM.md`: new command-table rows + exact output formats for
  every confirmed command; explicit "confirmed absent" list for the rest.
- `AGENTS.md`/`CLAUDE.md` fixed-command list updated (they must stay in sync),
  or annotated "policy list unchanged; capability list in BFFB_MOMENTUM.md".

**Verification:** transcript exists; doc updated; no FAP change; no suite
changes. This phase flips no `passes` except its own receipt entry.

**If the ESP32 cannot be enumerated:** stop; Phases 1/4/5/10 do not start.
Phases 2, 3, 6, 7, 8, 9 may proceed (they need no new upstream output).

---

### Phase 1 — BLE tracker sniff (`sniffbt -t`)

**Goal:** detect stalking trackers (AirTag/Find-My-class, Flipper, Flock,
Meta-class) using the firmware's own type filters documented at
`docs/BFFB_MOMENTUM.md:48` but never sent.

**Phase 0 verdict (2026-09-20):** the four `-t` variants are accepted by this
build but do NOT filter the serial stream (identical output under every
filter). This phase may proceed ONLY as: send the variants if desired for
PCAP-side effects, but any tracker labeling must come from host-side
classification (Phase 3 hints + OUI), never from the `-t` argument. Do not
claim "tracker filter" in UI/docs on this build.

**Contract** — extend `room_sweep_marauder.h` (host-tested):

```c
typedef enum {
    BleTargetAll = 0, BleTargetAirtag, BleTargetFlipper,
    BleTargetFlock, BleTargetMeta,
} RoomSweepBleTarget;

/* "sniffbt" / "sniffbt -t airtag" etc. Verified against Phase 0 output. */
const char* room_sweep_marauder_ble_cmd(RoomSweepBleTarget t);

/* True iff a BLE record came from a tracker-filtered scan. Pure labeling. */
bool room_sweep_marauder_ble_is_tracker_scan(const char* cmd_line);
```

**UX**

- Settings menu gains `BLE Target: All / Airtag / Flipper / Flock / Meta`
  (`room_sweep_settings.h`). BT tab scan sends the variant; header shows
  `BT TRK:AIRTAG` when filtered.
- Tracker-scan rows get a `[TRK]` badge on the BT list page and the detail
  page line `tracker-filter scan` — a filter result, **not** a device-type
  identification (the filter matched firmware-side advertising categories).
- Feedback: tracker-filtered rows feed the existing Geiger ladder unchanged;
  no new alarm class yet (that arrives with watchlist Phase 9).

**Files:** `room_sweep.h` (cmd consts), `room_sweep_marauder.h`,
`room_sweep_settings.h`, `room_sweep.c` (send path + badges),
`tests/test_marauder_parse.c`, `tests/test_settings.c`.

**Verification:** `./init.sh` (new command-builder tests) + `ufbt` +
`_verify_api.py` clean; on-device: tracker scan produces `[TRK]` rows; CSV
events carry `submode=TRK` and detail `filter=airtag`.

---

### Phase 2 — OUI vendor lookup

**Goal:** name the vendor behind every MAC we already capture. README.md:134-140
explicitly lists this as deferred future work; this phase delivers it.

**Contract** — new header `room_sweep_oui.h` (Flipper-free, host-tested):

```c
/* Curated prefix table, const, flash-resident. 3-byte OUI + short label. */
/* Lookup by "AA:BB:CC:..." (any case). Returns NULL when unlisted —
   callers print "vendor unlisted", never guess. */
const char* room_sweep_oui_lookup(const char* mac);
```

Curated set (≈50 entries, one line each, trimmed by Phase 0 observation):
Apple, Samsung, Google, Espressif (ESP32 — flags other dev boards/Marauders),
DJI (drones), Hikvision, Dahua, Reolink, TP-Link, Netgear, Arlo, Ring, Xiaomi,
Huawei, Tile, Garmin, Logitech, Amazon, Microsoft, Intel, Realtek, Raspberry
Pi Trading, Roku, Tuya, and the locally-administered/randomized prefix range
(`x2/ x6/ xA/ xE` second nibble) labeled `randomized` — a randomized MAC is
itself surveillance-relevant telemetry (privacy-conscious device or spoofing).

**UX**

- WiFi/BLE detail pages gain line `Vendor: <name|unlisted|randomized>`.
- List rows unchanged (no room); the Info-tab capability card gains
  "vendor: curated OUI table, not exhaustive".
- CSV: observation `detail` gains `oui=<name>` token.
- Report: "vendors seen: A, B, C" list only — no counts-as-devices claims.

**Files:** `room_sweep_oui.h`, `tests/test_oui.c` (case-insensitivity, unknown,
randomized detection, boundary NUL safety), `room_sweep.c` (draw + enqueue),
`room_sweep_report.h`, `init.sh`.

---

### Phase 3 — Name/SSID heuristic hints

**Goal:** surface likely device classes from self-reported names, honestly
labeled.

**Contract** — new header `room_sweep_classify.h` (host-tested, pure):

```c
typedef enum {
    ClassHintNone        = 0,
    ClassHintCamera      = 1 << 0,  /* ssid/name ~ ipc|cam|hichip|dvr|nvr|cctv */
    ClassHintPrinter     = 1 << 1,
    ClassHintPhoneHotspot= 1 << 2,  /* AndroidAP|iPhone|MiFi … */
    ClassHintIotGeneric  = 1 << 3,  /* tuya|smart|plug|bulb … */
    ClassHintDrone       = 1 << 4,  /* dji|mavic|fpv … */
    ClassHintDevboard    = 1 << 5,  /* esp_|marauder|flipper … */
} RoomSweepClassHints;

RoomSweepClassHints room_sweep_classify_ssid(const char* ssid);
RoomSweepClassHints room_sweep_classify_ble_name(const char* name);
/* Fixed text: "camera?" / "hotspot?" — question mark is part of the API. */
const char* room_sweep_classify_hint_text(RoomSweepClassHints h); /* first hint */
```

- Pattern tables are `static const char* const[]`, lowercase, substring match,
  word-boundary guarded where ambiguity exists. `Hidden/unknown` never hints.
- The question mark in displayed hints is mandatory — it carries the truth
  contract into the UI.

**UX**

- WiFi/BLE list rows: 3-char tag after clipped name (`CAM?`, `HS?`, `IOT?`,
  `DRN?`, `DEV?` — right-aligned column x=112..127, `room_sweep_ui_layout.h`).
- Detail page: `HINT: name suggests camera` + fixed footer
  `hints are name-pattern guesses, not identification`.
- Report: new subsection `Hints (name-pattern guesses only): N entries` with
  per-hint counts; never merges into "devices found".

**Files:** `room_sweep_classify.h`, `tests/test_classify.c` (positive, negative,
false-positive guards: `cameron` must NOT camera-hint — boundary tests),
`room_sweep.c` list/detail draw, `room_sweep_report.h`, `init.sh`.

---

### Phase 4 — Client/station visibility (`scanall`/`scansta`) — **Phase 0 gated**

**Goal:** see the devices *using* the network (streaming cameras are STAs, not
APs — today's biggest blindness).

**Phase 0 verdict (2026-09-20):** `scanall` does not exist here. Primary
mechanism pivots to **`sniffraw`** — verified one line PER 802.11 frame
(`RSSI: -NN Ch: n BSSID: m`), stations included, i.e. a live transmitter
radar. Secondary: `scanap` (note TRAILING SPACE after ESSID + opaque
`Beacon: a b c d` follow-up line) then `scansta` for AP-associated stations
(station line format still unobserved — parser gated on a capture with an
associated client). `sta_upsert` semantics unchanged.

**Contract** — new header `room_sweep_sta.h` (host-tested):

```c
#define MAX_STA_DEVS 10
typedef struct {
    char mac[18];
    char ap_bssid[18];   /* empty when unassociated/probe-only */
    int8_t rssi;
    uint32_t first_seen, last_seen;
    uint16_t observations;
    bool valid;
} StaRec;

/* Parses `list -s` / scanall STA lines — grammar pinned by Phase 0 transcript. */
bool room_sweep_sta_parse_line(const char* line, RoomSweepStaRecord* out);
/* MAC-first upsert into a fixed table; returns row index or -1 when full. */
int room_sweep_sta_upsert(StaRec* table, int max, const RoomSweepStaRecord* obs);
```

**UX (mirrors RF sub-mode precedent)**

- Wi tab gains Up/Down sub-mode cycling: `WI: BEACON` (existing) /
  `WI: CLIENTS` (STA table). Entering CLIENTS sends the verified scan/list
  sequence; leaving sends `stopscan`.
- CLIENTS list page: `RSSI MAC(→AP ordinal)`; detail page: vendor (Phase 2),
  hints (Phase 3), association (shown as the AP row's session ordinal or
  `unassoc`), first/last seen.
- Feedback: STA rows join `room_sweep_feedback_pick_wireless` candidates.
- CSV: `observation` rows with `submode=STA`, identifier ordinal `STA-NN`,
  `detail assoc=<AP ordinal>`.
- Full sweep sequencer: Wi-Fi phase keeps 15 s budget, beacon-first; STA list
  collected only if the phase has ≥4 s remaining (never starves GPS phase).

**Truth rule:** an STA row is "a transmitting client radio heard at this RSSI",
not "a person's phone" and not "the camera" — hints/vendor still carry `?`.

---

### Phase 5 — `sniffprobe` + hidden-SSID recovery — **Phase 0 gated**

**Goal:** recover hidden network names passively from client probe requests,
matched by BSSID (Kismet-style, no deauth). This closes the hidden-network
loop from the 2026-09-20 audit.

**Phase 0 verdict (2026-09-20):** `sniffprobe` exists and starts cleanly on
this build, but no probe lines were observed in a 6 s window — the line format
remains UNPINNED. Before implementing the parser, capture a longer window
(probes are bursty; ForceProbe is already `true`). The parser contract below
is written but must not land until a real fixture exists.

**Contract** — new header `room_sweep_probe.h` (host-tested):

```c
typedef struct {
    char ssid[33];
    char client_mac[18];
    char target_bssid[18];  /* empty on broadcast probes */
    int8_t rssi;
    bool valid;
} RoomSweepProbeRecord;

/* Grammar pinned by Phase 0 transcript. */
bool room_sweep_probe_parse_line(const char* line, RoomSweepProbeRecord* out);

/* Pure repair decision: given a stored hidden AP row (ssid == "Hidden/unknown"
   with a BSSID) and a probe record, return true iff the probe names that
   BSSID's network. Caller performs the rename + history-preserving update. */
bool room_sweep_probe_names_hidden(const WifiAp* ap, const RoomSweepProbeRecord* p);
```

- Rename is history-preserving: same row, same BSSID, `first_seen` intact;
  `observations` continues. One-time CSV event `hidden_resolved` with detail
  `recovered from a client probe request` + the BSSID ordinal.
- Unmatched probes still count as telemetry: a device probing for
  `residential-gateway-5G` is a client looking for a network it "remembers" —
  logged as `observation PROBE` rows with the SSID as advertised identifier
  (ordinal-redacted like everything else).

**UX**

- Wi BEACON list: hidden rows render `[hidden]` + last-4 MAC octets
  (BSSID stays in detail page; list is width-constrained).
- Summary line: `5 AP (2 hidden 1 named*)`; detail page of a repaired row:
  `was hidden; named by client probe`.
- Report: `Hidden-SSID networks: N heard, K named by passive probe recovery`.

**Truth rule:** the recovered name is "the SSID a client broadcast in
cleartext" — strongest identity claim in the app, and it's still just a name.

---

### Phase 6 — Duplicate-SSID / possible-rogue correlation

**Goal:** flag same-name-different-BSSID patterns (evil-twin/cloned hotspot/
mis-configured mesh) using only the existing AP table. Pure analysis — the
data already arrives every scan.

**Contract** — new header `room_sweep_rogue.h` (host-tested, pure):

```c
typedef struct {
    const WifiAp* rows[2];   /* representative pair */
    uint8_t count;           /* BSSIDs sharing the SSID */
} RoomSweepRogueGroup;

/* Exact-SSID (case-sensitive, 802.11 semantics) grouping over the AP table.
   Excludes "Hidden/unknown" labels and rows without BSSID. Returns groups. */
int room_sweep_rogue_scan(const WifiAp* table, int n, RoomSweepRogueGroup* out, int max);
```

**UX**

- Wi detail page of a duped SSID: badge `SAME NAME ON n BSSIDS`.
- Wi list: duped rows get `!` marker (ASCII, FontKeyboard-safe).
- Report section: `Possible cloned SSIDs (same name, different hardware
  address): K group(s)` with the honest footnote that enterprise mesh/roaming
  legitimately shares an SSID across BSSIDs — this flag is a lead, not a verdict.

---

### Phase 7 — Per-device RSSI evidence stats

**Goal:** stop throwing away signal history. Evidence quality: "−38…−72 dBm
over 14 min" instead of one number.

**Contract**

- `WifiAp`/`BleDev`/`StaRec` gain `int8_t rssi_min; int8_t rssi_max; uint32_t
  rssi_sum;` (all int math — `-Wdouble-promotion` is fatal). Maintained in
  `wifi_upsert`/`ble_upsert`/`sta_upsert` (`room_sweep.c`, wiring only; the
  math helper is host-tested):

```c
/* room_sweep_stats.h */
void room_sweep_stats_absorb(int8_t* min, int8_t* max, uint32_t* sum,
                             int8_t rssi, uint16_t obs); /* obs is pre-increment count */
int room_sweep_stats_avg(int32_t sum, uint16_t obs);      /* rounded int */
```

- CSV observation `detail` gains `min=-72 max=-38 avg=-55` tokens.
- Analyzer: locked-target trend now also shows `min/max` from the row (the
  live ring buffer stays as-is).
- Report: strongest-device line gains the range
  (`strongest about XdBm (range −a…−b)`).

**Host suite:** `tests/test_stats.c` (monotonic min/max, sum overflow bounds
at uint32 with obs=65535 worst case: 65535×127 < 2^33 — use uint32 sum of
int8 offsets: store `sum = Σ(rssi + 120)` to stay ≤ 65535×127 ≈ 8.3M, safe).

---

### Phase 8 — RF burst watch mode (lock-and-log)

**Goal:** catch duty-cycled transmitters the hop-dwell scanner misses, and log
burst *timing* — the fingerprint of an audio bug.

**Contract** — new header `room_sweep_watch.h` (host-tested):

```c
typedef struct {
    bool open;
    uint32_t open_tick, last_above_tick;
    int8_t max_rssi;
    uint32_t bursts, total_open_ms;   /* for duty% */
} RoomSweepWatch;

/* 5 ms tick fed from the RF thread. Threshold = ROOM_SWEEP_SIGNAL_THRESHOLD_DBM.
   Opens a burst on rising edge, closes after WATCH_BURST_GAP_MS (600) of
   silence, returns +1 when a burst CLOSES (caller logs it), 0 otherwise. */
int room_sweep_watch_tick(RoomSweepWatch* w, uint32_t now, int8_t rssi, bool rx_active);
```

- New `RfSubWatch` sub-mode: locks frequency (current peak, else selected
  preset), drives the existing RF thread at the 5 ms cadence on ONE channel —
  no hopping, no new thread, stack locals < 64 B (2 KiB stack rule).
- Per closed burst: CSV `observation RF` row, `id=WATCH`, `freq_hz=locked`,
  `rssi=max`, `count=burst_ms`, detail `duty=N% bursts=M`.
- UI reuses the waterfall canvas as a burst timeline strip + lines:
  `BURSTS n  LAST 3.2s  DUTY 4%` and `TUNED 433.920M` — honest units, no
  distance/direction claims.
- Feedback: Geiger ladder follows live RSSI as today; burst close gets the
  existing press-pulse pattern (no new notification sequence — PITFALLS rule).

**Files:** `room_sweep.h` (sub-mode enum + consts), `room_sweep_watch.h`,
`tests/test_watch.c` (edge/gap/duty math, never floats), `room_sweep.c`
(RF thread branch + draw), `init.sh`.

---

### Phase 9 — Cross-session watchlist (opt-in)

**Goal:** "this BSSID was here yesterday" — planted-device detection. Today
nothing is read back across sessions.

**Privacy design (hard requirements)**

- OFF by default; no file is read or written unless the user turns it on.
- The watchlist file `/ext/apps_data/room_sweep/watchlist.txt` stores raw
  `mac,label` lines the user deliberately flagged. Session CSVs keep ordinals
  as today — the raw MAC lives only in the opt-in file, like Raw Dump.
- Flagging is a manual user action per row; there is no automatic collection.

**Contract** — `room_sweep_watchlist.h` (pure logic host-tested; file I/O in
`room_sweep.c` via storage API — every symbol through `_verify_api.py`):

```c
typedef struct { char mac[18]; char label[24]; } RoomSweepWatchEntry;
bool room_sweep_watchlist_match(const RoomSweepWatchEntry* e, const char* mac);
/* parse one "aa:bb:cc:dd:ee:ff,label" line; tolerant of \r and blank lines */
bool room_sweep_watchlist_parse_line(const char* line, RoomSweepWatchEntry* out);
```

**UX**

- Settings: `Watchlist: OFF/ON` (uniform toggle confirmation per house style).
- Detail pages (Wi/BLE): new action `Hold Up = add to watchlist` (with the
  existing hold-confirm pulse) when ON; row then shows `WATCH` badge on every
  future hit; detail line `first flagged: <session ordinal/date>`.
- Watch hits get their own report section `Watchlist matches: N` — this is the
  app's only persistent-identity feature and the report says so explicitly.

---

### Phase 10 — Deauth-frame detection (`sniffdeauth`) — **Phase 0 gated**

**Goal:** ~~deauth detection~~ — **PIVOTED 2026-09-20:** `sniffdeauth` is
confirmed ABSENT from this build. Replacement capability, same mission goal
("tell me about hostile wireless activity in this room"): **`sniffesp`**
(banners as `Starting Espressif device sniff…`) detects other people's
Marauder/ESP32-class beacons and **`sniffpwn`** (`Starting Pwnagotchi sniff…`)
detects Pwnagotchi-class offensive-tooling peers. Both verified to start
cleanly; line formats unpinned pending a capture with a live emitter. UI copy
becomes `TOOLING?` — "device announcing itself like attack tooling", never
"attacker confirmed". The deauth phase is recorded as an honest skip for this
build in features.json.

**Contract** — new header `room_sweep_deauth.h` (host-tested):

```c
#define MAX_DEAUTH_SRCS 8
typedef struct {
    char from_mac[18], to_mac[18];
    int8_t rssi;
    uint32_t first_seen, last_seen;
    uint16_t frames;
    bool valid;
} DeauthRec;

/* Grammar pinned by Phase 0 transcript. */
bool room_sweep_deauth_parse_line(const char* line, RoomSweepDeauthRecord* out);
```

**UX**

- Wi tab third sub-page `WI: ATTACK` (only if Phase 0 confirms the command;
  otherwise the phase is honestly skipped and features.json records that).
- Page shows `DEAUTH FRAMES SEEN` + per-source rows; footer, fixed:
  `this app never transmits — frames observed from another source`.
- Distinct one-shot alert on first detection: a `static const`
  NotificationSequence (PITFALLS: NULL-terminated, force-volume prepended
  messages, delays from the allowed set) — used once per session, not per frame.
- CSV: `observation` rows `submode=DEAUTH`, count=frames, id ordinal `DA-NN`.
- Report section: `Deauthentication frames observed from K source address(es)
  during the session` (attack indicator — no attribution possible).

---

### Phase 11 — Coverage, docs, and full-sweep integration (capstone)

- RF presets: extend `rf_channels`/`rf_labels` 16 → 20, adding 850/880/902/
  915-cored entries labeled for what they are — `850c`/`880c` get the one-line
  hint "cellular uplink overlap; energy here may be a SIM tracker" in
  USER_GUIDE (display label stays 3 chars). `RF_NUM_CHANNELS` consumers
  (waterfall, survey arrays) are dimensioned off the macro — audit each.
- Full sweep sequencer: optional STA + probe phases (Phase 0-verified commands
  only), each with the honest-skip pattern from `room_sweep_full_sweep.h:137`.
- Info tab capability card + README + USER_GUIDE + `docs/` landing pages:
  new "What it can now find" table; every phase's truth line carried over;
  "Not here (yet)" list replaced with what shipped and what hardware still
  cannot see (5 GHz, BT Classic audio, LTE mid-band, wired, silent recorders).
- `features.json`: final verification entry (init.sh + ufbt + _verify_api +
  device deploy receipt).

---

## Never in scope (pinned)

`attack*`, beacon spam, evil portal/karma, `sniffpmkid`, deauth TX, mousejack/
nRF24 packet address recovery, jam/jam-detector TX modes, anything 5 GHz or
cellular-band RX the CC1101 cannot tune. Adding any of these violates
MISSION.md and is rejected per CONTRIBUTING.md.

## Global verification gate (every phase)

`./init.sh` ALL PASS (with that phase's new suite) → `python3 _verify_api.py`
CLEAN → `ufbt` zero warnings → device deploy + on-device receipt
(`.omo/evidence/`) → CSV inspected for the new rows → docs touched in the same
commit → commit on the phase branch, merge to `main`. No co-author trailers.

## Definition of done (program level)

- [ ] Phase 0 transcript exists; BFFB_MOMENTUM.md is the verified truth.
- [ ] Phases 1–11 each landed as one iteration with receipts, or are marked
      honestly skipped with the reason in features.json/progress.log.
- [ ] Every new on-screen/CSV/report string passes the truth-contract reading.
- [ ] No TX-path file changes outside `MARAUDER_CMD_*` additions.
- [ ] USER_GUIDE and README describe the new capabilities in plain language
      with the hardware-invisible list still intact.
