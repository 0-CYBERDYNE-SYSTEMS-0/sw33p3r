# fix_plan.md — Room Sweep v3.1+ work queue (graph-engineering kanban)
# Top = highest priority. One item per loop. Flip to DONE only after verification.
# Mirrors the session todo() kanban; this file is the durable, compression-proof copy.

[P0] BUG: no sound/vibro in any mode when toggled ON
     root cause: (1) sequences don't override global mute -> add
     message_force_speaker_volume_setting_1f + message_force_vibro_setting_on;
     (2) feedback signal-gated above -75/-90 dBm -> add heartbeat + lower Geiger floor.
     verify: on-device, toggle Up -> audible click; Down -> vibro pulse; RF mode -> heartbeat.

[P1] UI: SubGHz (RF) tab top-right corner crowded; want clean TX + RX indicators
     - move RSSI dBm readout, declutter S:ON/V:ON
     - add TX/RX status glyphs (RX while sweeping; TX when transmitting)

[P2] Mode audit: revisit RF / WiFi / BLE / GPS / Info for capability + clarity
     - each tab: what it does, what it needs (BFFB?), clear empty state

[P3] Robustness extension: add controlled SubGHz TX (carrier test / jammer-check)
     - SAFETY GATE: explicit OK-hold confirmation, on-own-property only, time-limited,
       single fixed freq, clear on-screen warning. Uses furi_hal_subghz_start_async_tx
       (verified in api_symbols.csv lines 1738/1740). RX path stops before TX (no collision).
     - harden: bounds, error paths, clean teardown on every exit.

[P4] Build + deploy + on-device verify all tabs, then commit + final report.

DONE:
[x] v3.0 GPS tab + NMEA parser (48/48 host tests), committed d764158
