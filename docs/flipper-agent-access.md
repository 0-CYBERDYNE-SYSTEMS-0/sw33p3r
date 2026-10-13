# Flipper access for coding agents

Install once from the repository:

```sh
sh tools/install_flipperctl.sh
```

The installer creates `~/.local/bin/flipperctl` with an isolated Python
environment, fetches the pinned CYBERDYNE `flipper-ops` and
`flipper-payload-forge` skills, and exposes those plus the Room Sweep
`flipper-device-harness` skill to Codex, Claude Code, and Hermes. It does not
open USB or change firmware. Ensure `~/.local/bin` is on PATH, or use the full
command path. Override `FLIPPERCTL_PYTHON`, `FLIPPERCTL_HOME`, or
`FLIPPERCTL_BIN` when installing elsewhere. Skill directories default to
`~/.agents/skills`, `~/.claude/skills`, and `$HERMES_HOME/skills` (default
`~/.hermes/skills`). Override a destination with
`FLIPPERCTL_CODEX_SKILLS_DIR`, `FLIPPERCTL_CLAUDE_SKILLS_DIR`, or
`FLIPPERCTL_HERMES_SKILLS_DIR`. Existing skills with those names are kept;
start new agent sessions after installation so they discover the skills.
The installer defaults to Python 3.12 and requires Python 3.10 or newer for
the current SDK. Direct script invocations also select the installed isolated
environment before opening USB, so a system Python 3.9 cannot close an app
and then fail importing the SDK's storage code.

## Use one entry point

```sh
flipperctl ports
flipperctl cli 'uptime'
flipperctl cli 'loader info'
flipperctl cli 'free'
flipperctl key right
flipperctl key back --long
flipperctl --timeout 20 snapshot /tmp/flipper-screen.png
```

The sole attached Flipper is selected by USB VID/PID. If there are multiple
devices, use `--port /dev/cu.usbmodemflip_NAME` or set `FLIPPER_PORT`.
`ports` only enumerates host device records. Other actions open the device.

The tool takes a shared host lock, checks for external serial owners with
`lsof`, and confirms a real `help` round trip before sending a command. It
uses complete PRESS → SHORT/LONG → RELEASE events. A startup greeting alone
is insufficient. CLI exchanges have a deadline; uploads, snapshots, and
external tools run in a supervised process group that is stopped on timeout.
Commands are never automatically replayed after a timeout.
Cleanup also stops descendants that ignore termination after their supervisor
exits. RPC screenshots wait for a complete frame and handle partial USB reads
and frames interleaved with command acknowledgements.

On mntm-012, `help` and `loader` are marked ParallelSafe in the firmware source;
an open GUI app alone does not explain those commands producing no reply.
Other commands may explicitly refuse to run while an app is open.

All agents on this host must use this entry point. A busy result identifies
the owner; allow that owner to finish. Do not bypass the lock with a scratch
script, qFlipper, Flipper Lab, or `ufbt launch`. The lock cannot prevent an
uncooperative program from opening USB between the ownership check and open;
the workflow depends on all agents following the same entry point.

A specific user instruction assigning USB to one agent still takes precedence
over tool availability. Other agents may install tooling, work on source,
and run offline transport tests without touching the Flipper.

## Upload and verify a FAP

For Room Sweep, pass the project gates first and preserve the Momentum SDK
(mntm-012, API 87.1, target 7):

```sh
./init.sh
python3 _verify_api.py
flipperctl --timeout 180 upload dist/room_sweep.fap /ext/apps/Tools/room_sweep.fap
flipperctl cli 'loader open "/ext/apps/Tools/room_sweep.fap"'
flipperctl --timeout 20 snapshot /tmp/room-sweep.png
```

`upload` closes the running app before transfer, uses the installed SDK's
text storage protocol, and requires matching remote file size and MD5. A
timeout can leave a partial file; do not launch it until a complete transfer
is verified. `download` also verifies the resulting file:

```sh
flipperctl --timeout 60 download /ext/apps_data/room_sweep/report-1.txt /tmp/report-1.txt
```

Wrap other USB tools or whole measurement scripts in the same lease:

```sh
flipperctl --timeout 180 run -- ufbt launch
flipperctl --timeout 180 run -- python3 /path/to/measurement.py
```

`run` passes `FLIPPER_PORT` to the child. The child must actually use that port;
do not auto-select another device. It holds the lock throughout the operation
and stops the child process group if the deadline expires. It forwards the
tool's output and fails when the child fails. It does not claim to verify a
measurement script's conclusions. A script reporting survival from blank
polls is still invalid evidence.

CLI can execute any single firmware command authorized for the task. Use
`run` for interactive or streaming tools rather than treating a missing final
prompt as success. Do not send `start_rpc_session` through `cli`; `snapshot`
manages the transition to protobuf, obtains the full 1024-byte frame, stops
the RPC session, and closes the serial handle. Follow MISSION.md for this
project's radio operations and keep TX disarmed during UI receipt walks.

## Recovery

If another process owns USB, the tool fails before opening it. Stop only a
known stale task that you own. The CLI connection sends Ctrl-C to end a stale
text monitor, then requests a prompt and verifies `help`.

If the Flipper is in the USB-UART bridge, the CDC port speaks Marauder rather
than Flipper CLI/RPC. Exit the bridge with on-device long Back. BFFB's GPIO
UART remains 115200 baud with LF endings; it is a different link from host
USB CDC. Read BFFB_MOMENTUM.md before issuing bridge commands.

If both CLI and RPC fail with no host owner, physical LEFT + BACK held for
5 seconds performs a normal reboot. Afterward require an actual command
response or screen frame before uploading. Host software cannot guarantee
CLI availability while the MCU is halted or its USB service is unresponsive.
Do not flash firmware, reset settings, format storage, or repeatedly toggle
DTR to conceal that condition.

## Skills and checks

CYBERDYNE.SKILLS supplies `flipper-ops` (USB command guidance) and
`flipper-payload-forge` (native payload file formats). The installer checks
out commit `c9c2c99ae4e8670f82782e6f8bf121b037567799` under
`$FLIPPERCTL_HOME/CYBERDYNE.SKILLS` and copies those skill folders into each
agent's skill directory. FAP compilation uses ufbt. The repository's
`skills/flipper-device-harness` skill describes this entry point. New agent
sessions discover installed skills; an existing session can read their
`SKILL.md` directly.

Run the transport checks without a Flipper:

```sh
~/.local/share/flipperctl/venv/bin/python tools/test_flipperctl.py
```

They create local pseudo-terminals and temporary lock files. They check
exclusive ownership, greeting-only failure, command timeouts without replay,
button release, supervised child cleanup, incomplete-frame refusal, and hash
mismatch refusal. They do not prove live Flipper access. Root's current
candidate still needs exact-image upload/readback and the handoff's five
launch/exit cycles, eight-tab walk, and survival receipt on the real device.

## Primary references

- [Official CLI commands and input sequences](https://docs.flipper.net/zero/development/cli)
- [Official reboot instructions](https://docs.flipper.net/zero/basics/reboot)
- [Official ufbt build/upload workflow](https://github.com/flipperdevices/flipperzero-ufbt)
- [Official GUI protobuf protocol](https://github.com/flipperdevices/flipperzero-protobuf/blob/dev/gui.proto)
- [CYBERDYNE Flipper operations skill](https://github.com/0-CYBERDYNE-SYSTEMS-0/CYBERDYNE.SKILLS/blob/main/flipper-ops/SKILL.md)
