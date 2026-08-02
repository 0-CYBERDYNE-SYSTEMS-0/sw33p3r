# Historical Room Sweep uncommitted working-tree gate review

> Superseded by the final post-deployment reviews and device receipt. This
> report predates the GPS, Back-routing, visual-QA, and exact-artifact fixes.

recommendation: REJECT

## blockers

1. `violatedCriterion: GPS-STALE-DATA-CORRECTNESS`
   - Observation: GPS freshness is refreshed by every checksum-valid NMEA sentence, including GSV and unsupported VTG sentences that do not update fix/position. A prior `has_fix` and coordinates can therefore remain displayed as fresh indefinitely while unrelated valid sentences continue, contradicting the user-visible stale-data fix and the guide's "always current, never stale" outcome.
   - `evidencePointer`: `/Users/scrimwiggins/sw33p3r/room_sweep.c:223`; `/Users/scrimwiggins/sw33p3r/room_sweep.c:1229`; `/Users/scrimwiggins/sw33p3r/nmea.c:224`; `/Users/scrimwiggins/sw33p3r/USER_GUIDE.md:111`; `/Users/scrimwiggins/sw33p3r/.omo/evidence/security-auditor-gate-review.md` (MEDIUM GPS freshness finding).

2. `violatedCriterion: GOAL-ALL-TABS-VERIFIED`
   - Observation: The evidence proves that a valid injected traversal did not crash and that the app exited, but it does not prove that the other tabs work as intended. There is no visual receipt, BFFB WiFi/BLE result verification, audible/haptic observation, known-signal RF sweep/peak observation, or non-transmitting TX state-machine observation. The current handoff itself labels RF Sweep, RF Peak, WiFi, BLE, TX, and audio as needing field or hardware verification.
   - `evidencePointer`: `/Users/scrimwiggins/sw33p3r/.debug-journal.md` (valid input and mirroring entries); `/Users/scrimwiggins/sw33p3r/handoff.md` (Current State and Open Items tables); `/Users/scrimwiggins/sw33p3r/MISSION.md` (unchecked Definition of done field tests).

3. `violatedCriterion: DOCS-MATCH-FIXED-BACK-BEHAVIOR`
   - Observation: The implementation and device receipt establish that long Back exits even from Settings, but the v3.0.1 user guide's all-context button table documents no long-Back action in Settings. That directly contradicts the requested bug fix on the exact context where the bug occurred.
   - `evidencePointer`: `/Users/scrimwiggins/sw33p3r/USER_GUIDE.md:185`; `/Users/scrimwiggins/sw33p3r/room_sweep.c:1568`; `/Users/scrimwiggins/sw33p3r/room_sweep_input.h:20`; `/Users/scrimwiggins/sw33p3r/tests/test_input_state.c:17`.

## originalIntent

Audit and actually use the Flipper app, fix long-Back exit and immediate Settings failure, audit the other tabs, review the code and safety behavior, and report only when the exact working tree is verified ready for user testing.

## desiredOutcome

A Target 7/API 87.1 FAP whose Settings overlay is stable, whose long-Back route exits from every context using valid physical input semantics, whose six tabs and controls have evidence that their intended user-visible behavior works, whose TX path remains explicit and bounded without transmitting during QA, and whose user documentation accurately describes the verified controls and evidence limits.

## userOutcomeReview

The primary reported bug is fixed and materially verified. Direct source inspection shows global Back routing occurs before Settings/TX-specific handling, and `room_sweep_back_action()` gives long press unconditional exit priority. The focused host test passes, and the supplied valid device sequence Press -> Long -> Release ended Room Sweep from Settings. Settings also survived valid traversal and control input. Synthetic standalone Short/Long evidence was correctly discarded as invalid after the installed GUI input semantics were checked.

The exact checkout also builds cleanly and both host suites pass. However, those facts do not establish the broader claim that all tabs work as intended. Serial loader state can prove process survival/exit but cannot prove rendered content, scan results, feedback, or RF behavior. iPhone Mirroring remained Timed Out, no RF transmission was performed, and the project's own current artifacts retain explicit field/hardware verification gaps. The artifact is therefore not evidence-backed for the full stated readiness criterion, even though it is ready for a narrower user test of the Back/Settings fix.

## criterionReview

