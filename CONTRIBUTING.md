# Contributing to Room Sweep

Room Sweep is a receive-side RF survey app for Flipper Zero (external FAP,
appid `room_sweep`) built for Momentum mntm-012, API 87.1, target 7. Read
`MISSION.md` first — it is the scope and legal contract this repo enforces.

## The mission constraint (non-negotiable)

Detect/analyze only. PRs that add jamming, deauth, capture/replay, flood,
blocking, or any other deny-service functionality will be rejected regardless
of code quality. The only transmitter is the TX tab's bounded, safety-gated
1–10 s carrier test (`Disarmed → Armed → Starting → Transmitting`), never a
replay or blocking burst. If a feature needs the radio to interfere with
another signal, it is out of scope by definition.

## Dev environment

- Any machine with a C compiler (`cc`) and Python 3 — the host tests and
  `_verify_api.py` run entirely on the host.
- [ufbt](https://github.com/flipper-zero/ufbt) for the firmware build:
  `pip install ufbt`. It downloads the SDK on first use; no manual setup.
- A real Flipper (and optionally the BFFB board) is only needed to deploy and
  field-test — see "Put it on a Flipper" in `README.md`.

## The gate: `./init.sh`

`./init.sh` must pass before any PR or commit is considered done. It compiles
all 18 host test suites with `cc -std=c11 -Wall -Wextra -Werror` (several also
with `-pedantic`) into `/tmp` and runs them, then builds the FAP with `ufbt`.
CI runs the same script on every push/PR to `main`.

To run one suite, copy its `cc` line from `init.sh`:

```sh
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/t_nmea && /tmp/t_nmea
```

After touching any Flipper API call, also run:

```sh
python3 _verify_api.py
```

It checks every called symbol against the Momentum API 87.1 export table. An
unlisted symbol links fine and then hard-faults at `loader open`, so this
check is not optional for API changes. It reads the export table from your
locally-initialized ufbt SDK state; set `UFBT_API_SYMBOLS` to point at a
different CSV.

## Code philosophy

- Decision logic lives in header-only state files (`room_sweep_state.h`,
  `room_sweep_scan.h`, `room_sweep_marauder.h`, ...) that include no Flipper
  headers, so plain `cc` can test them. Each has a matching suite in `tests/`
  built around the `CHECK(cond, msg)` macro — no test framework.
- `room_sweep.c` is wiring and rendering only. If logic can be unit-tested,
  it belongs in a state header with a host test.
- The RF and TX worker threads run 2 KiB stacks: keep thread stack-locals
  under 64 bytes.
- Integer math or explicit casts only: `-Werror` enables
  `-Wdouble-promotion`, so an accidental `float` promotion fails the gate.
- Display is 128×64; `FontKeyboard` is the smallest font (~25 chars/line).

## Workflow

- One feature per iteration: add a spec in `specs/`, implement the logic in a
  state header with a host test, wire it in `room_sweep.c`, verify, commit.
- Branches: `main` is the trunk; use `feat/*` for feature work. `restore/*`
  are pre-overhaul recovery snapshots, not working branches.
- No mock data — survey output must come from real radio/UART input.
- Commit messages: short conventional subjects (`feat:`, `fix:`, `docs:`,
  `chore:`), as in `git log --oneline`. No co-author trailers.
- QA evidence and unredacted captures live in gitignored local directories
  and must never be committed. Keep real MACs, SSIDs, device serials, and
  local paths out of every committed file.
- `features.json` is the QA checklist; flip a `passes` flag only after the
  scoped verify actually runs.

## Where the details live

- `AGENTS.md` / `CLAUDE.md` — full architecture map, thread/locking model,
  and hard rules (kept in sync; update both when changing scope or rules).
- `FLIPPER_PITFALLS.md` — read before touching notifications, input, UART,
  or radio code.
- `docs/BFFB_MOMENTUM.md` — source of truth for BFFB/Marauder UART commands
  and baud; check before changing any UART command.
- `DESIGN.md`, `USER_GUIDE.md`, `progress.log`, `specs/`, `features.json`.
