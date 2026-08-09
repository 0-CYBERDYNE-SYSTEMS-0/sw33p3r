# Spec: Full Room Sweep sequencer

## Goal
One operator action runs every receive-side sensor path that matters for a room
survey, in a fixed order, then the session report can be written.

## Order
1. RF Sub-GHz (survey presets / band pass as already implemented)
2. Wi-Fi (`sniffbeacon`)
3. BLE (`sniffbt`)
4. nRF24 RPD channel survey (detect-only)
5. GPS snapshot (if stream available)

## Rules
- Abort on Back returns to idle; partial coverage is honest.
- Does not arm or fire TX.
- Host-testable phase machine in `room_sweep_full_sweep.h`.
- UI: Settings → FullSweep OK starts; overlay or Info shows phase.
