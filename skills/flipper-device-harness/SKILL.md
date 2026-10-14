---
name: flipper-device-harness
description: Use the Room Sweep flipperctl entry point for Flipper Zero development, USB CLI, button input, screenshots, verified transfers, and bounded device work.
---

# Room Sweep Flipper access

Use the installed `flipperctl` command for all Flipper USB access in this
repository. Do not open the serial port directly or bypass its shared USB
ownership lock. Run external device helpers through `flipperctl run`.

```sh
flipperctl ports
flipperctl cli 'uptime'
flipperctl key right
flipperctl key back --long
flipperctl --timeout 20 snapshot /tmp/flipper-screen.png
flipperctl --timeout 180 upload dist/room_sweep.fap /ext/apps/Tools/room_sweep.fap
flipperctl cli 'loader open "/ext/apps/Tools/room_sweep.fap"'
flipperctl --timeout 180 run -- python3 /path/to/device-measurement.py
```

Before deploying Room Sweep, run `./init.sh` and `python3 _verify_api.py`.
Follow `MISSION.md`: Room Sweep is receive-side survey software with a
separately safety-gated bounded carrier test. Keep TX disarmed during normal
device checks. Use `docs/flipper-agent-access.md` for upload verification,
device recovery, and the exact USB ownership rules.
