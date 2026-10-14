# Spec: ESP32-C5 dual-band (2.4 + 5 GHz) board support

## Goal
Support Marauder boards with 5 GHz Wi-Fi (ESP32-C5 class, e.g. the
"Double Barrel 5G" rigs shipping since late 2025) so the Wi tab can survey
5 GHz networks and 5 GHz drone/Remote-ID Wi-Fi beacons — same UART protocol,
new board profile.

## Hardware / firmware facts (2026-10-11 web research)
- ESP32-C5: dual-band Wi-Fi 6 (2.4 + 5 GHz, channels ~36–165), runs
  ESP32Marauder (also Bruce/GhostESP). Controlled by the Flipper over the
  same UART Marauder CLI at 115200 — our existing parser/routing port.
- Marauder is linear (one scan at a time) — unchanged mutual-exclusion rules.
- Wi capture sources stay the pinned set: `sniffbeacon`, `sniffraw`,
  `sniffprobe`, `sniffesp`/`sniffpwn`. `scanap`/`scansta` remain UNSENT
  until their station-line format is pinned live (standing decision).
- 2.4-only BFFB boards must keep working unchanged.

## Contract
1. **Board profile** detected once per session: after `marauder_confirmed`,
   send `info` (and/or `help`); parse capability text into a Flipper-free
   `room_sweep_board.h` profile: {dual_band: bool, rid: bool, gps: bool,
   name/version}. Unknown responses = conservative 2.4-only profile. Host
   tests with synthetic `info`/`help` fixtures (`tests/test_board.c`).
2. When dual_band: Wi tab gains a **Band** selector (2.4 / 5 / both) driving
   the existing per-source scan starts; channel selection for 5 GHz uses the
   Marauder `channel` command (pin its exact output live first — same
   not-yet-observed annotation discipline as every other command).
3. Scan windows in "both" mode alternate bands (bounded windows, existing
   stopscan-before-switch pattern); scan_end events record the band so CSV
   rows are honest about which band produced a hit.
4. Observation CSV detail gains a `band=2|5` token; ordinals unchanged.
   5 GHz rows feed OUI/classify/rogue/stats exactly like 2.4 rows.
5. Room Report: per-band counts line ("2.4 GHz: n APs · 5 GHz: m APs").

## UI
Wi header tag becomes src+band (e.g. "BEACON 5G"). No new tab; no layout
regressions (128×64 budget checked in `room_sweep_ui_layout.h` tests).

## Verification
- Host: `tests/test_board.c` (info parsing, conservative default, band
  gating logic), band-token CSV composition. Registered in `init.sh`.
- `python3 _verify_api.py` clean; ufbt zero warnings.
- Device (requires C5-class board): profile detected; 5 GHz AP visible in
  Wi list with band token; 2.4-only BFFB regression pass.

## Out of scope
Deauth/attack commands on any band (mission ban), 6 GHz, Marauder `join`/
`evilportal` families, USB-attached 5 GHz adapters (RTL8812AU is host-side
hardware, not a Flipper expansion).
