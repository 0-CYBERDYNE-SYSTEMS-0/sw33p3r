# Historical QA, build, deployment, and documentation audit

> Superseded by the final post-deployment artifact, host-gate, device-QA, and
> review receipts. Its pre-fix findings and missing-toolchain observations do
> not describe the current working tree.

**Scope:** all 15 tracked files in `/Users/scrimwiggins/sw33p3r`; RF/UART implementation was consulted only to validate documentation and runtime claims. No device, browser, GUI, network-host, or live-service action was taken. The tracked working tree was clean before inspection; the only resulting untracked path is this required report artifact.

**Result:** `codeQualityStatus: BLOCK`; `recommendation: REQUEST_CHANGES`.

## Evidence collected

| Command | Outcome | What it establishes |
|---|---|---|
| `git ls-files` | 15 tracked files; no build manifest, CI workflow, Makefile, CMake file, or package manifest | Build and test discovery is manual/document-driven. |
| `cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -lm ... && test_nmea` | exit 0, `RESULT: ALL PASS (0 failures)` | The pure NMEA parser passes its host suite under strict warnings. |
| `PYTHONPYCACHEPREFIX=<temp> python3 -m py_compile _dev_check.py _smoke_test.py _verify_api.py` | exit 0 | Python scripts are syntactically valid only; they were not executed because they would access a serial device and/or launch a deployment. |
| `test -e` on the four absolute paths used by host/deploy scripts | all missing: old project root, `ufbt`, its activation script, and Momentum `api_symbols.csv` | The advertised build, API-verification, and smoke commands cannot be reproduced from this checkout as configured. |
| `git diff --check` and `git diff --exit-code` | exit 0 | No uncommitted changes and no whitespace defect in the working tree. |

The test contains **44** `CHECK(...)` assertions, not the **48** claimed in [fix_plan.md](../../fix_plan.md:27), [room_sweep.c](../../room_sweep.c:707), and commit text. This report relies on the executable test result and count, rather than those claims.

## Available automation and actual coverage

| Artifact | Intended operation | What it actually verifies | Limitation |
|---|---|---|---|
| [tests/test_nmea.c](../../tests/test_nmea.c:1) | Host-compile `nmea.c` | NMEA GGA/RMC/GLL/ZDA/GSV parsing, checksum rejection, no-fix behavior, garbage/partial input, and back-to-back sentences ([tests/test_nmea.c](../../tests/test_nmea.c:26)-[tests/test_nmea.c](../../tests/test_nmea.c:150)) | No executable build command is versioned; no app/UI/UART/RF coverage. |
| [_verify_api.py](../../_verify_api.py:1) | Scan C/H call-like symbols against Momentum export CSV | Regex-based `furi_*`, GUI, canvas, storage, expansion, and notification-name lookup ([ _verify_api.py](../../_verify_api.py:31)-[_verify_api.py](../../_verify_api.py:79)) | Uses missing absolute SDK and old app paths ([ _verify_api.py](../../_verify_api.py:11)-[_verify_api.py](../../_verify_api.py:12)); it does not compile or link, and regex scanning is not an ABI/link test. |
| [_smoke_test.py](../../_smoke_test.py:40) | `ufbt launch`, serial `loader info`, device info | Reports whether loader output names Room Sweep ([ _smoke_test.py](../../_smoke_test.py:59)-[_smoke_test.py](../../_smoke_test.py:72)) | Device/deploy script, not a host smoke test; hard-coded missing old checkout/tool path ([ _smoke_test.py](../../_smoke_test.py:3)-[_smoke_test.py](../../_smoke_test.py:5)); it neither exercises modes nor asserts output. |
| [_dev_check.py](../../_dev_check.py:1) | Send `device_info` and `power_info` over serial | Prints command responses ([ _dev_check.py](../../_dev_check.py:13)-[_dev_check.py](../../_dev_check.py:21)) | Diagnostic only; no assertions, no app launch, and hard-coded device path. |
| [ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:789) | Manual `ufbt` / `ufbt launch` instructions | Documents a proposed build/deploy flow | Points at a missing different project path and missing toolchain ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:790)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:797)). |

