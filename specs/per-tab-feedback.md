# Spec: Per-tab LED / sound / vibration

## Problem
LED escalation only runs inside the RF survey thread. WiFi/BLE/GPS/TX do not drive RGB from their own signals. Sound/vibro use a partial shared peak but do not fully reflect the active tab.

## Contract
Feedback is **mode-scoped**: each tab owns the signal that drives LED/sound/vibro while that tab is selected.

| Tab | Signal source | LED | Sound (if ON) | Vibro (if ON) |
|-----|---------------|-----|---------------|---------------|
| RF  | peak RSSI / alert | green→yellow→red→blink by peak | Geiger ∝ RSSI, lock tone | edge + lock pulse |
| WiFi | strongest AP RSSI | same RSSI color map | Geiger ∝ WiFi RSSI | edge + lock |
| BLE | strongest BLE RSSI | same RSSI color map | Geiger ∝ BLE RSSI | edge + lock |
| GPS | sats used + fix | green≥6 sats, yellow 3–5, red 1–2, off/no data; blink on fresh fix edge | slow tick when streaming; faster with more sats; chirp on fix acquire | pulse on fix acquire + slow heartbeat while fix |
| TX  | arm/tx state | yellow armed, red blink TX, off disarmed | arm double-beep (existing); TX active tone optional | arm pulse; TX heartbeat |
| Info | none | reset RGB idle | none (heartbeat only if sound ON at lowest rate) | none unless vibro ON heartbeat |

## Rules
1. Centralize LED updates in `feedback_tick` (or a shared helper). **Remove competing LED writes from RF thread** so tabs don't fight.
2. Force volume / force vibro messages remain (bypass global mute).
3. Sound/vibro still default OFF; enabling still plays test beep/buzz.
4. When leaving a tab, do not leave speaker stuck on (sound_off when alert ends).

## Verification
- Source: RF thread has no `sequence_set_*` LED calls.
- Source: `feedback_tick` switches on `app->mode`.
- Device (user): enable sound, walk near WiFi/BLE — click rate tracks that tab's RSSI; GPS fix blinks LED.
