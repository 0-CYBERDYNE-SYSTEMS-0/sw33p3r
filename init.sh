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

echo "== host wireless state =="
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/test_wireless_state.c -o /tmp/room_sweep_wireless_state_test
/tmp/room_sweep_wireless_state_test

echo "== host GPS presentation state =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_gps_state.c -o /tmp/room_sweep_gps_state_test
/tmp/room_sweep_gps_state_test

echo "== host recorder state =="
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/test_record_state.c -o /tmp/room_sweep_record_state_test
/tmp/room_sweep_record_state_test

echo "== host report state =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_report_state.c -o /tmp/room_sweep_report_state_test
/tmp/room_sweep_report_state_test

echo "== host settings/scan window =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_settings_state.c -o /tmp/room_sweep_settings_state_test
/tmp/room_sweep_settings_state_test

echo "== host full-sweep + radio path =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_full_sweep_state.c -o /tmp/room_sweep_full_sweep_test
/tmp/room_sweep_full_sweep_test

echo "== host nRF24 survey state =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_nrf24_state.c -o /tmp/room_sweep_nrf24_state_test
/tmp/room_sweep_nrf24_state_test

echo "== host analyzer / proximity meter =="
cc -std=c11 -Wall -Wextra -Werror -I. tests/test_analyzer_state.c -o /tmp/room_sweep_analyzer_test
/tmp/room_sweep_analyzer_test

echo "== host feedback cadence curves =="
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/test_feedback.c -o /tmp/room_sweep_feedback_test && /tmp/room_sweep_feedback_test

echo "== host UI layout helpers =="
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/test_ui_layout.c -o /tmp/room_sweep_ui_layout_test
/tmp/room_sweep_ui_layout_test

echo "== host input touch / browse-phase seam =="
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/test_input_touch.c -o /tmp/room_sweep_input_touch_test
/tmp/room_sweep_input_touch_test

echo "== host radar polar math =="
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/test_radar.c -o /tmp/room_sweep_radar_test
/tmp/room_sweep_radar_test

echo "== host waterfall state =="
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/test_waterfall.c -o /tmp/room_sweep_waterfall_test
/tmp/room_sweep_waterfall_test

if [[ -f tests/test_scan_logic.c ]]; then
  echo "== host scan/GPS helpers =="
  cc -std=c11 -Wall -Wextra -Werror -I. tests/test_scan_logic.c -lm -o /tmp/test_scan_logic
  /tmp/test_scan_logic
fi

echo "== host Marauder line parser =="
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/test_marauder_parse.c -o /tmp/test_marauder_parse
/tmp/test_marauder_parse

echo "== ufbt build =="
ufbt

echo "init.sh OK"
