# MISSION CONTRACT — Room Sweep v2.0

**Owner:** Hermes (pentest profile) · **Date:** 2026-08-01
**Status:** EXECUTING

## What the user asked for (verbatim intent)
1. **Better visibility** on the Sub-GHz frequency graph.
2. **Investigate + fix suspected distortion at the bottom** of the Sub-GHz chart.
3. **Very basic, simple, obvious, easy-to-understand UI.** Do not add gratuitous buttons/features.
4. **Something must happen (visual + audio + haptic) when a signal is identified**, in every mode — the LED must be used as a primary indicator beyond the display, streaming signal strength.
5. **Audio must be toggleable**, ideally a Geiger-style click that "clears up" (resolves to a steady tone) when a signal locks.
6. Use the graph-engineering skill. Commit before each phase (safe restore points). Verify every mode works.
7. Consider GPS (BFFB has it), extra sensors, extra tabs — *only if they improve usefulness without hurting simplicity*.

## Scope / legal
Authorized assessment on the user's own property. Passive RX only on Sub-GHz. WiFi/BLE/GPS via the user's own BFFB ESP32. No transmission. No third-party targets.

## Non-negotiable constraints
- **Simplest possible control scheme.** Every control must be obvious; no hidden long-press combos.
- **Must not regress the launch-crash fix.** All API calls verified against Momentum API 87.1 before use.
- **Commit before each phase.** `git revert` is the recovery path.

## Definition of done
- [ ] RF graph distortion fixed (no stray bottom lines, labels visible, threshold clean).
- [ ] Signal detection gives unmistakable feedback: escalating LED color+blink + Geiger→tone audio + vibro, in RF mode (and matching feedback for WiFi/BLE detections).
- [ ] Audio + vibro independently toggleable via obvious buttons (Up/Down), with on-screen state.
- [ ] All 4+ tabs build, deploy, and run crash-free.
- [ ] BFFB GPS/WiFi/BLE integrated to the extent the live probe supports.
- [ ] HTML guide updated to match.

## Control scheme (target)
| Button | Action |
|---|---|
| ◀ / ▶ | Cycle tabs (wrap) |
| OK | Start scan (WiFi/BLE) / re-trigger feedback test |
| ▲ Up | Toggle SOUND on/off |
| ▼ Down | Toggle VIBRO on/off |
| Back | Exit (clean teardown) |

## Recovery
Restore any phase: `git log --oneline` → `git checkout <baseline-sha>` or `git revert`. Baseline = the commit tagged `baseline-v1.0`.
