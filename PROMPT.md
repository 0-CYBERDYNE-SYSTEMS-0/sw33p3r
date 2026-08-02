# PROMPT.md — Room Sweep field-fix fix loop

## Stack (read every iteration, this order)
1. `progress.log` (tail)
2. `fix_plan.md` (top unchecked item only)
3. Matching file under `specs/`
4. `FLIPPER_PITFALLS.md` before any NotificationSequence / input / UART change
5. `room_sweep.c` / `room_sweep.h` / `nmea.c` as needed

## Rules
- **One item per iteration.** Mark done only after scoped verification.
- **Search before building.** Do not assume missing code.
- Host tests: `./init.sh` must pass before device deploy.
- Device serial: `/dev/cu.usbmodemflip_XXXX01` @ 115200.
- No mock data. Prefer Just Call Me Koko Marauder CLI facts.
- Commits after each successful item. No Claude/Anthropic co-author trailers.

## Stop when
`fix_plan.md` NEXT section has no open `[ ]` items for this mission, or budget exhausted.

## Mission focus
BLE sniff ERR, GPS full use (stream + distance), per-tab LED/sound/vibro.
