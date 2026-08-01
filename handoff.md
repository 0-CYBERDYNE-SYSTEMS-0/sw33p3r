# Room Sweep implementation handoff

**Date:** 2026-08-01  
**Repository:** sw33p3r  
**Audited revision:** 7918f7bdf28edea42ec3b604452e31bc8f202275 (origin/main)  
**Audience:** Next firmware/UI team taking this from audit into implementation

## Mission

Room Sweep is intended to be a simple Flipper application for passive room
assessment:

- Sub-GHz signal discovery through the Flipper CC1101.
- Wi-Fi and BLE observation through the user's BFFB ESP32 over UART.
- Passive GPS/NMEA status from the BFFB.
- Obvious visual, audio, and haptic feedback when a signal is detected.
- A small number of clear tabs and controls, with no hidden or ambiguous modes.

The current mission contract says Sub-GHz is passive RX only and that the
application must not transmit. That contract conflicts with the current source,
which contains a bounded RF carrier-transmit path. Resolve this conflict before
expanding the scanner or shipping a new build.

## What was audited

The full source, guide, work queue, verification scripts, and available host
test were inspected. The current source checkout matched origin/main at the
audited revision. No production C files were changed during the audit.

The complete source-backed report and architecture experience are in:

- [ROOM_SWEEP_QA_AUDIT.html](ROOM_SWEEP_QA_AUDIT.html)
- [DESIGN.md](DESIGN.md)

The delegated source audit reports and browser evidence are under .omo/evidence/:

- subghz-rf-ui-audit-code-review.md
- external-bffb-uart-audit-code-review.md
- audit-qa-build-deployment-coverage-code-review.md
- room-sweep-html-visual-qa/metadata.json
- final-fresh-review-a-gate-review.md
- room-sweep-html-manual-qa-2026-08-01-gate-review.md

The HTML artifact passed two fresh independent visual gate reviews. It was
checked at 375, 768, and 1280 pixel widths, across all five application modes
plus the Settings overlay. Keyboard navigation, filtering, reduced motion,
semantic structure, no horizontal overflow, no browser console errors, and
muted-text contrast passed. The current HTML hash matches the evidence
metadata.

## Evidence boundary

### Source-backed

- The RF path is implemented and averages eight RSSI samples per configured
  channel.
- The Wi-Fi/BLE paths send UART commands and retain raw text lines.
- The GPS path parses NMEA sentences.
- Sound and vibration settings exist as state flags.
- A bounded RF TX path exists in the current source.

### Host-verified

- The NMEA host test compiled with:

      cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/room_sweep_nmea_test
      /tmp/room_sweep_nmea_test

  It completed with zero failures. The test output contained 44 checks even
  though source comments and documentation claim 48/48.

- The standalone audit HTML passed automated and independent visual QA. This
  validates the report surface, not the Flipper firmware.

### Not yet live-verified

- Firmware build or deployment.
- Flipper screen behavior.
- Actual BFFB response lines and RSSI semantics.
- Audible speaker output.
- Physical vibration output.
- Wi-Fi/BLE response while moving toward a router or beacon.
- RF behavior at an in-between frequency.

At the final host check, /dev/cu.usbmodemflip_Rug1k01 and its /dev/tty
counterpart were visible. The configured ufbt executable, SDK export table,
and expected deployment checkout were missing, so no firmware was built or
flashed. The visible serial node confirms a Flipper USB serial surface, not a
verified BFFB protocol connection.

## Current behavior by mode

### RF / Sub-GHz

room_sweep.h defines RF_NUM_CHANNELS as 16 and supplies sixteen fixed
frequencies between 303.875 MHz and 925 MHz. The sweep thread tunes each
frequency, enters RX, collects eight RSSI samples with a 5 ms delay between
samples, averages them, and updates the chart and LED bands.

