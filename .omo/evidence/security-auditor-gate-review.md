# Historical security auditor gate review

> Superseded by the current post-fix security/radio-safety gate review and
> final device receipt. This report is retained as an earlier audit record.

recommendation: APPROVE

## blockers

None. No CRITICAL or HIGH security/safety issue was reproduced.

## originalIntent

Audit and QA-use the Flipper Room Sweep app, fix long-Back exit and Settings crashes, audit all tabs, preserve explicit TX safety and RF ownership limits, and report readiness only after build, host, and device-control evidence.

## desiredOutcome

The exact uncommitted tree at HEAD `d6182a155b1d4a198e916847ebe62da3a9e8cb4e` is safe to hand to the user for testing: Back behavior works, Settings is stable, TX remains deliberate and bounded, shared radio state is serialized, malformed UART data cannot overflow fixed buffers, teardown restores owned external state, and no secrets or captured data are exposed.

## userOutcomeReview

From the security/safety perspective, the requested outcome is met well enough for user testing. Direct source inspection confirms two-step TX activation, a 1-10 second duration bound, cancellation through `tx_active`, serialization of RX/TX HAL operations with `radio_mutex`, TX-thread joining before radio-state release, UART shutdown before app-state free, and expansion restoration both after failed acquisition and normal close. The changed `strncpy` destinations are explicitly terminated. No review action contacted the Flipper, changed device state, deployed, rebooted, or transmitted RF.

The supplied physical-device evidence remains serial/control evidence only; iPhone Mirroring timed out, so no visual screenshot claim is accepted. Host tests were independently rebuilt under `-Wall -Wextra -Werror` and passed.

## findings

### MEDIUM — GPS freshness is sentence freshness, not fix/position freshness

`uart_rx_cb` refreshes `gps_last_valid_tick` whenever `GpsFix.sentences` increments (`room_sweep.c:223-226`). `nmea_feed` increments that counter for every checksum-accepted sentence before determining whether its type updates fix/position (`nmea.c:224-235`). Consequently, a continuing stream of supported non-position sentences such as GSV, or even unsupported valid sentences such as VTG, can keep an old `has_fix`/`has_pos` marked fresh indefinitely; `draw_gps_tab` then displays the retained coordinates as a fresh 3D fix (`room_sweep.c:1229-1268`). This is an integrity/safety defect in GPS status, but it does not create RF transmission or memory corruption and is not CRITICAL/HIGH.

violatedCriterion: AUDIT-OTHER-TABS / GPS data should work as intended and stale data should not be represented as current.

evidencePointer: `room_sweep.c:223-226`, `room_sweep.c:1229-1268`, `nmea.c:181-188`, `nmea.c:224-235`.

### LOW — malformed checksum characters can be interpreted as zero nibbles

`hex2nib` returns zero for every non-hex character and the caller does not separately reject invalid checksum characters (`nmea.c:15-20`, `nmea.c:224-235`). A malformed checksum can therefore be accepted whenever the invalid character substitutes for a zero nibble of the calculated checksum. This weakens malformed-input rejection, although anyone able to inject UART bytes can also supply a correctly computed NMEA checksum, so this is not an authentication boundary or a high-impact exploit.

violatedCriterion: UART malformed input should be safely and accurately rejected.

evidencePointer: `nmea.c:15-20`, `nmea.c:224-235`; the current malformed-input tests cover bad numeric checksum and truncation but not non-hex checksum characters (`tests/test_nmea.c:53-73`, `tests/test_nmea.c:145-156`).

### NOTE — concurrent UART snapshots are not synchronized

The async RX callback mutates GPS and rolling line buffers while draw/main code reads them without a mutex (`room_sweep.c:216-246`, `room_sweep.c:555-592`, `room_sweep.c:1219-1275`). Bounds and explicit termination prevent an evident overflow, but snapshots can be internally inconsistent. No criterion establishes this as a CRITICAL/HIGH issue, and no crash was reproduced in the supplied device traversal.

## TX and external-state review