## Per-mode coverage and gaps

| Mode/tab | Source-backed behavior | Current coverage | Required missing QA |
|---|---|---|---|
| RF | Sub-GHz sweep mode ([room_sweep.h](../../room_sweep.h:10)-[room_sweep.h](../../room_sweep.h:17)); threshold/config ([room_sweep.h](../../room_sweep.h:22)-[room_sweep.h](../../room_sweep.h:42)); RF TX can be safety-confirmed with OK ([room_sweep.c](../../room_sweep.c:806)-[room_sweep.c:827]). | None. | Build/link against exact Momentum SDK; on-device launch; frequency/bar/threshold rendering; LED bands; TX warning, cancel, maximum-duration, RX resume, and teardown. |
| WiFi | `scanap` is sent over an acquired serial handle ([room_sweep.c](../../room_sweep.c:151)-[room_sweep.c:185), [room_sweep.c](../../room_sweep.c:821)-[room_sweep.c:823]). | None. | Test BFFB firmware/version and command-response contract at 115200; no-device/UART-busy state; command terminator; line rolling/truncation; tab-switch reset and exit cleanup. |
| BLE | `sniffbt` uses the same serial path ([room_sweep.c](../../room_sweep.c:431)-[room_sweep.c:451), [room_sweep.c](../../room_sweep.c:824)-[room_sweep.c:826]). | None. | Same serial lifecycle tests plus actual BFFB BLE command/result acceptance and recovery. |
| GPS | Parser is pure-host tested; app enables byte feeding only when GPS tab active ([room_sweep.c](../../room_sweep.c:123)-[room_sweep.c:145), [room_sweep.c](../../room_sweep.c:785)-[room_sweep.c:795)). | Parser only. | UART-to-parser integration, tab-entry/exit boundary, representative 115200 NMEA stream, no-fix/fix rendering, and BFFB GPS firmware/output verification. `GPS_BAUD_ALT` is defined but has no use in tracked source ([room_sweep.h](../../room_sweep.h:52)-[room_sweep.h](../../room_sweep.h:54)). |
| Info | Static mode in enum and draw dispatch ([room_sweep.h](../../room_sweep.h:15)-[room_sweep.h](../../room_sweep.h:17), [room_sweep.c](../../room_sweep.c:589)-[room_sweep.c:598)). | None. | On-device/manual review that displayed safety, tab count, controls, and feature claims match release documentation. |
| Cross-mode controls/settings | Short Back opens settings and long Back exits ([room_sweep.c](../../room_sweep.c:762)-[room_sweep.c:775)); Up/Down alter only flags in silent mode ([room_sweep.c](../../room_sweep.c:796)-[room_sweep.c:805)). | None. | Input-state matrix: each key in each tab/overlay, settings navigation, cancel paths, and every cleanup path. |

## Deployment/runtime assumptions

1. The configured target is an external Flipper app, entry `room_sweep_app`, Tools category, 4 KiB stack ([application.fam](../../application.fam:1)-[application.fam](../../application.fam:10)). No tracked build configuration pins a Momentum SDK revision; “API 87.1” is only documentation/script configuration ([MISSION.md](../../MISSION.md:20), [_verify_api.py](../../_verify_api.py:11)).
2. WiFi/BLE expects USART1 on PC0/PC1, 115200 baud, async RX, and CRLF commands ([room_sweep.h](../../room_sweep.h:45)-[room_sweep.h](../../room_sweep.h:50), [room_sweep.c](../../room_sweep.c:157)-[room_sweep.c:184)). It acquires a UART handle, not BFFB presence; therefore a non-null handle is not proof that BFFB, Marauder firmware, or the expected commands work.
3. GPS is simultaneously described as BFFB GPS input at 115200 ([room_sweep.c](../../room_sweep.c:471)-[room_sweep.c:474)) and has an unused `9600` alternative ([room_sweep.h](../../room_sweep.h:52)-[room_sweep.h](../../room_sweep.h:54)). No tracked artifact pins BFFB hardware revision, Marauder/GPS firmware revision, command mode, output format, or verifies baud empirically.
4. No local build/deploy result was established. Older commit prose and the guide’s installed/running assertion are historical claims, not current runtime evidence; this audit intentionally did not contact a device.

