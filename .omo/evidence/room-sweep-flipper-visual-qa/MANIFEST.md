# Room Sweep Flipper visual-QA capture manifest

These are retained iPhone Mirroring captures of the Flipper display, not HTML
report images. All captures are 310x684 JPEGs.

## Capture provenance

- `rf-survey.jpeg`: RF Survey screen, captured 2026-08-01 15:40:04.
- `settings.jpeg`: Settings overlay, captured 2026-08-01 15:40:50.
- `info.jpeg`: Info screen after the footer-spacing fix, captured 2026-08-01 15:38:16.

## Supplementary tab captures

- All seven captures were made during the preceding UI-identical deployment
  (`aaa8c8daa39c5f37b3b66b0f650a2bd8e3031c6248e8410d69f764ec96048b39`). The
  final `7994ddea...` artifact changes only host-tested NMEA semantic
  validation paths; no Flipper renderer changed. The exact final artifact's
  all-tab control traversal is recorded in the device receipt.

The visual review found and fixed Settings row/footer overlap and Info footer
overlap. No blank, clipped, or crashed screen was observed in the retained
captures. No TX transmission was initiated.
