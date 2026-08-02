# Room Sweep final artifact evidence

## Identity

- Repository: `/Users/scrimwiggins/sw33p3r`
- HEAD: `d6182a155b1d4a198e916847ebe62da3a9e8cb4e`
- Build command: `ufbt`
- Build result: exit 0, Target 7, API 87.1, `-Werror`
- Artifact: `dist/room_sweep.fap`
- Artifact SHA-256: `7994ddea5961dc2889a0f767831fc3428b32ae6d1df9f048ec2f0a2fe91e853c`
- Install target: `/ext/apps/Tools/room_sweep.fap`
- Device: Momentum `mntm-012`, API 87.1, target 7, `/dev/cu.usbmodemflip_Rug1k01`

## Host verification

- `cc -std=c11 -Wall -Wextra -Werror -I. nmea.c tests/test_nmea.c`: `RESULT: ALL PASS (0 failures)`; 58 executable assertions.
- `cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c`: `RESULT: ALL PASS`; 4 assertions.

## Device verification against this artifact

`ufbt launch` installed the artifact above immediately before the final runs;
the device file size was 28,136 bytes, matching the local artifact.
The detailed exact-artifact transcript is
`.omo/evidence/room-sweep-device-qa-exact-7994-2026-08-01.txt`.

1. Settings run changed Sound, Vibro, Auto-Rescan, and TX Duration with valid
   Press→Short→Release input sequences. Short Back closed Settings; reopening
   Settings and sending Press→Long→Release made `loader info` report:
   `No application is running`.
2. A fresh launch then traversed RF Survey, RF Sweep start/cancel, RF band
   selection, RF Peak, WiFi scan/control input, BLE scan/control input, GPS
   control input, TX arm/frequency/disarm input, Info, and tab wraparound.
   `loader info` reported `Application "Room Sweep" is running` after the
   traversal with no crash.
3. Press→Long→Release Back from the final traversal made `loader info` report:
   `No application is running`.

The traversal proves launch, input routing, tab survival, cleanup, and exit.
The final source additionally bounds peak refinement to the selected CC1101
band, queues UART lines out of the ISR, rejects malformed and semantically
invalid GPS fields, rejects malformed GPS coordinates,
detects scan silence after 30 seconds, and pre-loads a detected RF frequency in
TX. Retained iPhone Mirroring captures are listed in
`.omo/evidence/room-sweep-flipper-visual-qa/MANIFEST.md`; all were captured on
the immediately preceding UI-identical deployment, while this final parser-only
artifact was traversed through every tab on-device. Settings and Info footer
spacing were corrected and rechecked on-device. It does not claim live BFFB
result parsing, measured audio/haptic output, or a deliberate RF transmission.
