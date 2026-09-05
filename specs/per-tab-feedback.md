# Spec: Per-tab LED / sound / vibration

## Problem
LED escalation only runs inside the RF survey thread. WiFi/BLE/GPS/TX do not drive RGB from their own signals. Sound/vibro use a partial shared peak but do not fully reflect the active tab.

## Contract
Feedback is **mode-scoped**: each tab owns the signal that drives LED/sound/vibro while that tab is selected.

| Tab | Signal source | LED | Sound (if ON) | Vibro (if ON) |
|-----|---------------|-----|---------------|---------------|
| RF  | peak RSSI / alert | green→yellow→red→blink by peak | Geiger ∝ RSSI, lock tone | edge + graded pulse |
| WiFi | locked target > selected scroll row > strongest AP, **aged** (fresh → linear fade to silence over 2 s→6 s via `room_sweep_analyzer_aged_rssi`) | same RSSI color map | Geiger ∝ aged peak | edge + graded pulse |
| BLE | locked target > selected scroll row > strongest device, **aged** 2 s→6 s to silence | same RSSI color map | Geiger ∝ aged peak | edge + graded pulse |
| nR | **RPD activity integrator** (`room_sweep_nrf24_activity_score`, NOT the channel counter): sustained traffic saturates in <1 s, ~4 s idle decay to silence | same RSSI color map | Geiger ∝ activity-mapped dBm | edge + graded pulse |
| GPS | sats used + fix | green≥6 sats, yellow 3–5, red 1–2, off/no data; blink on fresh fix edge | slow tick when streaming; faster with more sats; chirp on fix acquire | pulse on fix acquire + slow (4 s) heartbeat while fix — NOT graded |
| TX  | arm/tx state | yellow armed, red blink TX, off disarmed | arm double-beep (existing); fixed 120 ms tick while transmitting | arm pulse + fixed 800 ms lock pulses — NOT graded |
| Info | none | reset RGB idle | none (heartbeat only if sound ON at lowest rate) | graded heartbeat (silent peak ⇒ 5000 ms) |

## Rules
1. Centralize LED updates in `feedback_tick` (or a shared helper). **Remove competing LED writes from RF thread** so tabs don't fight.
2. Force volume / force vibro messages remain (bypass global mute).
3. Sound/vibro still default OFF; enabling still plays test beep/buzz.
4. When leaving a tab, do not leave speaker stuck on (sound_off when alert ends).
5. WiFi/BLE peak selection lives in `room_sweep_feedback_pick_wireless` (priority target > selected > strongest, aged candidate drops to the next candidate; all dead ⇒ −120 dBm silent). The locked WiFi/BLE row keeps `target_rssi` synced (parity with the analyzer HUD). The RF 30 s target-lock expiry is unchanged.
6. Sound cadence = `room_sweep_feedback_sound_interval_ms` (scanner ladder 60/100/180/350/700/1200/2000 ms; GPS ladder 200/500/1000/2000 ms). Vibro cadence = `room_sweep_feedback_vibro_interval_ms`, graded 150/300/600/1200/2500/5000 ms, monotonic non-increasing as peak rises; GPS and TX are exempt (fixed cadences preserved).
7. `feedback_tick` is throttled to one pass per ~40 ms and is called at the top of every main-loop pass, so cadence stays stable while input events flow (held buttons no longer starve it).

## Verification
- Source: RF thread has no `sequence_set_*` LED calls.
- Source: `feedback_tick` switches on `app->mode`.
- Host: `tests/test_feedback.c` (priority + aging + ladders + vibro monotonicity) and `tests/test_nrf24_state.c` (activity integrator) PASS.
- Device (user): enable sound, walk near WiFi/BLE — click rate tracks that tab's locked/selected source and decays when you walk out of range; nRF24 tab clicks only with real RPD traffic; vibro tightens as you close in.
