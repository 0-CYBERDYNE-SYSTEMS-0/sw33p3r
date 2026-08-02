# Historical Room Sweep manual QA evidence gate

> Superseded by `room-sweep-device-qa-final-2026-08-01.txt` and the final
> artifact receipt. Its FAIL verdict belongs to the pre-deployment, stale-hash
> read-only audit and is not a current readiness result.

```yaml
manualQa:
  verdict: FAIL
  scope:
    worktree: /Users/scrimwiggins/sw33p3r
    attemptDir: /Users/scrimwiggins/sw33p3r/.omo/evidence
    head: d6182a155b1d4a198e916847ebe62da3a9e8cb4e
    requiredArtifactSha256: 5f3647f6851492c1107479f1b63a0754823cd29e041439e945cbd1f47451c9ed
    actualCurrentArtifactSha256: d7068179982e1b4d43bdd6adbeae50e00270cf65855db597485cd197cbcaf74b
    mode: read-only host audit and receipt reconciliation
    deployPerformed: false
    deviceMutationPerformed: false
    rfTransmissionPerformed: false
    physicalVisualQaClaimed: false
  surfaceEvidence:
    - scenarioId: P0-ARTIFACT-SHA-01
      criterionReference: ARTIFACT-EXACT-SHA-01
      surface: current ignored FAP artifact
      exactInvocation: "shasum -a 256 dist/room_sweep.fap"
      verdict: FAIL
      blocker: "Current artifact hashes to d7068179982e1b4d43bdd6adbeae50e00270cf65855db597485cd197cbcaf74b, not the required 5f3647f6851492c1107479f1b63a0754823cd29e041439e945cbd1f47451c9ed."
      artifactRefs: [AR-01, AR-02, AR-03]
    - scenarioId: P0-BUILD-01
      criterionReference: BUILD-FAP-01
      surface: host build receipt and current FAP shape
      exactInvocation: "Receipt invocation: ufbt; gate invocation: file dist/room_sweep.fap; strings dist/room_sweep.fap | rg 'Room Sweep|room_sweep_app|test_nmea|test_input_state|RESULT'"
      verdict: FAIL
      blocker: "The receipt claims ufbt exit 0, Target 7, API 87.1, but the resulting current artifact is not the required SHA; ufbt was not rerun because this gate was restricted to read-only host commands."
      artifactRefs: [AR-01, AR-03, AR-06]
    - scenarioId: P1-ARTIFACT-CONTENTS-01
      criterionReference: ARTIFACT-CONTENTS-01
      surface: current FAP plus application manifest
      exactInvocation: "sed -n '1,120p' application.fam; file dist/room_sweep.fap; strings dist/room_sweep.fap | rg 'Room Sweep|room_sweep_app|test_nmea|test_input_state|RESULT'"
      verdict: PASS
      artifactRefs: [AR-01, AR-06]
    - scenarioId: P1-HOST-NMEA-01
      criterionReference: GPS-NMEA-HOST-01
      surface: host NMEA test receipt and current test source
      exactInvocation: "Receipt invocation: cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/room_sweep_test_nmea && /tmp/room_sweep_test_nmea; gate cross-check: rg -n 'Test [0-9]+|RESULT' tests/test_nmea.c .omo/evidence/room-sweep-host-build-receipt-2026-08-01.txt"
      verdict: PASS
      caveat: "Existing receipt reports exit 0 and all pass; this gate did not recompile or execute because only read-only host commands were authorized. The current source includes Test 17 while the receipt's latest summary says through Test 16, recorded as an adversarial discrepancy below."
      artifactRefs: [AR-03, AR-07]
    - scenarioId: P1-HOST-BACK-01
      criterionReference: BACK-STATE-HOST-01
      surface: host Back-state test receipt and current test source
      exactInvocation: "Receipt invocation: cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c -o /tmp/room_sweep_test_input_state && /tmp/room_sweep_test_input_state; gate cross-check: rg -n 'RESULT|CHECK' tests/test_input_state.c .omo/evidence/room-sweep-host-build-receipt-2026-08-01.txt"
      verdict: PASS
      caveat: "Existing receipt reports exit 0 and all pass; no host test was rerun in this read-only gate."
      artifactRefs: [AR-03, AR-08]
    - scenarioId: P0-DEVICE-BINDING-01
      criterionReference: DEVICE-BUILD-BINDING-01
      surface: reported physical Flipper run versus required artifact SHA
      exactInvocation: "Receipt claim: ufbt launch; loader info; gate invocation: shasum -a 256 dist/room_sweep.fap; no deploy or device query permitted"
      verdict: FAIL
      blocker: "The physical-device receipt has no device-side artifact hash, loader artifact identity, or raw final serial transcript, and the current local FAP does not have the claimed SHA."
      artifactRefs: [AR-01, AR-02, AR-04]
    - scenarioId: P1-DEVICE-TRAVERSAL-01
      criterionReference: DEVICE-CHECKS-01
      surface: captured physical-device Settings and six-tab traversal summary
      exactInvocation: "Captured invocation: input send back press; input send back long; input send back release; loader info; final receipt's summarized tab traversal"
      verdict: FAIL
      blocker: "The journal supports one valid Press->Long->Release exit observation, but the broader traversal is summary-only and cannot be certified against the required exact artifact."
      artifactRefs: [AR-02, AR-04]
    - scenarioId: P1-DEVICE-VISUAL-01
      criterionReference: DEVICE-VISUAL-QA-01
      surface: physical Flipper display / iPhone Mirroring fallback
      exactInvocation: "Captured invocation: iPhone Mirroring connection attempt; gate inspection: rg -n 'visual|screenshot|Timed Out' .debug-journal.md .omo/evidence/room-sweep-final-artifact.md"
      verdict: FAIL
      blocker: "iPhone Mirroring timed out and no physical Flipper screenshot or faithful screen transcript exists. Visual QA is not claimed. The available PNGs are HTML-report captures, not device-display evidence."
      artifactRefs: [AR-02, AR-09]
  adversarialCases:
    - scenarioId: ADV-P0-HASH-01
      criterionReference: ARTIFACT-EXACT-SHA-01
      adversarialClass: reported SHA differs from bytes of current artifact
      expectedBehavior: "The current dist/room_sweep.fap hash equals the required SHA exactly."
      verdict: FAIL
      artifactRefs: [AR-01, AR-02]
    - scenarioId: ADV-P0-DEVICE-ID-01
      criterionReference: DEVICE-BUILD-BINDING-01
      adversarialClass: device run has no verifiable artifact identity
      expectedBehavior: "Physical-device checks are accompanied by a non-mutating artifact identity proof matching the required SHA."
      verdict: FAIL
      artifactRefs: [AR-02, AR-04]
    - scenarioId: ADV-P1-RECEIPT-STALE-01
      criterionReference: HOST-RECEIPT-CURRENTNESS-01
      adversarialClass: receipt coverage stops before current test source
      expectedBehavior: "A current host receipt covers the current source test set."
      verdict: FAIL
      artifactRefs: [AR-03, AR-07]
    - scenarioId: ADV-P0-SCOPE-01
      criterionReference: EXACT-WORKTREE-SCOPE-01
      adversarialClass: dirty worktree and unbound source changes
      expectedBehavior: "Receipt scope and current worktree agree, or all post-receipt changes are explicitly bound to the artifact."
      verdict: FAIL
      artifactRefs: [AR-03, AR-05]
    - scenarioId: ADV-P0-RF-01
      criterionReference: QA-NO-RF-TRANSMIT-01
      adversarialClass: prohibited RF transmission during QA
      expectedBehavior: "No RF transmission, deployment, or device-state mutation occurs in this gate."
      verdict: PASS
      artifactRefs: [AR-03, AR-04]
    - scenarioId: ADV-P1-VISUAL-CLAIM-01
      criterionReference: DEVICE-VISUAL-QA-01
      adversarialClass: unsupported physical visual success claim
      expectedBehavior: "Absent Flipper display evidence remains unclaimed."
      verdict: PASS
      artifactRefs: [AR-02, AR-09]
    - scenarioId: ADV-P1-FAP-CONTAMINATION-01
      criterionReference: ARTIFACT-CONTENTS-01
      adversarialClass: host test executable content embedded in FAP
      expectedBehavior: "FAP contains the app identity and no host test/result strings."
      verdict: PASS
      artifactRefs: [AR-01, AR-06]
  artifactRefs:
    - id: AR-01
      kind: current-artifact
      description: "Current ignored FAP; direct read-only file, type, strings, timestamp, and SHA inspection."
      path: /Users/scrimwiggins/sw33p3r/dist/room_sweep.fap
    - id: AR-02
      kind: artifact-receipt
      description: "Existing final artifact receipt claiming the required SHA and physical-device checks."
      path: /Users/scrimwiggins/sw33p3r/.omo/evidence/room-sweep-final-artifact.md
    - id: AR-03
      kind: execution-receipt
      description: "Existing host-test, ufbt, artifact-inspection, and evidence-reconciliation receipt."
      path: /Users/scrimwiggins/sw33p3r/.omo/evidence/room-sweep-host-build-receipt-2026-08-01.txt
    - id: AR-04
      kind: captured-device-journal
      description: "Existing serial/device journal with valid input sequence, device identity gap, and visual timeout."
      path: /Users/scrimwiggins/sw33p3r/.debug-journal.md
    - id: AR-05
      kind: worktree-status
      description: "Current worktree status and scope discrepancy evidence recorded by the existing QA receipt."
      path: /Users/scrimwiggins/sw33p3r/.omo/evidence/room-sweep-uncommitted-gate-review.md
    - id: AR-06
      kind: source-manifest
      description: "Current application manifest excluding tests from the FAP source list."
      path: /Users/scrimwiggins/sw33p3r/application.fam
    - id: AR-07
      kind: host-test-source
      description: "Current NMEA test source; includes Test 17, which exposes the stale receipt summary."
      path: /Users/scrimwiggins/sw33p3r/tests/test_nmea.c
    - id: AR-08
      kind: host-test-source
      description: "Current Back-state host test source."
      path: /Users/scrimwiggins/sw33p3r/tests/test_input_state.c
    - id: AR-09
      kind: unrelated-visual-evidence
      description: "HTML report screenshots/metadata; present but not evidence of the physical Flipper display."
      path: /Users/scrimwiggins/sw33p3r/.omo/evidence/room-sweep-html-visual-qa/metadata.json
```

## Blockers

1. `dist/room_sweep.fap` is `d7068179982e1b4d43bdd6adbeae50e00270cf65855db597485cd197cbcaf74b`, not the required `5f3647f6851492c1107479f1b63a0754823cd29e041439e945cbd1f47451c9ed`.
2. The captured device checks have no device-side artifact identity proof and cannot be bound to the required SHA without a permitted non-mutating device query or a trustworthy raw deployment transcript.
3. Physical visual QA is unavailable; the HTML PNG receipts are a separate artifact and do not validate the Flipper screen.
4. The current worktree is dirty, and the existing receipt itself records unbound `nmea.h`, `nmea.c`, and `tests/test_nmea.c` changes.
5. The current NMEA source contains Test 17 while the latest host receipt says coverage through Test 16; the receipt is stale or incomplete for the current source.