## Findings

### CRITICAL

None.

### HIGH

1. **Safety and release documentation falsely states receive-only/no TX while the current source implements active RF transmission.** [MISSION.md](../../MISSION.md:16) prohibits transmission; the guide says “never transmits” ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:382)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:384)) and “0 transmitters” ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:344)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:350)). In contrast, source starts async sub-GHz TX after confirmation ([room_sweep.c](../../room_sweep.c:806)-[room_sweep.c:827)) and has a TX UI warning ([room_sweep.c](../../room_sweep.c:638)-[room_sweep.c:665)). This can cause unsafe or non-compliant operation based on the official guide.
2. **The documented controls/modes are materially stale.** The guide describes four tabs ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:344)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:350), [ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:591)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:606)), says Back exits and Up/Down are unused ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:652)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:657)), but source has five tabs ([room_sweep.h](../../room_sweep.h:10)-[room_sweep.h](../../room_sweep.h:17)), a short-Back settings menu and long-Back exit ([room_sweep.c](../../room_sweep.c:762)-[room_sweep.c:775)), and RF OK opens TX confirmation ([room_sweep.c](../../room_sweep.c:806)-[room_sweep.c:820)). The guide’s procedure is unsafe and will not operate the shipped app as described.
3. **Build/API/smoke automation is non-reproducible from this repository.** Scripts and guide hard-code `/Users/scrimwiggins/flipper-room-sweep` and temporary tool locations ([ _verify_api.py](../../_verify_api.py:11)-[_verify_api.py](../../_verify_api.py:12), [_smoke_test.py](../../_smoke_test.py:3)-[_smoke_test.py](../../_smoke_test.py:5), [ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:790)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:797)); all were absent during audit. Thus no source-controlled command proves the current checkout builds, links, deploys, or validates its Momentum API surface.

### MEDIUM

1. **The documentation overstates test evidence.** `48/48` appears in [fix_plan.md](../../fix_plan.md:27) and source commentary ([room_sweep.c](../../room_sweep.c:707)), but the executable test has 44 `CHECK` calls and passed 44 observed checks. This is false evidence accounting, though the suite itself is useful.
2. **No tests cover production app behavior outside the NMEA parser.** RF, WiFi, BLE, Info, settings, notification behavior, API compatibility, and deployment have no automated coverage. The NMEA test is not a deletion-only, tautological, or implementation-mirroring test; it exercises observable parser outcomes, including malformed input ([tests/test_nmea.c](../../tests/test_nmea.c:53)-[tests/test_nmea.c:73)).
3. **BFFB contract is assumed rather than pinned or verified.** The guide names Marauder and 115200 ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:395)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:399)), but no tracked firmware/version/protocol fixture demonstrates that current BFFB images accept `scanap`/`sniffbt` and produce the expected CRLF/NMEA streams. The guide correctly notes UART ownership is not BFFB presence ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:697)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:701)); the UI label still conflates the two ([room_sweep.c](../../room_sweep.c:414)-[room_sweep.c:417)).
4. **Status documents conflict.** `progress.log` says the device was not connected and on-device verification is deferred ([progress.log](../../progress.log:3)-[progress.log](../../progress.log:9)), while the guide claims current deployment is installed and verified running ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:402)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:406)). `MISSION.md` remains `EXECUTING` with unchecked definition-of-done items ([MISSION.md](../../MISSION.md:3)-[MISSION.md](../../MISSION.md:29)); `fix_plan.md` leaves build/deploy/on-device verification as P4 ([fix_plan.md](../../fix_plan.md:24)). Do not report completion until a dated, attributable device result resolves this.

