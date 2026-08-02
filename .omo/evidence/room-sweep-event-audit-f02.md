# Room Sweep recorder event audit F02

This is the recorder contract for later F05 implementation, updated after the
F02 RF/TX state changes. It does not claim these events are recorded yet.

Sources: `room_sweep.c:1003-1173,1191-1230,2325-2369`,
`room_sweep.h:42-103,224-233`.

## TX intent/result contract

Record software transitions, not claims of RF output:

| Event | Trigger/fields | Result semantics |
|---|---|---|
| `tx_arm` | Short OK from Disarmed; tick, selected `freq_hz`, `duration_s`, radio path | User armed software state. |
| `tx_freq_select` | Armed Up/Down; old/new frequency and preset index | Software selection only. |
| `tx_start_intent` | Long OK while Armed; tick, frequency, requested duration, radio path, optional detected-frequency provenance | User requested transmission after arm gate. |
| `tx_start_result` | Return from `subghz_devices_start_async_tx`; `start_ok`, radio-present, initial `tx_started=false` | API accepted/rejected request; not proof of antenna output. |
| `tx_callback_started` | First `tx_carrier_cb` call sets `tx_started=true` | Carrier callback entered; still no field-strength proof. |
| `tx_end` | TX thread stops/finishes; requested/elapsed/remaining ms, callback-started, outcome | `completed`, `cancelled`, `app_exit`, `radio_missing`, or `start_failed`. |
| `tx_disarm` | Back/tab change/automatic completion; reason | Software returned to Disarmed. |

Do not record per-level callback toggles. Reports must say that the app
accepted/rejected a software request and/or entered its callback, but cannot
verify antenna radiation, power, field strength, modulation fidelity, IQ, or
legal authorization.

## RF candidate contract

Every RF event carries monotonic `tick_ms`, sequence, `source` (`internal` or
`external`), mode, candidate `freq_hz`, RSSI float dBm, threshold (`-75 dBm`),
above-threshold flag, sample count, and measurement status. RSSI is the device
API reading and threshold comparison is app logic, not calibrated field
strength.

- Survey candidate: preset index/label; 8-sample average from `radio_rssi`;
  eligibility from `rf_channel_allowed`; peak channel/frequency/RSSI,
  `rf_alert`, and qualified `signal_candidate` requested/tuned frequencies.
  External-band-filtered entries set
  `-120` and must be marked not measured, not logged as real observations.
- Coarse sweep candidate: band label/start/stop, selected external band,
  250 kHz step, 8 ms dwell, four samples, points done/total, best frequency
  and RSSI, and terminal status (`completed`, `cancelled`, `aborted`). The
  current producer discards non-best points, so do not claim a full spectrum.
- Peak refinement candidate: center (fresh `signal_candidate.tuned_hz`), span clamped to the
  band around ±1 MHz, 25 kHz step, four samples, points done/total, best fine
  frequency/RSSI, and terminal status. No IQ, demodulation, packet count, or
  transmitter measurement exists.
- Baseline event: capture all 16 baseline RSSI values and `baseline_set`; a
  delta is valid only when a baseline exists. Target-lock event may include
  `target_kind`, frequency/id, and current target RSSI, but this is app lock
  state, not identity proof.

Privacy wording remains: RF frequency/RSSI is retained only as calculated;
WiFi/BLE identifiers and GPS coordinates remain redacted or explicitly opted
into under the session recorder contract.
