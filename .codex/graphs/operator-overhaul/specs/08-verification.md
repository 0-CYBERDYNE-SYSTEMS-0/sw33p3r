# Verification graph

## Local required gates

1. Confirm branch, clean/expected status, author identity, and SDK API 87.1.
2. Compile/run every host test with C11, warnings, extra warnings, and Werror.
3. Run `_verify_api.py` and `git diff --check`.
4. Run a fresh `ufbt`; require APPCHK Target 7/API 87.1 and no warning/error.
5. Record final commit, FAP SHA-256, byte size, and build time together.
6. Run a final read-only code/safety review against this graph and the diff.

## Manual device gate (requires separate authorization)

Launch the exact hashed artifact, verify loader/storage identity, traverse every
tab and state without transmitting, inspect generated session/dump/report files,
and verify cleanup/relaunch. Any carrier transmission requires explicit separate
authorization and a lawful controlled test setup; it is not implied by device UI
testing.

Historical evidence generated for another commit or artifact cannot satisfy any
gate above.
