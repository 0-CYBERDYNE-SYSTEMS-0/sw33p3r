# Spec: Field sweep (outdoor, GPS-stamped survey)

## Goal
"Room sweep, but outside": a continuous walking/driving survey where every
detection is GPS-stamped, coverage is honest about where you actually were,
and the output supports drone/surveillance follow-up (see `specs/drone-rid.md`).

## Problem
The FullSweep sequencer is room-shaped: one pass per sensor, one location.
Outdoors the operator moves; without position-stamped events and
movement-aware sampling, a session is a bag of hits with no place.

## Contract
1. Settings → **FieldSweep** (OFF/ON, not persisted — factory state every
   launch, same pattern as WiSource/Watchlist). When ON:
   - Requires GPS Log ON (privacy contract: coordinates never land in CSVs
     otherwise — refuse with a clear message, never silently drop coords).
   - FullSweep phases LOOP continuously instead of one pass; Back stops.
   - Each event row carries the latest fix (lat/lon/alt) in its existing
     observation detail (coordinate token only when GPS Log ON).
   - Movement gate: when speed > walking pace, sampling bias shifts to
     new-device-first (already-seen rows are rate-limited harder) so a
     long walk cannot saturate the 64-entry identity map or the 8-slot
     queue; REDACTED overflow behavior stays honest as today.
2. Host-testable policy in `room_sweep_field.h` (Flipper-free): loop
   transition, rate-limit policy, coordinate-token eligibility, refusal
   state when GPS Log is OFF. `tests/test_field.c` in `init.sh`.
3. Report gains a "Field sweep" section: duration, distance walked (reuse
   GPS haversine), fixes used, per-source hit counts, area bounding box
   (honest: bounding box of YOUR path, not of the emitters).
4. No TX interaction of any kind (same as FullSweep).

## UI
Banner strip like FullSweep ("FIELD nN · phase · dist"), pages unchanged.
GPS tab still owns the radar page; field mode only stamps and loops.

## Verification
- Host: loop/abort, rate-limit policy, coordinate-eligibility matrix,
  refusal state (GPS Log OFF).
- `python3 _verify_api.py` clean; ufbt zero warnings.
- Device: walk a fixed path twice; compare session CSVs for coordinate
  tokens, bounding box sanity, no queue-drop events.

## Out of scope
GPX writing (still future per gps-full-capability spec), map rendering on
device (dashboard's job, `specs/host-dashboard.md`), geofencing/alerts on
location.
