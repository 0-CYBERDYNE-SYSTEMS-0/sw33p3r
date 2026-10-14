#!/bin/sh
# Install the host CLI and its isolated dependencies. Never opens Flipper USB.
set -eu
flipperctl_repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
flipperctl_home="${FLIPPERCTL_HOME:-$HOME/.local/share/flipperctl}"
flipperctl_venv="$flipperctl_home/venv"
flipperctl_bin="${FLIPPERCTL_BIN:-$HOME/.local/bin/flipperctl}"
flipperctl_python="${FLIPPERCTL_PYTHON:-python3.12}"
flipperctl_cyberdyne_repo="$flipperctl_home/CYBERDYNE.SKILLS"
flipperctl_cyberdyne_url="https://github.com/0-CYBERDYNE-SYSTEMS-0/CYBERDYNE.SKILLS.git"
flipperctl_cyberdyne_revision="c9c2c99ae4e8670f82782e6f8bf121b037567799"
flipperctl_codex_skills="${FLIPPERCTL_CODEX_SKILLS_DIR:-$HOME/.agents/skills}"
flipperctl_claude_home="${CLAUDE_CONFIG_DIR:-$HOME/.claude}"
flipperctl_claude_skills="${FLIPPERCTL_CLAUDE_SKILLS_DIR:-$flipperctl_claude_home/skills}"
flipperctl_hermes_home="${HERMES_HOME:-$HOME/.hermes}"
flipperctl_hermes_skills="${FLIPPERCTL_HERMES_SKILLS_DIR:-$flipperctl_hermes_home/skills}"

if ! "$flipperctl_python" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'; then
    echo 'Flipper SDK requires Python 3.10+; set FLIPPERCTL_PYTHON to a supported interpreter.' >&2
    exit 1
fi

mkdir -p "$flipperctl_home"
"$flipperctl_python" -m venv "$flipperctl_venv"
"$flipperctl_venv/bin/python" -m pip install pyserial==3.5 protobuf==3.20.2
# The old package pins NumPy 1.22.3 for its separate interactive console.
# The screen/input APIs used here do not import NumPy; avoid that obsolete pin.
"$flipperctl_venv/bin/python" -m pip install --no-deps flipperzero-protobuf==0.1.20221108
"$flipperctl_venv/bin/python" - "$flipperctl_repo" "$flipperctl_venv" "$flipperctl_bin" <<'PY'
from pathlib import Path
import shlex
import sys
repo, venv, name = sys.argv[1:]
launcher = Path(name)
marker = '# Installed by sw33p3r/tools/install_flipperctl.sh'
if launcher.exists() and marker not in launcher.read_text():
    raise SystemExit('Existing command is not managed by this installer: ' + name)
launcher.parent.mkdir(parents=True, exist_ok=True)
launcher.write_text('#!/bin/sh\n' + marker + '\nexec ' +
                    shlex.quote(venv + '/bin/python') + ' -I ' +
                    shlex.quote(repo + '/tools/flipperctl.py') + ' "$@"\n')
launcher.chmod(0o755)
print('Installed:', launcher)
PY
"$flipperctl_bin" --help

if ! command -v git >/dev/null 2>&1; then
    echo 'Git is required to install the pinned CYBERDYNE Flipper skills.' >&2
    exit 1
fi

if [ ! -e "$flipperctl_cyberdyne_repo" ]; then
    git clone --quiet --filter=blob:none --no-checkout --depth=1 \
        "$flipperctl_cyberdyne_url" "$flipperctl_cyberdyne_repo"
elif [ ! -d "$flipperctl_cyberdyne_repo/.git" ]; then
    echo "Refusing to replace unmanaged path: $flipperctl_cyberdyne_repo" >&2
    exit 1
fi

flipperctl_existing_url=$(git -C "$flipperctl_cyberdyne_repo" config --get remote.origin.url)
if [ "$flipperctl_existing_url" != "$flipperctl_cyberdyne_url" ]; then
    echo "Unexpected origin for $flipperctl_cyberdyne_repo: $flipperctl_existing_url" >&2
    exit 1
fi
flipperctl_cyberdyne_changes=$(git -C "$flipperctl_cyberdyne_repo" status --porcelain)
flipperctl_cyberdyne_files=$(find "$flipperctl_cyberdyne_repo" -mindepth 1 -maxdepth 1 \
    ! -name .git -print -quit)
if [ -n "$flipperctl_cyberdyne_changes" ] && [ -n "$flipperctl_cyberdyne_files" ]; then
    echo "Refusing to update modified skill checkout: $flipperctl_cyberdyne_repo" >&2
    exit 1
fi
git -C "$flipperctl_cyberdyne_repo" fetch --quiet --depth=1 origin "$flipperctl_cyberdyne_revision"
git -C "$flipperctl_cyberdyne_repo" sparse-checkout init --cone
git -C "$flipperctl_cyberdyne_repo" sparse-checkout set flipper-ops flipper-payload-forge
git -C "$flipperctl_cyberdyne_repo" checkout --quiet --detach "$flipperctl_cyberdyne_revision"

"$flipperctl_venv/bin/python" - "$flipperctl_repo" "$flipperctl_cyberdyne_repo" \
    "$flipperctl_codex_skills" "$flipperctl_claude_skills" "$flipperctl_hermes_skills" <<'PY'
from pathlib import Path
from shutil import copytree
import sys

repo, cyberdyne, *agent_skill_roots = map(Path, sys.argv[1:])
sources = {
    "flipper-ops": cyberdyne / "flipper-ops",
    "flipper-payload-forge": cyberdyne / "flipper-payload-forge",
    "flipper-device-harness": repo / "skills" / "flipper-device-harness",
}
targets = [
    (root / name, source)
    for root in agent_skill_roots
    for name, source in sources.items()
]

for source in sources.values():
    if not (source / "SKILL.md").is_file():
        raise SystemExit("Missing skill source: " + str(source))

# Copy real directories because Hermes discovery may skip directory symlinks.
for target, source in targets:
    if target.exists() or target.is_symlink():
        if (target / "SKILL.md").is_file():
            print("Preserved existing skill:", target)
            continue
        raise SystemExit("Refusing to replace existing path: " + str(target))

for target, source in targets:
    if target.exists() or target.is_symlink():
        continue
    target.parent.mkdir(parents=True, exist_ok=True)
    copytree(source, target)
    print("Installed:", target)

print("Flipper skills are available to Codex, Claude Code, and Hermes.")
PY
