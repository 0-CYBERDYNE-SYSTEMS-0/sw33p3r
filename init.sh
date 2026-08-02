#!/usr/bin/env bash
# Smoke check for Room Sweep host gates. Run from repo root.
set -euo pipefail
cd "$(dirname "$0")"

echo "== host NMEA =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nmea.c nmea.c -o /tmp/test_nmea
/tmp/test_nmea

echo "== host Back routing =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_input_state.c -o /tmp/room_sweep_input_test
/tmp/room_sweep_input_test

echo "== host RF/TX state helpers =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_rf_tx_state.c -o /tmp/room_sweep_rf_tx_state_test
/tmp/room_sweep_rf_tx_state_test

if [[ -f tests/test_scan_logic.c ]]; then
  echo "== host scan/GPS helpers =="
  cc -std=c11 -Wall -Wextra -Werror -I. tests/test_scan_logic.c -o /tmp/test_scan_logic
  /tmp/test_scan_logic
fi

echo "== ufbt build =="
ufbt

echo "init.sh OK"