This is the one mode with a real signal measurement today. The current chart
has a valid peak dBm value and threshold logic, but the global RX/TX indicator
can overlap the rightmost bars. There are no host tests for the radio path.

### Wi-Fi

OK sends scanap once when a serial handle exists. The UART callback stores
newline-delimited text in a rolling buffer. The screen displays the state and
up to five raw lines.

There is currently no BFFB handshake, firmware identity check, response parser,
RSSI extraction, AP count, freshness timestamp, periodic refresh, timeout, or
meaningful error state. Moving the Flipper closer to a router will not change
the screen unless a later scan produces different raw output.

### BLE

BLE follows the same implementation shape as Wi-Fi. OK sends sniffbt, then
raw UART lines are shown. It has the same missing protocol, parser, RSSI,
freshness, count, refresh, and error-state work.

### GPS

GPS consumes passive NMEA data while the GPS tab is active. The host parser
handles GGA, RMC, GLL, ZDA, and GSV-related data and has a basic fix/time/date/
satellite/position view.

Two correctness issues must be fixed before treating GPS as complete:

1. A valid fix is not cleared when later no-fix sentences arrive, so the UI
   can continue to claim 3D FIX with stale coordinates.
2. The GLL parser checks for five fields and then reads fields[5]. A short,
   checksum-valid sentence can reach an out-of-bounds read.

GPS should use fix quality, satellites, freshness, and stale-state labels as
its primary meter, not an RF-style dBm meter.

### Info

The Info screen says Passive RX. No transmit. and describes the app as a
16-channel RF tool. The current source also contains five modes, a Settings
overlay, and a TX path. The Info screen and ROOM_SWEEP_GUIDE.html need to be
regenerated from the final product contract after the TX decision is made.

### Settings and feedback

Sound and vibration default to OFF. Up/Down and the Settings overlay toggle
booleans, but feedback_tick() explicitly tracks alert state without emitting
notification sound or vibration messages. This directly explains why turning
the feature on does not produce audible volume or haptic feedback in the
current revision. It is a source defect, not merely a device-volume problem.

## Frequency coverage decision

The current sixteen frequencies are an application preset, not a CC1101 limit.
The hardware can tune within three usable bands:

- 300–348 MHz
- 387–464 MHz
- 779–928 MHz

The gaps between those bands are real. A continuous 300–928 MHz sweep is not
available on this hardware. The synthesizer resolution is approximately 400 Hz,
but scanning every synthesizer step is not the correct first design because RF
bandwidth, dwell time, signal type, and sweep duration dominate useful
detection.

Implement two explicit RF workflows:

1. Quick survey: keep the current known/common preset list for fast room checks.
2. Discovery sweep: let the user select one supported band, sweep with a coarse
   configurable step, retain peak values, and optionally refine around the
   strongest region.

The discovery mode needs visible progress, current frequency, dwell/step
settings, peak hold, and a clear stale/no-signal state. Do not silently replace
the quick survey with a full-band sweep; that would make the device feel slow
and make the primary meter less trustworthy.

## Recommended implementation order

### Phase 0: Freeze the product contract

- Decide whether this is passive RX only.
- Recommended choice: remove or hard-disable the current TX path.
- If TX is retained, make it a separately named, separately gated test mode
  with an explicit safety/legal contract; do not leave it hidden behind normal
  RF-tab OK behavior.
- Update MISSION.md, fix_plan.md, Info, and the guide to agree.

Acceptance: one documented contract; no screen or source says passive-only
while another path transmits.

### Phase 1: Make feedback truthful

- Implement actual audio and vibration notification sequences.
- Restore the required speaker-volume and vibration enable path for the target
  Flipper API.
- Keep sound and vibration independently toggleable.
- Emit an immediate short test pulse when each setting changes.
- Define feedback thresholds and behavior for RF, Wi-Fi, and BLE.

Acceptance: with a known test event, sound is audible, vibration is felt, the
LED changes, and the on-screen state agrees with the hardware. Test this on the
device; source inspection is not sufficient.

