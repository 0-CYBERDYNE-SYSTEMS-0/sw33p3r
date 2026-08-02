# F01 Settings input contract review (read-only)

Sources inspected: `room_sweep_input.h`, `tests/test_input_state.c`, and
`room_sweep.c:2242-2322`.

## Exhaustive pure contract matrix

| Context | Event/key | Required result |
|---|---|---|
| Any | `Press` or `Release` (no phase) | No-op; no selection, toggle, dump, or counter change. |
| Settings open | Short `Up` / `Down` | Move selection exactly once, wrapping `0 ↔ SET_COUNT-1`. |
| Settings open | Repeat `Up` / `Down` | Advance at most one item per delivered repeat, only if the runtime explicitly enables repeat browsing; never invoke a setting action. |
| Settings open | Long `Up` / `Down` | At most one browse move; never invoke a setting action. |
| Settings open | Short `OK` | Invoke exactly one selected setting action. |
| Settings open | Long `OK` (`Secondary`) | Reject/ignore in Settings; must not toggle Log or invoke Dump. |
| Settings open | Repeat `OK` | No-op; must not toggle Log or invoke Dump. |
| Settings open | Short `Left` / `Right` | No-op. |
| Settings open | Short `Back` | Close Settings only. |
| Settings open | Long `Back` | Exit immediately; Long wins regardless of settings/TX state. |
| Settings closed, TX disarmed | Short `Back` | Open Settings. |
| Settings closed, TX armed/transmitting | Short `Back` | Disarm only. |
| Settings closed, any TX state | Long `Back` | Exit immediately. |

For each selected index `{Sound,Vibro,Rescan,Log,ExtBand,Baseline,Dump,TXDur}`:

- Short `OK` performs one action only.
- Long `OK`, all repeat-like events, and non-`OK` keys perform no action.
- `Log` toggles once; `Dump` invokes its writer once; `Baseline` captures once;
  `TXDur` advances once modulo its range.

## Sequence cases

1. Short `Back` opens Settings; a generated `Repeat OK`/`Long OK` from the same
   hold does not act; the next intentional Short `OK` acts once. Repeat
   Up/Down, if enabled, may browse but cannot trigger the selected action.
2. Short `Back` closes Settings; it must not apply the current selection.
3. Short `OK` followed by any number of Repeat/Long events changes state or
   action count exactly once.
4. Selection at index 0 with Short `Up` wraps to `SET_COUNT-1`; selection at
   `SET_COUNT-1` with Short `Down` wraps to 0; each event advances once.

## Gap identified

The outer loop accepts Short and Long at `room_sweep.c:2242`, but the Settings
branch's `event.key == InputKeyOk` path at `2265-2322` does not require Short.
Therefore a Long `OK` currently toggles settings and can trigger Log or Dump.
The new pure classifier maps Long `OK` to a generic `Secondary` action; the
runtime Settings adapter must explicitly reject that action. Generic tests do
not prove this context-specific filter.
The existing `tests/test_input_state.c` covers four Back cases only; it does
not cover long Back in closed contexts, repeat filtering, Settings navigation,
or action side-effect cardinality.