- `ANCHOR-HEAD`: PASS. `git rev-parse HEAD` returned `d6182a155b1d4a198e916847ebe62da3a9e8cb4e`.
- `WORKTREE-SCOPE`: PASS. Intentional tracked changes are limited to `USER_GUIDE.md`, `application.fam`, `nmea.c`, `room_sweep.c`, and `tests/test_nmea.c`; untracked files are `room_sweep_input.h` and `tests/test_input_state.c`. No source was modified by this review.
- `BACK-SETTINGS-EXIT`: PASS. Source, focused host test, and valid device Press -> Long -> Release receipt agree.
- `INPUT-SEQUENCE-SEMANTICS`: PASS. Installed SDK `input.h` defines Short after Release and Long asynchronously after the long threshold; supplied device evidence correctly distinguishes valid complementary sequences from discarded standalone synthetic events.
- `SETTINGS-STABILITY`: PASS with bounded evidence. Valid device navigation/control traversal did not terminate the app unexpectedly, and final long Back exited it. No visual state receipt exists.
- `BUILD`: PASS. Reproduced in `/tmp/room-sweep-gate.WgS9JD/src`: `ufbt` exit 0, Target 7/API 87.1, 26,296-byte FAP.
- `HOST-NMEA`: PASS. Reproduced with `cc -std=c11 -Wall -Wextra -Werror`; 14 groups and 46 checks passed.
- `HOST-BACK`: PASS. Reproduced with `cc -std=c11 -Wall -Wextra -Werror`; all four Back-state checks passed.
- `TX-NO-RF-DURING-REVIEW`: PASS. This lane did not deploy, reboot, access the device, or transmit RF. Source bounds TX to configured 1-10 seconds and permits Back/tab change cancellation, but hardware behavior was not exercised.
- `GPS-STALE-DATA-CORRECTNESS`: FAIL. Unrelated checksum-valid sentence traffic can indefinitely refresh an old fix/position.
- `GOAL-ALL-TABS-VERIFIED`: FAIL. Traversal/no-crash is not behavioral verification of every tab; current project artifacts explicitly retain field/hardware gaps.
- `DOCS-MATCH-FIXED-BACK-BEHAVIOR`: FAIL. The Settings column in the button summary contradicts global long-Back exit.
- `VISUAL-QA`: UNAVAILABLE, accurately reported. iPhone Mirroring timed out; serial evidence was not treated as screenshot evidence.
- `PR-ATTRIBUTION`: N/A. No PR or commit was requested.

## directProgrammingAndSlopPass

Applied `omo:programming` shared criteria and `omo:remove-ai-slops` directly to the exact diff, tests, and production code.

- `tests/test_nmea.c` is a meaningful parser-boundary regression: changing the RMC guard back to `nf >= 9` causes access to a missing date field. It is not deletion-only, removal-only, tautological, prose-pinning, or implementation-mirroring.
- `tests/test_input_state.c` is narrow and behavior-oriented, but it only tests the extracted decision helper. By itself it would provide false confidence about real event routing; the valid physical input receipt is what closes that gap for the Back bug.
- `room_sweep_input.h` is a small production extraction used by the real input path and host test. It is justified as a test seam for a subtle state-ordering bug, not speculative normalization or parsing.
- No excessive test volume, requested-removal test, useless parser/normalizer, deleted-behavior assertion, or newly introduced dependency was found.
- Maintenance notes, non-blocking absent a criterion: `room_sweep.c` remains a very large multi-responsibility C unit; several new state paths lack automated behavior coverage. These increase maintenance risk but are not independent blockers beyond `GOAL-ALL-TABS-VERIFIED`.
- Documentation drift is not merely stylistic here because it contradicts the exact fixed Back behavior and therefore blocks `DOCS-MATCH-FIXED-BACK-BEHAVIOR`.

The concurrently supplied exact-diff security report explicitly includes both skill perspectives and overfit/slop categories. Its direct GPS finding agrees with this review, although its security-only APPROVE recommendation does not establish the broader goal. Older reports under `.omo/evidence/` review materially different committed or HTML-audit states and cannot certify this working tree. This direct pass supplies the required perspective coverage, but does not replace missing all-tab runtime evidence.

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
- `/Users/scrimwiggins/sw33p3r/.debug-journal.md`
- `/Users/scrimwiggins/sw33p3r/handoff.md`
- `/Users/scrimwiggins/sw33p3r/MISSION.md`
- `/Users/scrimwiggins/sw33p3r/fix_plan.md`
- `/Users/scrimwiggins/sw33p3r/.omo/evidence/subghz-rf-ui-audit-code-review.md`
- `/Users/scrimwiggins/sw33p3r/.omo/evidence/external-bffb-uart-audit-code-review.md`
- `/Users/scrimwiggins/sw33p3r/.omo/evidence/audit-qa-build-deployment-coverage-code-review.md`
- `/Users/scrimwiggins/sw33p3r/.omo/evidence/security-auditor-gate-review.md`
- `/Users/scrimwiggins/.ufbt/current/sdk_headers/f7_sdk/applications/services/input/input.h`
- `/Users/scrimwiggins/.codex/plugins/cache/sisyphuslabs/omo/4.19.4/skills/programming/SKILL.md`
- `/Users/scrimwiggins/.codex/plugins/cache/sisyphuslabs/omo/4.19.4/skills/remove-ai-slops/SKILL.md`

## exactEvidenceGaps

- No standalone current manual-QA matrix artifact was supplied.
- No visual receipt for any current Flipper screen; iPhone Mirroring remained Timed Out.
- No observable WiFi AP/BLE device result using BFFB hardware.
- No audible/haptic feedback observation.
- No known-signal observation for RF Sweep or Peak.
- No non-radiating observable TX UI/state-machine receipt; RF transmission was correctly not performed.
- No evidence that the stale-GPS UI transition was observed on device.
- Serial traversal output is summarized in `.debug-journal.md`; a raw timestamped serial transcript is not present.

## notes

- `USER_GUIDE.md` also calls UART state `connected / no device`, while the implementation deliberately displays `acquired / no device`; this is documentation drift, but it is not a separate blocker because the original goal did not require BFFB presence detection.
- The source banner still says v3.0 while user-visible UI/docs say v3.0.1; this is non-user-visible comment drift.
- The temporary build copy was outside the repository and did not alter device or workspace source state.