### Phase 2: Establish the BFFB transport contract

- Add a startup handshake or capability query.
- Record firmware identity and connection state separately from UART ownership.
- Define bounded line parsing, scan start, scan completion, timeout, and error
  states.
- Capture representative Wi-Fi and BLE output from the actual BFFB firmware.
- Add host fixtures for those exact lines before wiring the UI.

Acceptance: the app can distinguish absent, connected-but-silent, scanning,
completed-with-results, and error states.

### Phase 3: Add primary signal meters

For Wi-Fi and BLE, make the primary view a meter backed by parsed data:

- strongest RSSI;
- count of discovered APs/devices;
- age/freshness of the last result;
- scan/sniff state;
- stale, empty, unavailable, and error states;
- optional peak-hold and threshold feedback.

Use a textual numeric value alongside the visual meter. Never draw a fake meter
from scan count or raw-line activity when RSSI is unavailable.

Acceptance: repeated scans while moving toward a known router/beacon produce a
timestamped RSSI series that changes or clearly reports unchanged/stale data.

### Phase 4: Extend RF coverage

- Keep the 16-point quick survey.
- Add a band-limited coarse sweep and peak refinement.
- Define step size, dwell, bandwidth, progress, and cancellation behavior.
- Separate RX state from any future TX state.
- Rework the chart so the status indicator cannot cover bars or labels.

Acceptance: a known in-between-frequency carrier is detected in discovery mode,
with a logged frequency, step, dwell, and observed peak.

### Phase 5: Correct GPS state handling

- Clear has_fix and position state on valid no-fix messages.
- Correct the GLL field-count guard.
- Add tests for fix-to-no-fix transitions, stale timeout, and short GLL input.
- Decide and document the BFFB GPS baud contract.

Acceptance: the screen never reports a current fix from stale coordinates and
malformed input cannot read beyond parsed fields.

### Phase 6: Build, device QA, and documentation

- Replace hard-coded absolute SDK, app, and serial paths in verification
  scripts with a documented checkout-relative or configurable setup.
- Pin or document the target SDK/API revision.
- Build and deploy from the actual repository.
- Run the device matrix in the HTML report: every tab, long Back teardown,
  UART absence/presence, RF movement, BFFB movement, GPS loss of fix,
  audio/vibration, and relaunch.
- Update ROOM_SWEEP_GUIDE.html and Info from observed behavior.

Acceptance: a fresh developer can build, flash, exercise, and reproduce the
reported results without relying on /Users/scrimwiggins/... paths from a
different machine.

## Open decisions for the owner

The next team should obtain explicit answers before implementing the branches
that materially change behavior:

1. Is the product passive RX only, or is a separate controlled TX test mode
   authorized?
2. Is the RF priority a fast known-frequency room survey, unknown-signal
   discovery, or detailed frequency characterization?
3. What BFFB firmware/version and exact Wi-Fi/BLE response format are in use?
4. Should sound default ON or OFF once the real feedback path is restored?
5. Should Wi-Fi/BLE scan actions be manual, periodic, or user-selectable?

## Suggested skills for the next team

- omo:programming for .c/.h implementation work.
- omo:debugging for live Flipper, UART, audio, haptic, and lifecycle failures.
- tdd for parser, protocol, state-transition, and RF-iterator tests.
- omo:visual-qa after changing the Flipper-facing UI or the audit HTML.
- adaptive-delegation for independent protocol, source, and device QA lanes.
- omo:git-master for commits and history-sensitive changes.
- autoreview before a final PR or handoff back to maintainers.

## Handoff rule

Do not report a mode as working because its source path exists. Report three
separate statuses: source-backed, host-verified, and live/device-verified.
The next meaningful milestone is a small end-to-end device proof for one mode,
preferably RF feedback or Wi-Fi RSSI parsing, before broadening the feature
surface.

