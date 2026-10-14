# Agent USB access

Provide one installed host command for repeatable Flipper development access.
Keep the Room Sweep mission and the current user-assigned USB owner intact.

## Required behavior

- Install both CYBERDYNE Flipper skills and make the device harness discoverable
  to Codex, Claude, and Hermes on the developer host.
- Install an executable with isolated, reproducible Python dependencies.
- Select the actual Flipper USB CDC port, sharing a lock across agents,
  worktrees, and macOS cu/tty aliases; reject an existing external serial owner.
- Verify a real command round trip before issuing a requested CLI command.
  A greeting or enumeration alone is insufficient.
- Bound every command and supervise long SDK/RPC/external-tool operations.
  Stop only the operation's process group on timeout/cancellation; release
  the lease afterward. Never replay an uncertain mutation automatically.
- Send complete Press/Short-or-Long/Release input sequences.
- Close and confirm app exit before uploading; verify file size and hash after
  transfer. Produce screenshots only from complete 1024-byte RPC frames.
- Preserve current SDK/firmware and project deploy gates. Report physical
  recovery when the MCU/USB service cannot process host requests.

## Verification

Offline transport tests use fresh pseudo-terminals and temporary lock files;
they must never open a real Flipper. Check normal exchange, complete input,
timeout without replay, greeting-only refusal, command error detection,
manual app exit, concurrent ownership, cu/tty alias identity, external owners,
child timeout cleanup, incomplete frames, and mismatched upload hashes.

Live verification belongs to the assigned USB owner. Require actual CLI
responses, a real screen frame, exact-image upload verification, and the
candidate's existing launch/exit, tab walk, and survival receipts. Offline
tests and installed skills do not prove completion of live device access.

Implementation: tools/flipperctl.py. Setup: tools/install_flipperctl.sh.
Agent guide: docs/flipper-agent-access.md. Offline check:
`~/.local/share/flipperctl/venv/bin/python tools/test_flipperctl.py`.
