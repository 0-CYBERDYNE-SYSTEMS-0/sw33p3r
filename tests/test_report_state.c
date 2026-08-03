#include <stdio.h>
#include <string.h>

#include "../room_sweep_report.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

static void check_contains(const char* report, const char* text, const char* message) {
    check(strstr(report, text) != NULL, message);
}

int main(void) {
    RoomSweepReportState state;
    char report[1024];

    room_sweep_report_init(&state);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorRf);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorWifi);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorBle);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorGps);
    room_sweep_report_set_gps_included(&state);
    room_sweep_report_set_tx(&state, RoomSweepReportTxStarted);
    size_t length = room_sweep_report_format(&state, report, sizeof(report));
    check(room_sweep_report_is_clean(&state), "clean state stays clean");
    check(length > 0, "clean report is non-empty");
    check_contains(report, "Status: COMPLETE", "complete file status is plain English");
    check_contains(report, "Coverage: FULL", "full sensor coverage is explicit");
    check_contains(report, "Sensors confirmed: RF, Wi-Fi, BLE, GPS", "confirmed sensors are listed");
    check_contains(report, "unavailable: none", "no unavailable sensors are explicit");
    check_contains(report, "not confirmed: none", "no unconfirmed sensors are explicit");
    check_contains(report, "TX: started", "started TX is explicit");
    check_contains(report, "GPS: included", "included GPS is explicit");

    RoomSweepReportState privacy_state;
    room_sweep_report_init(&privacy_state);
    room_sweep_report_set_gps_omitted(&privacy_state);
    check(
        room_sweep_report_is_clean(&privacy_state),
        "deliberate GPS privacy omission does not make a clean session incomplete");
    room_sweep_report_format(&privacy_state, report, sizeof(report));
    check_contains(report, "Status: COMPLETE", "an unused sensor does not invalidate the file");
    check_contains(report, "Coverage: PARTIAL", "unconfirmed sensor coverage is not called full");

    room_sweep_report_set_tx(&state, RoomSweepReportTxArmed);
    room_sweep_report_format(&state, report, sizeof(report));
    check_contains(report, "TX: armed", "armed TX is explicit");
    room_sweep_report_set_tx(&state, RoomSweepReportTxRefused);
    room_sweep_report_format(&state, report, sizeof(report));
    check_contains(report, "TX: refused", "refused TX is explicit");
    room_sweep_report_set_tx(&state, RoomSweepReportTxAborted);
    room_sweep_report_format(&state, report, sizeof(report));
    check_contains(report, "TX: aborted", "aborted TX is explicit");

    room_sweep_report_sensor_unavailable(&state, RoomSweepReportSensorWifi);
    room_sweep_report_set_gps_omitted(&state);
    room_sweep_report_note_drop(&state, 3);
    room_sweep_report_note_storage_error(&state);
    room_sweep_report_format(&state, report, sizeof(report));
    check(!room_sweep_report_is_clean(&state), "drop/storage error state is incomplete");
    check_contains(report, "Status: INCOMPLETE", "incomplete status is plain English");
    check_contains(report, "unavailable: Wi-Fi", "unavailable sensor is listed");
    check_contains(report, "Dropped events: 3", "drop count is visible");
    check_contains(report, "Storage: error", "storage error is visible");
    check_contains(report, "GPS: omitted", "omitted GPS is explicit");

    check_contains(report, "RSSI is not distance", "RSSI limitation is explicit");
    check_contains(report, "wireless evidence does not prove Internet telemetry", "telemetry limitation is explicit");
    check_contains(report, "recording, ownership, or intent", "wireless evidence scope is explicit");
    check_contains(report, "no observation does not prove absence", "absence limitation is explicit");
    check_contains(report, "bounded carrier is not replay", "carrier limitation is explicit");

    char tiny[16];
    size_t tiny_length = room_sweep_report_format(&state, tiny, sizeof(tiny));
    check(tiny_length == sizeof(tiny) - 1U && tiny[sizeof(tiny) - 1U] == '\0',
          "small output buffer is bounded and terminated");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
