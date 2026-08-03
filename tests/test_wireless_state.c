#include <stdio.h>

#include "../room_sweep_wireless.h"

static int failures;

static void check(int condition, const char* message) {
    if(condition) {
        printf("PASS: %s\n", message);
    } else {
        printf("FAIL: %s\n", message);
        failures++;
    }
}

int main(void) {
    check(
        room_sweep_wireless_identity_matches(
            "AA:BB:CC:DD:EE:FF", "Kitchen", "aa:bb:cc:dd:ee:ff", "Renamed AP"),
        "matching MAC wins even when the display label changes");
    check(
        !room_sweep_wireless_identity_matches(
            "AA:BB:CC:DD:EE:FF", "Kitchen", "11:22:33:44:55:66", "Kitchen"),
        "same SSID cannot merge different MAC addresses");
    check(
        !room_sweep_wireless_identity_matches(
            "AA:BB:CC:DD:EE:FF", "Kitchen", "", "Kitchen"),
        "a MAC-bearing stored row cannot fall back to a MAC-less label");
    check(
        !room_sweep_wireless_identity_matches(
            "", "Kitchen", "11:22:33:44:55:66", "Kitchen"),
        "a MAC-bearing observation cannot match a MAC-less stored row by label");
    check(
        room_sweep_wireless_identity_matches("", "Kitchen", "", "Kitchen"),
        "two MAC-less rows may match by their label");
    check(
        !room_sweep_wireless_identity_matches("", "Kitchen", "", "kitchen"),
        "MAC-less labels use exact text matching");
    check(
        room_sweep_wireless_identity_matches(NULL, "Kitchen", NULL, "Kitchen"),
        "null MAC fields count as absent for label fallback");
    check(
        !room_sweep_wireless_identity_matches("", NULL, "", "Kitchen"),
        "missing labels do not create a fallback identity");

    check(
        room_sweep_wireless_evidence_text(RoomSweepWirelessEvidenceApBeacon)[0] != '\0' &&
            room_sweep_wireless_evidence_text(RoomSweepWirelessEvidenceApBeacon)[0] == 'A',
        "Wi-Fi evidence is labeled AP beacon heard");
    check(
        room_sweep_wireless_evidence_text(RoomSweepWirelessEvidenceBleAdvertisement)[0] == 'B',
        "BLE evidence is labeled BLE advertisement heard");
    check(
        room_sweep_wireless_evidence_text(RoomSweepWirelessEvidenceNotObservedInScan)[0] == 'N',
        "empty scan windows say Not observed in this scan");
    check(
        room_sweep_wireless_evidence_text(RoomSweepWirelessEvidenceInternetTelemetry)[0] == 'I',
        "Internet telemetry is explicitly marked not measured");
    check(
        room_sweep_wireless_evidence_text(RoomSweepWirelessEvidenceNoObservation)[0] == 'n',
        "no observation is not presented as proof of absence");

    printf("RESULT: %s (%d failure(s))\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
