# Room Sweep host-only manual QA review

Date: 2026-08-02
Scope: current uncommitted worktree in `/Users/scrimwiggins/sw33p3r`
Restriction: no connected Flipper interaction and no RF transmission.

## surfaceEvidence

| Scenario | Criterion | Surface | Exact invocation | Verdict | Artifact refs |
|---|---|---|---|---|---|
| HQA-01 | API/build gate | host verifier + ufbt | `PYTHONDONTWRITEBYTECODE=1 python3 _verify_api.py && ./init.sh` | PASS | A1, A2 |
| HQA-02 | button contract | pure input helpers | `./init.sh` (`host Back routing`) and source audit `nl -ba room_sweep.c \| sed -n '3270,3805p'` | PASS | A3, A4 |
| HQA-03 | wireless browsing/evidence wording | pure wireless helper tests | `./init.sh` (`host wireless state`) | PASS | A5 |
| HQA-04 | GPS pages/status | pure GPS helper tests | `./init.sh` (`host GPS presentation state`) | PASS | A6 |
| HQA-05 | recording bounds/redaction/lifecycle | pure recorder helper tests | `./init.sh` (`host recorder state`) | PASS | A7 |
| HQA-06 | report status/limitations | pure report helper tests | `./init.sh` (`host report state`) | PASS | A8 |
| HQA-07 | complete parser/scan regressions | NMEA + scan/GPS helpers | `./init.sh` (`host NMEA`, `host scan/GPS helpers`) | PASS | A9 |
| HQA-08 | rendered/device interaction | Flipper FAP and filesystem | Not run by assignment; no device actions performed | FAIL / BLOCKED | A10 |

## adversarialCases

| Scenario | Criterion | Adversarial class | Expected behavior | Verdict | Artifact refs |
|---|---|---|---|---|---|
| ADV-01 | input safety | held/long/repeat controls | only Up/Down browse repeats; held Left/Right, OK, Back do not retrigger unsafe actions | PASS | A3 |
| ADV-02 | wireless identity | missing/changed MAC | MAC identity wins; labels do not merge rows with different MACs | PASS for helper contract; parser placeholder merge remains a gap | A5, A11 |
| ADV-03 | scan truthfulness | zero-result bounded window | say `Not observed in this scan`; never claim absence or Internet telemetry | PASS | A5 |
| ADV-04 | report truthfulness | partial sensor confirmation | status can be complete while coverage remains partial and unconfirmed sensors are listed | PASS | A8 |
| ADV-05 | storage fault | dump/session write failure | latch storage error, stop recording, and report incomplete state | PARTIAL: tested pure recorder latch; live storage fault not run | A7, A10 |
| ADV-06 | RF safety | TX path | frequency-only, bounded, two-step confirmation; no replay/jamming | PASS by host RF/TX state/build gates; live RF intentionally not run | A2, A12 |

## artifactRefs

| ID | Kind | Description | Path |
|---|---|---|---|
| A1 | build | API symbol verifier passes 61/61 symbols on current source | `/Users/scrimwiggins/sw33p3r/_verify_api.py` |
| A2 | build | Current ufbt artifact built for Target 7/API 87.1 | `/Users/scrimwiggins/sw33p3r/dist/room_sweep.fap` |
| A3 | test | Input cursor/action receipt (39+ assertions) | `/Users/scrimwiggins/sw33p3r/.omo/evidence/f03-input-cursor-test-2026-08-02.md` |
| A4 | source | Runtime mode-specific button dispatch | `/Users/scrimwiggins/sw33p3r/room_sweep.c` |
| A5 | test | Wireless identity and evidence wording receipt | `/Users/scrimwiggins/sw33p3r/.omo/evidence/f04-wireless/test_wireless_state.txt` |
| A6 | test | GPS page/status receipt | `/Users/scrimwiggins/sw33p3r/tests/test_gps_state.c` |
| A7 | test | Recorder state/redaction receipt | `/Users/scrimwiggins/sw33p3r/.omo/evidence/room-sweep-record-state-f05.txt` |
| A8 | test | Report state/limitations receipt | `/Users/scrimwiggins/sw33p3r/tests/test_report_state.c` |
| A9 | test | NMEA and scan helper tests | `/Users/scrimwiggins/sw33p3r/tests/test_nmea.c` |
| A10 | blocker | Device QA intentionally not performed in this host-only lane | `/Users/scrimwiggins/sw33p3r/dist/room_sweep.fap` |
| A11 | source-gap | Missing identifiers use one `Hidden/unknown` label before MAC-first matching | `/Users/scrimwiggins/sw33p3r/room_sweep.c` |
| A12 | source | TX state machine and safety gates | `/Users/scrimwiggins/sw33p3r/tests/test_rf_tx_state.c` |

## Evidence-backed gaps

1. The parser's constant `Hidden/unknown` placeholder can collapse multiple
   MAC-less observations because label fallback is intentionally allowed for
   MAC-less rows. The helper contract itself is correct, but parser behavior
   needs a non-identity unknown-row policy.
2. GPS detail age should display an unknown marker when there is no valid
   navigation timestamp, rather than subtracting from tick zero.
3. Live storage fault injection and filesystem artifact inspection require a
   connected Flipper; this lane did not perform them.
