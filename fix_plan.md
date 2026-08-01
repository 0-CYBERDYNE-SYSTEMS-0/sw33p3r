# FIX PLAN — prioritized work queue (top = highest priority)
# One item per loop. Commit after each. Do NOT mix with features.json.

[P1] Fix RF bar distortion: replace hollow frame bars with FILLED boxes; force min-height 0 for sub-threshold (no stray 1px lines); ensure bars sit on a clean baseline row that is NOT the last pixel row.
[P1] Fix off-screen labels: move baseline up (base_y=56), draw frequency labels at y=63 (last visible row) in FontKeyboard, every 4th channel — all labels fully on-screen.
[P1] Fix threshold line: draw as a thin dashed/dotted line BEHIND bars (draw line first, then bars), so it reads as a reference not a smudge.
[P1] Visibility pass: SIGNAL! alert in FontBigNumbers inverse-video banner; active-channel numeric RSSI readout in FontPrimary; tab-strip labels (RF/WiFi/BLE/Info) replacing bare segments.
[P2] Streaming LED feedback: map peak RSSI to LED — below threshold = off/green-10; weak = yellow solid; strong = red solid; very strong = red blink-fast. Update every sweep (~fast).
[P2] Geiger audio: click rate scales with peak RSSI proximity to threshold; on lock (sustained above threshold) resolve to a steady tone (sequence_success-like) while locked; stops when signal drops. Only when sound enabled.
[P2] Vibro feedback: single pulse on new detection crossing threshold; off when disabled.
[P2] Up/Down toggles: Up=Sound on/off, Down=Vibro on/off. On-screen speaker+vibro state icons in tab strip. Defaults from user system setting where possible.
[P3] BFFB probe: send `help`/common Marauder cmds over USART, capture real command set + any GPS output. Decide GPS/WiFi/BLE feature scope from evidence.
[P3] Integrate BFFB detections with matching feedback (LED+audio+vibro) when a WiFi AP or BLE device is found, and show a GPS fix line if available.
[P4] Verify: build clean (zero warnings), deploy, drive every tab over CLI, confirm no crash, confirm feedback triggers. Update ROOM_SWEEP_GUIDE.html.
