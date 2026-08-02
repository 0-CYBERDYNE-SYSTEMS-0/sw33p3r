# Room Sweep final manual QA receipt

- Worktree: `/Users/scrimwiggins/sw33p3r`
- HEAD: `d6182a155b1d4a198e916847ebe62da3a9e8cb4e`
- Artifact: `dist/room_sweep.fap`
- SHA-256: `7994ddea5961dc2889a0f767831fc3428b32ae6d1df9f048ec2f0a2fe91e853c`
- Device: Momentum `mntm-012`, target 7, API 87.1
- Port: `/dev/cu.usbmodemflip_Rug1k01`
- Local and device artifact size: `28136` bytes.

## Host gates

- NMEA host test: 58/58 executable assertions pass with `-Wall -Wextra -Werror`.
- Back-routing host test: 4/4 assertions pass with `-Wall -Wextra -Werror`.
- API export scan: clean, 52/56 call-like symbols present and the remaining four are type-like include tokens.
- `ufbt`: exit 0, Target 7, API 87.1, `-Werror`.
- `git diff --check`: pass.

## Device gates

- Exact artifact installed with `ufbt launch`.
- `storage stat /ext/apps/Tools/room_sweep.fap` reported `28136b`; `loader info`
  reported `Application "Room Sweep" is running` after the final relaunch.
- Settings: all four settings changed through valid Press→Short→Release events; Short Back closed Settings; Long Back from Settings produced `No application is running`.
- Exact artifact relaunched; RF Survey, RF Sweep start/cancel, long band selection, RF Peak, WiFi scan path, BLE scan path, GPS controls, TX arm/frequency/disarm, Info, and tab wraparound were traversed without a crash.
- After traversal, `loader info` reported `Application "Room Sweep" is running`.
- Final Press→Long→Release Back produced `No application is running`.
- No deliberate RF transmission was performed.

## Boundary

- Retained iPhone Mirroring captures visually checked RF Survey, Settings, WiFi,
  BLE, GPS, TX, and Info on the immediately preceding UI-identical deployment;
  Settings and Info footer spacing were corrected and rechecked there. The
  final parser-only artifact was traversed on-device, but no exact-current
  screenshot claim is made.
- No BFFB result stream was attached, so live WiFi/BLE parser output, GPS freshness rendering, audio/haptic perception, and deliberate RF field behavior remain user field checks.
