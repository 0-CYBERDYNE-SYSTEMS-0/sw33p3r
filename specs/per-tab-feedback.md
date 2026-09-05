# Spec: Per-tab LED / sound / vibration

## Problem
LED escalation only runs inside the RF survey thread. WiFi/BLE/GPS/TX do not drive RGB from their own signals. Sound/vibro use a partial shared peak but do not fully reflect the active tab.

## Contract
Feedback is **mode-scoped**: each tab owns the signal that drives LED/sound/vibro while that tab is selected.

| Tab | Signal source | LED | Sound (if ON) | Vibro (if ON) |
|-----|---------------|-----|---------------|---------------|
| RF  | peak RSSI / alert | green→yellow→red→blink by peak | Geiger ∝ RSSI, lock tone | edge + graded pulse |
| WiFi | locked target > selected scroll row > strongest AP, **aged** (fresh → linear fade to silence over 2 s→6 s via `room_sweep_analyzer_aged_rssi`) | same RSSI color map | Geiger ∝ aged peak (continuous) | edge + continuous pulse curve |
| BLE | locked target > selected scroll row > strongest device, **aged** 2 s→6 s to silence | same RSSI color map | Geiger ∝ aged peak (continuous) | edge + continuous pulse curve |
| nR | **RPD activity integrator** (`room_sweep_nrf24_activity_score`, NOT the channel counter): sustained traffic saturates in <1 s, ~4 s idle decay to silence | same RSSI color map | Geiger ∝ activity-mapped dBm (continuous) | edge + continuous pulse curve |
| GPS | sats used + fix (sats mapped to peak = −100 + 4·sats) | green≥6 sats, yellow 3–5, red 1–2, off/no data; blink on fresh fix edge | continuous slide 2000→200 ms with sats; chirp on fix acquire | pulse on fix acquire + slow (4 s) heartbeat while fix — NOT graded |
| TX  | arm/tx state | yellow armed, red blink TX, off disarmed | arm double-beep (existing); fixed 120 ms tick while transmitting | arm pulse + fixed 800 ms lock pulses — NOT graded |
| Info | none | reset RGB idle | none (heartbeat only if sound ON at lowest rate) | continuous heartbeat (silent peak ⇒ 5000 ms) |

## Rules
1. Centralize LED updates in `feedback_tick` (or a shared helper). **Remove competing LED writes from RF thread** so tabs don't fight.
2. Force volume / force vibro messages remain (bypass global mute).
3. Sound/vibro still default OFF; enabling still plays test beep/buzz.
4. When leaving a tab, do not leave speaker stuck on (sound_off when alert ends).
5. WiFi/BLE peak selection lives in `room_sweep_feedback_pick_wireless` (priority target > selected > strongest, aged candidate drops to the next candidate; all dead ⇒ −120 dBm silent). The locked WiFi/BLE row keeps `target_rssi` synced (parity with the analyzer HUD). The RF 30 s target-lock expiry is unchanged.
6. Cadences are **continuous dB-linear curves** (no step ladders — every 1 dB closer smoothly shortens the interval; monotonic non-increasing by construction, pure uint32 integer math):
   - Sound = `room_sweep_feedback_sound_interval_ms`: peak rounded to 1 dBm, clamped [−110, −30], `2000 − 1940·s/80` → **2000→60 ms**, ~24 ms per dB (~80 distinct speeds).
   - Vibro = `room_sweep_feedback_vibro_interval_ms`: same clamp, `5000 − 4850·s/80` → **5000→150 ms**, ~61 ms per dB.
   - GPS = `room_sweep_feedback_gps_interval_ms`: peak (−100 + 4·sats) clamped [−100, −60], `2000 − 1800·s/40` → **2000→200 ms**.
   - TX exempt (fixed 120 ms while transmitting); GPS vibro stays the fixed 4 s fix heartbeat.
7. `feedback_tick` is throttled to one pass per ~40 ms and is called at the top of every main-loop pass, so cadence stays stable while input events flow (held buttons no longer starve it).
8. **Hunting keep-alive:** while the analyzer is open **or a WiFi/BLE target is locked** (`keep_alive` in the main loop), scan windows restart within ~250 ms of window end regardless of `auto_rescan` — the 5 s idle gap is longer than the 2 s meter stale time and would visibly freeze then fade the bar. Without keep-alive each window still stops and logs `scan_end`; while keep-alive is active the window stop is skipped (continuous scanning) and restarts use `marauder_start_scan(..., clear=false)` so the table and lock survive.
9. **OK semantics on WiFi/BLE tabs:** with a populated list, short **OK = lock/unlock** the selected row (the intuitive "lock it in"); **Hold OK = manual rescan** (`marauder_start_for_mode`, intentionally clears the table and the lock). With an empty list, short OK still starts the first scan. RF/nRF24/GPS/TX handlers are unchanged (RF keeps OK=start/stop sub-mode + Hold OK=lock).

## Verification
- Source: RF thread has no `sequence_set_*` LED calls.
- Source: `feedback_tick` switches on `app->mode`.
- Host: `tests/test_feedback.c` (priority + aging + continuous-curve endpoints/monotonicity/smoothness/spot values/rounding) and `tests/test_nrf24_state.c` (activity integrator) PASS.
- Device (user): enable sound, walk near WiFi/BLE — click rate tracks that tab's locked/selected source, tightens smoothly with every dB, and decays when you walk out of range; nRF24 tab clicks only with real RPD traffic; vibro tightens smoothly as you close in. On a populated Wi/BT list, short OK locks the selected row and Hold OK rescans; a locked target keeps scan windows cycling (~250 ms gap) so meters/feedback never starve.
