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
    char report[2048];

    room_sweep_report_init(&state);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorRf);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorWifi);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorBle);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorGps);
    room_sweep_report_sensor_confirmed(&state, RoomSweepReportSensorNrf24);
    room_sweep_report_set_gps_included(&state);
    room_sweep_report_set_tx(&state, RoomSweepReportTxStarted);
    size_t length = room_sweep_report_format(&state, report, sizeof(report));
    check(room_sweep_report_is_clean(&state), "clean state stays clean");
    check(length > 0, "clean report is non-empty");
    check_contains(report, "ROOM REPORT", "title is plain Room Report");
    check_contains(report, "Status: COMPLETE", "complete file status is plain English");
    check_contains(report, "Coverage: FULL", "full sensor coverage is explicit");
    check_contains(
        report,
        "Sensors confirmed: RF, Wi-Fi, BLE, GPS, nRF24",
        "confirmed sensors include nRF24");
    check_contains(report, "unavailable: none", "no unavailable sensors are explicit");
    check_contains(report, "not confirmed: none", "no unconfirmed sensors are explicit");
    check_contains(report, "TX: started", "started TX is explicit");
    check_contains(report, "GPS: included", "included GPS is explicit");
    check_contains(
        report,
        "nRF24 rows are 2.4 GHz channel energy only, not packets or device IDs",
        "nRF24 limitation is explicit");

    RoomSweepReportFindings findings;
    room_sweep_report_findings_init(&findings);
    findings.rf_observations = 4;
    findings.rf_strongest_rssi = -55;
    findings.rf_strongest_hz = 433920000UL;
    findings.wifi_observations = 2;
    findings.wifi_windows = 1;
    findings.wifi_strongest_rssi = -60;
    findings.ble_observations = 3;
    findings.ble_windows = 1;
    findings.ble_strongest_rssi = -70;
    findings.nrf_active_channels = 2;
    findings.nrf_top_channel = 40;
    findings.nrf_observations = 5;
    findings.nrf_total_hits = 123;
    findings.gps_snapshots = 1;
    findings.full_sweep_completed = true;
    check(
        room_sweep_report_activity(&findings) == RoomSweepReportActivityBusy,
        "many hits rate as busy");
    size_t used = length;
    used = room_sweep_report_append_findings(&findings, report, sizeof(report), used);
    check(used > length, "findings extend the report");
    check_contains(report, "Summary: This room looked busy", "summary is plain English");
    check_contains(report, "Full room sweep: finished", "full sweep noted");
    check_contains(report, "Sub-GHz RF: 4 hits", "RF findings line");
    check_contains(
        report, "Sub-GHz RF: 4 hits; strongest about -55dBm near 433920000 Hz (energy only, no ID)",
        "RF findings line carries the energy-only/no-ID qualifier");
    check_contains(
        report, "Wi-Fi: 2 AP beacons heard", "Wi-Fi findings describe beacons heard");
    check_contains(
        report, "BLE: 3 advertisements heard", "BLE findings describe advertisements heard");
    check_contains(
        report,
        "nRF24 (2.4 GHz): energy on 2 channels; top channel 40; 123 RPD hits in 5 pass(es)",
        "nRF24 findings use real RPD hit totals, not pass counts");
    check_contains(
        report,
        "Energy detection only - no packets or device IDs",
        "nRF24 findings carry the energy-only qualifier");

    /* Passes ran but the band was quiet: no hits may not read as activity. */
    RoomSweepReportFindings quiet_nrf;
    room_sweep_report_findings_init(&quiet_nrf);
    quiet_nrf.nrf_observations = 2;
    quiet_nrf.nrf_total_hits = 0;
    char nrf_report[1024];
    room_sweep_report_format(&state, nrf_report, sizeof(nrf_report));
    size_t nrf_used = room_sweep_report_append_findings(
        &quiet_nrf, nrf_report, sizeof(nrf_report), strlen(nrf_report));
    check(nrf_used > 0, "quiet nRF24 findings appended");
    check_contains(
        nrf_report,
        "2 energy pass(es), no RPD hits",
        "zero-hit nRF24 passes are reported as no hits");

    RoomSweepReportState privacy_state;
    room_sweep_report_init(&privacy_state);
    room_sweep_report_set_gps_omitted(&privacy_state);
    check(
        room_sweep_report_is_clean(&privacy_state),
        "deliberate GPS privacy omission does not make a clean session incomplete");
    room_sweep_report_format(&privacy_state, report, sizeof(report));
    check_contains(report, "Status: COMPLETE", "an unused sensor does not invalidate the file");
    check_contains(report, "Coverage: PARTIAL", "unconfirmed sensor coverage is not called full");

    /* Mark nRF unavailable; confirm the rest → FULL. */
    RoomSweepReportState core;
    room_sweep_report_init(&core);
    room_sweep_report_sensor_confirmed(&core, RoomSweepReportSensorRf);
    room_sweep_report_sensor_confirmed(&core, RoomSweepReportSensorWifi);
    room_sweep_report_sensor_confirmed(&core, RoomSweepReportSensorBle);
    room_sweep_report_sensor_confirmed(&core, RoomSweepReportSensorGps);
    room_sweep_report_sensor_unavailable(&core, RoomSweepReportSensorNrf24);
    check(room_sweep_report_coverage_full(&core), "FULL when only unavailable sensors are missing");

    RoomSweepReportState tx_state;
    room_sweep_report_init(&tx_state);
    room_sweep_report_set_tx(&tx_state, RoomSweepReportTxArmed);
    room_sweep_report_format(&tx_state, report, sizeof(report));
    check_contains(report, "TX: armed", "armed TX is explicit");
    room_sweep_report_set_tx(&tx_state, RoomSweepReportTxRefused);
    room_sweep_report_format(&tx_state, report, sizeof(report));
    check_contains(report, "TX: refused", "refused TX is explicit");
    room_sweep_report_set_tx(&tx_state, RoomSweepReportTxAborted);
    room_sweep_report_format(&tx_state, report, sizeof(report));
    check_contains(report, "TX: aborted", "aborted TX is explicit");

    room_sweep_report_set_tx(&tx_state, RoomSweepReportTxStarted);
    room_sweep_report_set_tx(&tx_state, RoomSweepReportTxAborted);
    room_sweep_report_format(&tx_state, report, sizeof(report));
    check_contains(
        report,
        "TX: aborted (started earlier)",
        "terminal TX outcome preserves earlier start history");

    room_sweep_report_set_tx(&tx_state, RoomSweepReportTxStarted);
    room_sweep_report_set_tx(&tx_state, RoomSweepReportTxCompleted);
    room_sweep_report_format(&tx_state, report, sizeof(report));
    check_contains(report, "TX: completed", "successful TX end is explicit");

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
