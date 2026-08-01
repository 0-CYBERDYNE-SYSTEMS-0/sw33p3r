# External BFFB UART / feedback audit

**Review mode:** source-only, read-only audit requested by the user. No device, browser, GUI, network, live-service, build, or test execution was performed. The worktree was clean (`git status --porcelain` produced no output); this reviews current source rather than a pending diff.

**Skill-perspective check:** ran. I read `omo:remove-ai-slops` and `omo:programming` before assessing tests and maintainability. The parser itself is appropriate boundary parsing, rather than needless validation. The current source does violate the perspectives through dead/unused state and configuration (MEDIUM below), and it lacks behavior coverage for the external UART paths. The existing NMEA tests are behavior-oriented, not deletion-only, tautological, brittle prompt, or implementation-mirroring tests.

## Status

- `codeQualityStatus`: **BLOCK**
- `recommendation`: **REQUEST_CHANGES**

## Source-verified behavior

### UART and BFFB presence

- At startup, the app disables the expansion module, acquires USART, initializes it at `MARAUDER_BAUD` (115200), and starts async receive. It stores only the acquired handle as availability state: [room_sweep.c:151-165](../../room_sweep.c#L151-L165), [room_sweep.h:45-50](../../room_sweep.h#L45-L50).
- The RX callback reads a byte, conditionally feeds NMEA, and stores CR/LF-delimited raw text in an eight-line rolling buffer; it does not interpret Wi-Fi or BLE output: [room_sweep.c:116-146](../../room_sweep.c#L116-L146).
- Sends transmit the command plus CRLF and do not observe a response or return a send result: [room_sweep.c:181-185](../../room_sweep.c#L181-L185). Exit stops RX, deinitializes/releases serial, and re-enables expansion: [room_sweep.c:168-179](../../room_sweep.c#L168-L179), [room_sweep.c:858-865](../../room_sweep.c#L858-L865).
- There is no handshake, probe, response timeout, or response classification. `app->serial != NULL` is therefore UART ownership, not BFFB presence. The guide explicitly acknowledges this limitation: [ROOM_SWEEP_GUIDE.html:697-701](../../ROOM_SWEEP_GUIDE.html#L697-L701).

### Wi-Fi and BLE

- OK sends `scanap` in Wi-Fi mode and `sniffbt` in BLE mode, then sets one shared state to `MarauderScanning`: [room_sweep.c:821-827](../../room_sweep.c#L821-L827).
- Both tabs draw the same raw rolling lines (five of eight); neither has an output parser, an RSSI field, a meter, a discovered-device/AP model, or a detection callback: [room_sweep.c:408-451](../../room_sweep.c#L408-L451), [room_sweep.c:116-146](../../room_sweep.c#L116-L146).
- Consequently, Wi-Fi RSSI may be visible only if it happens to fit as text in the external firmware's raw output. It is not source-parsed or metered. BLE RSSI has the same status; the source makes no claim that a BLE line contains RSSI.

### GPS

- GPS is passive: no GPS command is sent. Bytes are fed to NMEA only while the GPS tab is selected and a UART handle exists: [room_sweep.c:123-127](../../room_sweep.c#L123-L127), [room_sweep.c:785-794](../../room_sweep.c#L785-L794).
- The UART stays at 115200 for all modes. `GPS_BAUD_ALT` (9600) is declared but never used: [room_sweep.c:160-161](../../room_sweep.c#L160-L161), [room_sweep.h:52-54](../../room_sweep.h#L52-L54).
- The parser accepts checksum-verified GGA/RMC/GLL/ZDA/GSV and exposes time/date, position, quality, satellites, speed, and course: [nmea.c:74-176](../../nmea.c#L74-L176), [nmea.c:179-228](../../nmea.c#L179-L228), [nmea.h:15-43](../../nmea.h#L15-L43). The UI presents fix/no-fix/waiting states and UTC/satellite/position fields: [room_sweep.c:454-515](../../room_sweep.c#L454-L515).

### Feedback

- RF thread drives only RGB LED sequences based on sub-GHz peak RSSI: [room_sweep.c:238-258](../../room_sweep.c#L238-L258). There is no LED path tied to Wi-Fi, BLE, or GPS output.
- `sound_on` and `vibro_on` default to off and are toggleable from buttons/settings, but the feedback function intentionally sends no sound or vibration notification: [room_sweep.c:305-323](../../room_sweep.c#L305-L323), [room_sweep.c:691-692](../../room_sweep.c#L691-L692), [room_sweep.c:750-753](../../room_sweep.c#L750-L753), [room_sweep.c:796-804](../../room_sweep.c#L796-L804). The reset sequences at exit are cleanup, not feedback: [room_sweep.c:840-843](../../room_sweep.c#L840-L843).

## Findings

### CRITICAL

None.

### HIGH

1. **GPS can retain and display a stale valid fix after valid no-fix input.** GGA sets `has_fix`/`has_pos` only in the positive-quality branch; RMC and GLL likewise only set the flags for active status. None clears the flags for a subsequent quality-0/`V` sentence. Once a fix has been received, later valid no-fix sentences do not move the UI to `NO FIX`; it can continue to show `3D FIX` and old coordinates. This is a source-level correctness defect in a live status display. [nmea.c:86-105](../../nmea.c#L86-L105), [nmea.c:106-152](../../nmea.c#L106-L152), [room_sweep.c:466-510](../../room_sweep.c#L466-L510).

2. **A checksum-valid but short GLL sentence can dereference an uninitialized field pointer in the UART callback path.** The GLL branch admits `nf >= 5` but immediately reads `fields[5]`; five parsed fields have valid indices 0–4. Because parsing occurs from the async RX callback, malformed yet checksum-valid input can cause an out-of-bounds read/crash rather than an empty/error state. The NMEA test covers normal GLL but no shortest accepted GLL boundary. [nmea.c:37-49](../../nmea.c#L37-L49), [nmea.c:134-152](../../nmea.c#L134-L152), [room_sweep.c:116-127](../../room_sweep.c#L116-L127), [tests/test_nmea.c:89-97](../../tests/test_nmea.c#L89-L97).

3. **Audio/vibration and external-mode detection feedback are unimplemented despite enabled-looking controls and the mission requirement.** The source explicitly makes feedback silent; toggles change only displayed flags. Wi-Fi/BLE parsing never creates an alert signal, and neither mode has LED, audio, or vibro feedback. This directly fails the mission's required audio/haptic feedback and matching external-mode feedback. [room_sweep.c:305-323](../../room_sweep.c#L305-L323), [room_sweep.c:408-451](../../room_sweep.c#L408-L451), [MISSION.md:10-11](../../MISSION.md#L10-L11), [MISSION.md:23-26](../../MISSION.md#L23-L26). `fix_plan.md` independently records this as P0. [fix_plan.md:5-9](../../fix_plan.md#L5-L9).

### MEDIUM

1. **The BFFB connection label is not a connection detector, and scans cannot fail or complete in UI state.** Acquisition success yields `Idle`; it is also true when nothing is physically connected. `MarauderError` is rendered but never assigned, and `MarauderScanning` is only reset on tab change, not on output, completion, timeout, or TX failure. An empty scan, wrong firmware, wrong baud, or unplugged BFFB is indistinguishable from a scan that has not produced output. [room_sweep.c:151-165](../../room_sweep.c#L151-L165), [room_sweep.c:420-451](../../room_sweep.c#L420-L451), [room_sweep.c:781-794](../../room_sweep.c#L781-L794), [room_sweep.c:821-827](../../room_sweep.c#L821-L827).

2. **Wi-Fi and BLE RSSI are neither parsed nor metered.** The raw-line UI means long output can be truncated at 127 bytes and only five lines are visible; no source path extracts RSSI, RSSI thresholds, a bar/meter, or a Wi-Fi/BLE alert. The guide's claim that the configuration reports metadata including RSSI is a claim about external firmware, not an application-level parsed contract. [room_sweep.c:48-51](../../room_sweep.c#L48-L51), [room_sweep.c:129-145](../../room_sweep.c#L129-L145), [room_sweep.c:408-451](../../room_sweep.c#L408-L451), [ROOM_SWEEP_GUIDE.html:775-779](../../ROOM_SWEEP_GUIDE.html#L775-L779).

3. **The guide is stale for current mode/control behavior.** It says the tab sequence has no GPS and that Up/Down are unused, while current source has a GPS tab and toggles sound/vibration. This conflicts with the mission requirement to update the guide and causes incorrect user instructions. [ROOM_SWEEP_GUIDE.html:592-595](../../ROOM_SWEEP_GUIDE.html#L592-L595), [ROOM_SWEEP_GUIDE.html:654-657](../../ROOM_SWEEP_GUIDE.html#L654-L657), [room_sweep.h:10-17](../../room_sweep.h#L10-L17), [room_sweep.c:785-804](../../room_sweep.c#L785-L804), [MISSION.md:28-29](../../MISSION.md#L28-L29).

4. **Dead/unimplemented UART/GPS state adds false confidence and violates the requested anti-slop perspective.** `uart_rx_flag`, four `gps_*` fields, `MARAUDER_RX_BUF_SIZE`, `GPS_BAUD_ALT`, and the `MarauderError` state have no active implementation path. They suggest buffering, alternate-baud handling, state signaling, or error reporting that the code does not perform. This is needless production complexity, not required parsing/validation. [room_sweep.c:52](../../room_sweep.c#L52), [room_sweep.c:67-71](../../room_sweep.c#L67-L71), [room_sweep.h:48-53](../../room_sweep.h#L48-L53), [room_sweep.h:59-63](../../room_sweep.h#L59-L63).

5. **`3D FIX` is not established by parsed data.** The UI uses `has_fix` as the complete predicate for a `3D FIX` label, but that flag is set from nonzero GGA quality or active RMC/GLL status; the parser neither records fix dimensionality nor parses altitude into `GpsFix`. The label overstates the source evidence. [room_sweep.c:466-470](../../room_sweep.c#L466-L470), [nmea.c:86-105](../../nmea.c#L86-L105), [nmea.c:106-152](../../nmea.c#L106-L152), [nmea.h:29-38](../../nmea.h#L29-L38).

### LOW

1. **Raw UART lines are written in the async callback and read by the draw path with no synchronization.** This can produce torn/transient displayed text while a line is shifted or copied. The current fixed buffers limit the impact, but it is a display-consistency risk. [room_sweep.c:129-146](../../room_sweep.c#L129-L146), [room_sweep.c:425-428](../../room_sweep.c#L425-L428), [room_sweep.c:448-451](../../room_sweep.c#L448-L451).

2. **GPS parser state is file-static, so it supports only one parser instance.** This matches the current one-app design but makes the otherwise public parser API non-reentrant. [nmea.c:8-13](../../nmea.c#L8-L13), [nmea.c:28-33](../../nmea.c#L28-L33).

## Failure and empty-state matrix

| Scenario | Source behavior | Gap |
|---|---|---|
| USART acquisition fails | Wi-Fi/BLE/GPS render `BFFB not connected`. | This represents unavailable USART, not tested BFFB absence. [room_sweep.c:414-417](../../room_sweep.c#L414-L417), [room_sweep.c:437-440](../../room_sweep.c#L437-L440), [room_sweep.c:460-463](../../room_sweep.c#L460-L463) |
| BFFB absent but USART acquired | `Idle`; no lines. | No handshake/timeout/error. [room_sweep.c:157-165](../../room_sweep.c#L157-L165), [ROOM_SWEEP_GUIDE.html:697-701](../../ROOM_SWEEP_GUIDE.html#L697-L701) |
| Wi-Fi/BLE empty or failed scan | Blank list; state remains Scanning/Sniffing after OK. | No empty result, failed command, completion, or disconnected state. [room_sweep.c:420-428](../../room_sweep.c#L420-L428), [room_sweep.c:443-451](../../room_sweep.c#L443-L451), [room_sweep.c:821-827](../../room_sweep.c#L821-L827) |
| GPS has not produced a valid checksum | `Waiting for GPS...`; otherwise `NO FIX` if sentence count is nonzero. | Correct basic empty-state split, but it relies on 115200 passive input. [room_sweep.c:466-474](../../room_sweep.c#L466-L474), [nmea.c:179-228](../../nmea.c#L179-L228) |
| GPS loses a prior fix | Still may show `3D FIX` and old position. | HIGH stale-state defect above. |
| Sound is inaudible | Guaranteed by source: no sound notifications are emitted, toggles are state-only, and sound starts OFF. | Not a hardware-volume hypothesis. [room_sweep.c:305-323](../../room_sweep.c#L305-L323), [room_sweep.c:691-692](../../room_sweep.c#L691-L692), [room_sweep.c:796-804](../../room_sweep.c#L796-L804) |

## Evidence limitations and hypotheses

- **Verified:** all findings above are derived from the cited current source/docs. The host test source covers NMEA parsing, including normal GLL and no-fix cases, but it does not cover UART lifecycle, BFFB protocol compatibility, Wi-Fi/BLE parsers (none exist), stale-fix clearing, or short valid GLL. [tests/test_nmea.c:26-150](../../tests/test_nmea.c#L26-L150).
- **Not verified by design:** no BFFB hardware, Marauder firmware version/output format, GPS serial stream, speaker/vibro hardware, or live test result was inspected. Whether `scanap`/`sniffbt` are accepted by the installed BFFB is an external compatibility hypothesis, not established here.
- **Likely reason sound is inaudible:** source deliberately implements silent operation; it is not merely gated by a threshold or global mute setting in the current code. `fix_plan.md` proposes additional possible device-setting/threshold causes, but those are not present in the production feedback path and remain hypotheses until hardware verification. [fix_plan.md:5-9](../../fix_plan.md#L5-L9).

## Blockers before approval

1. Clear stale GPS fix/position state on valid no-fix input and add an edge test.
2. Fix the GLL field-count guard and add a valid checksum short-GLL boundary test.
3. Implement or accurately remove/disable advertised audio/vibro controls and provide feedback for Wi-Fi/BLE detections consistent with the mission.