- Two distinct gestures gate TX: short OK moves Disarmed to Armed; long OK starts TX only from Armed (`room_sweep.c:1616-1630`).
- Duration is constrained to 1-10 seconds and the TX loop exits on duration, app exit, Back cancellation, or tab navigation (`room_sweep.c:795-820`, `room_sweep.c:1576-1599`, `room_sweep.c:1641-1657`).
- The shared CC1101 HAL is protected by `radio_mutex`; TX always calls stop after a successful start, then idles the radio (`room_sweep.c:598-815`).
- Normal exit clears TX state, stops/releases UART, joins TX and RF threads, sleeps the radio, resets notification outputs, and frees mutexes only afterward (`room_sweep.c:1749-1771`).
- Failed UART acquisition re-enables expansion, avoiding a leaked disabled external state (`room_sweep.c:252-283`).
- No credential, secret, private payload, or network transmission path exists in the changed files.

## directSlopAndProgrammingPass

Applied directly to production code and tests. `tests/test_input_state.c` is narrowly behavior-oriented and covers the specific Back state outcomes; it is not deletion-only, tautological, or implementation-mirroring. The truncated-RMC test protects an actual out-of-bounds boundary correction. The extracted inline Back action is small and creates a host-testable seam for otherwise device-only input behavior; it is justified rather than speculative abstraction. No excessive test volume, requested-removal-only test, unnecessary parsing/normalization, secret plumbing, or scope-expanding production abstraction was found. The prior code-review artifact explicitly includes the same `remove-ai-slops` and `programming` perspectives and discusses false confidence and missing behavior coverage; direct review, not report prose, is the basis for this recommendation.

## checkedArtifactPaths

- `/Users/scrimwiggins/sw33p3r/USER_GUIDE.md`
- `/Users/scrimwiggins/sw33p3r/application.fam`
- `/Users/scrimwiggins/sw33p3r/nmea.c`
- `/Users/scrimwiggins/sw33p3r/nmea.h`
- `/Users/scrimwiggins/sw33p3r/room_sweep.c`
- `/Users/scrimwiggins/sw33p3r/room_sweep.h`
- `/Users/scrimwiggins/sw33p3r/room_sweep_input.h`
- `/Users/scrimwiggins/sw33p3r/tests/test_nmea.c`
- `/Users/scrimwiggins/sw33p3r/tests/test_input_state.c`
- `/Users/scrimwiggins/sw33p3r/.omo/evidence/audit-qa-build-deployment-coverage-code-review.md`
- `/Users/scrimwiggins/sw33p3r/.omo/evidence/room-sweep-gate-review.md`
- `/Users/scrimwiggins/.codex/plugins/cache/sisyphuslabs/omo/4.19.4/skills/programming/SKILL.md`
- `/Users/scrimwiggins/.codex/plugins/cache/sisyphuslabs/omo/4.19.4/skills/remove-ai-slops/SKILL.md`

## reproducedEvidence

- `git rev-parse HEAD` => `d6182a155b1d4a198e916847ebe62da3a9e8cb4e`.
- `git status --short` matched the six intentional tracked/untracked paths before this required report artifact.
- `git diff --check` exited 0.
- `cc -std=c11 -Wall -Wextra -Werror -I. nmea.c tests/test_nmea.c -lm ...` exited 0; all 14 groups passed.
- `cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c ...` exited 0; all four Back cases passed.
- Supplied build evidence: `ufbt` exit 0, Target 7/API 87.1. Not rerun because this lane is read-only and must not alter device state.
- Supplied device control evidence: valid Press -> Long -> Release exited from Settings, and valid all-tab/control traversal ended with `No application is running`. Accepted as serial/control evidence, not visual QA.

## exactEvidenceGaps

- No visual UI evidence because iPhone Mirroring remained Timed Out.
- No adversarial host test for non-hex NMEA checksum characters.
- No host/integration test proving stale coordinates become stale while unrelated valid NMEA sentences continue.
- No race/instrumentation evidence for atomic UART-to-main snapshots.
- No RF transmission was performed in this review lane, by constraint; TX approval is based on source control-flow and supplied bounded device evidence, not live RF measurement.
- No separate current-attempt directory or notepad exists because `omo ulw-loop status --json` returned `ULW_LOOP_PLAN_MISSING`; this report uses the required fallback evidence path.
