# Room Sweep code-quality review — exact current artifact

Date: 2026-08-01  
Repository: `/Users/scrimwiggins/sw33p3r`  
Review mode: read-only for production source, build artifacts, and device state  
HEAD: `d6182a155b1d4a198e916847ebe62da3a9e8cb4e`  
Artifact: `dist/room_sweep.fap`  
SHA-256: `7994ddea5961dc2889a0f767831fc3428b32ae6d1df9f048ec2f0a2fe91e853c`  
Size: `28136` bytes

## Verdict

- **codeQualityStatus:** WATCH
- **recommendation:** APPROVE
- **blockers:** None.

There are no CRITICAL or HIGH findings. The GPS freshness repair is correctly
rooted at the parser/application boundary: only semantically valid GGA/RMC/GLL
sentences increment `GpsFix.nav_sentences`, and the app refreshes the GPS timer
only when that counter changes. Telemetry (`VTG`, `GSV`, `ZDA`) therefore cannot
keep an old position fresh.

## Scope and exact-identity verification

- `git rev-parse HEAD` returned the HEAD above.
- Direct SHA-256 and `stat` checks returned the stated exact FAP hash and
  `28136` bytes.
- The exact-current device receipt is
  `.omo/evidence/room-sweep-device-qa-exact-7994-2026-08-01.txt`. It records
  a successful Target 7/API 87.1 `-Werror` build, device storage at `28136b`,
  and final loader state `Room Sweep` running on Momentum `mntm-012`
  (`flip_Rug1k0`).
- The retained screenshots are explicitly identified as the preceding
  UI-identical `aaa8...` deployment in
  `.omo/evidence/room-sweep-flipper-visual-qa/MANIFEST.md`; they are not used
  as exact-current visual evidence.
- `omo ulw-loop status --json` reported `ULW_LOOP_PLAN_MISSING`; this is the
  required fallback report location rather than an attempt-directory report.

## Direct verification performed

| Gate | Result |
| --- | --- |
| NMEA host test | PASS — compiled with `cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c`; all checks passed. |
| Back-routing host test | PASS — compiled with `cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c`; 4/4 checks passed. |
| API verifier | PASS — `python3 _verify_api.py` reported 52/52 referenced symbols exported with status `+`. |
| Diff hygiene | PASS — `git diff --check` clean. |
| Exact artifact identity | PASS — direct SHA-256 and byte-count checks matched `7994...` / `28136`. |
| Firmware build | Not rerun: this review was forbidden from changing `dist` artifacts. The exact-current build result is evidenced by the device receipt above. |

## GPS freshness and test review

The freshness ordering is correct at `nmea.c:192-313`: input fields are
validated before `nav_sentences` advances. Invalid RMC and GLL times are tested
independently at `tests/test_nmea.c:173-179`; each asserts that a
checksum-valid-but-semantically-invalid navigation sentence leaves the counter
at zero. The telemetry exclusion test at `tests/test_nmea.c:190-197` uses a
valid GGA followed by VTG/GSV/ZDA and proves the count remains one. The
application consumes that boundary at `room_sweep.c:599-603`; GPS rendering
uses the resulting timer at `room_sweep.c:1292-1334`.

The new NMEA tests are behavior tests, not deletion-only tests, prompt tests,
constant mirrors, or tautologies. `feed_body()` derives a real checksum and
exercises the public character-feed interface. The small Back-routing test
exercises all distinct outcomes of its pure decision seam; the exact device
receipt separately covers the input lifecycle on hardware.

## Skill-perspective check

The required `omo:remove-ai-slops` and `omo:programming` skill perspectives
were loaded and applied before judging tests and maintainability.

- **remove-ai-slops:** No deletion-only, requested-removal-only, tautological,
  or implementation-constant tests were found in the changed production/test
  code. The semantic parser validation is relevant to the freshness contract;
  it is not unnecessary production parsing.
- **programming:** No prompt tests, untyped escape hatches, or needless
  validation outside the NMEA input boundary were introduced. The Back-routing
  helper is single-use in production but supplies a narrow, real host-test seam;
  it is acceptable here rather than a speculative abstraction.
- **Skill violations retained:** Both changed C implementation files exceed
  the skills' 250 pure-LOC preference (`nmea.c`: 363; `room_sweep.c`: 1445).
  This is recorded below as maintainability debt, not elevated to a correctness
  blocker because the review found no concrete regression attributable to it.

## Findings

### CRITICAL

None.

### HIGH

None.

### MEDIUM

1. **Changed implementation remains monolithic.** `nmea.c:51-354` now has
   363 pure LOC, and `room_sweep.c` has 1445 pure LOC with its lifecycle,
   radio, UART parsing, rendering, and input coordination in one translation
   unit. This violates the loaded skill perspectives' size rule and increases
   review/regression cost. It is non-blocking for this narrowly verified
   artifact, but future feature work should split by responsibility before
   adding further behavior.

### LOW

1. **Field boundaries remain intentionally unverified.** Per the exact device
   receipt: live BFFB/GPS streams, measured audio/haptics, known-signal RF
   behavior, and deliberate RF transmission were not exercised. These are
   residual validation risks, not defects demonstrated by this diff.

## Residual risk and approval basis

The exact-current physical traversal covers settings, lifecycle/back routing,
RF no-signal controls, WiFi silent timeout boundary, BLE/GPS navigation,
TX arm/preset/disarm without Long-OK, Info wrapping, final exit, and relaunch.
It deliberately does not establish live external-stream or RF-transmission
behavior. Approval is for the reviewed exact `7994...` artifact and its
host/device evidence boundaries, not for those unperformed field tests.
