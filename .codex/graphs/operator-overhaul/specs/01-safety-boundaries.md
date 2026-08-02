# Safety and truthfulness boundaries

## Allowed

- Receive-only Sub-GHz surveys, coarse sweeps, and fine peak refinement.
- A fresh, threshold-qualified RF frequency candidate may preload the existing
  bounded OOK carrier-test tab.
- TX must remain two-step, time-limited, cancellable, visibly armed, and checked
  against device frequency validity and TX permission before thread start.
- Wi-Fi and Bluetooth metadata collection supported by the attached firmware.
- Bounded, user-enabled local evidence recording with visible failures.

## Excluded

- Captured waveform, key, packet, or modulation replay.
- Jamming, deauthentication, intentional blocking, or continuous carrier modes.
- Automatically transmitting the strongest observation.
- Claims that RSSI identifies distance, intent, or ownership.
- Claims that a beacon, probe, station frame, or BLE advertisement proves
  Internet access, application telemetry, recording, or compromise.
- Claims that no observation proves a device is absent; radio-silent, sleeping,
  wired-only, shielded, or off-channel devices remain undetectable here.

## Failure behavior

Unknown, stale, invalid, unsupported, unavailable, partial, dropped, and storage
failure states must be visible. They must never be rendered as success.
