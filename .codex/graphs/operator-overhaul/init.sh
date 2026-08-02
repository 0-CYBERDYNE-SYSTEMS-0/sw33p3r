#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$repo_dir"

test "$(git rev-parse HEAD)" != ""
test "$(git config user.name)" = "0-CYBERDYNE-SYSTEMS-0"
test "$(git config user.email)" = "134018026+0-CYBERDYNE-SYSTEMS-0@users.noreply.github.com"
head -n 2 "$HOME/.ufbt/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv" | grep -F '87.1'
PYTHONDONTWRITEBYTECODE=1 python3 _verify_api.py
./init.sh
git diff --check
