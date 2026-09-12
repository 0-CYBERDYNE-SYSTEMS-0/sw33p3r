# Room Sweep SIGINT Logic Audit and Repair

You are a team of three software agents. Work together in the Room Sweep repository.

Repository:

```text
/Users/scrimwiggins/sw33p3r
```

Primary goal:

Audit the actual SIGINT logic. Find the issues listed below. Fix each issue when the hardware and available data permit an accurate fix. When a real fix is not possible, make the UI and documentation state the limitation clearly.

Read these files before you change code:

```text
AGENTS.md
MISSION.md
PROMPT.md
FLIPPER_PITFALLS.md
docs/BFFB_MOMENTUM.md
DESIGN.md
USER_GUIDE.md
progress.log
features.json
relevant files in specs/
```

Follow all repository rules. This is a receive-side survey application. Do not add jamming, deauthentication, replay, flooding, capture, or other attack functions.

## Team roles

### Agent 1: Sub-GHz and analyzer logic

- Audit CC1101 scan, RSSI, thresholds, baseline use, peak refinement, and ExtBand behavior.
- Verify what the OOK 650 kHz preset measures.
- Determine whether the 25 kHz refinement has useful meaning with that bandwidth.
- Audit SIGNAL, CLOSER, FARTHER, radar angles, and distance-like displays.
- Fix incorrect logic when the available measurements support a correct result.
- Otherwise, replace false claims with accurate terms such as ENERGY, STRONGER, WEAKER, CHANNEL MAP, and NO DIRECTION.

### Agent 2: Wi-Fi, BLE, Marauder, and GPS

- Audit the current upstream Marauder commands and output formats.
- Inspect the current upstream source, documentation, releases, and relevant issues.
- Verify local UART parsing against current upstream output.
- Reproduce the SSID truncation issue. Test names such as:
  - `Lab AB CD`
  - `Test 12 34`
  - `NormalNetwork`
- Remove stale capability-byte handling if current Marauder does not produce those bytes.
- Audit Wi-Fi and BLE identity claims. Distinguish advertised identity from verified physical identity.
- Audit NMEA checksum, position, and fix-state logic.
- Do not claim a usable position when only receiver fix status exists.

### Agent 3: nRF24, UI truth, documentation, and integration

- Audit the nRF24 survey implementation against the Nordic nRF24L01+ specification.
- Confirm whether the code reads packets or only the RPD threshold bit.
- Find every conversion from RPD hit counts to synthetic RSSI.
- Do not display synthetic values as measured dBm.
- Decide whether real packet-based nRF24 identification is possible with the current hardware, API, memory, and mission limits.
- If it is practical, implement and test it.
- If it is not practical, label the mode as 2.4 GHz energy detection.
- Audit all screens, reports, logs, documentation, and control maps for misleading identification, range, or direction claims.
- Integrate the three work areas and check consistency.

## Known issues to verify

1. Sub-GHz scanning reads RSSI only. It does not identify protocol, modulation, packet contents, source type, or device type.
2. SIGNAL currently means RSSI above a fixed -75 dBm threshold.
3. The baseline affects the graph but does not appear to control signal qualification.
4. The 25 kHz peak search uses a 650 kHz receive filter. Its reported precision can be misleading.
5. CLOSER and FARTHER are inferred only from RSSI changes.
6. Radar angles represent frequency, channel, or row position. They do not represent physical direction.
7. nRF24 mode reads the RPD threshold bit. It does not prove nRF24 protocol traffic.
8. nRF24 hit counts become synthetic RSSI values and can appear as measured dBm.
9. Wi-Fi and BLE use real protocol reports, but names and addresses are advertised identifiers. They do not prove owner, product, intent, or physical identity.
10. The Wi-Fi parser can truncate an SSID ending in two two-character fields. Current Marauder output can make this old cleanup rule invalid.
11. GPS can show receiver fix status without a usable parsed position.
12. ExtBand Auto cannot detect the physical BFFB antenna-switch position.

## Work method

1. Start with a read-only audit.
2. Inspect current upstream source and official technical documents.
3. Separate these evidence classes:
   - Current upstream behavior.
   - Local source behavior.
   - Host-test evidence.
   - Live-device evidence.
   - Inference.
4. Do not assume that source presence proves the device route works.
5. For each known issue, classify it as:
   - Confirmed defect.
   - Confirmed misleading claim.
   - Correct behavior.
   - Not reproducible.
   - Hardware limitation.
   - Needs physical testing.
6. For each confirmed issue, decide:
   - Fix the measurement logic.
   - Fix only the label or explanation.
   - Remove the unsupported feature.
   - Leave unchanged with a documented reason.
7. Do not simulate precision that the hardware does not provide.
8. Do not convert threshold counts into dBm unless a valid calibration supports that conversion.
9. Put testable decision logic in Flipper-header-free state files.
10. Add or update host tests before changing behavior.
11. Keep `room_sweep.c` limited to wiring and rendering where practical.
12. Make one focused change at a time.
13. Do not refactor unrelated code.
14. Do not expose observed SSIDs, MAC addresses, BLE addresses, or GPS coordinates in reports or commits.

## Required verification

Run:

```sh
./init.sh
python3 _verify_api.py
git diff --check
git status --short
```

If hardware is connected, perform receive-only tests for:

- Sub-GHz energy display.
- Wi-Fi beacon parsing.
- BLE advertisement parsing.
- nRF24 RPD display.
- GPS status and position separation.

Do not enter or test TX mode.

For device testing, record:

- Firmware version.
- API version.
- FAP hash.
- Whether the device file matches the local build.
- Exact test route.
- Expected result.
- Observed result.

Do not disclose nearby device identifiers.

## Team coordination

- Each agent owns its listed files during parallel work.
- Announce overlapping files before editing them.
- Do not overwrite another agent's changes.
- Share evidence, test failures, and upstream findings.
- The integration agent resolves shared UI and documentation changes after the logic changes are stable.
- Stop and report any change that would violate `MISSION.md`.
- Do not claim success until the complete build and API checks pass.

## Output

Produce one final report with this table:

| Issue | Evidence | Classification | Fix possible? | Change made | Verification |
|---|---|---|---|---|---|

Then include:

- Remaining hardware limits.
- Remaining claims that require physical testing.
- Files changed.
- Tests added or changed.
- Exact commands and results.
- Device-test results, if performed.
- Any behavior removed because it could not be truthful.
- Current Git status.

Use short and direct sentences. Clearly state what the app detects and what it cannot identify.

Do not create a pull request unless I request one.

If you create commits, use:

```text
0-CYBERDYNE-SYSTEMS-0
134018026+0-CYBERDYNE-SYSTEMS-0@users.noreply.github.com
```

Before you call the work ready, verify:

```sh
git config user.name
git config user.email
git log --format='%h %an <%ae> %s'
```

Do not add Claude or Anthropic co-author trailers.