### LOW

1. The guide calls itself generated from deployed source ([ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:835)-[ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:838)), but it is not synchronized with current tracked source (version, modes, controls, TX). State whether it is hand-maintained or add a reproducible generation/check procedure.
2. Source comments call the app v2.0 ([room_sweep.c](../../room_sweep.c:1)-[room_sweep.c](../../room_sweep.c:5)) while plan/status use v3.1 and guide uses v1.0 ([fix_plan.md](../../fix_plan.md:1), [ROOM_SWEEP_GUIDE.html](../../ROOM_SWEEP_GUIDE.html:835)). Adopt one release identifier for artifacts and reports.

## Required remediation before approval

- Reconcile the product safety contract: either remove/disable TX or update MISSION, guide, UI procedure, legal statements, and QA to describe the safety-gated transmitter accurately.
- Update the guide for five tabs, GPS, short-Back settings, long-Back exit, Up/Down behavior, and RF OK/TX warning.
- Add a tracked, relative-path build/API/test entry point and pin/record the exact Momentum SDK/API version; make the API verifier inspect the current checkout.
- Correct `48/48` claims to the measured test count, or add the four missing assertions and show their command output.
- Define and execute a device-gated QA run for the matrix below after hardware is explicitly available; record firmware, baud, command-response, build, deploy, launch, all-tab, TX-gate, and teardown evidence separately from host tests.

## Proposed tab-by-tab QA matrix

| Scenario | RF | WiFi | BLE | GPS | Info/settings | Evidence / pass criterion |
|---|---|---|---|---|---|---|
| Build + load | Build/link exact SDK | Same binary | Same binary | Same binary | Same binary | Versioned command exits 0; artifact/API tag recorded; loader opens app. |
| Flipper only | Sweep/chart/LED works | Honest no-device/UART-acquired state | Same | Honest no-NMEA state | Five-tab labels and controls correct | Device video/log plus serial loader result. |
| BFFB protocol | N/A | `scanap\\r\\n` accepted and rolling results shown | `sniffbt\\r\\n` accepted and results shown | NMEA stream at declared baud parsed | State resets across tabs | Firmware/version, UART capture, and expected UI state. |
| Input/state | RF OK warning, cancel, confirm, bounded TX, RX resume | OK only sends scan | OK only sends sniff | No accidental scan/TX | Short Back settings; long Back exit; Up/Down menu and direct behavior | Per-key expected-state table signed by tester. |
| Failure/teardown | No stuck radio/LED | UART-busy/no output/reconnect | Same | malformed/no-fix/baud mismatch | exit closes threads, releases UART, restores expansion | Re-open app and another GPIO app; no crash/leak symptom. |
| Host regression | N/A | serial-line helper fixture if extracted | serial-line helper fixture if extracted | existing parser test plus boundary cases | pure input/state helper fixtures if extracted | Repeatable host command exits 0, no hardware required. |

## Skill-perspective check

Ran: `omo:remove-ai-slops` and `omo:programming` (Python reference consulted before evaluating the Python scripts). The diff/current test suite **does not violate the test-specific slop rules**: `test_nmea.c` is behavior-oriented rather than a deletion/removal, tautological, or implementation-constant test. It does, however, violate the programming perspective operationally in the Python utilities: unpinned hard-coded environment paths and no typed/configured boundary make the scripts brittle and non-reusable ([ _dev_check.py](../../_dev_check.py:1)-[_dev_check.py](../../_dev_check.py:13), [_smoke_test.py](../../_smoke_test.py:1)-[_smoke_test.py](../../_smoke_test.py:5), [_verify_api.py](../../_verify_api.py:9)-[_verify_api.py](../../_verify_api.py:12)). This is a scope/reproducibility defect, not a reason to add brittle prompt/text tests.

## Blockers

1. Contradictory safety documentation versus active TX implementation.
2. Incorrect control/tab documentation for the current app.
3. No reproducible source-controlled build/API/smoke command for this checkout.
