# Room Sweep operator overhaul

Implement a novice-friendly, evidence-first control and recording overhaul for the
Flipper Zero Room Sweep app without weakening radio safety.

The app must make every physical button useful in context, let operators browse
observations, preserve comprehensive bounded session evidence, and explain what
was and was not observed in plain English. RF observations may preload a
frequency-only carrier test, but this project does not add captured-signal replay,
jamming, deauthentication, continuous transmission, or claims that passive radio
metadata proves Internet telemetry or the absence of silent devices.

Execution state lives in this directory. `features.json` is the only feature
ledger. Do not change or delete feature descriptions; only flip `passes` after
its checks succeed. Each implementation iteration must update `progress.log`, run the
applicable host/API/build gates, and create a focused local commit with the
configured CYBERDYNE author identity. Do not push or operate physical radios.

## Verified starting point

- Restore branch: `restore/pre-operator-overhaul-20260802`
- Working branch: `feat/operator-controls-session-recording`
- Starting commit: `2a6cde33972cafc9e0c0b71f263c2786f90b13d0`
- Current upstream feature branch matches that commit; `origin/main` is older.
- Baseline `./init.sh` passed locally before implementation.
- Historical `.omo/evidence` receipts do not certify this branch or new work.

## Definition of done

All features in `features.json` have `passes: true`; strict host suites, API scan,
`git diff --check`, and a fresh `ufbt` build pass; the resulting FAP hash and size
are recorded against the final commit; operator docs match source; a final
read-only code/safety review finds no critical issue. Hardware traversal and any
authorized carrier test are separate manual gates and may remain explicitly
pending when no device operation was authorized. Physical-device traversal is a
separate human gate and is intentionally not part of the local feature ledger.
