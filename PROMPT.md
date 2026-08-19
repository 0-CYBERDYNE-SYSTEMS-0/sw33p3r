# PROMPT.md — Full-sweep expansion (detect-only)

Status: this expansion is complete (`features.json` all `passes: true`).
Current operator docs: `README.md`, `USER_GUIDE.md`,
`docs/room_sweep_control_map.html`. Keep this file as the graph-loop recipe.

## Stack (read every iteration, this order)
1. `progress.log` (tail)
2. `features.json` (first `passes: false` only)
3. Matching file under `specs/`
4. `MISSION.md` + `FLIPPER_PITFALLS.md` before radio/UART/notification changes
5. `docs/BFFB_MOMENTUM.md` before BFFB/nRF24 assumptions
6. Header-only state files first; `room_sweep.c` is wiring only

## Rules
- **One feature per iteration.** Flip `passes` only after scoped host verify (and ufbt for wiring).
- **Search before building.** Do not assume missing code.
- **Detect-only.** No jam, block, deauth, flood, mousejack, or continuous deny-service TX.
- Host gate: `./init.sh` must pass before device deploy.
- Commits after each successful item. No Claude/Anthropic co-author trailers.
- Restore point: tag/branch `restore/pre-full-sweep-2026-08-09`.

## Stop when
All `features.json` entries have `passes: true`, or budget exhausted.

## Mission focus
Room Report (human-readable) + full-sweep sequencer + nRF24 RX survey +
internal CC1101 while BFFB SPI is nRF24. Coverage checklist in the report.
