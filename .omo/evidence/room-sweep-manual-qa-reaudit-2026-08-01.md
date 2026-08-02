# Room Sweep exact-7994 manual-QA re-audit

Date: 2026-08-01  
Role: QA evidence gate  
Verdict: **PASS**

This report supersedes the historical blocked content in this path. That older
content evaluated superseded `37e6...` / 28,112-byte and earlier device states.

## Exact identity

- HEAD reproduced: `d6182a155b1d4a198e916847ebe62da3a9e8cb4e`.
- Local artifact reproduced: `dist/room_sweep.fap`.
- SHA-256 reproduced: `7994ddea5961dc2889a0f767831fc3428b32ae6d1df9f048ec2f0a2fe91e853c`.
- Local size reproduced: 28,136 bytes.
- Exact device receipt: `.omo/evidence/room-sweep-device-qa-exact-7994-2026-08-01.txt`.
- Receipt binds the run to Momentum `mntm-012`, target 7/API 87.1, serial
  `flip_Rug1k0`, port `/dev/cu.usbmodemflip_Rug1k01`, and install path
  `/ext/apps/Tools/room_sweep.fap`.
- Receipt records `ufbt` exit 0 with `-Werror`, `ufbt launch`, device storage
  size `28136b`, and final loader state `Application "Room Sweep" is running`.

No device query, deployment, rebuild, or RF operation was performed by this
re-audit. Device identity is accepted from the assigned exact-current receipt;
local HEAD, hash, and size were independently reproduced.

## Host gates independently reproduced

- `cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c ...`:
  `RESULT: ALL PASS (0 failures)`, 58 executable assertions across 18 cases.
- `cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c ...`:
  `RESULT: ALL PASS`, 4/4 assertions.
- `python3 _verify_api.py`: clean; all applicable function symbols resolve to
  export status `+`.
- `git diff --check`: exit 0.
- The supplied FAP build receipt records `ufbt` exit 0, Target 7/API 87.1 and
  `-Werror`; this gate did not rebuild the FAP.

## Input semantics and all-tab matrix

The exact receipt explicitly states that every injected gesture used
`Press -> Short/Long -> Release`; no standalone Short/Long event was used.

| Surface | Exact-7994 evidence | Verdict |
|---|---|---|
| Settings | All four rows toggled; short Back closed; reopen + long Back exited | PASS |
| Exit/relaunch | Loader showed no app after exit, then Room Sweep running after relaunch | PASS |
| RF | Survey; Sweep start/cancel; long band change; Peak no-signal path | PASS |
| WiFi | Scan entered; 32-second silent boundary survived | PASS |
| BLE | Scan path entered; app remained running | PASS |
| GPS | Up and Down controls exercised; app remained running | PASS |
| TX | Short arm, preset change, short Back disarm; no Long OK | PASS |
| Info/wrap | Info entered; Right wrapped to RF | PASS |
| Final lifecycle | Long Back exited; final exact relaunch running; size remained 28136b | PASS |

No deliberate RF transmission occurred.

## Visual evidence boundary

`.omo/evidence/room-sweep-flipper-visual-qa/MANIFEST.md` truthfully binds all
retained physical-display captures to preceding UI-identical artifact
`aaa8c8da...`. It explicitly says exact `7994...` changed only host-tested NMEA
semantic validation and does not claim same-hash screenshots. The exact-current
receipt proves all-tab control traversal and crash survival, not exact-current
pixel appearance. This is an honest residual boundary, not a blocker.

## GPS freshness and test-quality re-audit

Direct source inspection confirms `nav_sentences` advances only after semantic
time validation in GGA, RMC, and GLL paths. The app refreshes
`gps_last_valid_tick` from that navigation counter rather than every accepted
NMEA sentence. Independent tests cover invalid RMC/GLL time, telemetry not
refreshing navigation, and active RMC without coordinates clearing stale
position.

The direct remove-AI-slops/overfit pass found no excessive or useless tests,
deletion-only tests, requested-removal pins, tautologies, prose pins, mocks, or
implementation-mirroring assertions. Tests construct checksum-valid external
NMEA inputs and assert observable parser state. The parsing helpers implement
necessary trust-boundary validation; no unnecessary extraction or normalization
was introduced. `room_sweep.c` remains a large multi-responsibility unit, which
is a maintenance note but does not fail this exact-artifact QA criterion.

The assigned code-review report explicitly covers the programming and
remove-ai-slops perspectives, the required overfit classes, and the corrected
RMC/GLL ordering. Its only remaining watch item is the large translation unit;
the current exact report supports the present verdict.

## Residual risks (not blockers)

- No live BFFB WiFi/BLE result stream was attached.
- No live GPS receiver/freshness transition was observed.
- Audio and haptic output were not physically measured.
- Known-signal RF behavior was not field-measured.
- Deliberate RF transmission was not performed.
- Exact-current screenshots do not exist; retained captures are explicitly
  UI-identical historical evidence.
- Device-side identity is supported by the exact `ufbt launch` receipt and
  matching 28,136-byte storage stat; the receipt contains no device-side
  cryptographic hash command.

## Final verdict

**PASS.** No concrete evidence blocker remains for the requested exact-7994
identity, valid input semantics, all-tab traversal, lifecycle behavior, or
truthful visual-boundary criteria.
