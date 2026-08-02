# RF/Sub-GHz to TX operator workflow audit

Read-only source audit of `feat/operator-controls-session-recording` at HEAD
`2a6cde33972cafc9e0c0b71f263c2786f90b13d0`. No production files, build
artifacts, device state, or RF state were changed.

## Source scenarios and observables

| Scenario | Invocation / source path | Binary observable | Evidence |
|---|---|---|---|
| Survey control | `room_sweep.c:2407-2419`, `room_sweep.c:1003-1054` | Up/Down changes `RfSubMode`; Survey continuously samples the 16 `rf_channels`; candidate is published only when full-pass peak is `> -75 dBm`. | `room_sweep.h:23-28,46-79`; `room_sweep.c:1003-1054` |
| Sweep control | `room_sweep.c:2374-2404,2420-2428`, `room_sweep.c:1065-1117` | Idle long Left/Right selects one of three hardcoded bands; short OK starts/cancels; 250 kHz samples update progress/peak. | `room_sweep.h:90-103`; `room_sweep.c:1065-1117` |
| Peak control | `room_sweep.c:2429-2436`, `room_sweep.c:1118-1174` | Short OK starts +/-1 MHz, 25 kHz refinement only when `last_signal_freq > 0`; completion unconditionally writes `last_signal_freq`, including no-signal/cancel paths. | `room_sweep.c:1118-1174` |
| Long-OK overlap | `room_sweep.c:2420-2436` then `2492-2505` | Long OK is not type-gated in RF action block, then global long-OK lock also runs; Sweep/Peak Long OK can start/cancel/refine and lock together. | `room_sweep.c:2420-2436,2492-2505` |
| TX handoff | `room_sweep.c:315-319`, calls at `2361-2368` and `2385-2403` | Entering TX copies `last_signal_freq` directly into `tx_freq_hz`; no valid bit, source, timestamp, or age check. | `room_sweep.c:315-319` |
| TX lifecycle | `room_sweep.c:1191-1239,2325-2369,2244-2258` | Generated OOK TX is bounded by duration and auto-disarms, but Back/tab stop only flips flags; completed thread pointer is reused on next TX because cleanup runs after `tx_active=true`. | `room_sweep.c:1233-1239,2331-2339` |
| Capture/replay boundary | `rg -n "start_async_rx|read_packet|write_packet|tx_carrier_cb" room_sweep.c` | Only RSSI reads (`get_rssi`) and generated `tx_carrier_cb`; no RF waveform capture, decoder, or replay API. UART dump is text only. | `room_sweep.c:1191-1199,966-976` |

## Current upstream API checks

Fetched read-only from current Momentum `dev` source:

- `lib/subghz/devices/devices.h` exposes `subghz_devices_is_frequency_valid`
  and `subghz_devices_check_tx`.
- `lib/subghz/devices/tx.h` defines TX reasons: allowed,
  region-not-provisioned, region, default-range, unsupported.
- `targets/f7/furi_hal/furi_hal_subghz.c:371-424` accepts extended RX ranges
  281-361, 378-481, 749-962 MHz, warns that extended PLL ranges may not lock
  or may damage hardware, and gates TX by default/region policy.
- `applications/drivers/subghz/cc1101_ext/cc1101_ext.c:519-583` has the same
  extended validity and TX regulation behavior for `cc1101_ext`.
- `lib/drivers/cc1101.c:127-139` returns the actual quantized frequency from
  `cc1101_set_frequency`; Room Sweep currently discards that return in RX/TX.

URLs:

- https://raw.githubusercontent.com/Next-Flip/Momentum-Firmware/dev/lib/subghz/devices/devices.h
- https://raw.githubusercontent.com/Next-Flip/Momentum-Firmware/dev/lib/subghz/devices/tx.h
- https://raw.githubusercontent.com/Next-Flip/Momentum-Firmware/dev/targets/f7/furi_hal/furi_hal_subghz.c
- https://raw.githubusercontent.com/Next-Flip/Momentum-Firmware/dev/applications/drivers/subghz/cc1101_ext/cc1101_ext.c
- https://raw.githubusercontent.com/Next-Flip/Momentum-Firmware/dev/lib/drivers/cc1101.c

## Minimal implementation specification (not implemented here)

1. Keep Survey, coarse Sweep, and Peak as receive-side RSSI modes. Gate their
   start/cancel/refine actions to short OK; reserve Long OK for RF lock only
   while idle and with a valid candidate.
2. Replace `last_signal_freq` as the handoff contract with a candidate record:
   frequency, RSSI, source mode, completion tick, and `valid`. Publish only a
   completed sample set above threshold. Invalidate on cancellation, no-signal,
   band/radio change, and expiry. TX may preload only a fresh valid candidate;
   otherwise retain the safe preset.
3. Show plain-English, separate fields: `Detected (listen-only): ...` with
   source/age and `TX carrier setpoint: ...`. Label stale candidates as expired;
   never present a detected RSSI as captured waveform data.
4. Before arming/transmitting, call the radio TX-check API and show its refusal
   reason. Store/display the actual frequency returned by `set_frequency`.
5. On every stop/disarm/tab exit, join/free the old TX worker before allowing a
   new arm/start. There must be one active TX worker and one stop for every
   successful start.
6. Keep TX explicitly as a bounded, generated OOK carrier. Do not add arbitrary
   replay, waveform capture, or jamming/blocking behavior.

## Required tests

- Pure candidate tests: initial invalid; Survey/Sweep/Peak publish threshold,
  source, and tick; canceled/no-signal Peak does not publish; expiry blocks
  preload; invalidation on band/tab change.
- Input matrix: short vs long OK for all RF submodes; Long OK never starts or
  cancels an active sweep/refine and only locks an idle valid candidate.
- Handoff tests: fresh exact candidate copies; stale/invalid leaves safe preset;
  Up/Down explicitly overrides; actual tuner-return frequency is displayed.
- TX lifecycle test: start -> auto-stop -> arm/start again; start -> Back/tab
  stop -> rapid arm/start; assert old worker joined and no duplicate TX.
- TX API matrix: RX-valid but region/default-blocked frequencies never arm or
  start and produce an observable refusal reason.
- Negative capture/replay check: no async RX capture/replay path is introduced.

## Unresolved feasibility

- No hardware/field verification was performed. Known-signal RSSI behavior,
  exact tuner quantization on the deployed radio, TX field power, cancellation
  under active emission, and regional policy remain unmeasured.
- The app's conservative 300-348 / 387-464 / 779-928 bands avoid current
  Momentum's extended edge ranges; widening them requires an explicit decision
  because upstream warns about PLL lock/damage risk.
- Existing `USER_GUIDE.md`/`README.md` are local and drift from current source
  (v3.0.1 wording, generic RF OK summary); docs must be updated with any control
  or candidate-state change.
