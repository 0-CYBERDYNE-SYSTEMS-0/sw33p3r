# F03 input/cursor host evidence

Scenario: compile and run the F03 Furi-free input-state test, covering bounded/wrapping Up/Down cursor movement, empty/invalid cursor handling, non-browse no-op behavior, and the tab predicate rejecting long/repeated Left/Right actions.

Invocation:

```sh
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c -o /tmp/room_sweep_input_test && /tmp/room_sweep_input_test
```

Binary observable: process exited 0 and printed `RESULT: ALL PASS` after 40 PASS checks.

Captured output:

```text
PASS: long Back exits even when Settings is open
PASS: long Back exits when Settings and TX state overlap
PASS: long Back exits outside Settings
PASS: long Back exits while TX is active
PASS: short Back closes Settings
PASS: short Back closes Settings before changing hidden TX state
PASS: short Back disarms an armed TX tab
PASS: short Back opens Settings outside TX
PASS: short Up is classified safely
PASS: long Up is classified safely
PASS: repeat Up is classified safely
PASS: short Down is classified safely
PASS: long Down is classified safely
PASS: repeat Down is classified safely
PASS: short Left is classified safely
PASS: long Left is classified safely
PASS: repeat Left is classified safely
PASS: short Right is classified safely
PASS: long Right is classified safely
PASS: repeat Right is classified safely
PASS: short OK is classified safely
PASS: long OK is classified safely
PASS: repeat OK is classified safely
PASS: short Back is classified safely
PASS: long Back is classified safely
PASS: repeat Back is classified safely
PASS: unknown key is rejected
PASS: unsupported input phase is rejected
PASS: Up wraps the browse cursor from the first item
PASS: Down wraps the browse cursor from the last item
PASS: Up steps toward the first item
PASS: Down steps toward the last item
PASS: an invalid cursor is normalized before stepping
PASS: an empty browse list always returns cursor zero
PASS: a non-browse action leaves the cursor unchanged
PASS: short Left is the only previous-tab action
PASS: short Right is the only next-tab action
PASS: long Left/Right alternate actions cannot change tabs
PASS: repeated Left/Right cannot change tabs
RESULT: ALL PASS
```
