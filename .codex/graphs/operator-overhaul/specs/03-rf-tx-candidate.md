# RF candidate and TX handoff

Replace the unqualified `last_signal_freq` scalar with a candidate containing:
requested frequency, actual tuned frequency where available, RSSI, source
(survey/coarse/fine), observation tick, and validity. Create/update it only after
a completed observation above `RF_ALERT_THRESHOLD`. Cancellation and noise-only
results do not create candidates. Expire candidates after a documented short
window and invalidate them when the relevant radio path/band configuration makes
them unusable.

The TX screen must say `Detected RX candidate` and show source, RSSI, and age.
Copying it to TX changes only the generated carrier setpoint. It is not a captured
signal and does not preserve protocol, modulation, data, keys, or waveform.

Before arming and again before transmission, require a radio, a fresh candidate or
explicit preset, `subghz_devices_is_frequency_valid`, a provisioned region, and
`furi_hal_region_is_frequency_allowed`. Show a plain refusal reason and treat a
failed asynchronous start as not transmitted. Current Momentum `dev` also exposes
`subghz_devices_check_tx`, but the installed API 87.1 SDK does not; do not add an
unavailable symbol or claim equivalent detailed refusal reasons. Capture the actual
tuned frequency returned by `subghz_devices_set_frequency`.

Always join/free a finished TX thread before allocating/starting another. Never
reuse a completed thread object. Leaving TX, Back, Settings entry, or shutdown
must stop and synchronously clean up TX before another start can occur.
