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

    /* ---------------------------------------------------------- */
    /* Identification evidence: vendors + hints (Phase 1)         */
    /* ---------------------------------------------------------- */

    /* Token extraction from bounded detail strings. */
    char token[16];
    check(
        room_sweep_report_detail_token("oui=Apple; AP beacon", "oui", token, sizeof(token)) &&
            strcmp(token, "Apple") == 0,
        "detail token extracts oui label");
    check(
        room_sweep_report_detail_token("oui=randomized hints=DEV?; AP beacon", "hints", token, sizeof(token)) &&
            strcmp(token, "DEV?") == 0,
        "detail token extracts hint tag after another token");
    check(
        room_sweep_report_detail_token("oui=unlisted; BLE adv", "oui", token, sizeof(token)) &&
            strcmp(token, "unlisted") == 0,
        "detail token extracts unlisted marker");
    check(
        !room_sweep_report_detail_token("AP beacon heard", "oui", token, sizeof(token)),
        "detail token absent returns false");
    check(
        !room_sweep_report_detail_token("hints=; AP beacon", "hints", token, sizeof(token)),
        "detail token empty value returns false");
    check(
        room_sweep_report_detail_token("noui=Apple; x", "oui", token, sizeof(token)) == false,
        "detail token key must start at a token boundary");

    /* Distinct-label accumulation. */
    char vendors[ROOM_SWEEP_REPORT_VENDORS_MAX][ROOM_SWEEP_REPORT_VENDOR_LEN];
    uint8_t vendor_count = 0;
    check(room_sweep_report_vendor_note(vendors, ROOM_SWEEP_REPORT_VENDORS_MAX, &vendor_count, "Apple"),
          "first vendor label is added");
    check(room_sweep_report_vendor_note(vendors, ROOM_SWEEP_REPORT_VENDORS_MAX, &vendor_count, "Samsung"),
          "second vendor label is added");
    check(!room_sweep_report_vendor_note(vendors, ROOM_SWEEP_REPORT_VENDORS_MAX, &vendor_count, "Apple"),
          "duplicate vendor label is ignored");
    check(!room_sweep_report_vendor_note(vendors, ROOM_SWEEP_REPORT_VENDORS_MAX, &vendor_count, ""),
          "empty vendor label is rejected");
    check(vendor_count == 2, "vendor count reflects distinct labels only");

    /* The report lines themselves. */
    RoomSweepReportFindings ident;
    room_sweep_report_findings_init(&ident);
    ident.hint_observations = 7;
    ident.vendor_count = 2;
    strncpy(ident.vendors[0], "Apple", ROOM_SWEEP_REPORT_VENDOR_LEN - 1U);
    strncpy(ident.vendors[1], "Samsung", ROOM_SWEEP_REPORT_VENDOR_LEN - 1U);
    char ident_report[1024];
    room_sweep_report_format(&state, ident_report, sizeof(ident_report));
    room_sweep_report_append_findings(&ident, ident_report, sizeof(ident_report), strlen(ident_report));
    check_contains(
        ident_report,
        "Vendors seen (curated OUI, not exhaustive): Apple, Samsung",
        "vendor list names registrants, never device counts");
    check_contains(
        ident_report,
        "Hints (name-pattern guesses only): 7 observation(s)",
        "hint line is labeled as name-pattern guesses only");

    /* No identification evidence -> no lines (no zero-noise). */
    RoomSweepReportFindings quiet_ident;
    room_sweep_report_findings_init(&quiet_ident);
    char quiet_report[1024];
    room_sweep_report_format(&state, quiet_report, sizeof(quiet_report));
    room_sweep_report_append_findings(&quiet_ident, quiet_report, sizeof(quiet_report), strlen(quiet_report));
    check(
        strstr(quiet_report, "Vendors seen") == NULL &&
            strstr(quiet_report, "Hints (name-pattern") == NULL,
        "identification lines are omitted when nothing was identified");

    /* Vendor list overflow stops at the cap. */
    char capped[ROOM_SWEEP_REPORT_VENDORS_MAX][ROOM_SWEEP_REPORT_VENDOR_LEN];
    uint8_t capped_count = 0;
    for(int i = 0; i < ROOM_SWEEP_REPORT_VENDORS_MAX + 4; i++) {
        char label[ROOM_SWEEP_REPORT_VENDOR_LEN];
        snprintf(label, sizeof(label), "V%02d", i);
        room_sweep_report_vendor_note(capped, ROOM_SWEEP_REPORT_VENDORS_MAX, &capped_count, label);
    }
    check(capped_count == ROOM_SWEEP_REPORT_VENDORS_MAX, "vendor list is capped at the maximum");

    /* ---------------------------------------------------------- */
    /* Duplicate-SSID correlation (Phase 6)                       */
    /* ---------------------------------------------------------- */
    RoomSweepReportFindings rogue;
    room_sweep_report_findings_init(&rogue);
    check(rogue.rogue_groups == 0, "findings init has no rogue groups");
    rogue.rogue_groups = 2;
    char rogue_report[1024];
    room_sweep_report_format(&state, rogue_report, sizeof(rogue_report));
    room_sweep_report_append_findings(&rogue, rogue_report, sizeof(rogue_report), strlen(rogue_report));
    check_contains(
        rogue_report,
        "Possible cloned SSIDs (same name, different BSSID): 2 group(s)",
        "duplicate-SSID line is a possible-clone lead, not a verdict");
    check_contains(
        rogue_report,
        "Mesh/roaming systems legitimately share one name across addresses",
        "mesh caveat travels with the rogue line");
    RoomSweepReportFindings no_rogue;
    room_sweep_report_findings_init(&no_rogue);
    char no_rogue_report[1024];
    room_sweep_report_format(&state, no_rogue_report, sizeof(no_rogue_report));
    room_sweep_report_append_findings(
        &no_rogue, no_rogue_report, sizeof(no_rogue_report), strlen(no_rogue_report));
    check(
        strstr(no_rogue_report, "Possible cloned SSIDs") == NULL,
        "no duplicate-SSID line when no groups exist");

    /* ---------------------------------------------------------- */
    /* Opt-in cross-session watchlist (Phase 9)                   */
    /* ---------------------------------------------------------- */
    RoomSweepReportFindings watch;
    room_sweep_report_findings_init(&watch);
    check(!watch.watchlist_enabled && watch.watchlist_matches == 0,
          "findings init has the watchlist OFF and empty");
    watch.watchlist_enabled = true;
    watch.watchlist_matches = 2;
    char watch_report[1024];
    room_sweep_report_format(&state, watch_report, sizeof(watch_report));
    room_sweep_report_append_findings(
        &watch, watch_report, sizeof(watch_report), strlen(watch_report));
    check_contains(
        watch_report,
        "Watchlist matches: 2",
        "watchlist section reports the distinct-match count");
    check_contains(
        watch_report,
        "only persistent-identity feature",
        "the report states the watchlist is the app's only persistent-identity feature");
    check_contains(
        watch_report,
        "opt-in",
        "the report states the watchlist is opt-in");
    check_contains(
        watch_report,
        "not proof it is the same physical device",
        "the report carries the address-match caveat");
    /* Disabled: no section, even with a stale count. */
    RoomSweepReportFindings watch_off;
    room_sweep_report_findings_init(&watch_off);
    watch_off.watchlist_enabled = false;
    watch_off.watchlist_matches = 5;
    char watch_off_report[1024];
    room_sweep_report_format(&state, watch_off_report, sizeof(watch_off_report));
    room_sweep_report_append_findings(
        &watch_off, watch_off_report, sizeof(watch_off_report), strlen(watch_off_report));
    check(
        strstr(watch_off_report, "Watchlist matches") == NULL &&
            strstr(watch_off_report, "persistent-identity") == NULL,
        "watchlist section is omitted entirely when the feature stayed OFF");

    /* ---------------------------------------------------------- */
    /* Per-device RSSI evidence range (Phase 7)                   */
    /* ---------------------------------------------------------- */
    RoomSweepReportFindings ranged;
    room_sweep_report_findings_init(&ranged);
    check(ranged.wifi_strongest_min == 0 && ranged.wifi_strongest_max == 0,
          "findings init has no strongest range");
    ranged.wifi_observations = 5;
    ranged.wifi_windows = 2;
    ranged.wifi_strongest_rssi = -38;
    ranged.wifi_strongest_min = -72;
    ranged.wifi_strongest_max = -38;
    ranged.ble_observations = 4;
    ranged.ble_windows = 2;
    ranged.ble_strongest_rssi = -50;
    ranged.ble_strongest_min = -80;
    ranged.ble_strongest_max = -50;
    char ranged_report[1024];
    room_sweep_report_format(&state, ranged_report, sizeof(ranged_report));
    room_sweep_report_append_findings(
        &ranged, ranged_report, sizeof(ranged_report), strlen(ranged_report));
    check_contains(
        ranged_report,
        "Wi-Fi: 5 AP beacons heard across 2 scan windows; strongest about -38dBm (range -72..-38)",
        "strongest Wi-Fi line carries the observed range");
    check_contains(
        ranged_report,
        "BLE: 4 advertisements heard across 2 scan windows; strongest about -50dBm (range -80..-50)",
        "strongest BLE line carries the observed range");
    /* A single sighting (min == max) keeps the plain line, no fake spread. */
    RoomSweepReportFindings single;
    room_sweep_report_findings_init(&single);
    single.wifi_observations = 1;
    single.wifi_windows = 1;
    single.wifi_strongest_rssi = -60;
    char single_report[1024];
    room_sweep_report_format(&state, single_report, sizeof(single_report));
    room_sweep_report_append_findings(
        &single, single_report, sizeof(single_report), strlen(single_report));
    check_contains(
        single_report,
        "strongest about -60dBm.",
        "no range printed when no spread was observed");
    check(
        strstr(single_report, "(range") == NULL,
        "range omitted when the strongest device was heard once");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
